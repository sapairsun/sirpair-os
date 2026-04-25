// USB/EHCI driver for ThinkPad X220 - based on proven reference code
//
// Key design:
//   - DMA buffers in kernel BSS (static allocation, like reference code)
//   - QH/qTD structures include ext_bufptr[5] for 64-bit EHCI (Intel 6 Series)
//   - Stop/start ASE per transfer (not ASE-always-on)
//   - Data toggle tracking from hardware (qTD token bit 31)
//   - Two-phase init: takeover -> full reset
//   - Minimal PCI operations (only bus master + memory space, like reference code)

#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "pci.h"
#include "usb.h"
#include "buf.h"
#include "spinlock.h"

static int ehci_init_reset(struct pci_device *pdev);
static int ehci_enumerate_reset(void);
static int msd_init_device(void);

// ============================================================================
// DMA buffers in kernel BSS (static allocation, matches reference code)
//
// CRITICAL: QH and qTD structures MUST include ext_bufptr[5] fields!
// Intel 6 Series EHCI is 64-bit capable (HCCPARAMS bit 0 = 1).
// Without ext_bufptr, the controller reads adjacent qTD data as the
// upper 32 bits of buffer addresses, creating invalid 64-bit DMA
// addresses (in the terabyte range) that cause Master Abort!
//
// We use V2P() to convert kernel virtual addresses to physical addresses
// for EHCI controller registers and link pointers.
// ============================================================================

// Static DMA buffers with alignment (matches reference code)
static struct ehci_qh   dma_qh_bss
    __attribute__((aligned(32)));
static struct ehci_qtd   dma_qtd_bss[8]
    __attribute__((aligned(32)));
static uint              dma_pfl_bss[1024]
    __attribute__((aligned(4096)));
static uchar             dma_buf_bss[8192]
    __attribute__((aligned(4096)));

// Pointers (for compatibility with existing code)
static struct ehci_qh   *dma_qh  = &dma_qh_bss;
static struct ehci_qtd   *dma_qtd = dma_qtd_bss;
static uint              *dma_pfl = dma_pfl_bss;
static uchar             *dma_buf = dma_buf_bss;

// Compatibility macros so existing code needs minimal changes
#define static_qh       (*dma_qh)
#define static_qtd       dma_qtd
#define static_pfl       dma_pfl
#define static_buf       dma_buf

// Physical addresses (initialized in usbinit via V2P)
static uint qh_phys;
static uint pfl_phys;
static uint buf_phys;

// ============================================================================
// Global state
// ============================================================================
static int usb_inited = 0;
static uint ehci_base = 0;
static uint ehci_opbase = 0;
static uint ehci_nports = 0;
static uint ehci_pci_bus = 0;
static uint ehci_pci_dev = 0;
static uint ehci_pci_func = 0;

/* 成功枚举 MSD 后保存，供 QEMU 等环境下虚拟 EHCI 复位后重新 init+enumerate */
static struct pci_device usb_ehci_saved;
static int usb_ehci_saved_ok;

// Mass storage device state
static int msd_found = 0;
static uchar msd_addr = 0;
static uchar msd_ep_in = 0;
static uchar msd_ep_out = 0;
static ushort msd_maxpkt_in = 0;
static ushort msd_maxpkt_out = 0;
static uint msd_tag = 1;
static uint msd_capacity = 0;

// Data toggle tracking (from reference code)
static uchar bulk_toggle[32];

// Interrupt state
static int usb_irq = 0;
static int usb_intr_mode = 0;
static int usb_busy = 0;
volatile int usb_xfer_done = 0;
volatile int usb_waiting = 0;

static struct spinlock usblock;

// Forward declarations for functions used before their definition
static void clear_pci_errors(void);
static void pci_reenable_busmaster(void);

// ============================================================================
// Delay functions (io_wait based - matches reference code for accurate timing)
//
// On real hardware (i5-2520M), outb(0x80,0) takes ~1-2us.
// ms*2000 io_waits ≈ 2-4ms per "ms" on real HW (safe margin).
// This matches the reference code that works on X220.
// ============================================================================
static inline void
io_wait(void)
{
  outb(0x80, 0);
}

static void
delay_us(int us)
{
  while(us-- > 0)
    io_wait();  // ~1-2us per io_wait on real HW
}

static void
delay_ms(int ms)
{
  int i;
  while(ms-- > 0)
    for(i = 0; i < 2000; i++)
      io_wait();
}

// ============================================================================
// Memory barriers
// ============================================================================
static inline void
memory_barrier(void)
{
  __asm__ volatile("mfence" ::: "memory");
}

// ============================================================================
// EHCI MMIO register access
// ============================================================================
static uint
ehci_cap_read(uint offset)
{
  return *(volatile uint*)(ehci_base + offset);
}

static uint
ehci_op_read(uint offset)
{
  return *(volatile uint*)(ehci_opbase + offset);
}

static void
ehci_op_write(uint offset, uint val)
{
  *(volatile uint*)(ehci_opbase + offset) = val;
}

// ============================================================================
// PCI helpers (minimal - matches reference code)
// Only enable bus master + memory space, nothing else.
// Reference code has NO extra PCI config writes during operation.
// ============================================================================

// ============================================================================
// Async Schedule Enable/Disable (reference code approach)
// ============================================================================
static int
ehci_stop_async(void)
{
  uint cmd = ehci_op_read(EHCI_OP_USBCMD);
  if(!(cmd & EHCI_CMD_ASE))
    return 0;
  cmd &= ~EHCI_CMD_ASE;
  ehci_op_write(EHCI_OP_USBCMD, cmd);
  int timeout = 200000;
  while((ehci_op_read(EHCI_OP_USBSTS) & EHCI_STS_ASS) && --timeout > 0)
    delay_us(1);
  return (timeout > 0) ? 0 : -1;
}

static int
ehci_start_async(void)
{
  uint cmd = ehci_op_read(EHCI_OP_USBCMD);
  if(cmd & EHCI_CMD_ASE){
    if(ehci_op_read(EHCI_OP_USBSTS) & EHCI_STS_ASS)
      return 0;
  }
  cmd |= EHCI_CMD_ASE;
  ehci_op_write(EHCI_OP_USBCMD, cmd);
  int timeout = 200000;
  while(!(ehci_op_read(EHCI_OP_USBSTS) & EHCI_STS_ASS) && --timeout > 0)
    delay_us(1);
  return (timeout > 0) ? 0 : -1;
}

// ============================================================================
// Initialize qTD
// ============================================================================
static void
qtd_init(struct ehci_qtd *qtd, uint next_phys, uint pid, int toggle,
         uint buf_phys_addr, uint len)
{
  memset(qtd, 0, sizeof(*qtd));
  qtd->nextqtd = next_phys ? next_phys : EHCI_LP_TERMINATE;
  qtd->altnextqtd = EHCI_LP_TERMINATE;
  qtd->token = QTD_TOKEN_ACTIVE | QTD_TOKEN_CERR(3) | pid |
               QTD_TOKEN_TOTALBYTES(len) |
               (toggle ? QTD_TOKEN_DT : 0);
  if(buf_phys_addr && len > 0){
    qtd->bufptr[0] = buf_phys_addr;
    uint page = buf_phys_addr & ~0xFFF;
    int i;
    for(i = 1; i < 5; i++)
      qtd->bufptr[i] = page + (uint)i * 0x1000;
  }
}

// ============================================================================
// Wait for qTD completion (polling) - matches reference code approach
// Reference code does NOT check USBSTS/PCI during polling - just polls qTD token.
// ============================================================================
static int
ehci_wait_qtd(struct ehci_qtd *qtd, int timeout_ms)
{
  int i;
  uint sts;
  // Fast poll with PAUSE (critical for HyperThreading on X220 i5-2520M)
  for(i = 0; i < 50000; i++){
    uint token = qtd->token;
    if(!(token & QTD_TOKEN_ACTIVE)){
      if(token & QTD_TOKEN_HALTED) return -2;
      if(token & (QTD_TOKEN_BUFERR | QTD_TOKEN_BABBLE | QTD_TOKEN_XACTERR))
        return -3;
      return 0;
    }
    asm volatile("pause");
  }
  // Slow poll with HSE recovery
  while(timeout_ms-- > 0){
    uint token = qtd->token;
    if(!(token & QTD_TOKEN_ACTIVE)){
      if(token & QTD_TOKEN_HALTED) return -2;
      if(token & (QTD_TOKEN_BUFERR | QTD_TOKEN_BABBLE | QTD_TOKEN_XACTERR))
        return -3;
      return 0;
    }
    // Check for Host System Error - recover if detected
    sts = ehci_op_read(EHCI_OP_USBSTS);
    if(sts & EHCI_STS_HSE){
      ehci_op_write(EHCI_OP_USBSTS, EHCI_STS_HSE);
      clear_pci_errors();
      pci_reenable_busmaster();
    }
    delay_ms(1);
  }
  return -1;
}

// ============================================================================
// Clear QH overlay (after failed transfer) - matches reference code exactly
// Reference code does NOT try to restart halted controller here.
// ============================================================================
static void
ehci_clear_qh(void)
{
  int i;
  ehci_stop_async();

  static_qh.curqtd = 0;
  static_qh.nextqtd = EHCI_LP_TERMINATE;
  static_qh.altnextqtd = EHCI_LP_TERMINATE;
  static_qh.token = 0;
  for(i = 0; i < 5; i++) static_qh.bufptr[i] = 0;
  for(i = 0; i < 5; i++) static_qh.ext_bufptr[i] = 0;
  memory_barrier();
  ehci_start_async();
}

// ============================================================================
// USB control transfer
// ============================================================================
static int
usb_control(uint addr, struct usb_setup *setup, void *data, int datalen)
{
  struct ehci_qtd *setup_qtd = &static_qtd[0];
  struct ehci_qtd *data_qtd = &static_qtd[1];
  struct ehci_qtd *status_qtd = &static_qtd[2];
  int r;

  uint setup_phys_addr = buf_phys;
  uint data_phys_addr = buf_phys + 64;
  uint qtd0_phys = v2p((void*)&static_qtd[0]);
  uint qtd1_phys = v2p((void*)&static_qtd[1]);
  uint qtd2_phys = v2p((void*)&static_qtd[2]);

  memmove(static_buf, setup, 8);

  if(datalen > 0 && !(setup->bmRequestType & USB_DIR_IN))
    memmove(static_buf + 64, data, datalen);
  else if(datalen > 0)
    memset(static_buf + 64, 0, datalen);

  if(datalen > 0){
    qtd_init(setup_qtd, qtd1_phys, QTD_TOKEN_PID_SETUP, 0, setup_phys_addr, 8);
    if(setup->bmRequestType & USB_DIR_IN){
      qtd_init(data_qtd, qtd2_phys, QTD_TOKEN_PID_IN, 1, data_phys_addr, datalen);
      qtd_init(status_qtd, 0, QTD_TOKEN_PID_OUT, 1, 0, 0);
    } else {
      qtd_init(data_qtd, qtd2_phys, QTD_TOKEN_PID_OUT, 1, data_phys_addr, datalen);
      qtd_init(status_qtd, 0, QTD_TOKEN_PID_IN, 1, 0, 0);
    }
    status_qtd->token |= QTD_TOKEN_IOC;
  } else {
    qtd_init(setup_qtd, qtd2_phys, QTD_TOKEN_PID_SETUP, 0, setup_phys_addr, 8);
    if(setup->bmRequestType & USB_DIR_IN)
      qtd_init(status_qtd, 0, QTD_TOKEN_PID_OUT, 1, 0, 0);
    else
      qtd_init(status_qtd, 0, QTD_TOKEN_PID_IN, 1, 0, 0);
    status_qtd->token |= QTD_TOKEN_IOC;
  }

  ehci_stop_async();

  // EHCI spec: high-speed default control pipe MUST use MPL=64
  static_qh.epchar = QH_EPCHAR_DEVADDR(addr) |
                      QH_EPCHAR_ENDPT(0) |
                      QH_EPCHAR_EPS_HIGH |
                      QH_EPCHAR_DTC |
                      QH_EPCHAR_H |
                      QH_EPCHAR_MPL(64) |
                      QH_EPCHAR_RL(15);
  static_qh.epcap = QH_EPCAP_MULT(1);
  static_qh.curqtd = 0;
  static_qh.altnextqtd = EHCI_LP_TERMINATE;
  static_qh.token = 0;
  {
    int i;
    for(i = 0; i < 5; i++) static_qh.bufptr[i] = 0;
    for(i = 0; i < 5; i++) static_qh.ext_bufptr[i] = 0;
  }
  static_qh.nextqtd = qtd0_phys;
  memory_barrier();

  if(ehci_start_async() < 0)
    return -1;

  r = ehci_wait_qtd(status_qtd, 500);
  if(r != 0){
    // Print diagnostic info (like reference code)
    cprintf("usb: ctrl fail @%d r=%d tok=%x/%x/%x\n",
            addr, r, setup_qtd->token, data_qtd->token, status_qtd->token);
    ehci_clear_qh();
    return -1;
  }

  if(datalen > 0 && (setup->bmRequestType & USB_DIR_IN))
    memmove(data, static_buf + 64, datalen);

  return 0;
}

// ============================================================================
// USB bulk transfer (with data toggle tracking from hardware)
// ============================================================================
static int
usb_bulk(uint addr, uint ep_num, int is_in, void *data, int datalen,
         ushort maxpkt)
{
  struct ehci_qtd *qtd = &static_qtd[0];
  uchar *xfer_buf = static_buf + 1024;
  uint xfer_phys = buf_phys + 1024;
  uint qtd_phys_addr = v2p((void*)&static_qtd[0]);
  int toggle_idx, r;

  if(!is_in && datalen > 0)
    memmove(xfer_buf, data, datalen);
  else if(datalen > 0)
    memset(xfer_buf, 0, datalen);

  toggle_idx = (int)(ep_num & 0x0F) * 2 + (is_in ? 1 : 0);

  qtd_init(qtd, 0,
           is_in ? QTD_TOKEN_PID_IN : QTD_TOKEN_PID_OUT,
           bulk_toggle[toggle_idx],
           xfer_phys, datalen);
  qtd->token |= QTD_TOKEN_IOC;

  ehci_stop_async();

  static_qh.epchar = QH_EPCHAR_DEVADDR(addr) |
                      QH_EPCHAR_ENDPT(ep_num) |
                      QH_EPCHAR_EPS_HIGH |
                      QH_EPCHAR_DTC |
                      QH_EPCHAR_H |
                      QH_EPCHAR_MPL(maxpkt) |
                      QH_EPCHAR_RL(15);
  static_qh.epcap = QH_EPCAP_MULT(1);
  static_qh.curqtd = 0;
  static_qh.altnextqtd = EHCI_LP_TERMINATE;
  static_qh.token = 0;
  {
    int i;
    for(i = 0; i < 5; i++) static_qh.bufptr[i] = 0;
    for(i = 0; i < 5; i++) static_qh.ext_bufptr[i] = 0;
  }
  static_qh.nextqtd = qtd_phys_addr;
  memory_barrier();

  ehci_start_async();

  r = ehci_wait_qtd(qtd, 2000);

  // Read toggle from hardware
  bulk_toggle[toggle_idx] = (uchar)((qtd->token >> 31) & 1);

  if(r != 0){
    ehci_clear_qh();
    return -1;
  }

  if(is_in && datalen > 0)
    memmove(data, xfer_buf, datalen);

  return 0;
}

// ============================================================================
// USB standard requests
// ============================================================================
static int
usb_get_descriptor(uint addr, uint desc_type, uint desc_idx, void *buf, int len)
{
  struct usb_setup setup;
  setup.bmRequestType = USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE;
  setup.bRequest = USB_REQ_GET_DESCRIPTOR;
  setup.wValue = (ushort)((desc_type << 8) | desc_idx);
  setup.wIndex = 0;
  setup.wLength = (ushort)len;
  return usb_control(addr, &setup, buf, len);
}

static int
usb_set_address(uint old_addr, uint new_addr)
{
  struct usb_setup setup;
  setup.bmRequestType = USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE;
  setup.bRequest = USB_REQ_SET_ADDRESS;
  setup.wValue = (ushort)new_addr;
  setup.wIndex = 0;
  setup.wLength = 0;
  return usb_control(old_addr, &setup, 0, 0);
}

static int
usb_set_config(uint addr, uint config)
{
  struct usb_setup setup;
  setup.bmRequestType = USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE;
  setup.bRequest = USB_REQ_SET_CONFIG;
  setup.wValue = (ushort)config;
  setup.wIndex = 0;
  setup.wLength = 0;
  return usb_control(addr, &setup, 0, 0);
}

// ============================================================================
// USB Hub operations
// ============================================================================
static int
hub_get_desc(uint addr, void *buf, int len)
{
  struct usb_setup setup;
  setup.bmRequestType = USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_DEVICE;
  setup.bRequest = HUB_REQ_GET_DESCRIPTOR;
  setup.wValue = (USB_DESC_HUB << 8);
  setup.wIndex = 0;
  setup.wLength = (ushort)len;
  return usb_control(addr, &setup, buf, len);
}

static int
hub_set_feature(uint addr, uint port, uint feature)
{
  struct usb_setup setup;
  setup.bmRequestType = USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_OTHER;
  setup.bRequest = HUB_REQ_SET_FEATURE;
  setup.wValue = (ushort)feature;
  setup.wIndex = (ushort)port;
  setup.wLength = 0;
  return usb_control(addr, &setup, 0, 0);
}

static int
hub_clear_feature(uint addr, uint port, uint feature)
{
  struct usb_setup setup;
  setup.bmRequestType = USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_OTHER;
  setup.bRequest = HUB_REQ_CLEAR_FEATURE;
  setup.wValue = (ushort)feature;
  setup.wIndex = (ushort)port;
  setup.wLength = 0;
  return usb_control(addr, &setup, 0, 0);
}

static int
hub_get_port_status(uint addr, uint port, uint *status)
{
  struct usb_setup setup;
  uchar resp[4];
  int r;
  setup.bmRequestType = USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_OTHER;
  setup.bRequest = HUB_REQ_GET_STATUS;
  setup.wValue = 0;
  setup.wIndex = (ushort)port;
  setup.wLength = 4;
  memset(resp, 0, 4);
  r = usb_control(addr, &setup, resp, 4);
  if(r == 0)
    *status = *(uint*)resp;
  else
  *status = 0;
  return r;
}

// ============================================================================
// MSC SCSI commands
// ============================================================================
static int
msd_scsi_command(uchar *cmd, int cmdlen, void *data, int datalen, int dir_in)
{
  struct usb_cbw cbw;
  struct usb_csw csw;
  int r;

  memset(&cbw, 0, sizeof(cbw));
  cbw.dCBWSignature = CBW_SIGNATURE;
  cbw.dCBWTag = msd_tag++;
  cbw.dCBWDataTransferLength = (uint)datalen;
  cbw.bmCBWFlags = dir_in ? 0x80 : 0x00;
  cbw.bCBWLUN = 0;
  cbw.bCBWCBLength = (uchar)cmdlen;
  memmove(cbw.CBWCB, cmd, cmdlen);

  r = usb_bulk(msd_addr, msd_ep_out, 0, &cbw, 31, msd_maxpkt_out);
  if(r < 0) return -1;

  if(datalen > 0){
    if(dir_in)
      r = usb_bulk(msd_addr, msd_ep_in, 1, data, datalen, msd_maxpkt_in);
    else
      r = usb_bulk(msd_addr, msd_ep_out, 0, data, datalen, msd_maxpkt_out);
    if(r < 0) return -1;
  }

  memset(&csw, 0, sizeof(csw));
  r = usb_bulk(msd_addr, msd_ep_in, 1, &csw, 13, msd_maxpkt_in);
  if(r < 0) return -1;
  if(csw.dCSWSignature != CSW_SIGNATURE) return -1;
  if(csw.bCSWStatus != 0) return -1;

  return 0;
}

// ============================================================================
// Sector read/write
// ============================================================================
static int
usb_read_sector(uint lba, void *buf)
{
  uchar cmd[10];
  int r, retry;
  memset(cmd, 0, 10);
  cmd[0] = SCSI_READ_10;
  cmd[2] = (uchar)((lba >> 24) & 0xFF);
  cmd[3] = (uchar)((lba >> 16) & 0xFF);
  cmd[4] = (uchar)((lba >> 8) & 0xFF);
  cmd[5] = (uchar)(lba & 0xFF);
  cmd[8] = 1;
  for(retry = 0; retry < 10; retry++){
    r = msd_scsi_command(cmd, 10, buf, 512, 1);
    if(r == 0) return 0;
    delay_ms(20);
  }
  return -1;
}

static int
usb_write_sector(uint lba, void *buf)
{
  uchar cmd[10];
  int r, retry;
  memset(cmd, 0, 10);
  cmd[0] = SCSI_WRITE_10;
  cmd[2] = (uchar)((lba >> 24) & 0xFF);
  cmd[3] = (uchar)((lba >> 16) & 0xFF);
  cmd[4] = (uchar)((lba >> 8) & 0xFF);
  cmd[5] = (uchar)(lba & 0xFF);
  cmd[8] = 1;
  for(retry = 0; retry < 10; retry++){
    r = msd_scsi_command(cmd, 10, buf, 512, 0);
    if(r == 0) return 0;
    delay_ms(20);
  }
  return -1;
}

/*
 * 仅「运行位被清、调度停」时恢复异步门铃，不做端口复位与重新枚举。
 * 模拟器常在首访块设备前短暂置位「已停止」，若直接走完整复位会误报 I/O 并刷屏。
 * 返回：0 表示本就未处于停止态；-1 表示仍停止或启动失败；1 表示已从停止态恢复运行。
 */
static int
ehci_try_unhalt(void)
{
  uint sts, cmd;
  int t;

  if(!ehci_opbase)
    return -1;

  sts = ehci_op_read(EHCI_OP_USBSTS);
  if(!(sts & EHCI_STS_HCHALTED))
    return 0;

  if(sts & EHCI_STS_HSE){
    ehci_op_write(EHCI_OP_USBSTS, EHCI_STS_HSE);
    clear_pci_errors();
    pci_reenable_busmaster();
  }
  ehci_op_write(EHCI_OP_USBSTS, 0x3F);

  cmd = ehci_op_read(EHCI_OP_USBCMD);
  cmd |= EHCI_CMD_RS | EHCI_CMD_ASE;
  ehci_op_write(EHCI_OP_USBCMD, cmd);

  t = 500000;
  while((ehci_op_read(EHCI_OP_USBSTS) & EHCI_STS_HCHALTED) && --t > 0)
    delay_us(1);
  if(t <= 0)
    return -1;

  if(ehci_start_async() < 0)
    return -1;

  delay_ms(2);
  pci_reenable_busmaster();
  clear_pci_errors();
  return 1;
}

/*
 * QEMU 可能把主机控制器复位，客户机仍认为 USB 已就绪，后续扇区读/写会失败。
 * 轻量恢复失败时再完整 HCRESET + 重新枚举 MSD（与 usbinit 末尾路径一致）。
 */
static void
usb_try_recover_disk(void)
{
  int attempt;

  acquire(&usblock);
  if(!usb_ehci_saved_ok){
    release(&usblock);
    return;
  }
  delay_ms(200);
  for(attempt = 0; attempt < 8; attempt++){
    msd_found = 0;
    memset(bulk_toggle, 0, sizeof(bulk_toggle));
    if(ehci_init_reset(&usb_ehci_saved) == 0 && ehci_enumerate_reset() == 0){
      msd_init_device();
      release(&usblock);
      return;
    }
    delay_ms(250);
  }
  release(&usblock);
  panic("usb: recover failed after EHCI reset");
}

// ============================================================================
// Block I/O interface (called by disk.c)
// ============================================================================
void
usb_rw(struct buf *b)
{
  uint sector;

  acquire(&usblock);

  if(!usb_inited || !msd_found){
    release(&usblock);
    panic("usb_rw: not init");
  }

  if(usb_intr_mode){
    while(usb_busy)
      sleep(&usb_busy, &usblock);
    usb_busy = 1;
  }

  if(ehci_opbase && (ehci_op_read(EHCI_OP_USBSTS) & EHCI_STS_HCHALTED)){
    if(usb_intr_mode){
      usb_busy = 0;
      wakeup(&usb_busy);
    }
    release(&usblock);
    if(ehci_try_unhalt() == -1)
      usb_try_recover_disk();
    acquire(&usblock);
    if(!usb_inited || !msd_found){
      release(&usblock);
      panic("usb_rw: not init after recover");
    }
    if(usb_intr_mode){
      while(usb_busy)
        sleep(&usb_busy, &usblock);
      usb_busy = 1;
    }
  }

  sector = b->sector;
  if(b->dev == ROOTDEV)
    sector += FS_SECTOR_OFFSET;

  if(b->flags & B_DIRTY){
    if(usb_write_sector(sector, b->data) < 0){
      if(usb_intr_mode){
        usb_busy = 0;
        wakeup(&usb_busy);
      }
      release(&usblock);
      usb_try_recover_disk();
      acquire(&usblock);
      if(usb_intr_mode){
        while(usb_busy)
          sleep(&usb_busy, &usblock);
        usb_busy = 1;
      }
      if(usb_write_sector(sector, b->data) < 0){
        {
          volatile ushort *v = (volatile ushort *)P2V(0xB8000);
          v[24*80+0] = 0x4F00 | 'W';
          v[24*80+1] = 0x4F00 | ('0' + (sector/10000)%10);
          v[24*80+2] = 0x4F00 | ('0' + (sector/1000)%10);
          v[24*80+3] = 0x4F00 | ('0' + (sector/100)%10);
          v[24*80+4] = 0x4F00 | ('0' + (sector/10)%10);
          v[24*80+5] = 0x4F00 | ('0' + sector%10);
        }
        if(usb_intr_mode){
          usb_busy = 0;
          wakeup(&usb_busy);
        }
        release(&usblock);
        return;
      }
    }
    b->flags &= ~B_DIRTY;
  } else {
    if(usb_read_sector(sector, b->data) < 0){
      if(usb_intr_mode){
        usb_busy = 0;
        wakeup(&usb_busy);
      }
      release(&usblock);
      usb_try_recover_disk();
      acquire(&usblock);
      if(usb_intr_mode){
        while(usb_busy)
          sleep(&usb_busy, &usblock);
        usb_busy = 1;
      }
      if(usb_read_sector(sector, b->data) < 0){
        {
          volatile ushort *v = (volatile ushort *)P2V(0xB8000);
          v[24*80+0] = 0x4F00 | 'R';
          v[24*80+1] = 0x4F00 | ('0' + (sector/10000)%10);
          v[24*80+2] = 0x4F00 | ('0' + (sector/1000)%10);
          v[24*80+3] = 0x4F00 | ('0' + (sector/100)%10);
          v[24*80+4] = 0x4F00 | ('0' + (sector/10)%10);
          v[24*80+5] = 0x4F00 | ('0' + sector%10);
        }
        if(usb_intr_mode){
          usb_busy = 0;
          wakeup(&usb_busy);
        }
        release(&usblock);
        return;
      }
    }
  }
  b->flags |= B_VALID;

  if(usb_intr_mode){
    usb_busy = 0;
    wakeup(&usb_busy);
  }

  release(&usblock);
}

// ============================================================================
// EHCI port reset (improved for Intel 6 Series)
// ============================================================================
static int
ehci_port_reset(int port)
{
  uint portsc;
  int timeout;

  portsc = ehci_op_read(EHCI_OP_PORTSC(port));
  if(!(portsc & EHCI_PORTSC_CCS))
    return -1;

  // Check line status - release low-speed devices to companion controller
  {
    uint line_status = (portsc >> 10) & 0x03;
    if(line_status == 1){
      cprintf("usb: port %d low-speed, skip\n", port);
      portsc |= EHCI_PORTSC_OWNER;
      portsc &= ~(EHCI_PORTSC_CSC | EHCI_PORTSC_PEC | EHCI_PORTSC_OCC);
  ehci_op_write(EHCI_OP_PORTSC(port), portsc);
      return -1;
    }
  }

  // Assert reset
  ehci_op_write(EHCI_OP_PORTSC(port), EHCI_PORTSC_PP | EHCI_PORTSC_RESET);
  delay_ms(55);

  // De-assert reset
  ehci_op_write(EHCI_OP_PORTSC(port), EHCI_PORTSC_PP);

  // Wait for RESET bit to clear (reference code approach)
  timeout = 200;
  while(timeout > 0){
    delay_ms(1);
  portsc = ehci_op_read(EHCI_OP_PORTSC(port));
    if(!(portsc & EHCI_PORTSC_RESET)) break;
    timeout--;
  }
  if(timeout <= 0){
    cprintf("usb: port %d reset stuck\n", port);
    return -1;
  }

  delay_ms(10);  // Recovery time
  portsc = ehci_op_read(EHCI_OP_PORTSC(port));

  if(!(portsc & EHCI_PORTSC_PE)){
    cprintf("usb: port %d not enabled (sts=%x)\n", port, portsc);
    // Release to companion if device still connected
    if(portsc & EHCI_PORTSC_CCS){
      portsc |= EHCI_PORTSC_OWNER;
      portsc &= ~(EHCI_PORTSC_CSC | EHCI_PORTSC_PEC | EHCI_PORTSC_OCC);
      ehci_op_write(EHCI_OP_PORTSC(port), portsc);
    }
    return -1;
  }

  // Clear change bits
  portsc = ehci_op_read(EHCI_OP_PORTSC(port));
  portsc |= EHCI_PORTSC_CSC | EHCI_PORTSC_PEC | EHCI_PORTSC_OCC;
  ehci_op_write(EHCI_OP_PORTSC(port), portsc);
  delay_ms(10);
  return 0;
}

// ============================================================================
// Enumerate a device at address 0, assign new address
// ============================================================================
static int
enumerate_device(uchar dev_addr)
{
  struct usb_dev_desc *dd;
  uchar resp[64];
  int r, retry;

  dd = (struct usb_dev_desc*)resp;

  // GET_DESCRIPTOR(8 bytes) at address 0 - with retry
  // Some devices need multiple attempts after port reset
  for(retry = 0; retry < 3; retry++){
    memset(resp, 0, 64);
  r = usb_get_descriptor(0, USB_DESC_DEVICE, 0, resp, 8);
    if(r == 0) break;
    delay_ms(50);
  }
  if(r < 0){
    cprintf("usb: enum @0 get_desc8 failed\n");
    return -1;
  }
  delay_ms(5);

  // SET_ADDRESS
  r = usb_set_address(0, dev_addr);
  if(r < 0){
    cprintf("usb: set_addr %d failed\n", dev_addr);
    return -1;
  }
  delay_ms(5);

  // GET full device descriptor at new address
  memset(resp, 0, 64);
  r = usb_get_descriptor(dev_addr, USB_DESC_DEVICE, 0, resp, 18);
  if(r < 0){
    cprintf("usb: enum @%d get_desc18 failed\n", dev_addr);
    return -1;
  }

  return dd->bDeviceClass;
}

// ============================================================================
// Parse config descriptor for MSC endpoints
// ============================================================================
static int
parse_msc_config(uchar dev_addr, uchar *config_buf, int total)
{
  int offset;
  struct usb_config_desc *cfg = (struct usb_config_desc*)config_buf;

  offset = cfg->bLength;
  while(offset < total){
    uchar *p = config_buf + offset;
    uchar dlen = p[0], dtype = p[1];
    if(dlen == 0) break;

    if(dtype == USB_DESC_INTERFACE){
      struct usb_iface_desc *iface = (struct usb_iface_desc*)p;
      if(iface->bInterfaceClass == USB_CLASS_MASS_STORAGE &&
         iface->bInterfaceSubClass == 0x06 &&
         iface->bInterfaceProtocol == 0x50){
        msd_addr = dev_addr;
      }
    }

    if(dtype == USB_DESC_ENDPOINT && msd_addr == dev_addr){
      struct usb_ep_desc *ep = (struct usb_ep_desc*)p;
      if((ep->bmAttributes & 0x03) == 0x02){
        if(ep->bEndpointAddress & 0x80){
          msd_ep_in = ep->bEndpointAddress & 0x0F;
          msd_maxpkt_in = ep->wMaxPacketSize;
        } else {
          msd_ep_out = ep->bEndpointAddress & 0x0F;
          msd_maxpkt_out = ep->wMaxPacketSize;
        }
      }
    }
    offset += dlen;
  }

  if(msd_addr == dev_addr && msd_ep_in && msd_ep_out){
    usb_set_config(dev_addr, cfg->bConfigurationValue);
    bulk_toggle[(int)msd_ep_in * 2 + 1] = 0;
    bulk_toggle[(int)msd_ep_out * 2] = 0;
    msd_found = 1;
    return 0;
  }
  return -1;
}

// ============================================================================
// Hub enumeration (for Intel Rate Matching Hub on X220)
// ============================================================================
static int
setup_hub(uchar hub_addr)
{
  uchar resp[64];
  struct usb_hub_desc *hub_desc;
  uint status;
  int r, port, nports;
  uchar next_addr = hub_addr + 1;

  // CRITICAL: Must SET_CONFIGURATION before class requests!
  memset(resp, 0, 64);
  r = usb_get_descriptor(hub_addr, USB_DESC_CONFIG, 0, resp, 32);
  if(r < 0){
    cprintf("usb: hub %d get_config fail\n", hub_addr);
    return -1;
  }
  struct usb_config_desc *cfg = (struct usb_config_desc*)resp;
  r = usb_set_config(hub_addr, cfg->bConfigurationValue);
  if(r < 0){
    cprintf("usb: hub %d set_config fail\n", hub_addr);
    return -1;
  }
  delay_ms(20);

  // Get hub descriptor
  memset(resp, 0, 64);
  r = hub_get_desc(hub_addr, resp, 16);
  if(r < 0){
    cprintf("usb: hub %d get_desc fail\n", hub_addr);
    return -1;
  }
  hub_desc = (struct usb_hub_desc*)resp;
  nports = hub_desc->bNbrPorts;

  // Power on all ports
  for(port = 1; port <= nports; port++){
    hub_set_feature(hub_addr, port, HUB_PORT_POWER);
    delay_ms(5);
  }
  // Wait for power stabilization + device detection
  {
    uint pwr_delay = (uint)hub_desc->bPwrOn2PwrGood * 2;
    if(pwr_delay < 100) pwr_delay = 100;
    delay_ms((int)(pwr_delay + 100));
  }

  // Enumerate each port (multiple rounds for slow devices)
  {
    int round;
    for(round = 0; round < 3 && !msd_found; round++){
      if(round > 0){
        delay_ms(200);
      }
  for(port = 1; port <= nports && !msd_found; port++){
        int retry;
    status = 0;

        // Read port status with retry
        for(retry = 0; retry < 3; retry++){
          r = hub_get_port_status(hub_addr, port, &status);
          if(r == 0) break;
          delay_ms(20);
        }
        if(r < 0) continue;

        // Clear change bits
        if(status & 0x00010000u)
          hub_clear_feature(hub_addr, port, HUB_C_PORT_CONNECTION);
        if(status & 0x00100000u)
          hub_clear_feature(hub_addr, port, HUB_C_PORT_RESET);

        if(!(status & 0x0001)){
      continue;
        }

        // Reset port via hub
        hub_set_feature(hub_addr, port, HUB_PORT_RESET);
    delay_ms(60);
        hub_clear_feature(hub_addr, port, HUB_C_PORT_RESET);
    delay_ms(10);

    status = 0;
        hub_get_port_status(hub_addr, port, &status);
        if(!(status & 0x0002)){
      continue;
    }
        hub_clear_feature(hub_addr, port, HUB_C_PORT_CONNECTION);
        delay_ms(20);

    int devclass = enumerate_device(next_addr);
        if(devclass < 0){ next_addr++; continue; }

    if(devclass == USB_CLASS_HUB){
      setup_hub(next_addr);
      next_addr++;
    } else {
          uchar config_buf[128];
          memset(config_buf, 0, 128);
      r = usb_get_descriptor(next_addr, USB_DESC_CONFIG, 0, config_buf, 128);
          if(r >= 0){
            int total = ((struct usb_config_desc*)config_buf)->wTotalLength;
      if(total > 128) total = 128;
            parse_msc_config(next_addr, config_buf, total);
          }
          next_addr++;
        }
      }
    }
  }
  return 0;
}

// ============================================================================
// Initialize mass storage (SCSI)
// ============================================================================
static int
msd_init_device(void)
{
  uchar cmd[10];
  uchar data[36];
  int r, retry;

  for(retry = 0; retry < 10; retry++){
    memset(cmd, 0, 6);
    cmd[0] = SCSI_TEST_UNIT_READY;
    r = msd_scsi_command(cmd, 6, 0, 0, 0);
    if(r == 0) break;
    delay_ms(100);
  }

  memset(cmd, 0, 6);
  cmd[0] = SCSI_INQUIRY;
  cmd[4] = 36;
  memset(data, 0, 36);
  msd_scsi_command(cmd, 6, data, 36, 1);

  memset(cmd, 0, 10);
  cmd[0] = SCSI_READ_CAPACITY;
  memset(data, 0, 8);
  r = msd_scsi_command(cmd, 10, data, 8, 1);
  if(r == 0){
    msd_capacity = ((uint)data[0] << 24) | ((uint)data[1] << 16) |
                   ((uint)data[2] << 8) | data[3];
  }
  return 0;
}

// ============================================================================
// EHCI BIOS handoff
// ============================================================================
static void
ehci_bios_handoff(uint bus, uint dev, uint func)
{
  uint hccparams, eecp, cap, legsup;
  int timeout;

  hccparams = ehci_cap_read(EHCI_CAP_HCCPARAMS);
  eecp = (hccparams >> 8) & 0xFF;
  if(eecp < 0x40) return;

  while(eecp >= 0x40){
    cap = pci_read32(bus, dev, func, eecp);
    if((cap & 0xFF) == 0x01) break;
    eecp = (cap >> 8) & 0xFF;
    if(eecp == 0) break;
  }
  if((cap & 0xFF) != 0x01) return;

  legsup = cap;

  if(legsup & (1 << 16)){
    pci_write32(bus, dev, func, eecp, legsup | (1 << 24));
    timeout = 500;
    while(timeout > 0){
      legsup = pci_read32(bus, dev, func, eecp);
      if(!(legsup & (1 << 16))) break;
      delay_ms(1);
      timeout--;
    }
    if(legsup & (1 << 16)){
      pci_write32(bus, dev, func, eecp, (legsup & ~(1u << 16)) | (1u << 24));
      delay_ms(10);
    }
  } else {
    pci_write32(bus, dev, func, eecp, legsup | (1 << 24));
  }

  // Disable ALL SMI sources
  uint legctlsts = pci_read32(bus, dev, func, eecp + 4);
  legctlsts &= ~0xFFFFu;
  pci_write32(bus, dev, func, eecp + 4, legctlsts);

  delay_ms(50);
}

// ============================================================================
// Setup QH for async schedule
// ============================================================================
static void
ehci_setup_async_qh(void)
{
  memset(dma_qh, 0, sizeof(struct ehci_qh));
  static_qh.hlp = qh_phys | EHCI_LP_TYPE_QH;
  static_qh.epchar = QH_EPCHAR_H | QH_EPCHAR_EPS_HIGH |
                      QH_EPCHAR_MPL(64) | QH_EPCHAR_DTC;
  static_qh.epcap = QH_EPCAP_MULT(1);
  static_qh.nextqtd = EHCI_LP_TERMINATE;
  static_qh.altnextqtd = EHCI_LP_TERMINATE;
  static_qh.token = (1 << 6);  // HALT
  memory_barrier();
  ehci_op_write(EHCI_OP_ASYNCLISTADDR, qh_phys);

  // Verify ASYNCLISTADDR was accepted
  uint readback = ehci_op_read(EHCI_OP_ASYNCLISTADDR);
  if(readback != qh_phys)
    cprintf("usb: ASYNCLISTADDR mismatch! wrote=%x read=%x\n", qh_phys, readback);
}

// ============================================================================
// Setup PFL
// ============================================================================
static void
ehci_setup_pfl(void)
{
  int i;
  for(i = 0; i < 1024; i++)
    dma_pfl[i] = EHCI_LP_TERMINATE;
  memory_barrier();
  ehci_op_write(EHCI_OP_PERIODICBASE, pfl_phys);
}

// ============================================================================
// PCI helper functions (simplified - matches reference code)
// ============================================================================
static void
pci_reenable_busmaster(void)
{
  uint cmd = pci_read32(ehci_pci_bus, ehci_pci_dev, ehci_pci_func, PCI_COMMAND);
  uint newcmd = cmd | PCI_CMD_MEM | PCI_CMD_MASTER;
  if(newcmd != cmd)
    pci_write32(ehci_pci_bus, ehci_pci_dev, ehci_pci_func, PCI_COMMAND, newcmd);
}

static void
clear_pci_errors(void)
{
  uint val = pci_read32(ehci_pci_bus, ehci_pci_dev, ehci_pci_func, PCI_STATUS);
  if(val & 0xF900)
    pci_write16(ehci_pci_bus, ehci_pci_dev, ehci_pci_func, PCI_STATUS, val & 0xF900);
}

// ============================================================================
// Phase 1: Takeover mode (no HCRESET)
// ============================================================================
static int
ehci_init_takeover(struct pci_device *pdev)
{
  uint sts, cmd;
  int timeout;

  ehci_bios_handoff(pdev->bus, pdev->dev, pdev->func);
  pci_setup_device(pdev->bus, pdev->dev, pdev->func);
  clear_pci_errors();

  sts = ehci_op_read(EHCI_OP_USBSTS);

  // Ensure CONFIGFLAG=1
  if(ehci_op_read(EHCI_OP_CONFIGFLAG) != EHCI_CF_CF){
    ehci_op_write(EHCI_OP_CONFIGFLAG, EHCI_CF_CF);
    delay_ms(20);
  }

  // Ensure CTRLDSSEG=0 (32-bit DMA addresses)
  // Safe to write even while running - it's a latched register
  ehci_op_write(EHCI_OP_CTRLDSSEG, 0);

  // Stop async schedule safely to replace ASYNCLISTADDR
  ehci_stop_async();

  // Replace async QH - this ONLY writes ASYNCLISTADDR
  ehci_setup_async_qh();

  if(sts & EHCI_STS_HCHALTED){
    // Controller halted - do full register init
    ehci_setup_pfl();
    ehci_op_write(EHCI_OP_USBSTS, 0x3F);
    ehci_op_write(EHCI_OP_USBINTR, 0);
    cmd = EHCI_CMD_RS | EHCI_CMD_ASE;
    ehci_op_write(EHCI_OP_USBCMD, cmd);
    timeout = 500000;
    while((ehci_op_read(EHCI_OP_USBSTS) & EHCI_STS_HCHALTED) && --timeout > 0)
      ;
    if(timeout <= 0) return -1;
  } else {
    // Controller already running - ONLY enable ASE
    // CRITICAL: Do NOT write PERIODICBASE (PSS may be active!)
  cmd = ehci_op_read(EHCI_OP_USBCMD);
    cmd |= EHCI_CMD_RS | EHCI_CMD_ASE;
  ehci_op_write(EHCI_OP_USBCMD, cmd);
  }

  if(ehci_start_async() < 0){
    cprintf("usb: takeover ASE fail\n");
      return -1;
    }
  delay_ms(5);

  // Verify async schedule is actually running
  sts = ehci_op_read(EHCI_OP_USBSTS);

  if(sts & EHCI_STS_HSE){
    ehci_op_write(EHCI_OP_USBSTS, EHCI_STS_HSE);
    clear_pci_errors();
  delay_ms(10);
  }

  pci_reenable_busmaster();
  clear_pci_errors();
  return 0;
}

// ============================================================================
// Takeover enumeration: scan BIOS addresses (NO port reset!)
//
// CRITICAL for X220: In takeover mode, BIOS already enumerated the Intel
// Rate Matching Hub and downstream devices. Doing EHCI port reset would
// disrupt this configuration. Instead, scan addresses 1-10 for already-
// configured devices. If we find a hub, enumerate its downstream ports.
// ============================================================================
static int
ehci_enumerate_takeover(void)
{
  uchar resp[128];
  struct usb_dev_desc *dd;
  int addr, r;
  int max_bios_addr = 0;

  for(addr = 1; addr <= 10 && !msd_found; addr++){
    dd = (struct usb_dev_desc*)resp;
    memset(resp, 0, 64);
    r = usb_get_descriptor((uint)addr, USB_DESC_DEVICE, 0, resp, 18);
    if(r < 0) continue;

    if(addr > max_bios_addr) max_bios_addr = addr;

    if(dd->bDeviceClass == USB_CLASS_HUB){
      setup_hub((uchar)addr);
      continue;
    }

    // Check for MSC (class 0 = check interface, class 8 = MSC)
    if(dd->bDeviceClass == 0 || dd->bDeviceClass == USB_CLASS_MASS_STORAGE){
      memset(resp, 0, 128);
      r = usb_get_descriptor((uint)addr, USB_DESC_CONFIG, 0, resp, 128);
      if(r >= 0){
        int total = ((struct usb_config_desc*)resp)->wTotalLength;
        if(total > 128) total = 128;
        parse_msc_config((uchar)addr, resp, total);
      }
    }
  }

  if(msd_found) return 0;

  // Fallback: BIOS address scan found nothing or no MSC
  // Try root port reset + enumerate (still in takeover, no HCRESET)
  if(max_bios_addr == 0){
    uchar dev_addr = 1;
    uint port;
    for(port = 0; port < ehci_nports && !msd_found; port++){
      uint sts = ehci_op_read(EHCI_OP_PORTSC(port));
      if(!(sts & EHCI_PORTSC_CCS)) continue;
      if(ehci_port_reset((int)port) < 0) continue;
      delay_ms(50);

      int devclass = enumerate_device(dev_addr);
      if(devclass < 0){ dev_addr++; continue; }
    if(devclass == USB_CLASS_HUB){
      setup_hub(dev_addr);
    } else {
        uchar config_buf[128];
        memset(config_buf, 0, 128);
        r = usb_get_descriptor(dev_addr, USB_DESC_CONFIG, 0, config_buf, 128);
      if(r >= 0){
          int total = ((struct usb_config_desc*)config_buf)->wTotalLength;
        if(total > 128) total = 128;
          parse_msc_config(dev_addr, config_buf, total);
        }
      }
      dev_addr++;
    }
  }

  return msd_found ? 0 : -1;
}

// ============================================================================
// Phase 2: Full reset mode (HCRESET)
// ============================================================================
static int
ehci_init_reset(struct pci_device *pdev)
{
  uint cmd, sts;
  int timeout;

  ehci_bios_handoff(pdev->bus, pdev->dev, pdev->func);
  pci_setup_device(pdev->bus, pdev->dev, pdev->func);

  // Halt
  cmd = ehci_op_read(EHCI_OP_USBCMD);
  cmd &= ~(EHCI_CMD_RS | EHCI_CMD_ASE | EHCI_CMD_PSE);
  ehci_op_write(EHCI_OP_USBCMD, cmd);
  timeout = 500000;
  while(!(ehci_op_read(EHCI_OP_USBSTS) & EHCI_STS_HCHALTED) && --timeout > 0)
    ;
  delay_ms(10);

  // HCRESET
  ehci_op_write(EHCI_OP_USBCMD, EHCI_CMD_HCRESET);
  timeout = 500000;
  while((ehci_op_read(EHCI_OP_USBCMD) & EHCI_CMD_HCRESET) && --timeout > 0)
    ;
  if(timeout <= 0) return -1;
  delay_ms(50);

  pci_setup_device(pdev->bus, pdev->dev, pdev->func);
  clear_pci_errors();

  ehci_op_write(EHCI_OP_USBINTR, 0);
  ehci_op_write(EHCI_OP_USBSTS, 0x3F);
  ehci_op_write(EHCI_OP_CTRLDSSEG, 0);

  ehci_setup_pfl();
  ehci_setup_async_qh();

  // CONFIGFLAG=1 (route all ports to EHCI)
  ehci_op_write(EHCI_OP_CONFIGFLAG, EHCI_CF_CF);

  // Start controller
  cmd = EHCI_CMD_RS | EHCI_CMD_ASE;
  ehci_op_write(EHCI_OP_USBCMD, cmd);
  timeout = 500000;
  while((ehci_op_read(EHCI_OP_USBSTS) & EHCI_STS_HCHALTED) && --timeout > 0)
    ;
  if(timeout <= 0) return -1;

  // Power on all root ports
  {
    uint i;
    for(i = 0; i < ehci_nports; i++)
      ehci_op_write(EHCI_OP_PORTSC(i), EHCI_PORTSC_PP);
  }

  pci_reenable_busmaster();
  clear_pci_errors();

  // CRITICAL: Wait 500ms for CONFIGFLAG routing + port power + device detection
  // Intel 6 Series RMH needs this long to stabilize
  delay_ms(500);

  sts = ehci_op_read(EHCI_OP_USBSTS);

  if(sts & EHCI_STS_HSE){
    ehci_op_write(EHCI_OP_USBSTS, EHCI_STS_HSE);
    clear_pci_errors();
    pci_reenable_busmaster();
    delay_ms(10);
  }

  ehci_op_write(EHCI_OP_USBSTS, 0x3F);

  return 0;
}

// ============================================================================
// Full reset enumeration (with port reset)
// ============================================================================
static int
ehci_enumerate_reset(void)
{
  uint sts;
  int port;
  uchar dev_addr = 1;
  int hse_retry_done = 0;

  msd_addr = 0;
  msd_ep_in = 0;
  msd_ep_out = 0;

  for(port = 0; port < (int)ehci_nports && !msd_found; port++){
    sts = ehci_op_read(EHCI_OP_PORTSC(port));
    if(!(sts & EHCI_PORTSC_CCS)) continue;

    if(ehci_port_reset(port) < 0){
      continue;
    }
    delay_ms(50);

    int devclass = enumerate_device(dev_addr);

    // If first enumeration fails with DMA error, clear and retry once
    if(devclass < 0 && !hse_retry_done){
      hse_retry_done = 1;
      sts = ehci_op_read(EHCI_OP_USBSTS);
      if(sts & (EHCI_STS_HSE | EHCI_STS_USBERRINT)){
        ehci_op_write(EHCI_OP_USBSTS, sts & 0x3F);
        clear_pci_errors();
        pci_reenable_busmaster();
        delay_ms(50);

        // Re-reset the port and try again
        if(ehci_port_reset(port) == 0){
          delay_ms(50);
          devclass = enumerate_device(dev_addr);
        }
      }
    }

    if(devclass < 0){ dev_addr++; continue; }

    if(devclass == USB_CLASS_HUB){
      setup_hub(dev_addr);
      dev_addr++;
    } else {
      uchar config_buf[128];
      int r;
      memset(config_buf, 0, 128);
      r = usb_get_descriptor(dev_addr, USB_DESC_CONFIG, 0, config_buf, 128);
      if(r >= 0){
        int total = ((struct usb_config_desc*)config_buf)->wTotalLength;
        if(total > 128) total = 128;
        parse_msc_config(dev_addr, config_buf, total);
      }
      dev_addr++;
    }
  }
  return msd_found ? 0 : -1;
}

// ============================================================================
// EHCI controller init entry point
// ============================================================================
static int
ehci_init(struct pci_device *pdev)
{
  uint bar0_phys = pdev->bar0;
  uint caplength, hcsparams;

  ehci_pci_bus = pdev->bus;
  ehci_pci_dev = pdev->dev;
  ehci_pci_func = pdev->func;

  if(bar0_phys < DEVSPACE) return -1;
  ehci_base = bar0_phys;

  caplength = ehci_cap_read(EHCI_CAP_CAPLENGTH) & 0xFF;
  hcsparams = ehci_cap_read(EHCI_CAP_HCSPARAMS);
  ehci_nports = hcsparams & 0x0F;
  ehci_opbase = ehci_base + caplength;

  // Phase 1: Takeover + scan BIOS addresses (no port reset!)
  if(ehci_init_takeover(pdev) == 0){
    if(ehci_enumerate_takeover() == 0)
      return 0;
  }

  // Phase 2: Full HCRESET + port reset enumeration
  msd_found = 0;
  msd_addr = 0;
  msd_ep_in = 0;
  msd_ep_out = 0;

  if(ehci_init_reset(pdev) == 0){
    if(ehci_enumerate_reset() == 0)
      return 0;
  }

  return msd_found ? 0 : -1;
}

// ============================================================================
// USB subsystem init (public API)
// ============================================================================
int
usbinit(void)
{
  struct pci_device ehci_devs[4];
  int nehci, i;

  initlock(&usblock, "usb");
  memset(bulk_toggle, 0, sizeof(bulk_toggle));

  // DMA buffers are in kernel BSS (static allocation)
  // Use V2P to get physical addresses for EHCI controller
  qh_phys  = v2p((void*)&dma_qh_bss);
  pfl_phys = v2p((void*)dma_pfl_bss);
  buf_phys = v2p((void*)dma_buf_bss);

  // Zero DMA buffers (BSS is already zeroed, but be explicit)
  memset(&dma_qh_bss, 0, sizeof(dma_qh_bss));
  memset(dma_qtd_bss, 0, sizeof(dma_qtd_bss));
  memset(dma_pfl_bss, 0, sizeof(dma_pfl_bss));
  memset(dma_buf_bss, 0, sizeof(dma_buf_bss));

  nehci = pci_find_ehci(ehci_devs, 4);
  if(nehci == 0){
    cprintf("usb: no EHCI found");
    boot_fail();
    return -1;
  }

  for(i = 0; i < nehci && !msd_found; i++){
    usb_irq = ehci_devs[i].irq;
    msd_found = 0;
    ehci_init(&ehci_devs[i]);
  }

  if(!msd_found){
    cprintf("usb: no mass storage");
    boot_fail();
    return -1;
  }

  {
    int j;
    for(j = 0; j < nehci; j++){
      if(ehci_devs[j].bar0 == ehci_base){
        memmove(&usb_ehci_saved, &ehci_devs[j], sizeof(usb_ehci_saved));
        usb_ehci_saved_ok = 1;
        break;
      }
    }
  }

  msd_init_device();
  usb_inited = 1;

  if(usb_irq > 0){
    ioapicenable_level(usb_irq, 0);
    usb_intr_mode = 1;
  }
  return 0;
}

int
usb_is_available(void)
{
  return usb_inited && msd_found;
}

void
usb_intr(void)
{
  uint sts;
  if(!ehci_opbase) return;
  sts = ehci_op_read(EHCI_OP_USBSTS);
  if(sts & (EHCI_STS_USBINT | EHCI_STS_USBERRINT)){
    ehci_op_write(EHCI_OP_USBSTS, sts & (EHCI_STS_USBINT | EHCI_STS_USBERRINT));
    usb_xfer_done = 1;
    wakeup((void*)&usb_xfer_done);
  }
  if(sts & EHCI_STS_PCD)
    ehci_op_write(EHCI_OP_USBSTS, EHCI_STS_PCD);
  if(sts & EHCI_STS_HSE){
    ehci_op_write(EHCI_OP_USBSTS, EHCI_STS_HSE);
    clear_pci_errors();
    pci_reenable_busmaster();
  }
}

int
usb_get_irq(void)
{
  return usb_irq;
}

uint
usb_get_capacity(void)
{
  return msd_capacity;
}

uint
usb_get_nports(void)
{
  return ehci_nports;
}

uint
usb_get_ehci_base(void)
{
  return ehci_base;
}

// Switch USB to pure polling mode: disable EHCI hardware interrupts
// and IOAPIC IRQ. This eliminates ALL interrupt-related races between
// the EHCI interrupt handler (on BSP) and USB I/O (on any CPU).
// On ThinkPad X220 with two EHCI controllers, the second controller
// (which we don't manage) can also share the same IRQ line, causing
// interrupt storms that the handler can't clear (level-triggered).
void
usb_disable_interrupts(void)
{
  if(!ehci_opbase) return;
  // Disable all EHCI interrupt sources in hardware
  ehci_op_write(EHCI_OP_USBINTR, 0);
  // Clear any pending EHCI status bits
  ehci_op_write(EHCI_OP_USBSTS,
    ehci_op_read(EHCI_OP_USBSTS) & 0x3F);
  // Disable USB IRQ in IOAPIC (prevents level-triggered storm)
  if(usb_irq > 0)
    ioapicdisable(usb_irq);
  // Switch to polling mode
  usb_intr_mode = 0;
  // USB switched to polling mode silently
}

// Halt EHCI controller completely - stops all DMA operations.
// MUST be called before system reset on real hardware (X220),
// otherwise active DMA can prevent the chipset from resetting.
void
usb_halt_controller(void)
{
  uint cmd;
  volatile int timeout;

  if(!ehci_opbase) return;

  // Disable all EHCI interrupts first
  ehci_op_write(EHCI_OP_USBINTR, 0);

  // Clear pending status
  ehci_op_write(EHCI_OP_USBSTS, 0x3F);

  // Stop controller: clear Run/Stop, Async Schedule, Periodic Schedule
  cmd = ehci_op_read(EHCI_OP_USBCMD);
  cmd &= ~(EHCI_CMD_RS | EHCI_CMD_ASE | EHCI_CMD_PSE);
  ehci_op_write(EHCI_OP_USBCMD, cmd);

  // Wait for HCHalted (controller confirms DMA stopped)
  for(timeout = 0; timeout < 500000; timeout++){
    if(ehci_op_read(EHCI_OP_USBSTS) & EHCI_STS_HCHALTED)
      break;
  }

  // Disable bus master in PCI config to absolutely stop DMA
  if(ehci_pci_bus || ehci_pci_dev || ehci_pci_func){
    uint pcicmd = pci_read32(ehci_pci_bus, ehci_pci_dev, ehci_pci_func,
                             PCI_COMMAND);
    pcicmd &= ~0x04;  // Clear Bus Master Enable
    pci_write32(ehci_pci_bus, ehci_pci_dev, ehci_pci_func,
                PCI_COMMAND, pcicmd);
  }
}

#include "types.h"
#include "x86.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "pinfo.h"
#include "pci.h"
#include "usb.h"
#include "net.h"
#include "fb_console.h"

int
sys_fork(void)
{
  return fork();
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_getpid(void)
{
  return proc->pid;
}

int
sys_setfgpid(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  setfgpid(pid);
  return 0;
}

int
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = proc->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

int
sys_sleep(void)
{
  int n;
  uint ticks0;
  
  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(proc->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{
  uint xticks;
  
  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

/*
 * 返回当前控制台可见行列：
 * - 帧缓冲启用时，取实际像素分辨率换算后的字符格 rows/cols；
 * - 否则回退到 80x25。
 */
int
sys_consize(void)
{
  int *urows;
  int *ucols;
  int rows = 25;
  int cols = 80;

  if(argptr(0, (char**)&urows, sizeof(*urows)) < 0)
    return -1;
  if(argptr(1, (char**)&ucols, sizeof(*ucols)) < 0)
    return -1;

  if(fb_is_active()){
    rows = (int)fb_rows_get() - 1;  /* 预留最后一行为 panic 行 */
    cols = (int)fb_cols_get();
  }
  if(rows < 2)
    rows = 2;
  *urows = rows;
  *ucols = cols;
  return 0;
}

int
sys_mouse(void)
{
  int op;
  struct mouse_event *pev;
  struct mouse_event kev;

  if(argint(0, &op) < 0)
    return -1;
  if(op == 0)
    return mouse_hw_init();
  if(op != 1)
    return -1;
  if(argptr(1, (char**)&pev, sizeof(struct mouse_event)) < 0)
    return -1;
  if(mouse_pop(&kev) == 0)
    return 0;
  if(copyout(proc->pgdir, (uint)pev, &kev, sizeof(kev)) < 0)
    return -1;
  return 1;
}

int
sys_guimode(void)
{
  int on;

  if(argint(0, &on) < 0)
    return -1;
  fb_gui_mode_set(on);
  return 0;
}

// POSIX time(2)：返回自 1970-01-01 UTC 起的秒数；若 tloc 非空则写入。
int
sys_time(void)
{
  int addr;
  uint t;

  if(argint(0, &addr) < 0)
    return -1;
  t = ktime_now();
  if(addr != 0){
    if((uint)addr >= proc->sz || (uint)addr + 4 > proc->sz)
      return -1;
    *(int*)addr = (int)t;
  }
  return (int)t;
}

int
sys_dhcp(void)
{
  return net_dhcp_acquire();
}

int
sys_getnetcfg(void)
{
  struct net_cfg *ucfg;
  struct net_cfg cfg;

  if(argptr(0, (char**)&ucfg, sizeof(*ucfg)) < 0)
    return -1;
  memset(&cfg, 0, sizeof(cfg));
  net_get_cfg(&cfg);
  if(copyout(proc->pgdir, (uint)ucfg, &cfg, sizeof(cfg)) < 0)
    return -1;
  return 0;
}

int
sys_ping(void)
{
  int ip;
  int count;

  if(argint(0, &ip) < 0)
    return -1;
  if(argint(1, &count) < 0)
    return -1;
  return net_ping((uint)ip, count);
}

int
sys_dig(void)
{
  char *name;
  uint *uip;
  uint ip;

  if(argstr(0, &name) < 0)
    return -1;
  if(argptr(1, (char**)&uip, sizeof(*uip)) < 0)
    return -1;
  if(net_dns_query(name, &ip) < 0)
    return -1;
  if(copyout(proc->pgdir, (uint)uip, &ip, sizeof(ip)) < 0)
    return -1;
  return 0;
}

int
sys_getprocs(void)
{
  struct pinfo *pinfos;
  int max;

  if(argint(1, &max) < 0)
    return -1;
  if(argptr(0, (char**)&pinfos, max * sizeof(struct pinfo)) < 0)
    return -1;
  return getprocs(pinfos, max);
}

// PCI class code to name mapping (common classes)
static char*
pci_class_name(uchar class, uchar subclass)
{
  if(class == 0x00) return "Legacy";
  if(class == 0x01){
    if(subclass == 0x01) return "IDE";
    if(subclass == 0x06) return "SATA";
    return "Storage";
  }
  if(class == 0x02){
    if(subclass == 0x00) return "Ethernet";
    if(subclass == 0x80) return "Network";
    return "Network";
  }
  if(class == 0x03) return "Display";
  if(class == 0x04){
    if(subclass == 0x03) return "Audio";
    return "Multimedia";
  }
  if(class == 0x05) return "Memory";
  if(class == 0x06){
    if(subclass == 0x00) return "Host Bridge";
    if(subclass == 0x01) return "ISA Bridge";
    if(subclass == 0x04) return "PCI Bridge";
    return "Bridge";
  }
  if(class == 0x07){
    if(subclass == 0x00) return "Serial";
    if(subclass == 0x80) return "Communication";
    return "Communication";
  }
  if(class == 0x08) return "System";
  if(class == 0x0C){
    if(subclass == 0x03) return "USB";
    if(subclass == 0x05) return "SMBus";
    return "Serial Bus";
  }
  return "Other";
}

static void
ehci_reboot_cleanup_light(uint dev)
{
  uint bar0, hccparams, eecp, cap, pcicmd;
  volatile uint *mmio;

  if(pci_read16(0, dev, 0, PCI_VENDOR_ID) != 0x8086)
    return;

  bar0 = pci_read32(0, dev, 0, PCI_BAR0) & ~0x0Fu;
  if(bar0 >= 0xFE000000){
    mmio = (volatile uint*)bar0;
    hccparams = mmio[EHCI_CAP_HCCPARAMS / 4];
    eecp = (hccparams >> 8) & 0xFF;
    while(eecp >= 0x40){
      cap = pci_read32(0, dev, 0, eecp);
      if((cap & 0xFF) == 0x01){
        cap &= ~(1u << 24);
        cap |= (1u << 16);
        pci_write32(0, dev, 0, eecp, cap);
        cap = pci_read32(0, dev, 0, eecp + 4);
        pci_write32(0, dev, 0, eecp + 4, cap & ~0xFFFFu);
        break;
      }
      eecp = (cap >> 8) & 0xFF;
    }
  }

  pcicmd = pci_read16(0, dev, 0, PCI_COMMAND);
  pcicmd &= ~PCI_CMD_MASTER;
  pcicmd |= PCI_CMD_MEM;
  pci_write16(0, dev, 0, PCI_COMMAND, pcicmd);
}

static void
reboot_firmware_prep(void)
{
  // Give BIOS a quieter USB/PIC state before chipset reset without touching
  // the heavier EHCI halt path that previously self-hung on the X220.
  ehci_reboot_cleanup_light(0x1a);
  ehci_reboot_cleanup_light(0x1d);

  outb(0x20, 0x11);
  outb(0x21, 0x08);
  outb(0x21, 0x04);
  outb(0x21, 0x01);
  outb(0xA0, 0x11);
  outb(0xA1, 0x70);
  outb(0xA1, 0x02);
  outb(0xA1, 0x01);
  outb(0x21, 0x00);
  outb(0xA1, 0x00);
}

static void
i8042_reboot_cmd(uchar cmd)
{
  int i;
  uchar status;

  for(i = 0; i < 1000000; i++){
    status = inb(0x64);
    if(!(status & 0x02))
      break;
    if(status & 0x01)
      inb(0x60);
  }

  outb(0x64, cmd);

  for(i = 0; i < 100000; i++)
    outb(0x80, 0);
}

static void
cf9_reset(uchar code)
{
  // Some real chipsets can wedge on the "0x02 then code" sequence.
  // Keep the CF9 write minimal and issue the target value directly.
  outb(0xCF9, code);
}

// 320x200x256-compatible VGA register set.
static const uchar vga_mode13_regs[] = {
  0x63,
  0x03, 0x01, 0x0F, 0x00, 0x0E,
  0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0xBF, 0x1F,
  0x00, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x9C, 0x0E, 0x8F, 0x28, 0x40, 0x96, 0xB9, 0xA3, 0xFF,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x05, 0x0F, 0xFF,
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
  0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
  0x41, 0x00, 0x0F, 0x00, 0x00
};

static void
vga_write_regs(const uchar *regs)
{
  int i;
  uchar crtc[25];

  outb(0x3C2, *regs++);

  // Sequencer synchronous reset while programming VGA mode registers.
  outb(0x3C4, 0x00);
  outb(0x3C5, 0x01);
  for(i = 0; i < 5; i++){
    outb(0x3C4, i);
    outb(0x3C5, *regs++);
  }
  outb(0x3C4, 0x00);
  outb(0x3C5, 0x03);

  for(i = 0; i < 25; i++)
    crtc[i] = *regs++;
  crtc[0x03] |= 0x80;
  crtc[0x11] &= ~0x80;

  outb(0x3D4, 0x03);
  outb(0x3D5, inb(0x3D5) | 0x80);
  outb(0x3D4, 0x11);
  outb(0x3D5, inb(0x3D5) & 0x7F);
  for(i = 0; i < 25; i++){
    outb(0x3D4, i);
    outb(0x3D5, crtc[i]);
  }

  for(i = 0; i < 9; i++){
    outb(0x3CE, i);
    outb(0x3CF, *regs++);
  }

  for(i = 0; i < 21; i++){
    inb(0x3DA);
    outb(0x3C0, i);
    outb(0x3C0, *regs++);
  }
  inb(0x3DA);
  outb(0x3C0, 0x20);
}

static void
vga_set_rgb332_palette(void)
{
  int i;

  outb(0x3C6, 0xFF);
  outb(0x3C8, 0);
  for(i = 0; i < 256; i++){
    uchar r = ((i >> 5) & 0x07) * 9;
    uchar g = ((i >> 2) & 0x07) * 9;
    uchar b = (i & 0x03) * 21;
    outb(0x3C9, r);
    outb(0x3C9, g);
    outb(0x3C9, b);
  }
}

/* 与 RGB332 编码一致，供 VGA 回退路径将 BGR888 转为调色板索引 */
static uchar
rgb332_from_bgr(uchar bb, uchar bg, uchar br)
{
  return (uchar)(((br & 0xE0) | ((bg & 0xE0) >> 3) | ((bb & 0xC0) >> 6)) & 0xFF);
}

static int
draw_to_framebuffer(char *pixels, int srcw, int srch, int pixbytes)
{
  volatile uchar *mode = (volatile uchar*)P2V(0x9000);
  volatile uchar *vesa_ok = (volatile uchar*)P2V(0x8FF0);
  volatile uchar *bda_mode = (volatile uchar*)P2V(0x449);
  ushort mode_attrs;
  ushort width, height, pitch;
  uchar bpp;
  uint physbase;
  volatile uchar *fb;
  uint nbytes;
  uint i;
  int x, y;

  mode_attrs = *(volatile ushort*)(mode + 0x00);
  pitch = *(volatile ushort*)(mode + 0x10);
  width = *(volatile ushort*)(mode + 0x12);
  height = *(volatile ushort*)(mode + 0x14);
  bpp = *(volatile uchar*)(mode + 0x19);
  physbase = *(volatile uint*)(mode + 0x28);

  if(*vesa_ok != 1)
    return -1;
  if(*bda_mode == 0x03 || *bda_mode == 0x07)
    return -1;

  if(!(mode_attrs & 0x80) || width == 0 || height == 0)
    return -1;
  if(pitch == 0)
    return -1;
  if(bpp != 16 && bpp != 24 && bpp != 32)
    return -1;
  if(pixbytes != 1 && pixbytes != 3)
    return -1;

  nbytes = pitch * height;
  for(i = 0; i < nbytes; i += PGSIZE){
    if(acpi_map_phys(physbase + i) == 0)
      return -1;
  }
  fb = (volatile uchar*)acpi_map_phys(physbase);
  if(fb == 0)
    return -1;

  for(y = 0; y < height; y++){
    int sy = (y * srch) / height;
    for(x = 0; x < width; x++){
      int sx = (x * srcw) / width;
      uchar br, bg, bb;

      if(pixbytes == 1){
        /* 用户缓冲为 RGB332，与 tools/bmp_to_rgb332.py 一致 */
        uchar c = (uchar)pixels[sy * srcw + sx];
        ushort r5 = (ushort)((c >> 5) & 0x07) * 31 / 7;
        ushort g6 = (ushort)((c >> 2) & 0x07) * 63 / 7;
        ushort b5 = (ushort)(c & 0x03) * 31 / 3;
        br = (uchar)((r5 * 255) / 31);
        bg = (uchar)((g6 * 255) / 63);
        bb = (uchar)((b5 * 255) / 31);
      } else {
        int off = (sy * srcw + sx) * 3;
        bb = (uchar)pixels[off];
        bg = (uchar)pixels[off + 1];
        br = (uchar)pixels[off + 2];
      }
      if(bpp == 32){
        volatile uchar *p = (volatile uchar*)(fb + y * pitch + x * 4);
        p[0] = bb;
        p[1] = bg;
        p[2] = br;
        p[3] = 0xFF;
      } else if(bpp == 24){
        volatile uchar *p = (volatile uchar*)(fb + y * pitch + x * 3);
        p[0] = bb;
        p[1] = bg;
        p[2] = br;
      } else {
        ushort r5 = (ushort)br * 31 / 255;
        ushort g6 = (ushort)bg * 63 / 255;
        ushort b5 = (ushort)bb * 31 / 255;
        volatile ushort *row = (volatile ushort*)(fb + y * pitch);
        row[x] = (r5 << 11) | (g6 << 5) | b5;
      }
    }
  }
  return 0;
}

static int
gui_can_try_runtime_vesa(void)
{
  ushort vid = pci_read16(0, 0, 0, PCI_VENDOR_ID);
  ushort did = pci_read16(0, 0, 0, PCI_DEVICE_ID);

  // QEMU i440fx/q35 host bridges are unstable on this trampoline path.
  if(vid == 0x8086 && (did == 0x1237 || did == 0x29C0))
    return 0;
  // ThinkPad X220 Sandy Bridge host bridge (00:00.0 8086:0104) also hangs
  // during runtime real-mode VBE trampoline on gui command.
  if(vid == 0x8086 && did == 0x0104)
    return 0;
  return 1;
}

static int
gui_should_log(void)
{
  static uint n;
  n++;
  if(n <= 3)
    return 1;
  if((n % 120) == 0)
    return 1;
  return 0;
}

int
sys_gui(void)
{
  char *pixels;
  int len;
  int pixbytes;
  volatile uchar *vram = (volatile uchar*)P2V(0xA0000);
  volatile uchar *mode = (volatile uchar*)P2V(0x9000);
  volatile uchar *vesa_ok = (volatile uchar*)P2V(0x8FF0);
  volatile uchar *bda_mode = (volatile uchar*)P2V(0x449);
  int x, y;
  int i;
  int srcw = 1024;
  int srch = 768;
  ushort mode_attrs, width, height, pitch;
  uchar bpp;
  uint physbase;
  uchar pmin = 0xFF, pmax = 0;
  int psum = 0;
  int vsum = 0;
  int vmin = 255, vmax = 0;
  ushort host_vid, host_did;
  int logit;

  if(argint(1, &len) < 0)
    return -1;
  if(len == srcw * srch)
    pixbytes = 1;
  else if(len == srcw * srch * 3)
    pixbytes = 3;
  else
    return -1;
  if(argptr(0, &pixels, len) < 0)
    return -1;

  if(pixbytes == 1){
    for(i = 0; i < len; i++){
      uchar p = (uchar)pixels[i];
      if(p < pmin)
        pmin = p;
      if(p > pmax)
        pmax = p;
      psum += p;
    }
  } else {
    int nstat = len < 96 ? len : 96;
    pmin = (uchar)pixels[0];
    pmax = pmin;
    psum = 0;
    for(i = 0; i < nstat; i++){
      uchar p = (uchar)pixels[i];
      if(p < pmin)
        pmin = p;
      if(p > pmax)
        pmax = p;
      psum += p;
    }
  }

  mode_attrs = *(volatile ushort*)(mode + 0x00);
  pitch = *(volatile ushort*)(mode + 0x10);
  width = *(volatile ushort*)(mode + 0x12);
  height = *(volatile ushort*)(mode + 0x14);
  bpp = *(volatile uchar*)(mode + 0x19);
  physbase = *(volatile uint*)(mode + 0x28);
  host_vid = pci_read16(0, 0, 0, PCI_VENDOR_ID);
  host_did = pci_read16(0, 0, 0, PCI_DEVICE_ID);
  logit = gui_should_log();

  if(logit){
    cprintf("gui: img len=%d min=%d max=%d sum=%d head=%d,%d,%d,%d\n",
            len, pmin, pmax, psum,
            (uchar)pixels[0], (uchar)pixels[1], (uchar)pixels[2], (uchar)pixels[3]);
    cprintf("gui: pre vbe ok=%d bda=%x attrs=%x %dx%d pitch=%d bpp=%d phys=%x host=%x:%x\n",
            *vesa_ok, *bda_mode, mode_attrs, width, height, pitch, bpp, physbase, host_vid, host_did);
  }

  // Step1: try runtime VESA mode switch. Failure is allowed and will fallback.
  // Skip this path on QEMU chipsets where real-mode trampoline is unstable.
  if(gui_can_try_runtime_vesa()){
    extern uchar _binary_vesareal_start[], _binary_vesareal_size[];
    extern pde_t entrypgdir[];
    uchar *code = (uchar*)P2V(0x5000);
    volatile uchar *vesa_ok = (volatile uchar*)P2V(0x8FF0);
    uint oldcr3;
    void (*vesa_call)(void);

    *vesa_ok = 0;
    if(logit)
      cprintf("gui: runtime vesa try\n");
    memmove(code, _binary_vesareal_start, (uint)_binary_vesareal_size);
    pushcli();
    oldcr3 = rcr3();
    lcr3(v2p(entrypgdir));
    vesa_call = (void(*)(void))0x5000;
    asm volatile("movl %0, %%edx; call *%1"
                 :
                 : "r"(oldcr3), "r"(vesa_call)
                 : "eax", "ecx", "edx", "memory");
    popcli();
    mode_attrs = *(volatile ushort*)(mode + 0x00);
    pitch = *(volatile ushort*)(mode + 0x10);
    width = *(volatile ushort*)(mode + 0x12);
    height = *(volatile ushort*)(mode + 0x14);
    bpp = *(volatile uchar*)(mode + 0x19);
    physbase = *(volatile uint*)(mode + 0x28);
    if(logit){
      cprintf("gui: post vbe ok=%d bda=%x attrs=%x %dx%d pitch=%d bpp=%d phys=%x\n",
              *vesa_ok, *bda_mode, mode_attrs, width, height, pitch, bpp, physbase);
    }
  } else {
    if(logit)
      cprintf("gui: runtime vesa skip on host=%x:%x\n", host_vid, host_did);
  }

  // Preferred path: use VBE framebuffer info probed at boot (if available).
  if(draw_to_framebuffer(pixels, srcw, srch, pixbytes) == 0){
    if(logit)
      cprintf("gui: vesa framebuffer\n");
    return 0;
  }

  // Fallback path: legacy VGA mode13-compatible rendering.
  if(logit)
    cprintf("gui: vga fallback\n");
  vga_write_regs(vga_mode13_regs);
  vga_set_rgb332_palette();

  for(y = 0; y < 200; y++){
    int sy = (y * srch) / 200;
    for(x = 0; x < 320; x++){
      int sx = (x * srcw) / 320;
      if(pixbytes == 1)
        vram[y * 320 + x] = (uchar)pixels[sy * srcw + sx];
      else {
        int off = (sy * srcw + sx) * 3;
        vram[y * 320 + x] = rgb332_from_bgr((uchar)pixels[off],
                                            (uchar)pixels[off + 1],
                                            (uchar)pixels[off + 2]);
      }
    }
  }
  for(i = 0; i < 1024; i++){
    int v = vram[i];
    if(v < vmin)
      vmin = v;
    if(v > vmax)
      vmax = v;
    vsum += v;
  }
  if(logit){
    cprintf("gui: vga sample=%d,%d,%d,%d\n", vram[0], vram[1], vram[2], vram[3]);
    cprintf("gui: vga stats min=%d max=%d sum=%d\n", vmin, vmax, vsum);
  }
  return 0;
}

// System information (CPU, memory, storage, PCI devices)
int
sys_info(void)
{
  uint eax, ebx, ecx, edx;
  char vendor[13];
  char brand[49];
  int i;
  uint family, model, stepping;
  uint freepg;
  extern char end[];

  cprintf("\n========== Hardware Information ==========\n");

  // ---- CPU Information ----
  cprintf("\n--- CPU ---\n");

  // Vendor string (CPUID leaf 0)
  asm volatile("cpuid"
    : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
    : "a"(0));
  memmove(vendor + 0, &ebx, 4);
  memmove(vendor + 4, &edx, 4);
  memmove(vendor + 8, &ecx, 4);
  vendor[12] = 0;
  cprintf("  Vendor:    %s\n", vendor);

  // Brand string (CPUID leaf 0x80000002-4)
  brand[0] = 0;
  asm volatile("cpuid"
    : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
    : "a"(0x80000000));
  if(eax >= 0x80000004){
    for(i = 0; i < 3; i++){
      asm volatile("cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(0x80000002 + i));
      memmove(brand + i*16 + 0,  &eax, 4);
      memmove(brand + i*16 + 4,  &ebx, 4);
      memmove(brand + i*16 + 8,  &ecx, 4);
      memmove(brand + i*16 + 12, &edx, 4);
    }
    brand[48] = 0;
    // Skip leading spaces
    char *b = brand;
    while(*b == ' ') b++;
    cprintf("  Model:     %s\n", b);
  }

  // Family/Model/Stepping (CPUID leaf 1)
  asm volatile("cpuid"
    : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
    : "a"(1));
  family = (eax >> 8) & 0xF;
  model = (eax >> 4) & 0xF;
  stepping = eax & 0xF;
  if(family == 0x6 || family == 0xF)
    model |= ((eax >> 16) & 0xF) << 4;
  if(family == 0xF)
    family += (eax >> 20) & 0xFF;
  cprintf("  Family:    %d  Model: %d  Stepping: %d\n", family, model, stepping);

  // CPU Features (from CPUID leaf 1 EDX and ECX)
  cprintf("  Features:  ");
  if(edx & (1 << 0))  cprintf("FPU ");
  if(edx & (1 << 4))  cprintf("TSC ");
  if(edx & (1 << 5))  cprintf("MSR ");
  if(edx & (1 << 6))  cprintf("PAE ");
  if(edx & (1 << 9))  cprintf("APIC ");
  if(edx & (1 << 15)) cprintf("CMOV ");
  if(edx & (1 << 19)) cprintf("CLFLUSH ");
  if(edx & (1 << 23)) cprintf("MMX ");
  if(edx & (1 << 24)) cprintf("FXSR ");
  if(edx & (1 << 25)) cprintf("SSE ");
  if(edx & (1 << 26)) cprintf("SSE2 ");
  if(edx & (1 << 28)) cprintf("HT ");
  cprintf("\n");
  cprintf("             ");
  if(ecx & (1 << 0))  cprintf("SSE3 ");
  if(ecx & (1 << 9))  cprintf("SSSE3 ");
  if(ecx & (1 << 19)) cprintf("SSE4.1 ");
  if(ecx & (1 << 20)) cprintf("SSE4.2 ");
  if(ecx & (1 << 21)) cprintf("x2APIC ");
  if(ecx & (1 << 25)) cprintf("AES ");
  if(ecx & (1 << 26)) cprintf("XSAVE ");
  if(ecx & (1 << 28)) cprintf("AVX ");
  if(ecx & (1 << 5))  cprintf("VMX ");
  cprintf("\n");

  // Number of CPUs
  cprintf("  Cores:     %d (SMP %s)\n", ncpu, ismp ? "enabled" : "disabled");

  // ---- Cache Information (CPUID leaf 4) ----
  cprintf("\n--- Cache ---\n");
  {
    uint leaf4_eax, leaf4_ebx, leaf4_ecx, leaf4_edx;
    for(i = 0; i < 8; i++){
      asm volatile("cpuid"
        : "=a"(leaf4_eax), "=b"(leaf4_ebx), "=c"(leaf4_ecx), "=d"(leaf4_edx)
        : "a"(4), "c"(i));
      uint ctype = leaf4_eax & 0x1F;
      if(ctype == 0) break;
      uint level = (leaf4_eax >> 5) & 0x7;
      uint ways = ((leaf4_ebx >> 22) & 0x3FF) + 1;
      uint parts = ((leaf4_ebx >> 12) & 0x3FF) + 1;
      uint linesize = (leaf4_ebx & 0xFFF) + 1;
      uint sets = leaf4_ecx + 1;
      uint size_kb = (ways * parts * linesize * sets) / 1024;
      char *tname = "Unknown";
      if(ctype == 1) tname = "Data";
      if(ctype == 2) tname = "Instruction";
      if(ctype == 3) tname = "Unified";
      cprintf("  L%d %s:  %d KB (%d-way, %d B line)\n",
              level, tname, size_kb, ways, linesize);
    }
  }

  // ---- Memory Information ----
  cprintf("\n--- Memory ---\n");
  cprintf("  Total:     %d MB (phystop=0x%x)\n",
          phystop / (1024 * 1024), phystop);
  freepg = kfreepages();
  cprintf("  Free:      %d MB (%d pages x 4 KB)\n",
          (freepg * 4) / 1024, freepg);
  cprintf("  Used:      %d MB\n",
          (int)(phystop / (1024 * 1024)) - (int)((freepg * 4) / 1024));
  cprintf("  Kernel:    0x%x - 0x%x (%d KB)\n",
          KERNBASE, (uint)end, ((uint)end - KERNBASE) / 1024);
  cprintf("  KERNBASE:  0x%x\n", KERNBASE);
  cprintf("  DEVSPACE:  0x%x\n", DEVSPACE);

  // ---- Storage Information ----
  cprintf("\n--- Storage ---\n");
  if(usb_is_available()){
    uint cap = usb_get_capacity();
    uint nports = usb_get_nports();
    uint ehci_bar = usb_get_ehci_base();
    cprintf("  Type:      USB Mass Storage (EHCI)\n");
    cprintf("  EHCI:      BAR0=0x%x, %d ports\n", ehci_bar, nports);
    cprintf("  Capacity:  %d sectors (%d MB)\n",
            cap + 1, (cap + 1) / 2048);
    cprintf("  I/O Mode:  %s\n",
            usb_get_irq() > 0 ? "Interrupt-driven" : "Polling");
    if(usb_get_irq() > 0)
      cprintf("  IRQ:       %d\n", usb_get_irq());
    cprintf("  FS Offset: sector %d (%d sectors)\n",
            FS_SECTOR_OFFSET, FS_SIZE);
  } else {
    cprintf("  No USB storage detected\n");
  }

  // ---- Wired Network Driver Information ----
  cprintf("\n--- Wired Network ---\n");
  if(net_is_available()){
    uchar mac[6];
    uchar ip[4];
    uchar gw[4], mask[4], dns[4];
    uint lease_sec;
    int dhok;
    struct net_stats st;
    net_get_mac(mac);
    net_get_ipv4(ip);
    net_get_dhcp_cfg(gw, mask, dns, &lease_sec, &dhok);
    net_get_stats(&st);
    cprintf("  Driver:    Intel e1000e family (82579LM/82574L) + ARP/IPv4/TCP-min\n");
    cprintf("  Link:      Kernel netif queue ready for TCP/IP stack\n");
    cprintf("  IRQ:       %d\n", net_get_irq());
    cprintf("  MAC:       %x:%x:%x:%x:%x:%x\n",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    cprintf("  IPv4:      %d.%d.%d.%d\n", ip[0], ip[1], ip[2], ip[3]);
    cprintf("  Netmask:   %d.%d.%d.%d\n", mask[0], mask[1], mask[2], mask[3]);
    cprintf("  Gateway:   %d.%d.%d.%d\n", gw[0], gw[1], gw[2], gw[3]);
    cprintf("  DNS:       %d.%d.%d.%d\n", dns[0], dns[1], dns[2], dns[3]);
    cprintf("  DHCP:      %s (lease=%d sec)\n", dhok ? "ok" : "pending", lease_sec);
    cprintf("  Stats:     tx=%d rx=%d txerr=%d rxdrop=%d arp=%d arp_rep=%d\n",
            st.tx_pkts, st.rx_pkts, st.tx_errs, st.rx_drop, st.rx_arp, st.arp_reply);
    cprintf("             ip4=%d tcp=%d synack=%d ack=%d\n",
            st.rx_ipv4, st.rx_tcp, st.tcp_synack, st.tcp_ack);
    cprintf("             udp=%d dhcp_offer=%d dhcp_ack=%d\n",
            st.rx_udp, st.dhcp_offer, st.dhcp_ack);
  } else {
    cprintf("  No supported wired NIC initialized\n");
  }

  // ---- PCI Device Enumeration ----
  cprintf("\n--- PCI Devices ---\n");
  {
    uint bus, dev, func;
    int ndevs = 0;
    int is_multifunc;
    for(bus = 0; bus < 256; bus++){
      for(dev = 0; dev < 32; dev++){
        // Check function 0 first; skip device if not present
        ushort vid0 = pci_read16(bus, dev, 0, PCI_VENDOR_ID);
        if(vid0 == 0xFFFF)
          continue;
        // Check if multi-function device
        is_multifunc = (pci_read32(bus, dev, 0, 0x0C) >> 16) & 0x80;
        for(func = 0; func < (is_multifunc ? 8 : 1); func++){
          ushort vid = pci_read16(bus, dev, func, PCI_VENDOR_ID);
          if(vid == 0xFFFF)
            continue;
          ushort did = pci_read16(bus, dev, func, PCI_DEVICE_ID);
          uint classrev = pci_read32(bus, dev, func, PCI_CLASS_REV);
          uchar class = (classrev >> 24) & 0xFF;
          uchar subclass = (classrev >> 16) & 0xFF;
          cprintf("  %x:%x.%d  %x:%x  %s\n",
                  bus, dev, func, vid, did,
                  pci_class_name(class, subclass));
          ndevs++;
        }
      }
    }
    cprintf("  Total: %d device(s)\n", ndevs);
  }

  cprintf("\n==========================================\n");
  return 0;
}

// ============================================================================
// Lessons from 7 rounds of X220 real hardware testing:
//
//   EVERY "cleanup" operation we tried BROKE the reboot on X220:
//     - halt_aps() (INIT IPI)     → hangs (possible LAPIC/chipset issue)
//     - disable_smi()             → BIOS POST hangs (needs SMI)
//     - disable_ehci_dma()        → hangs (MMIO/SMI issue)
//     - usb_halt_controller()     → hangs
//     - IOAPIC mask               → hangs
//
//   The reference code (proven on X220) does NONE of these. It just:
//     1. Waits for 8042 IBF clear
//     2. Sends 0xFE to port 0x64
//     3. Triple fault as fallback
//   No cli(), no halt_aps(), no disable_smi(). NOTHING.
//
//   For shutdown: ACPI S5 is a PCH hardware power-off that does NOT
//   need BIOS POST. So we can safely disable SMI for shutdown.
// ============================================================================

// ============================================================================
// ACPI S5 shutdown (power off)
// ============================================================================
int
sys_shutdown(void)
{
  uint pmbase, qemu_pmbase;
  ushort pm1_cnt;
  volatile int i;
  int t;

  // Read all PCI config data BEFORE cli (avoid potential races with APs)
  pmbase = pci_read32(0, 0x1f, 0, 0x40) & 0xFF80;
  qemu_pmbase = 0;
  {
    ushort vid = pci_read16(0, 1, 3, PCI_VENDOR_ID);
    if(vid == 0x8086)
      qemu_pmbase = pci_read32(0, 1, 3, 0x40) & 0xFFC0;
  }

  cprintf("\n*** System shutting down ***\n");

  // Disable BSP interrupts
  cli();

  // Disable global SMI (direct I/O only, no PCI config needed)
  // S5 power-off doesn't need BIOS POST, so disabling SMI is safe.
  if(pmbase && pmbase != 0xFF80)
    outl(pmbase + 0x30, 0);
  outl(0xCF8, 0);

  // Intel ICH/PCH ACPI S5 shutdown via LPC bridge
  if(pmbase && pmbase != 0xFF80){
    // Clear all PM1 status bits
    outw(pmbase, 0xFFFF);

    // Set SCI_EN=1 directly (safe — SMI is disabled)
    pm1_cnt = inw(pmbase + 4);
    if(!(pm1_cnt & 1))
      outw(pmbase + 4, pm1_cnt | 1);

    // S5 shutdown: try all SLP_TYP values (7 down to 0)
    for(t = 7; t >= 0; t--){
      pm1_cnt = inw(pmbase + 4);
      pm1_cnt &= ~((7 << 10) | (1 << 13));
      pm1_cnt |= (t << 10);
      outw(pmbase + 4, pm1_cnt);
      pm1_cnt |= (1 << 13);
      outw(pmbase + 4, pm1_cnt);
      for(i = 0; i < 5000000; i++)
        ;
    }
  }

  // PIIX4 PM (QEMU i440fx) at PCI 00:01.3
  if(qemu_pmbase && qemu_pmbase != 0xFFC0){
    outw(qemu_pmbase + 4, (5 << 10) | (1 << 13));
    for(i = 0; i < 5000000; i++)
      ;
  }

  // QEMU specific shutdown ports
  outw(0x604, 0x2000);
  for(i = 0; i < 5000000; i++)
    ;
  outw(0xB004, 0x2000);

  for(;;)
    asm volatile("hlt");
  return 0;
}

// ============================================================================
// System reboot
//
// On this X220, direct 8042 reset reliably reaches the "BIOS handoff hang"
// state (red 12). ACPI reset register also proved unstable on both X220 and
// QEMU, so keep the reboot chain minimal and prefer the chipset reset port.
//
// VGA diagnostic:
//   '1' = entered reboot path
//   '7' = lightweight USB/PIC handoff completed
//   '9' = trying CF9 safe reset (direct 0x06)
//   '3' = triple fault fallback
//   '2' = trying Linux-style 8042 reboot (0x472=0x1234 + cmd 0xFC)
//   '5' = trying CF9 hard reset (direct 0x0E)
// ============================================================================
int
sys_reboot(void)
{
  volatile ushort *vga = (volatile ushort*)((uint)P2V(0xB8000));
  volatile ushort *bios_reset_flag = (volatile ushort*)((uint)P2V(0x472));

  cprintf("\n*** System rebooting ***\n");
  cli();
  reboot_prepare_cpus();

  // Mark that we entered reboot path.
  vga[24 * 80] = 0x4F31; // '1'

  reboot_firmware_prep();
  vga[24 * 80 + 1] = 0x4F37; // '7'

  // Prefer safer CF9 reset code first on this platform.
  vga[24 * 80 + 2] = 0x4F39; // '9'
  cf9_reset(0x06);

  // Fallback 1: triple fault reset.
  {
    struct {
      ushort limit;
      uint base;
    } __attribute__((packed)) null_idt = {0, 0};
    asm volatile("lidt %0" : : "m"(null_idt));
    asm volatile("ud2");
  }
  vga[24 * 80 + 3] = 0x4F33; // '3'

  // Fallback 2: old platform keyboard-controller reset command.
  *bios_reset_flag = 0x1234;
  vga[24 * 80 + 4] = 0x4F32; // '2'
  i8042_reboot_cmd(0xFC);

  // Final fallback: hard CF9 reset.
  vga[24 * 80 + 5] = 0x4F35; // '5'
  cf9_reset(0x0E);

  for(;;)
    asm volatile("hlt");
  return 0;
}

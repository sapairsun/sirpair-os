// Disk driver - USB mass storage only
// IDE PIO completely removed, all disk I/O via USB driver stack
// Driver stack: PCI -> EHCI -> USB -> Mass Storage -> SCSI -> Block I/O
//
// Access USB drive on real ThinkPad X220 hardware
// Test via usb-storage device in QEMU emulator

#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"
#include "buf.h"

// Disk driver initialization
// Initialize PCI bus scan and USB driver stack
void
diskinit(void)
{
  // Initialize PCI bus
  pciinit();

  // Initialize USB driver stack (PCI -> EHCI -> USB -> Mass Storage)
  if(usbinit() == 0){
    /*
     * 枚举完成后立即关闭 EHCI 线中断改纯轮询，使后续 initlog 等块设备路径不再依赖
     * IOAPIC 电平触发与 usb_intr 唤醒；在 QEMU 四核下可显著降低宿主侧「resetting ehci HC」
     * 与首进程建立前的挂起概率。真机亦减轻共享 IRQ 线风暴。
     */
    usb_disable_interrupts();
    cprintf("disk: USB ready");
    boot_ok();
  } else {
    panic("disk: USB mass storage init failed, no disk available");
  }
}

// Disk block I/O
// All read/write requests via USB driver stack
void
diskrw(struct buf *b)
{
  if(!(b->flags & B_BUSY))
    panic("diskrw: buf not busy");
  if((b->flags & (B_VALID|B_DIRTY)) == B_VALID)
    panic("diskrw: nothing to do");
  if(b->dev != 0 && b->dev != ROOTDEV)
    panic("diskrw: invalid device");

  // Block I/O via USB driver
  usb_rw(b);
}

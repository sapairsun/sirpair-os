#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"

// Interrupt descriptor table (shared by all CPUs).
struct gatedesc idt[256];
extern uint vectors[];  // in vectors.S: array of 256 entry pointers
struct spinlock tickslock;
uint ticks;

void
tvinit(void)
{
  int i;

  for(i = 0; i < 256; i++)
    SETGATE(idt[i], 0, SEG_KCODE<<3, vectors[i], 0);
  SETGATE(idt[T_SYSCALL], 1, SEG_KCODE<<3, vectors[T_SYSCALL], DPL_USER);
  
  initlock(&tickslock, "time");
}

void
idtinit(void)
{
  lidt(idt, sizeof(idt));
}

//PAGEBREAK: 41
void
trap(struct trapframe *tf)
{
  if(tf->trapno == T_SYSCALL){
    if(proc->killed)
      exit();
    proc->tf = tf;
    syscall();
    if(proc->killed)
      exit();
    return;
  }

  switch(tf->trapno){
  case T_IRQ0 + IRQ_TIMER:
    if(rebooting() && cpu != &cpus[mpbcpu()]){
      lapiceoi();
      reboot_halt_if_requested();
    }
    // 仅在引导处理器上累加 ticks：BSP 的 APIC ID 未必为 0（ACPI MADT），不可与 0 比较。
    if(cpu == &cpus[mpbcpu()]){
      acquire(&tickslock);
      ticks++;
      wakeup(&ticks);
      release(&tickslock);
      console_cursor_tick();
      // Periodic safety wakeup for USB interrupt-driven transfer timeout.
      // If a process is waiting for a USB transfer, wake it up so it can
      // check for timeout. wakeup on a channel with no sleepers is a no-op.
      if(usb_waiting)
        wakeup((void*)&usb_xfer_done);
      /*
       * 避免在“用户进程内核栈”上执行网络重轮询路径：
       * 网络栈 + 调试输出调用链较深，叠加时钟中断可压垮 4KB kstack，
       * 导致 proc 结构或页表指针被破坏，后续表现为 dig 后 sh 在 wait 返回点缺页。
       * 仅在 proc 为空（调度器/空闲路径）时执行轮询，降低栈风险。
       */
      if(proc == 0){
        if(net_is_available())
          net_poll();
      }
      /*
       * TCP connect/recv 超时与重传依赖协议栈定时器。
       * 仅在用户路径轮询会导致某些阻塞系统调用期间定时器停摆，
       * 进而出现 curl 外联偶发卡住或长期无响应。
       * 这里恢复时钟中断中的轻量定时器轮询，确保重传与超时推进。
       */
      if(net_is_available())
        sirpair_microps_poll_timers();
    }
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE:
  case T_IRQ0 + IRQ_IDE+1:
    // IDE removed, ignore residual IDE interrupts
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_KBD:
    kbdintr();
    lapiceoi();
    break;
  case T_IRQ0 + 12:
    mouse_intr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_COM1:
    uartintr();
    lapiceoi();
    break;
  case T_IRQ0 + 7:
  case T_IRQ0 + IRQ_SPURIOUS:
    cprintf("cpu%d: spurious interrupt at %x:%x\n",
            cpu->id, tf->cs, tf->eip);
    lapiceoi();
    break;
   
  //PAGEBREAK: 13
  default:
    // Check if this is a USB/EHCI interrupt (dynamic IRQ from PCI)
    if(tf->trapno >= T_IRQ0){
      int uirq = usb_get_irq();
      if(uirq > 0 && tf->trapno == T_IRQ0 + uirq){
        usb_intr();
        lapiceoi();
        break;
      }
      {
        int nirq = net_get_irq();
        if(nirq > 0 && tf->trapno == T_IRQ0 + nirq){
          net_intr();
          lapiceoi();
          break;
        }
      }
      // CRITICAL FIX for real hardware (ThinkPad X220):
      // Silently acknowledge unhandled hardware IRQs and return.
      lapiceoi();
      break;
    }
    // CRITICAL FIX for real hardware (ThinkPad X220):
    // On real hardware, CPUs receive unexpected exceptions (vector 0-31)
    // from BIOS-configured hardware sources that use LOW vectors:
    //   - Thermal interrupts (LAPIC Thermal LVT with BIOS vector)
    //   - CMCI (Corrected Machine Check) interrupts
    //   - NMI from chipset (watchdog, memory errors)
    //   - Performance monitoring interrupts
    //   - Pending LAPIC interrupts from before lapicinit masked them
    // These can fire on ANY CPU at ANY time, even when proc != 0
    // and in kernel mode (e.g., AP running exec for initcode process).
    // Previously this caused panic("trap") → panicked=1 → ALL CPUs
    // frozen via consputc()'s infinite loop.
    //
    // Fix: absorb ALL kernel-mode exceptions without panicking.
    // Write diagnostic marker to bottom of VGA screen (visible area).
    if(proc == 0 || (tf->cs&3) == 0){
      // 向量 < 32 为 CPU 异常，不是 PIC/APIC 外设 IRQ，禁止 lapiceoi（误 EOI 会破坏 LAPIC 状态）。
      // Absorb kernel-mode exception silently (real hardware quirk)
      break;
    }
    // In user space, assume process misbehaved.
    if(tf->trapno == T_PGFLT){
      uint bad = rcr2();
      vm_dump_user_mapping(proc ? proc->pgdir : 0, (uint)tf->eip, "pgflt-eip");
      vm_dump_user_mapping(proc ? proc->pgdir : 0, bad, "pgflt-cr2");
    }
    cprintf("pid %d %s: trap %d err %d on cpu %d "
            "eip 0x%x addr 0x%x--kill proc\n",
            proc->pid, proc->name, tf->trapno, tf->err, cpu->id, tf->eip, 
            rcr2());
    proc->killed = 1;
  }

  // Force process exit if it has been killed and is in user space.
  // (If it is still executing in the kernel, let it keep running 
  // until it gets to the regular system call return.)
  if(proc && proc->killed && (tf->cs&3) == DPL_USER)
    exit();

  // Force process to give up CPU on clock tick.
  // If interrupts were on while locks held, would need to check nlock.
  if(proc && proc->state == RUNNING && tf->trapno == T_IRQ0+IRQ_TIMER)
    yield();

  // Check if the process has been killed since we yielded
  if(proc && proc->killed && (tf->cs&3) == DPL_USER)
    exit();
}

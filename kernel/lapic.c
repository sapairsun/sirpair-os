// The local APIC manages internal (non-I/O) interrupts.
// See Chapter 8 & Appendix C of Intel processor manual volume 3.

#include "types.h"
#include "defs.h"
#include "memlayout.h"
#include "traps.h"
#include "mmu.h"
#include "x86.h"

// Local APIC registers, divided by 4 for use as uint[] indices.
#define ID      (0x0020/4)   // ID
#define VER     (0x0030/4)   // Version
#define TPR     (0x0080/4)   // Task Priority
#define EOI     (0x00B0/4)   // EOI
#define SVR     (0x00F0/4)   // Spurious Interrupt Vector
  #define ENABLE     0x00000100   // Unit Enable
#define ESR     (0x0280/4)   // Error Status
#define ICRLO   (0x0300/4)   // Interrupt Command
  #define INIT       0x00000500   // INIT/RESET
  #define STARTUP    0x00000600   // Startup IPI
  #define DELIVS     0x00001000   // Delivery status
  #define ASSERT     0x00004000   // Assert interrupt (vs deassert)
  #define DEASSERT   0x00000000
  #define LEVEL      0x00008000   // Level triggered
  #define BCAST      0x00080000   // Send to all APICs, including self.
  #define BUSY       0x00001000
  #define FIXED      0x00000000
#define ICRHI   (0x0310/4)   // Interrupt Command [63:32]
#define TIMER   (0x0320/4)   // Local Vector Table 0 (TIMER)
  #define X1         0x0000000B   // divide counts by 1
  #define PERIODIC   0x00020000   // Periodic
#define THERM   (0x0330/4)   // LVT Thermal Monitor
#define PCINT   (0x0340/4)   // Performance Counter LVT
#define LINT0   (0x0350/4)   // Local Vector Table 1 (LINT0)
#define LINT1   (0x0360/4)   // Local Vector Table 2 (LINT1)
#define ERROR   (0x0370/4)   // Local Vector Table 3 (ERROR)
#define CMCI    (0x02F0/4)   // LVT Corrected Machine Check Interrupt
  #define MASKED     0x00010000   // Interrupt masked
#define TICR    (0x0380/4)   // Timer Initial Count
#define TCCR    (0x0390/4)   // Timer Current Count
#define TDCR    (0x03E0/4)   // Timer Divide Configuration

volatile uint *lapic;  // Initialized in mp.c
static uint lapic_timer_icr = 10000000;
static int lapic_timer_calibrated;
static void lapicw(int index, int value);

#define PIT_FREQ            1193182
#define IO_TIMER1           0x040
#define PIT_CH2_DATA        (IO_TIMER1 + 2)
#define PIT_MODE            (IO_TIMER1 + 3)
#define PIT_CH2             0x80
#define PIT_LOHI            0x30
#define PIT_MODE0           0x00
#define PIT_MODE2           0x04

static uint
lapic_calibrate_timer(void)
{
  // Calibrate LAPIC timer against PIT channel 2 over 10ms.
  // This gives a hardware-specific count for 100Hz periodic interrupts.
  uint start, end, elapsed;
  uint pit_cnt = PIT_FREQ / 100;  // 10ms window
  uchar p61;

  // Route PIT channel 2 to gate, disable speaker output.
  p61 = inb(0x61);
  outb(0x61, (p61 | 0x01) & ~0x02);

  // Program PIT channel 2 in one-shot mode with 10ms countdown.
  outb(PIT_MODE, PIT_CH2 | PIT_LOHI | PIT_MODE0);
  outb(PIT_CH2_DATA, pit_cnt & 0xFF);
  outb(PIT_CH2_DATA, (pit_cnt >> 8) & 0xFF);

  // Start LAPIC one-shot free run from max value.
  lapicw(TDCR, X1);
  lapicw(TIMER, T_IRQ0 + IRQ_TIMER);
  lapicw(TICR, 0xFFFFFFFF);
  start = lapic[TCCR];

  // Wait until PIT channel 2 reaches terminal count (OUT2=1, bit5).
  while((inb(0x61) & 0x20) == 0)
    ;

  end = lapic[TCCR];
  elapsed = start - end;

  // Restore channel 2 to a harmless periodic setting.
  outb(PIT_MODE, PIT_CH2 | PIT_LOHI | PIT_MODE2);
  outb(PIT_CH2_DATA, 0xFF);
  outb(PIT_CH2_DATA, 0xFF);
  outb(0x61, p61);

  // Convert 10ms count to 100Hz period (1 tick = 10ms).
  if(elapsed < 1000)
    return 10000000;
  return elapsed;
}

static void
lapicw(int index, int value)
{
  lapic[index] = value;
  lapic[ID];  // wait for write to finish, by reading
}
//PAGEBREAK!

void
lapicinit(int c)
{
  if(!lapic) 
    return;

  // Enable local APIC; set spurious interrupt vector.
  lapicw(SVR, ENABLE | (T_IRQ0 + IRQ_SPURIOUS));

  // The timer repeatedly counts down at bus frequency
  // from lapic[TICR] and then issues an interrupt.  
  // If Sirpair cared more about precise timekeeping,
  // TICR would be calibrated using an external time source.
  if(c == mpbcpu() && !lapic_timer_calibrated){
    lapic_timer_icr = lapic_calibrate_timer();
    lapic_timer_calibrated = 1;
    cprintf("lapic: calibrated timer icr=%d (target 100Hz)", lapic_timer_icr);
    boot_ok();
  }
  lapicw(TDCR, X1);
  lapicw(TIMER, PERIODIC | (T_IRQ0 + IRQ_TIMER));
  lapicw(TICR, lapic_timer_icr);

  // Disable logical interrupt lines.
  lapicw(LINT0, MASKED);
  lapicw(LINT1, MASKED);

  // Disable performance counter overflow interrupts
  // on machines that provide that interrupt entry.
  if(((lapic[VER]>>16) & 0xFF) >= 4)
    lapicw(PCINT, MASKED);

  // CRITICAL: Mask Thermal Monitor LVT interrupt.
  // On real hardware (ThinkPad X220), the BIOS configures thermal management
  // and may leave the Thermal LVT with a low vector (0-31). When CPU temp
  // crosses a threshold, the LAPIC sends a thermal interrupt. If the vector
  // is in the exception range (< 32), the trap handler panics because
  // it's not recognized as an IRQ. This is the root cause of AP panic
  // within microseconds of entering the scheduler on real hardware.
  lapicw(THERM, MASKED);

  // Mask CMCI (Corrected Machine Check Interrupt) if present.
  // Sandy Bridge CPUs support CMCI which can fire at any time.
  lapicw(CMCI, MASKED);

  // Map error interrupt to IRQ_ERROR.
  lapicw(ERROR, T_IRQ0 + IRQ_ERROR);

  // Clear error status register (requires back-to-back writes).
  lapicw(ESR, 0);
  lapicw(ESR, 0);

  // Ack any outstanding interrupts.
  lapicw(EOI, 0);

  // Send an Init Level De-Assert to synchronise arbitration ID's.
  lapicw(ICRHI, 0);
  lapicw(ICRLO, BCAST | INIT | LEVEL);
  while(lapic[ICRLO] & DELIVS)
    ;

  // Enable interrupts on the APIC (but not on the processor).
  lapicw(TPR, 0);
}

int
cpunum(void)
{
  // Cannot call cpu when interrupts are enabled:
  // result not guaranteed to last long enough to be used!
  // Would prefer to panic but even printing is chancy here:
  // almost everything, including cprintf and panic, calls cpu,
  // often indirectly through acquire and release.
  if(readeflags()&FL_IF){
    static int n;
    if(n++ == 0)
      cprintf("cpu called from %x with interrupts enabled\n",
        __builtin_return_address(0));
  }

  if(lapic)
    return lapic[ID]>>24;
  return 0;
}

// Acknowledge interrupt.
void
lapiceoi(void)
{
  if(lapic)
    lapicw(EOI, 0);
}

// Spin for a given number of microseconds.
// Uses port 0x80 I/O (each ~1-2µs on real hardware).
// Critical for INIT/SIPI delays in lapicstartap on real hardware!
void
microdelay(int us)
{
  // outb(0x80,0) takes ~1-2µs on real HW (ISA bus cycle).
  // We do 2 io_waits per µs for safety margin.
  while(us-- > 0){
    outb(0x80, 0);
    outb(0x80, 0);
  }
}

#define IO_RTC  0x70

// Start additional processor running entry code at addr.
// See Appendix B of MultiProcessor Specification.
void
lapicstartap(uchar apicid, uint addr)
{
  int i;
  ushort *wrv;
  
  // "The BSP must initialize CMOS shutdown code to 0AH
  // and the warm reset vector (DWORD based at 40:67) to point at
  // the AP startup code prior to the [universal startup algorithm]."
  outb(IO_RTC, 0xF);  // offset 0xF is shutdown code
  outb(IO_RTC+1, 0x0A);
  wrv = (ushort*)P2V((0x40<<4 | 0x67));  // Warm reset vector
  wrv[0] = 0;
  wrv[1] = addr >> 4;

  // "Universal startup algorithm."
  // Send INIT (level-triggered) interrupt to reset other CPU.
  lapicw(ICRHI, apicid<<24);
  lapicw(ICRLO, INIT | LEVEL | ASSERT);
  microdelay(200);
  lapicw(ICRLO, INIT | LEVEL);
  // MP spec requires 10ms delay between INIT deassert and STARTUP.
  // CRITICAL for real hardware (X220 Sandy Bridge)!
  microdelay(10000);  // 10ms - required by MP specification
  
  // Send startup IPI (twice!) to enter code.
  // Regular hardware is supposed to only accept a STARTUP
  // when it is in the halted state due to an INIT.  So the second
  // should be ignored, but it is part of the official Intel algorithm.
  // Bochs complains about the second one.  Too bad for Bochs.
  for(i = 0; i < 2; i++){
    lapicw(ICRHI, apicid<<24);
    lapicw(ICRLO, STARTUP | (addr>>12));
    microdelay(200);
  }
}



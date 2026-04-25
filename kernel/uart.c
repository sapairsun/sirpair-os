// Intel 8250 serial port (UART).

#include "types.h"
#include "defs.h"
#include "param.h"
#include "traps.h"
#include "spinlock.h"
#include "fs.h"
#include "file.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"

#define COM1    0x3f8

static int uart;    // is there a uart?

// Early UART init: just enable serial port for output (no interrupts).
// Called before mpinit() so SMP detection messages appear on serial console.
void
uartearlyinit(void)
{
  // Turn off the FIFO
  outb(COM1+2, 0);

  // 9600 baud, 8 data bits, 1 stop bit, parity off.
  outb(COM1+3, 0x80);    // Unlock divisor
  outb(COM1+0, 115200/9600);
  outb(COM1+1, 0);
  outb(COM1+3, 0x03);    // Lock divisor, 8 data bits.
  outb(COM1+4, 0);
  outb(COM1+1, 0x00);    // No interrupts yet.

  // If status is 0xFF, no serial port.
  if(inb(COM1+5) == 0xFF)
    return;
  uart = 1;

  // Acknowledge pre-existing interrupt conditions.
  inb(COM1+2);
  inb(COM1+0);
}

void
uartinit(void)
{
  if(!uart){
    // If early init was not done, do full init here
    // Turn off the FIFO
    outb(COM1+2, 0);

    // 9600 baud, 8 data bits, 1 stop bit, parity off.
    outb(COM1+3, 0x80);    // Unlock divisor
    outb(COM1+0, 115200/9600);
    outb(COM1+1, 0);
    outb(COM1+3, 0x03);    // Lock divisor, 8 data bits.
    outb(COM1+4, 0);

    // If status is 0xFF, no serial port.
    if(inb(COM1+5) == 0xFF)
      return;
    uart = 1;

    // Acknowledge pre-existing interrupt conditions.
    inb(COM1+2);
    inb(COM1+0);
  }

  // Enable receive interrupts.
  outb(COM1+1, 0x01);

  // Enable interrupt routing.
  picenable(IRQ_COM1);
  ioapicenable(IRQ_COM1, 0);
}

void
uartputc(int c)
{
  if(!uart)
    return;
  early_uart_mirror(c);
  // Don't busy-wait for UART ready. On ThinkPad X220, there's no real
  // COM1 serial port, but Intel AMT/KT Controller may make uart=1.
  // The original 128-iteration loop with microdelay(10) took ~2.6ms
  // per character, making 17-char strings take ~44ms. This creates a
  // large window where the BSP holds cons.lock, during which APs
  // could trigger events that freeze the system.
  // Just write and move on - if UART is real, chars may be dropped
  // under heavy load, but boot messages aren't critical for serial.
  outb(COM1+0, c);
}

static int
uartgetc(void)
{
  if(!uart)
    return -1;
  if(!(inb(COM1+5) & 0x01))
    return -1;
  return inb(COM1+0);
}

void
uartintr(void)
{
  consoleintr(uartgetc);
}

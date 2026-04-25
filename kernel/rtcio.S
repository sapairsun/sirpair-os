// CMOS 单字节端口访问（不自行 cli/sti）：由 rtc_read_unix 一次性关中断读完所有寄存器，
// 避免逐字节 sti/cli 与 LAPIC 定时器在 QEMU 上交错触发异常。
#include "asm.h"

.text
.globl cmos_read_byte
cmos_read_byte:
  pushl %ebp
  movl %esp, %ebp
  movl 8(%ebp), %eax
  andl $0x7f, %eax
  movw $0x70, %dx
  outb %al, %dx
  movw $0x71, %dx
  inb %dx, %al
  movzbl %al, %eax
  popl %ebp
  ret

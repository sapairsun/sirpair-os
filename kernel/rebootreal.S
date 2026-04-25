#include "asm.h"
#include "memlayout.h"
#include "mmu.h"

# Linux-style BIOS reboot trampoline.
# Copied to low memory, entered with entrypgdir loaded so that both the
# identity map and KERNBASE alias of low memory are valid.

.code32
.globl start
start:
  cli

  # Turn paging off but remain in protected mode for one far jump.
  movl    %cr0, %eax
  andl    $~CR0_PG, %eax
  movl    %eax, %cr0
  ljmpl   $(SEG_KCODE<<3), $nopaging

nopaging:
  # Leave protected mode and immediately far jump into 16-bit code.
  movl    %cr0, %eax
  andl    $~CR0_PE, %eax
  movl    %eax, %cr0
  .byte   0xEA
  .word   realmode
  .word   0x0000

.code16
realmode:
  xorw    %ax, %ax
  movw    %ax, %ds
  movw    %ax, %es
  movw    %ax, %ss
  movw    $0x6ff0, %sp

  # Real-mode IVT: 1KB at physical address 0.
  lidt    real_idt_desc

  # BIOS reset vector (same physical address as F000:FFF0, but this is
  # the form Linux commonly uses in its reboot trampoline).
  ljmp    $0xFFFF, $0x0000

spin:
  hlt
  jmp     spin

real_idt_desc:
  .word   0x03FF
  .long   0x00000000

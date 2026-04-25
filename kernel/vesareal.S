#include "asm.h"
#include "memlayout.h"
#include "mmu.h"

# Runtime VBE switch trampoline.
# Call convention:
#   edx = old cr3
# Side effects:
#   writes mode info to 0000:9000
#   writes status to    0000:8FF0 (1 success, 0 failure)

.code32
.globl start
start:
  movl    %edx, old_cr3
  popl    old_ret
  movl    %esp, old_esp
  movl    $0x6f00, %esp
  cli

  # Turn paging off, still in protected mode.
  movl    %cr0, %eax
  andl    $~CR0_PG, %eax
  movl    %eax, %cr0
  ljmpl   $(SEG_KCODE<<3), $nopaging

nopaging:
  # Leave protected mode and jump into 16-bit code.
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
  movw    $0x6e00, %sp
  movb    $0, 0x8FF0

  # Query mode 0x118 info into 0x9000.
  movw    $0x9000, %di
  movw    $0x4F01, %ax
  movw    $0x0118, %cx
  int     $0x10
  cmpw    $0x004F, %ax
  jne     rm_done

  # Set mode 0x118 with linear framebuffer bit.
  movw    $0x4F02, %ax
  movw    $0x4118, %bx
  int     $0x10
  cmpw    $0x004F, %ax
  jne     rm_done

  movb    $1, 0x8FF0

rm_done:
  # Re-enter protected mode.
  lgdt    gdt_desc
  movl    %cr0, %eax
  orl     $CR0_PE, %eax
  movl    %eax, %cr0
  .byte   0x66, 0xEA
  .long   prot32
  .word   (SEG_KCODE<<3)

.code32
prot32:
  movw    $(SEG_KDATA<<3), %ax
  movw    %ax, %ds
  movw    %ax, %es
  movw    %ax, %ss
  movw    %ax, %fs
  movw    %ax, %gs

  # Re-enable paging with current (entrypgdir) CR3 first.
  movl    %cr0, %eax
  orl     $CR0_PG, %eax
  movl    %eax, %cr0
  ljmpl   $(SEG_KCODE<<3), $paged

paged:
  movl    %esp, %esi
  movl    old_esp, %ebx
  movl    old_ret, %edi
  movl    old_cr3, %ecx
  movl    $paged_high, %eax
  addl    $KERNBASE, %eax
  jmpl    *%eax

paged_high:
  movl    %ecx, %cr3
  movl    %ebx, %esp
  pushl   %edi
  ret

.align 8
gdt:
  .long   0x00000000, 0x00000000
  .long   0x0000ffff, 0x00cf9a00
  .long   0x0000ffff, 0x00cf9200
gdt_desc:
  .word   (3*8)-1
  .long   gdt

old_esp:
  .long   0
old_ret:
  .long   0
old_cr3:
  .long   0

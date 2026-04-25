#include "asm.h"
#include "memlayout.h"
#include "mmu.h"

# ==========================================================================
# Boot sector for Sirpair OS: USB drive boot on ThinkPad X220
#
# CRITICAL FIX: Includes BPB (BIOS Parameter Block) header to prevent
# BIOS from overwriting our boot code during USB-HDD boot.
# Many BIOSes (including X220's) overwrite bytes 0x03-0x3D with drive
# geometry when booting from USB. Without BPB protection, our segment
# register initialization code gets corrupted -> instant crash.
#
# Layout:
#   0x000: JMP + NOP (3 bytes)
#   0x003: Fake BPB (59 bytes, BIOS may overwrite)
#   0x03E: Boot code (safe from BIOS BPB overwrites)
#   0x1FE: Boot signature 0x55AA (added by sign.pl)
#
# BIOS loads this to 0x7C00, DL = boot drive number
# ==========================================================================

.code16
.globl start
start:
  jmp     real_start
  nop

  # ========================================================================
  # Fake BPB (BIOS Parameter Block) - offset 0x03 to 0x3D
  # BIOS may overwrite this area with actual drive geometry for USB-HDD.
  # Our actual boot code is placed AFTER this area (at offset 0x3E).
  # ========================================================================
  .ascii  "SIRPAIR  "       # OEM name (8 bytes)          @ 0x03
  .word   512             # Bytes per sector            @ 0x0B
  .byte   1               # Sectors per cluster         @ 0x0D
  .word   1               # Reserved sectors            @ 0x0E
  .byte   2               # Number of FATs              @ 0x10
  .word   224             # Root dir entries             @ 0x11
  .word   0               # Total sectors 16 (0=use32)  @ 0x13
  .byte   0xF8            # Media descriptor (HDD)      @ 0x15
  .word   0               # Sectors per FAT             @ 0x16
  .word   32              # Sectors per track            @ 0x18
  .word   64              # Number of heads              @ 0x1A
  .long   0               # Hidden sectors              @ 0x1C
  .long   11024           # Total sectors 32            @ 0x20
  .byte   0x80            # Drive number                @ 0x24
  .byte   0               # Reserved                    @ 0x25
  .byte   0x29            # Extended boot signature     @ 0x26
  .long   0x20260320      # Volume serial number        @ 0x27
  .ascii  "SIRPAIR-OS  "     # Volume label (11 bytes)     @ 0x2B
  .ascii  "FAT16   "      # FS type string (8 bytes)    @ 0x36
                           # End of BPB                  @ 0x3E

  # ========================================================================
  # Boot code starts here (offset 0x3E) - SAFE from BIOS BPB overwrites
  # ========================================================================
real_start:
  cli

  # Initialize segment registers (BIOS only guarantees CS:IP and DL)
  xorw    %ax,%ax
  movw    %ax,%ds
  movw    %ax,%es
  movw    %ax,%ss
  movw    $0x7C00,%sp

  # Save boot drive number (DL from BIOS)
  movb    %dl, boot_drive

  sti                         # Enable interrupts for BIOS calls

  # ====== Load kernel via LBA Extended Read (INT 13h, AH=42h) ======
  # USB-HDD always supports LBA extended read; no CHS fallback needed.
  # Load NKERN_SECT sectors starting from LBA 1 to physical 0x10000.
  movw    $NKERN_SECT,%cx     # Total sectors to read
  movw    $0x1000,%bx         # Load segment (0x1000:0 = phys 0x10000)
  movl    $1,%edi             # Start at LBA 1

.Llba_loop:
  testw   %cx,%cx
  jz      .Lreaddone

  movw    %cx,%ax
  cmpw    $8,%ax              # Max 8 sectors per batch (USB friendly)
  jbe     1f
  movw    $8,%ax
1:
  # Fill Disk Address Packet (DAP)
  movw    $0x0010,dap+0       # DAP size=16, reserved=0
  movw    %ax,dap+2           # sector count
  movw    $0,dap+4            # offset = 0
  movw    %bx,dap+6           # segment
  movl    %edi,dap+8          # LBA low
  movl    $0,dap+12           # LBA high

  pushw   %cx
  pushw   %ax

  movb    $3,%cl              # Retry counter
.Llba_retry:
  movw    $dap,%si
  movb    boot_drive,%dl
  movb    $0x42,%ah
  int     $0x13
  jnc     .Llba_ok
  # Reset disk and retry
  xorw    %ax,%ax
  movb    boot_drive,%dl
  int     $0x13
  decb    %cl
  jnz     .Llba_retry

  # LBA failed after retries - display error and halt
  popw    %ax
  popw    %cx
  movb    $'E',%al
  jmp     .Lerror

.Llba_ok:
  popw    %ax
  popw    %cx
  subw    %ax,%cx
  /*
   * 推进 LBA：.code16 下勿用 movzwl ax,eax（会被误汇编）。NKERN_SECT 下下一 LBA < 65536，
   * 仅加 di 即可，edi 高字保持 0（见上方 movl $1,%edi）。
   */
  addw    %ax, %di
  shlw    $5,%ax              # sectors * 32 = paragraphs (512/16)
  addw    %ax,%bx             # advance load segment
  jmp     .Llba_loop

  # ====== Error: print char in AL and halt ======
.Lerror:
  movb    $0x0e,%ah
  xorw    %bx,%bx
  int     $0x10
1:  hlt
    jmp     1b

  # ====== Kernel loaded - enter protected mode ======
.Lreaddone:
  # 为节省引导扇区空间，不在此调用 E820；0x8000 处条数置 0，由内核 CMOS 探测内存
  xorw    %ax,%ax
  movw    %ax,0x8000

  # ---- VBE：查询模式 0x118 信息至 0x9000，并设置线性帧缓冲（0x4118）----
  movb    $0, 0x8FF0
  xorw    %ax,%ax
  movw    %ax,%es
  movw    $0x9000,%di
  movw    $0x4F01,%ax
  movw    $0x0118,%cx
  int     $0x10
  cmpw    $0x004F,%ax
  jne     .Lvbe_done
  movw    $0x4F02,%ax
  movw    $0x4118,%bx
  int     $0x10
  cmpw    $0x004F,%ax
  jne     .Lvbe_done
  movb    $1, 0x8FF0
.Lvbe_done:

  # ---- Enable A20 (after disk reads to avoid Intel 6 Series USB SMM issues) ----
  movw    $0x2401,%ax
  int     $0x15               # BIOS method
  inb     $0x92,%al
  orb     $0x02,%al
  andb    $0xfe,%al           # Don't reset system
  outb    %al,$0x92           # Fast A20 method

  cli
  lgdt    gdtdesc
  movl    %cr0,%eax
  orl     $CR0_PE,%eax
  movl    %eax,%cr0
  ljmp    $(SEG_KCODE<<3),$start32

.code32
start32:
  movw    $(SEG_KDATA<<3),%ax
  movw    %ax,%ds
  movw    %ax,%es
  movw    %ax,%ss
  xorw    %ax,%ax
  movw    %ax,%fs
  movw    %ax,%gs
  movl    $start,%esp
  call    bootmain
spin:
  jmp     spin

# ==========================================================================
# Data
# ==========================================================================
.p2align 2
dap:
  .space 16

boot_drive:
  .byte 0

# 须 >= build/kernel.elf 占用扇区数（约 468KiB → 936 扇区），留裕量便于内核增长
.equ NKERN_SECT, 1024

.p2align 2
gdt:
  SEG_NULLASM
  SEG_ASM(STA_X|STA_R, 0x0, 0xffffffff)
  SEG_ASM(STA_W, 0x0, 0xffffffff)

gdtdesc:
  .word   (gdtdesc - gdt - 1)
  .long   gdt

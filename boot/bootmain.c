// 引导加载器 - C 语言部分
//
// 与 bootasm.S 一起构成引导扇区。
// bootasm.S 已通过 BIOS INT 13h 将内核 ELF 加载到物理地址 0x10000，
// 并已切换到32位保护模式。
// bootmain() 解析 ELF 头，将各段复制到正确的物理地址，然后跳转到内核入口。

#include "types.h"
#include "elf.h"
#include "x86.h"
#include "memlayout.h"

// 内核 ELF 数据已由 bootasm.S 预加载到此地址
#define KERNEL_LOAD_ADDR 0x10000

void
bootmain(void)
{
  struct elfhdr *elf;
  struct proghdr *ph, *eph;
  void (*entry)(void);
  uchar *pa, *src;
  uint i;

  elf = (struct elfhdr*)KERNEL_LOAD_ADDR;

  // 验证 ELF 魔数
  if(elf->magic != ELF_MAGIC)
    return;  // 返回到 bootasm.S 的错误处理

  // 遍历各程序段，从缓冲区复制到最终物理地址
  ph = (struct proghdr*)((uchar*)elf + elf->phoff);
  eph = ph + elf->phnum;
  for(; ph < eph; ph++){
    pa = (uchar*)ph->paddr;
    src = (uchar*)KERNEL_LOAD_ADDR + ph->off;

    // 复制段数据
    for(i = 0; i < ph->filesz; i++)
      pa[i] = src[i];

    // 将超出文件大小的部分清零 (BSS)
    if(ph->memsz > ph->filesz)
      stosb(pa + ph->filesz, 0, ph->memsz - ph->filesz);
  }

  // 跳转到内核入口点，不再返回
  entry = (void(*)(void))(elf->entry);
  entry();
}

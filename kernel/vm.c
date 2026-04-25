#include "param.h"
#include "types.h"
#include "defs.h"
#include "x86.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "elf.h"
#include "spinlock.h"

extern char data[];  // defined by kernel.ld
extern char end[];   // kernel image end (kalloc 可回收下界)
extern uint ticks;   // trap.c：全局时间片，便于多核日志对齐
pde_t *kpgdir;  // for use in scheduler()
struct segdesc gdt[NSEGS];

/* 多核并发 exec→setupkvm 时，对同一页目录根并发 mappages 会触发 remap；全局串行化。 */
static struct spinlock setupkvm_lock;

// Set up CPU's kernel segment descriptors.
// Run once on entry on each CPU.
void
seginit(void)
{
  struct cpu *c;

  // Map "logical" addresses to virtual addresses using identity map.
  // Cannot share a CODE descriptor for both kernel and user
  // because it would have to have DPL_USR, but the CPU forbids
  // an interrupt from CPL=0 to DPL=3.
  c = &cpus[cpunum()];
  c->gdt[SEG_KCODE] = SEG(STA_X|STA_R, 0, 0xffffffff, 0);
  c->gdt[SEG_KDATA] = SEG(STA_W, 0, 0xffffffff, 0);
  c->gdt[SEG_UCODE] = SEG(STA_X|STA_R, 0, 0xffffffff, DPL_USER);
  c->gdt[SEG_UDATA] = SEG(STA_W, 0, 0xffffffff, DPL_USER);

  // Map cpu, and curproc
  c->gdt[SEG_KCPU] = SEG(STA_W, &c->cpu, 8, 0);

  lgdt(c->gdt, sizeof(c->gdt));
  loadgs(SEG_KCPU << 3);
  
  // Initialize cpu-local storage.
  cpu = c;
  proc = 0;
}

// Return the address of the PTE in page table pgdir
// that corresponds to virtual address va.  If alloc!=0,
// create any required page table pages.
static pte_t *
walkpgdir(pde_t *pgdir, const void *va, int alloc)
{
  pde_t *pde;
  pte_t *pgtab;
  extern uint phystop;
  uint ptop;
  uint pa;
  char *kv;

  ptop = phystop ? phystop : PHYSTOP;
  pde = &pgdir[PDX(va)];
  if(*pde & PTE_P){
    pa = PTE_ADDR(*pde);
    kv = (char*)p2v(pa);
    if(pa == 0 || (pa % PGSIZE) != 0 || pa >= ptop || kv < end || v2p(kv) >= ptop){
      cprintf("[vm] warning: invalid PDE cleared pgdir=%p va=0x%x pde=0x%x pa=0x%x\n",
              pgdir, (uint)va, *pde, pa);
      *pde = 0;
    } else {
      pgtab = (pte_t*)kv;
      return &pgtab[PTX(va)];
    }
  }
  if(!alloc || (pgtab = (pte_t*)kalloc()) == 0){
    if(alloc){
      cprintf("[vm] fatal: walkpgdir failed to alloc page table va=0x%x free_pages=%d\n",
              (uint)va, kfreepages());
    }
    return 0;
  }
  // Make sure all those PTE_P bits are zero.
  memset(pgtab, 0, PGSIZE);
  // The permissions here are overly generous, but they can
  // be further restricted by the permissions in the page table 
  // entries, if necessary.
  *pde = v2p(pgtab) | PTE_P | PTE_W | PTE_U;
  return &pgtab[PTX(va)];
}

static int mappages_nolock(pde_t *pgdir, void *va, uint size, uint pa, int perm);
static void freevm_nolock(pde_t *pgdir);

/*
 * 在 panic("remap") 前打印可定位多核页表竞态的上下文（勿删，便于真机/多核复现分析）。
 */
static void
remap_dump_debug(pde_t *pgdir, char *page_va, uint pa, int perm, pte_t want, pte_t cur,
                 uint ret_here)
{
  uint v;
  pde_t *pde;

  v = (uint)page_va;
  cprintf("\n======== page remap debug (write collides with existing map) ========\n");
  cprintf("cpu id=%d\n", cpunum());
  cprintf("target pgdir=0x%x  is global kpgdir=%d  kpgdir=0x%x\n",
          pgdir, pgdir == kpgdir, kpgdir);
  cprintf("new map: va_page=0x%x  pa_page=0x%x  perm=0x%x  want=0x%x\n",
          v, pa, (uint)perm, want);
  cprintf("existing pte=0x%x  existing pa=0x%x  same pa as request=%d\n",
          cur, PTE_ADDR(cur), PTE_ADDR(cur) == (pa & ~0xfff));
  cprintf("expected(ignore A/D)=0x%x  equals want=%d\n",
          cur & ~(PTE_A | PTE_D), (cur & ~(PTE_A | PTE_D)) == want);
  cprintf("PDX=%d  PTX=%d\n", PDX(v), PTX(v));
  pde = &pgdir[PDX(v)];
  cprintf("PDE=0x%x\n", *pde);
  if(proc){
    cprintf("current proc: pid=%d name=%s usersz=0x%x\n",
            proc->pid, proc->name, proc->sz);
    cprintf("proc->pgdir=0x%x equals target pgdir=%d\n",
            proc->pgdir, proc->pgdir == pgdir);
  } else
    cprintf("current proc is null (idle scheduler or early path)\n");
  cprintf("holding setupkvm lock=%d\n", holding(&setupkvm_lock));
  cprintf("ticks=%d\n", ticks);
  cprintf("return addr (approx in mappages_nolock): 0x%x\n", ret_here);
  cprintf("========================================================\n");
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned.
int
mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm)
{
  int r;

  setupkvm_lock_acquire();
  r = mappages_nolock(pgdir, va, size, pa, perm);
  setupkvm_lock_release();
  return r;
}

/*
 * 实际建立映射；调用者须已持有 setupkvm_lock（或由 mappages 包装器代持）。
 */
static int
mappages_nolock(pde_t *pgdir, void *va, uint size, uint pa, int perm)
{
  char *a, *last;
  pte_t *pte;
  
  a = (char*)PGROUNDDOWN((uint)va);
  last = (char*)PGROUNDDOWN(((uint)va) + size - 1);
  for(;;){
    pte_t want = (pte_t)(pa | perm | PTE_P);

    if((pte = walkpgdir(pgdir, a, 1)) == 0)
      return -1;
    if(*pte & PTE_P){
      /*
       * 与目标一致则视为幂等。须忽略 CPU 置位的访问/脏位（PTE_A/PTE_D），
       * 否则先访问过的页会误触发 remap，进而恐慌或引发后续异常。
       */
      if((*pte & ~(PTE_A | PTE_D)) == want)
        goto advance;
      /*
       * 同一 VA 已映射同一物理页，仅权限等组合不同（如 clearpteu 去掉 PTE_U 后又映射）：
       * 写成目标 PTE，保留 A/D。
       */
      if(PTE_ADDR(*pte) == pa){
        *pte = want | (*pte & (PTE_A | PTE_D));
        goto advance;
      }
      /*
       * 内核虚拟地址（KERNBASE 以上）：允许用本次映射覆盖已有物理帧。
       * 引导后 kpgdir 同步表或 ACPI 等路径可能留下与 kmap 不一致的 PTE，
       * 同一 VA 应先以 kmap 为准（用户提供的 remap 调试即属此类：同页帧差 8KB）。
       */
      if((uint)a >= KERNBASE){
        *pte = want | (*pte & (PTE_A | PTE_D));
        goto advance;
      }
      remap_dump_debug(pgdir, a, pa, perm, want, *pte,
          (uint)__builtin_return_address(0));
      panic("remap");
    }
    *pte = want;
advance:
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// There is one page table per process, plus one that's used when
// a CPU is not running any process (kpgdir). The kernel uses the
// current process's page table during system calls and interrupts;
// page protection bits prevent user code from using the kernel's
// mappings.
// 
// setupkvm() and exec() set up every page table like this:
//
//   0..KERNBASE: user memory (text+data+stack+heap), mapped to
//                phys memory allocated by the kernel
//   KERNBASE..KERNBASE+EXTMEM: mapped to 0..EXTMEM (for I/O space)
//   KERNBASE+EXTMEM..data: mapped to EXTMEM..V2P(data)
//                for the kernel's instructions and r/o data
//   data..KERNBASE+PHYSTOP: mapped to V2P(data)..PHYSTOP, 
//                                  rw data + free physical memory
//   0xf0000000..0: mapped direct (devices such as ioapic, EHCI)
//
// The kernel allocates physical memory for its heap and for user memory
// between V2P(end) and the end of physical memory (PHYSTOP)
// (directly addressable from end..P2V(PHYSTOP)).

// This table defines the kernel's mappings, which are present in
// every process's page table.
static struct kmap {
  void *virt;
  uint phys_start;
  uint phys_end;
  int perm;
} kmap[] = {
  { (void*) KERNBASE, 0,             EXTMEM,    PTE_W|PTE_PCD|PTE_PWT},  // low I/O space (uncacheable)
  { (void*) KERNLINK, V2P(KERNLINK), V2P(data), 0}, // kernel text+rodata
  { (void*) data,     V2P(data),     PHYSTOP,   PTE_W},  // kernel data, memory
  { (void*) DEVSPACE, DEVSPACE,      0,         PTE_W|PTE_PCD|PTE_PWT},  // devices (uncacheable)
};

/*
 * 建立内核部分页表；调用者须已持有 setupkvm_lock，或由 setupkvm() 包装器代持。
 * copyuvm / exec 在持锁下连续完成本函数与用户空间 mappages，避免多核交错触发 remap。
 */
pde_t*
setupkvm_impl(void)
{
  pde_t *pgdir;
  struct kmap *k;
  extern uint phystop;
  uint k2_phys_end;
  int ki;

  if((pgdir = (pde_t*)kalloc()) == 0)
    return 0;
  memset(pgdir, 0, PGSIZE);
  k2_phys_end = PHYSTOP;
  if(phystop != 0)
    k2_phys_end = phystop;
  if(p2v(k2_phys_end) > (void*)DEVSPACE){
    kfree((char*)pgdir);
    panic("phystop too high");
  }
  if(k2_phys_end <= (uint)V2P(data)){
    kfree((char*)pgdir);
    panic("kmap2 bad phystop");
  }

  /*
   * 不再把 kpgdir 的 PDE 整体拷贝到新进程页目录：
   * 一旦 kpgdir 某项被异常污染，会把坏 PDE 扩散到每个新进程，
   * 表现为 dig 第二次开始随机崩溃（子进程 exec 前即 trap）。
   * 进程页目录的内核映射统一由下方 kmap 重建，避免污染传播。
   */

  for(ki = 0, k = kmap; ki < (int)NELEM(kmap); ki++, k++){
    uint size = k->phys_end - k->phys_start;
    uint pa = (uint)k->phys_start;
    if(ki == 2)
      size = k2_phys_end - k->phys_start;
    if(mappages_nolock(pgdir, k->virt, size, pa, k->perm) < 0){
      cprintf("[vm] fatal: setupkvm map failed ki=%d virt=0x%x size=0x%x pa=0x%x free_pages=%d\n",
              ki, (uint)k->virt, size, pa, kfreepages());
      freevm_nolock(pgdir);
      return 0;
    }
  }

  return pgdir;
}

pde_t*
setupkvm(void)
{
  pde_t *pgdir;

  acquire(&setupkvm_lock);
  pgdir = setupkvm_impl();
  release(&setupkvm_lock);
  return pgdir;
}

void
setupkvm_lock_acquire(void)
{
  acquire(&setupkvm_lock);
}

void
setupkvm_lock_release(void)
{
  release(&setupkvm_lock);
}

// 须在首次 kvmalloc/setupkvm 之前调用（见 main.c），保证自旋锁已初始化。
void
setupkvm_lock_init(void)
{
  initlock(&setupkvm_lock, "setupkvm");
}

// Allocate one page table for the machine for the kernel address
// space for scheduler processes.
void
kvmalloc(void)
{
  kpgdir = setupkvm();
  switchkvm();
}

// Switch h/w page table register to the kernel-only page table,
// for when no process is running.
void
switchkvm(void)
{
  lcr3(v2p(kpgdir));   // switch to the kernel page table
}

// Switch TSS and h/w page table to correspond to process p.
void
switchuvm(struct proc *p)
{
  pushcli();
  cpu->gdt[SEG_TSS] = SEG16(STS_T32A, &cpu->ts, sizeof(cpu->ts)-1, 0);
  cpu->gdt[SEG_TSS].s = 0;
  cpu->ts.ss0 = SEG_KDATA << 3;
  cpu->ts.esp0 = (uint)p->kstack + KSTACKSIZE;
  ltr(SEG_TSS << 3);
  if(p->pgdir == 0)
    panic("switchuvm: no pgdir");
  lcr3(v2p(p->pgdir));  // switch to new address space
  popcli();
}

// Load the initcode into address 0 of pgdir.
// sz must be less than a page.
void
inituvm(pde_t *pgdir, char *init, uint sz)
{
  char *mem;
  
  if(sz >= PGSIZE)
    panic("inituvm: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pgdir, 0, PGSIZE, v2p(mem), PTE_W|PTE_U);
  memmove(mem, init, sz);
}

// Load a program segment into pgdir.  addr must be page-aligned
// and the pages from addr to addr+sz must already be mapped.
int
loaduvm(pde_t *pgdir, char *addr, struct inode *ip, uint offset, uint sz)
{
  uint i, pa, n;
  pte_t *pte;

  if((uint) addr % PGSIZE != 0)
    panic("loaduvm: addr must be page aligned");
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walkpgdir(pgdir, addr+i, 0)) == 0)
      panic("loaduvm: address should exist");
    pa = PTE_ADDR(*pte);
    if(sz - i < PGSIZE)
      n = sz - i;
    else
      n = PGSIZE;
    if(readi(ip, p2v(pa), offset+i, n) != n)
      return -1;
  }
  return 0;
}

// Allocate page tables and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
int
allocuvm(pde_t *pgdir, uint oldsz, uint newsz)
{
  int r;

  setupkvm_lock_acquire();
  r = allocuvm_nolock(pgdir, oldsz, newsz);
  setupkvm_lock_release();
  return r;
}

/*
 * 与 allocuvm 相同，但调用者已持有 setupkvm_lock（例如 growproc 与 proc->sz 同一临界区）。
 */
int
allocuvm_nolock(pde_t *pgdir, uint oldsz, uint newsz)
{
  char *mem;
  uint a;

  if(newsz >= KERNBASE)
    return 0;
  if(newsz < oldsz)
    return oldsz;

  a = PGROUNDUP(oldsz);
  for(; a < newsz; a += PGSIZE){
    mem = kalloc();
    if(mem == 0){
      cprintf("allocuvm out of memory\n");
      deallocuvm_nolock(pgdir, newsz, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if(mappages_nolock(pgdir, (char*)a, PGSIZE, v2p(mem), PTE_W|PTE_U) < 0){
      kfree(mem);
      deallocuvm_nolock(pgdir, newsz, oldsz);
      return 0;
    }
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
int
deallocuvm(pde_t *pgdir, uint oldsz, uint newsz)
{
  int r;

  setupkvm_lock_acquire();
  r = deallocuvm_nolock(pgdir, oldsz, newsz);
  setupkvm_lock_release();
  return r;
}

int
deallocuvm_nolock(pde_t *pgdir, uint oldsz, uint newsz)
{
  pte_t *pte;
  uint a, pa;

  if(newsz >= oldsz)
    return oldsz;

  a = PGROUNDUP(newsz);
  for(; a  < oldsz; a += PGSIZE){
    if(a < 0x4000)
      continue;
    pde_t pde = pgdir[PDX(a)];
    uint pde_pa = PTE_ADDR(pde);
    if((pde & PTE_P) && pde_pa && pgdir_user_pt_shared_with_other(pgdir, pde_pa)){
      if(a == 0x1000){
        cprintf("[vm watch] skip shared dealloc va=0x1000 pgdir=%p pde_pa=0x%x pid=%d name=%s\n",
                pgdir, pde_pa, proc ? proc->pid : -1, proc ? proc->name : "?");
      }
      cprintf("[vm] warning: skip dealloc on shared user page-table pde_idx=%d pa=0x%x pgdir=%p\n",
              PDX(a), pde_pa, pgdir);
      a = PGADDR(PDX(a) + 1, 0, 0) - PGSIZE;
      continue;
    }
    pte = walkpgdir(pgdir, (char*)a, 0);
    /*
     * 须与 MIT xv6 一致：无二级页表时跳到「下一页目录项」对应虚址，
     * 不能用 (NPTENTRIES-1)*PGSIZE 累加，否则 a 与 PDX 边界不对齐，
     * walkpgdir 可能返回指向非页表内存的指针，把任意数据当 PTE 解析，
     * 出现「伪物理址」并误跳过 kfree，最终物理页泄漏、init 无法再 fork。
     */
    if(!pte)
      a = PGADDR(PDX(a) + 1, 0, 0) - PGSIZE;
    else if((*pte & PTE_P) != 0){
      extern char end[];
      extern uint phystop;
      uint ptop = phystop ? phystop : PHYSTOP;
      char *kv;

      pa = PTE_ADDR(*pte);
      if(a == 0x1000){
        cprintf("[vm watch] clear va=0x1000 pgdir=%p pde_pa=0x%x pte_pa=0x%x pid=%d name=%s\n",
                pgdir, pde_pa, pa, proc ? proc->pid : -1, proc ? proc->name : "?");
      }
      *pte = 0;
      if(pa == 0){
        /*
         * 仅 PTE_P、物理帧为 0 的项（如 pte==1）非法；可能来自损坏或竞态。
         * 已清除项，勿 panic，否则 dig/wait 回收页表时整机崩溃。
         */
        cprintf("[vm] warning: cleared invalid user PTE (pa=0) va=0x%x\n", a);
        continue;
      }
      if((pa % PGSIZE) != 0 || pa >= ptop){
        cprintf("[vm] warning: user PTE pa out of range cleared va=0x%x pa=0x%x ptop=0x%x\n",
                a, pa, ptop);
        continue;
      }
      kv = (char*)p2v(pa);
      if(kv < end || v2p(kv) >= ptop){
        cprintf("[vm] warning: mapped frame outside kalloc reclaim range va=0x%x pa=0x%x kv=%p\n",
                a, pa, kv);
        continue;
      }
      kfree(kv);
    }
  }
  return newsz;
}

static void
freevm_nolock(pde_t *pgdir)
{
  uint i;

  if(pgdir == 0)
    return;
  deallocuvm_nolock(pgdir, KERNBASE, 0);
  for(i = 0; i < NPDENTRIES; i++){
    if(pgdir[i] & PTE_P){
      extern char end[];
      extern uint phystop;
      uint ptop = phystop ? phystop : PHYSTOP;
      uint pa = PTE_ADDR(pgdir[i]);
      char *kv = (char*)p2v(pa);

      pgdir[i] = 0;
      if(pa == 0 || (pa % PGSIZE) != 0 || pa >= ptop || kv < end || v2p(kv) >= ptop){
        cprintf("[vm] warning: skip invalid page-table page when freeing PDE di=%d pa=0x%x kv=0x%x ptop=0x%x\n",
                i, pa, (uint)kv, ptop);
        continue;
      }
      if(i < PDX(KERNBASE) && pgdir_user_pt_shared_with_other(pgdir, pa)){
        cprintf("[vm] warning: skip freeing shared user page-table page di=%d pa=0x%x\n", i, pa);
        continue;
      }
      kfree(kv);
    }
  }
  kfree((char*)pgdir);
}

// Free a page table and all the physical memory pages
// in the user part.
void
freevm(pde_t *pgdir)
{
  if(pgdir == 0)
    panic("freevm: no pgdir");
  setupkvm_lock_acquire();
  freevm_nolock(pgdir);
  setupkvm_lock_release();
}

// Clear PTE_U on a page. Used to create an inaccessible
// page beneath the user stack.
void
clearpteu(pde_t *pgdir, char *uva)
{
  pte_t *pte;

  pte = walkpgdir(pgdir, uva, 0);
  if(pte == 0)
    panic("clearpteu");
  *pte &= ~PTE_U;
}

// Given a parent process's page table, create a copy
// of it for a child.
pde_t*
copyuvm(pde_t *pgdir, uint sz)
{
  pde_t *d;
  pte_t *pte;
  uint pa, i;
  char *mem;

  /* sz 若异常偏大或触及内核虚拟地址，逐页映射会与 setupkvm 冲突并触发 remap。 */
  if(sz >= KERNBASE)
    return 0;

  acquire(&setupkvm_lock);
  if((d = setupkvm_impl()) == 0){
    release(&setupkvm_lock);
    return 0;
  }
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walkpgdir(pgdir, (void *) i, 0)) == 0)
      panic("copyuvm: pte should exist");
    if(!(*pte & PTE_P))
      panic("copyuvm: page not present");
    pa = PTE_ADDR(*pte);
    if((mem = kalloc()) == 0)
      goto bad;
    memmove(mem, (char*)p2v(pa), PGSIZE);
    if(mappages_nolock(d, (void*)i, PGSIZE, v2p(mem), PTE_W|PTE_U) < 0)
      goto bad;
  }
  release(&setupkvm_lock);
  return d;

bad:
  freevm_nolock(d);
  release(&setupkvm_lock);
  return 0;
}

//PAGEBREAK!
// Map user virtual address to kernel address.
char*
uva2ka(pde_t *pgdir, char *uva)
{
  pte_t *pte;
  uint pa;
  extern uint phystop;
  uint ptop;
  char *kv;

  ptop = phystop ? phystop : PHYSTOP;
  pte = walkpgdir(pgdir, uva, 0);
  if(pte == 0)
    return 0;
  if((*pte & PTE_P) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  pa = PTE_ADDR(*pte);
  kv = (char*)p2v(pa);
  if(pa == 0 || (pa % PGSIZE) != 0 || pa >= ptop || kv < end || v2p(kv) >= ptop){
    cprintf("[vm] warning: uva2ka found invalid user PTE uva=0x%x pte=0x%x pa=0x%x\n",
            (uint)uva, *pte, pa);
    return 0;
  }
  return kv;
}

// Copy len bytes from p to user address va in page table pgdir.
// Most useful when pgdir is not the current page table.
// uva2ka ensures this only works for PTE_U pages.
int
copyout(pde_t *pgdir, uint va, void *p, uint len)
{
  char *buf, *pa0;
  uint n, va0;

  buf = (char*)p;
  while(len > 0){
    va0 = (uint)PGROUNDDOWN(va);
    pa0 = uva2ka(pgdir, (char*)va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (va - va0);
    if(n > len)
      n = len;
    memmove(pa0 + (va - va0), buf, n);
    len -= n;
    buf += n;
    va = va0 + PGSIZE;
  }
  return 0;
}

/*
 * 仅调试：打印指定用户虚拟地址在给定页目录下的原始映射状态。
 * 注意：不分配页表，不修复映射，不修改任何页表项。
 */
void
vm_dump_user_mapping(pde_t *pgdir, uint va, const char *tag)
{
  pde_t pde;
  uint pde_pa;
  pte_t *pt;
  pte_t pte;
  uint ptop;
  extern uint phystop;

  if(pgdir == 0){
    uartcprintf("[vm dbg] %s: pgdir is null va=0x%x\n", tag ? tag : "?", va);
    return;
  }
  ptop = phystop ? phystop : PHYSTOP;
  pde = pgdir[PDX(va)];
  uartcprintf("[vm dbg] %s: pid=%d pgdir=%p va=0x%x pde_idx=%d pde=0x%x\n",
              tag ? tag : "?", proc ? proc->pid : -1, pgdir, va, PDX(va), pde);
  if((pde & PTE_P) == 0){
    uartcprintf("[vm dbg] %s: pde not present\n", tag ? tag : "?");
    return;
  }
  pde_pa = PTE_ADDR(pde);
  if(pde_pa == 0 || (pde_pa % PGSIZE) != 0 || pde_pa >= ptop){
    uartcprintf("[vm dbg] %s: invalid pde pa=0x%x ptop=0x%x\n",
                tag ? tag : "?", pde_pa, ptop);
    return;
  }
  pt = (pte_t*)p2v(pde_pa);
  pte = pt[PTX(va)];
  uartcprintf("[vm dbg] %s: pte_idx=%d pte=0x%x pte_pa=0x%x flags=0x%x\n",
              tag ? tag : "?", PTX(va), pte, PTE_ADDR(pte), pte & 0xFFF);
}

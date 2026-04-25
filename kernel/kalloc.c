// Physical memory allocator, intended to allocate
// memory for user processes, kernel stacks, page table pages,
// and pipe buffers. Allocates 4096-byte pages.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "spinlock.h"

void freerange(void *vstart, void *vend);
extern char end[]; // first address after kernel loaded from ELF file

struct run {
  struct run *next;
};

#define PAGE_STATE_ALLOC   0
#define PAGE_STATE_FREE    1
#define PAGE_STATE_UNKNOWN 2
#define PAGE_TRACK_SLOTS (PHYSTOP / PGSIZE)
static uchar page_state[PAGE_TRACK_SLOTS];

struct {
  struct spinlock lock;
  int use_lock;
  struct run *freelist;
  int nfree;  // O(1) free page counter
} kmem;
static struct run *kmem_freelist_shadow;
static int kmem_nfree_shadow;

static int
run_ptr_valid(struct run *r, uint ptop)
{
  if(r == 0)
    return 1;
  if(((uint)r % PGSIZE) != 0)
    return 0;
  if((char*)r < end)
    return 0;
  if(v2p((char*)r) >= ptop)
    return 0;
  return 1;
}

static void
rebuild_freelist_locked(uint ptop)
{
  uint pa_start, pa_end, pa, idx;
  struct run *r;

  pa_start = PGROUNDUP(v2p(end));
  pa_end = PGROUNDDOWN(ptop - 1);
  kmem.freelist = 0;
  kmem.nfree = 0;
  for(pa = pa_end; pa >= pa_start; pa -= PGSIZE){
    idx = pa / PGSIZE;
    if(idx < PAGE_TRACK_SLOTS && page_state[idx] == PAGE_STATE_FREE){
      r = (struct run*)p2v(pa);
      r->next = kmem.freelist;
      kmem.freelist = r;
      kmem.nfree++;
    }
    if(pa < pa_start + PGSIZE)
      break;
  }
  kmem_freelist_shadow = kmem.freelist;
  kmem_nfree_shadow = kmem.nfree;
}

// Initialization happens in two phases.
// 1. main() calls kinit1() while still using entrypgdir to place just
// the pages mapped by entrypgdir on free list.
// 2. main() calls kinit2() with the rest of the physical pages
// after installing a full page table that maps them on all cores.
void
kinit1(void *vstart, void *vend)
{
  int i;
  for(i = 0; i < PAGE_TRACK_SLOTS; i++)
    page_state[i] = PAGE_STATE_UNKNOWN;
  initlock(&kmem.lock, "kmem");
  kmem.use_lock = 0;
  kmem_freelist_shadow = 0;
  kmem_nfree_shadow = 0;
  freerange(vstart, vend);
}

// E820 memory map entry (matches bootloader format at physical 0x8000)
struct e820ent {
  uint base_lo, base_hi;
  uint len_lo, len_hi;
  uint type;
  uint acpi;
};

#define E820_MAP_PHYS  0x8000
#define E820_USABLE    1

// Check if physical address is in a usable E820 region
static int
e820_page_usable(uint pa)
{
  uint *cnt = (uint*)P2V(E820_MAP_PHYS);
  struct e820ent *ents = (struct e820ent*)P2V(E820_MAP_PHYS + 4);
  uint count = *cnt;
  uint i;

  if(count == 0 || count > 32)
    return 1;  // No E820 data: assume usable (backward compat)

  for(i = 0; i < count; i++){
    if(ents[i].type != E820_USABLE)
      continue;
    if(ents[i].base_hi != 0)
      continue;  // 64-bit address, skip
    uint base = ents[i].base_lo;
    uint end = base + ents[i].len_lo;
    if(end < base)
      continue;  // overflow
    if(pa >= base && pa + PGSIZE <= end)
      return 1;
  }
  return 0;  // Not in any usable region: SKIP this page
}

void
kinit2(void *vstart, void *vend)
{
  char *p;
  int freed = 0, skipped = 0;

  // CRITICAL FIX for ThinkPad X220:
  // Free pages from HIGH to LOW so that kalloc() returns LOW pages first.
  // With phystop=1792MB, the old order (low-to-high) caused kalloc to
  // return pages near 1792MB first. These high-address pages may overlap
  // with firmware-reserved regions (ACPI NVS, SMM, ME stolen memory)
  // that the E820 map marks as non-usable.
  // By freeing high-to-low, the freelist head points to the LOWEST page,
  // and early allocations (kstack, page tables) use safe low addresses.
  //
  // Additionally, check E820 map to skip non-usable pages.
  p = (char*)PGROUNDDOWN((uint)vend - 1);
  for(; p >= (char*)PGROUNDUP((uint)vstart); p -= PGSIZE){
    if(e820_page_usable(v2p(p))){
      kfree(p);
      freed++;
    } else {
      skipped++;
    }
  }
  kmem.use_lock = 1;
  if(skipped > 0)
    cprintf("kinit2: freed %d pages, skipped %d non-usable pages\n",
            freed, skipped);
}

void
freerange(void *vstart, void *vend)
{
  char *p;
  p = (char*)PGROUNDUP((uint)vstart);
  for(; p + PGSIZE <= (char*)vend; p += PGSIZE)
    kfree(p);
}

//PAGEBREAK: 21
// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(char *v)
{
  struct run *r;
  extern uint phystop;
  uint ptop = phystop ? phystop : PHYSTOP;
  uint pa, idx;

  /*
   * 空指针：v2p(0) 在无符号减法下会下溢成 0x80000000，误判为 phys=0x80000000，
   * 与「合法低物理页经 p2v 后落在 KERNBASE 附近」混淆，并触发恐慌。
   * 与 free(NULL) 语义一致：忽略。
   */
  if(v == 0)
    return;

  if((uint)v % PGSIZE || v < end || v2p(v) >= ptop){
    cprintf("kfree: bad addr v=%x (end=%x phys=%x phystop=%x)\n",
            v, end, v2p(v), ptop);
    panic("kfree");
  }
  pa = v2p(v);
  idx = pa / PGSIZE;
  if(idx < PAGE_TRACK_SLOTS){
    if(page_state[idx] == PAGE_STATE_FREE)
      return;
    page_state[idx] = PAGE_STATE_FREE;
  }
  // Fill with junk to catch dangling refs.
  memset(v, 1, PGSIZE);

  if(kmem.use_lock)
    acquire(&kmem.lock);
  if(!run_ptr_valid(kmem.freelist, ptop) && run_ptr_valid(kmem_freelist_shadow, ptop)){
    cprintf("[page dbg] warning: freelist pointer corrupted, restored from shadow bad=%x shadow=%x\n",
            kmem.freelist, kmem_freelist_shadow);
    kmem.freelist = kmem_freelist_shadow;
    kmem.nfree = kmem_nfree_shadow;
  }
  r = (struct run*)v;
  r->next = kmem.freelist;
  kmem.freelist = r;
  kmem.nfree++;
  kmem_freelist_shadow = kmem.freelist;
  kmem_nfree_shadow = kmem.nfree;
  if(kmem.use_lock)
    release(&kmem.lock);
}

// Return number of free pages (O(1) via counter)
int
kfreepages(void)
{
  int count;

  if(kmem.use_lock)
    acquire(&kmem.lock);
  count = kmem.nfree;
  if(kmem.use_lock)
    release(&kmem.lock);
  return count;
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
char*
kalloc(void)
{
  struct run *r;
  struct run *next;
  extern uint phystop;
  uint ptop = phystop ? phystop : PHYSTOP;
  uint pa, idx, nidx;

  if(kmem.use_lock)
    acquire(&kmem.lock);
  if(!run_ptr_valid(kmem.freelist, ptop) && run_ptr_valid(kmem_freelist_shadow, ptop)){
    cprintf("[page dbg] warning: freelist pointer corrupted, restored from shadow bad=%x shadow=%x\n",
            kmem.freelist, kmem_freelist_shadow);
    kmem.freelist = kmem_freelist_shadow;
    kmem.nfree = kmem_nfree_shadow;
  }
  if(kmem.freelist == 0 && kmem.nfree > 0){
    rebuild_freelist_locked(ptop);
  }
  r = 0;
  while(kmem.freelist){
    r = kmem.freelist;
    if(((uint)r % PGSIZE) != 0 || (char*)r < end || v2p((char*)r) >= ptop){
      cprintf("[page dbg] warning: invalid node in freelist, dropped r=%x end=%x ptop=%x\n",
              r, end, ptop);
      rebuild_freelist_locked(ptop);
      r = 0;
      continue;
    }
    next = r->next;
    if(!run_ptr_valid(next, ptop)){
      cprintf("[page dbg] warning: invalid next in freelist node r=%x next=%x, rebuilding\n",
              r, next);
      rebuild_freelist_locked(ptop);
      r = 0;
      continue;
    }
    if(next){
      nidx = v2p((char*)next) / PGSIZE;
      if(nidx < PAGE_TRACK_SLOTS && page_state[nidx] != PAGE_STATE_FREE){
        cprintf("[page dbg] warning: freelist next node state mismatch r=%x next=%x nidx=%d state=%d\n",
                r, next, nidx, page_state[nidx]);
        rebuild_freelist_locked(ptop);
        r = 0;
        continue;
      }
    }
    kmem.freelist = next;
    pa = v2p((char*)r);
    idx = pa / PGSIZE;
    if(idx < PAGE_TRACK_SLOTS && page_state[idx] != PAGE_STATE_FREE){
      cprintf("[page dbg] warning: freelist page state mismatch, rebuilt freelist pa=%x idx=%d state=%d\n",
              pa, idx, page_state[idx]);
      rebuild_freelist_locked(ptop);
      r = 0;
      continue;
    }
    if(idx < PAGE_TRACK_SLOTS)
      page_state[idx] = PAGE_STATE_ALLOC;
    if(kmem.nfree > 0)
      kmem.nfree--;
    kmem_freelist_shadow = kmem.freelist;
    kmem_nfree_shadow = kmem.nfree;
    break;
  }
  if(r == 0){
    kmem_freelist_shadow = kmem.freelist;
    kmem_nfree_shadow = kmem.nfree;
  }
  if(kmem.use_lock)
    release(&kmem.lock);
  return (char*)r;
}


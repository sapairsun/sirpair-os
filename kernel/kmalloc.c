#include "types.h"
#include "defs.h"
#include "spinlock.h"

/*
 * 简单内核堆（供 microps 等子系统使用），首适配合并空闲块。
 * 与按页 kalloc 分离，避免改动既有页分配器语义。
 */
#define KM_ARENA_BYTES (512 * 1024)
static uchar km_arena[KM_ARENA_BYTES];
static struct spinlock km_lock;
static int km_inited;

static int
km_block_in_arena(void *p)
{
  uchar *q = p;
  return q >= km_arena && q < km_arena + sizeof(km_arena);
}

struct km_hdr {
  struct km_hdr *next;
  uint size;
  uint magic;
};

static struct km_hdr *km_freelist;
enum {
  KM_MAGIC_FREE = 0x4b4d4652u,   /* "KMFR" */
  KM_MAGIC_ALLOC = 0x4b4d414cu,  /* "KMAL" */
};

void
kmalloc_init(void)
{
  struct km_hdr *h;

  if(km_inited)
    return;
  initlock(&km_lock, "kmalloc");
  h = (struct km_hdr *)km_arena;
  h->next = 0;
  h->size = sizeof(km_arena) - sizeof(struct km_hdr);
  h->magic = KM_MAGIC_FREE;
  km_freelist = h;
  km_inited = 1;
}

static void
km_coalesce(void)
{
  /* 线性扫描合并相邻空闲块（arena 内地址连续时）。 */
  struct km_hdr *p;

  again:
  for(p = km_freelist; p; p = p->next){
    if(p->next && (uchar *)p + sizeof(struct km_hdr) + p->size == (uchar *)p->next){
      p->size += sizeof(struct km_hdr) + p->next->size;
      p->next = p->next->next;
      p->magic = KM_MAGIC_FREE;
      goto again;
    }
  }
}

void *
kmalloc(uint nbytes)
{
  struct km_hdr *p, *best, *bestprev, *prev;
  uint need;
  uint bsz;

  if(!km_inited)
    kmalloc_init();
  if(nbytes == 0)
    return 0;
  need = (nbytes + 7u) & ~7u;
  acquire(&km_lock);
  best = 0;
  bestprev = 0;
  prev = 0;
  for(p = km_freelist; p; prev = p, p = p->next){
    if(p->size >= need && (!best || p->size < best->size)){
      best = p;
      bestprev = prev;
    }
  }
  if(!best){
    release(&km_lock);
    return 0;
  }
  if(bestprev)
    bestprev->next = best->next;
  else
    km_freelist = best->next;

  bsz = best->size;
  if(bsz >= need + sizeof(struct km_hdr) + 16){
    struct km_hdr *rest;

    rest = (struct km_hdr *)((uchar *)best + sizeof(struct km_hdr) + need);
    rest->size = bsz - need - sizeof(struct km_hdr);
    rest->magic = KM_MAGIC_FREE;
    rest->next = km_freelist;
    km_freelist = rest;
    best->size = need;
  }
  best->magic = KM_MAGIC_ALLOC;
  best->next = 0;
  release(&km_lock);
  return (void *)(best + 1);
}

void
kmfree(void *ap)
{
  struct km_hdr *h;

  if(!ap)
    return;
  h = (struct km_hdr *)ap - 1;
  if(!km_block_in_arena(h) || !km_block_in_arena(ap)){
    cprintf("kmfree: invalid pointer ap=0x%x hdr=0x%x arena=[0x%x,0x%x)\n",
            (uint)ap, (uint)h, (uint)km_arena,
            (uint)(km_arena + sizeof(km_arena)));
    panic("kmfree");
  }
  if(h->magic != KM_MAGIC_ALLOC){
    cprintf("kmfree: warning duplicate free or bad block, ignored ap=0x%x hdr=0x%x magic=0x%x size=0x%x\n",
            (uint)ap, (uint)h, h->magic, h->size);
    return;
  }
  acquire(&km_lock);
  h->magic = KM_MAGIC_FREE;
  h->next = km_freelist;
  km_freelist = h;
  km_coalesce();
  release(&km_lock);
}

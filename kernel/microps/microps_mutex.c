#include "types.h"
#include "defs.h"
#include "platform.h"

static struct spinlock mtx_depth_lock;
static int mtx_depth_lock_inited;

struct mtx_depth_ent {
  mutex_t *m;
  int owner_cpu;
  int depth;
};

static struct mtx_depth_ent mtx_depth_tab[64];

static void
mtx_depth_init_once(void)
{
  if(mtx_depth_lock_inited)
    return;
  initlock(&mtx_depth_lock, "mps-mtx-depth");
  mtx_depth_lock_inited = 1;
}

static int
mtx_current_cpu(void)
{
  int id;
  pushcli();
  id = cpunum();
  popcli();
  return id;
}

static struct mtx_depth_ent*
mtx_depth_lookup(mutex_t *mutex, int owner_cpu)
{
  int i;
  for(i = 0; i < 64; i++){
    if(mtx_depth_tab[i].depth > 0 &&
       mtx_depth_tab[i].m == mutex &&
       mtx_depth_tab[i].owner_cpu == owner_cpu)
      return &mtx_depth_tab[i];
  }
  return 0;
}

static struct mtx_depth_ent*
mtx_depth_alloc(mutex_t *mutex, int owner_cpu)
{
  int i;
  for(i = 0; i < 64; i++){
    if(mtx_depth_tab[i].depth == 0){
      mtx_depth_tab[i].m = mutex;
      mtx_depth_tab[i].owner_cpu = owner_cpu;
      return &mtx_depth_tab[i];
    }
  }
  return 0;
}

int
mutex_init(mutex_t *mutex)
{
  if(!mutex)
    return -1;
  mtx_depth_init_once();
  initlock(mutex, "microps");
  return 0;
}

int
mutex_lock(mutex_t *mutex)
{
  struct mtx_depth_ent *e;
  int cpuid;

  if(!mutex)
    return -1;
  mtx_depth_init_once();
  cpuid = mtx_current_cpu();
  if(holding(mutex)){
    acquire(&mtx_depth_lock);
    e = mtx_depth_lookup(mutex, cpuid);
    if(!e)
      e = mtx_depth_alloc(mutex, cpuid);
    if(!e){
      release(&mtx_depth_lock);
      return -1;
    }
    e->depth++;
    release(&mtx_depth_lock);
    return 0;
  }
  acquire(mutex);
  acquire(&mtx_depth_lock);
  e = mtx_depth_lookup(mutex, cpuid);
  if(!e)
    e = mtx_depth_alloc(mutex, cpuid);
  if(e)
    e->depth = 1;
  release(&mtx_depth_lock);
  return 0;
}

int
mutex_unlock(mutex_t *mutex)
{
  struct mtx_depth_ent *e;
  int cpuid;

  if(!mutex)
    return -1;
  mtx_depth_init_once();
  cpuid = mtx_current_cpu();
  if(!holding(mutex)){
    return -1;
  }
  acquire(&mtx_depth_lock);
  e = mtx_depth_lookup(mutex, cpuid);
  if(e && e->depth > 1){
    e->depth--;
    release(&mtx_depth_lock);
    return 0;
  }
  if(e){
    e->depth = 0;
    e->m = 0;
    e->owner_cpu = -1;
  }
  release(&mtx_depth_lock);
  release(mutex);
  return 0;
}

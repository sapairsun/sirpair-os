#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
#include "platform.h"

extern int errno;

#ifndef EINTR
#define EINTR 4
#endif

int
sched_ctx_init(struct sched_ctx *ctx)
{
  if(!ctx)
    return -1;
  ctx->interrupted = 0;
  ctx->wc = 0;
  return 0;
}

int
sched_ctx_destroy(struct sched_ctx *ctx)
{
  (void)ctx;
  return 0;
}

int
sched_sleep(struct sched_ctx *ctx, mutex_t *mutex, const struct timespec *abstime)
{
  (void)abstime;

  if(!ctx || !mutex){
    return -1;
  }
  if(proc && proc->killed){
    errno = EINTR;
    return -1;
  }
  if(ctx->interrupted){
    errno = EINTR;
    return -1;
  }
  ctx->wc++;
  sleep(ctx, mutex);
  ctx->wc--;
  if(proc && proc->killed){
    errno = EINTR;
    return -1;
  }
  if(ctx->interrupted){
    if(!ctx->wc)
      ctx->interrupted = 0;
    errno = EINTR;
    return -1;
  }
  return 0;
}

int
sched_wakeup(struct sched_ctx *ctx)
{
  if(!ctx)
    return -1;
  wakeup(ctx);
  return 0;
}

int
sched_interrupt(struct sched_ctx *ctx)
{
  if(!ctx)
    return -1;
  ctx->interrupted = 1;
  wakeup(ctx);
  return 0;
}

#ifndef MICROPS_PLATFORM_H
#define MICROPS_PLATFORM_H

#include "microps_kernel.h"
#include "spinlock.h"

void *memory_alloc(size_t size);
void memory_free(void *ptr);

typedef struct spinlock mutex_t;

int mutex_init(mutex_t *mutex);
int mutex_lock(mutex_t *mutex);
int mutex_unlock(mutex_t *mutex);

struct sched_ctx {
  int interrupted;
  int wc;
};

struct timespec {
  long tv_sec;
  long tv_nsec;
};

int sched_ctx_init(struct sched_ctx *ctx);
int sched_ctx_destroy(struct sched_ctx *ctx);
int sched_sleep(struct sched_ctx *ctx, mutex_t *mutex,
                const struct timespec *abstime);
int sched_wakeup(struct sched_ctx *ctx);
int sched_interrupt(struct sched_ctx *ctx);

int intr_request_irq(unsigned int irq,
                     int (*handler)(unsigned int irq, void *dev), int flags,
                     const char *name, void *dev);
int intr_run(void);
int intr_init(void);

void microps_raise_softirq(void);

static inline void
raise_softirq(void)
{
  microps_raise_softirq();
}

int gettimeofday(struct timeval *tv, void *tz);

uint microps_random(void);

#endif

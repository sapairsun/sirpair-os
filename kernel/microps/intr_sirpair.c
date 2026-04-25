#include "types.h"
#include "platform.h"

int
intr_request_irq(unsigned int irq,
                   int (*handler)(unsigned int irq, void *dev), int flags,
                   const char *name, void *dev)
{
  (void)irq;
  (void)handler;
  (void)flags;
  (void)name;
  (void)dev;
  return 0;
}

int
intr_run(void)
{
  return 0;
}

int
intr_init(void)
{
  return 0;
}

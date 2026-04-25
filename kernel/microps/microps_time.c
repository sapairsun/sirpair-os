#include "types.h"
#include "param.h"
#include "platform.h"

extern uint ticks;

int
gettimeofday(struct timeval *tv, void *tz)
{
  if(!tv)
    return -1;
  (void)tz;
  tv->tv_sec = ticks / HZ;
  tv->tv_usec = (ticks % HZ) * (1000000 / HZ);
  return 0;
}

uint
microps_random(void)
{
  uint t;

  t = ticks;
  t ^= t << 13;
  t ^= t >> 17;
  t ^= t << 5;
  if(t == 0)
    t = 0x12345678u;
  return t;
}

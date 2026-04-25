#include "dat.h"
#include "types.h"
#include "user.h"

int64
nanoseconds(void)
{
  time_t t;

  t = time(0);
  return (int64)t * 1000000000LL;
}

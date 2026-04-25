#include "types.h"

int errno;

/* 与 <errno.h> 中 errno 宏配合，避免链接到 glibc */
int *
__errno_location(void)
{
  return &errno;
}

char *
strerror(int e)
{
  (void)e;
  return "errno";
}

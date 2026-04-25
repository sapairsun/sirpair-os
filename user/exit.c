#include "types.h"
#include "syscall.h"
#include "traps.h"

void
exit(int status)
{
  (void)status;
  asm volatile("movl %0, %%eax; int %1" :: "i"(SYS_exit), "i"(T_SYSCALL));
  for(;;)
    ;
}

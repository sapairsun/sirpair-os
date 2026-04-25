#include "types.h"
#include "defs.h"
#include "platform.h"

void *
memory_alloc(size_t size)
{
  return kmalloc(size);
}

void
memory_free(void *ptr)
{
  kmfree(ptr);
}

#include "types.h"

unsigned long
strtoul(const char *s, char **end, int base)
{
  unsigned long v;
  const char *p;

  (void)base;
  v = 0;
  p = s;
  if(!p)
    return 0;
  while(*p == ' ')
    p++;
  while(*p >= '0' && *p <= '9'){
    v = v * 10 + (unsigned long)(*p - '0');
    p++;
  }
  if(end)
    *end = (char *)p;
  return v;
}

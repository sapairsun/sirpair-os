/* beanstalkd 在 Sirpair 上链接无 libc：补齐上游通过 <string.h> 等引用的符号 */
#include "types.h"
#include "user.h"
#include <stdint.h>

int
strncmp(const char *a, const char *b, size_t n)
{
  size_t i;

  for(i = 0; i < n; i++){
    if(a[i] != b[i])
      return (unsigned char)a[i] - (unsigned char)b[i];
    if(a[i] == 0)
      return 0;
  }
  return 0;
}

char *
strncpy(char *dst, const char *src, size_t n)
{
  size_t i;

  for(i = 0; i < n && src[i]; i++)
    dst[i] = src[i];
  for(; i < n; i++)
    dst[i] = 0;
  return dst;
}

void *
memchr(const void *s, int c, size_t n)
{
  const unsigned char *p;
  size_t i;

  p = s;
  for(i = 0; i < n; i++){
    if(p[i] == (unsigned char)c)
      return (void *)(p + i);
  }
  return 0;
}

size_t
strspn(const char *s, const char *accept)
{
  size_t i;
  size_t j;

  for(i = 0; s[i]; i++){
    for(j = 0; accept[j]; j++){
      if(s[i] == accept[j])
        break;
    }
    if(accept[j] == 0)
      break;
  }
  return i;
}

uintmax_t
strtoumax(const char *nptr, char **endptr, int base)
{
  const char *p;
  uintmax_t v;
  int dig;

  p = nptr;
  v = 0;
  if(base == 0)
    base = 10;
  while(*p == ' ')
    p++;
  if(base != 10){
    if(endptr)
      *endptr = (char *)p;
    return 0;
  }
  while(*p >= '0' && *p <= '9'){
    dig = *p - '0';
    v = v * 10 + (uintmax_t)dig;
    p++;
  }
  if(endptr)
    *endptr = (char *)p;
  return v;
}

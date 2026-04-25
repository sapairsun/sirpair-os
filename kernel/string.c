#include "types.h"
#include "x86.h"

void*
memset(void *dst, int c, uint n)
{
  if ((int)dst%4 == 0 && n%4 == 0){
    c &= 0xFF;
    stosl(dst, (c<<24)|(c<<16)|(c<<8)|c, n/4);
  } else
    stosb(dst, c, n);
  return dst;
}

int
memcmp(const void *v1, const void *v2, uint n)
{
  const uchar *s1, *s2;
  
  s1 = v1;
  s2 = v2;
  while(n-- > 0){
    if(*s1 != *s2)
      return *s1 - *s2;
    s1++, s2++;
  }

  return 0;
}

void*
memmove(void *dst, const void *src, uint n)
{
  const uchar *s;
  uchar *d;
  uint cnt;

  s = (const uchar*)src;
  d = (uchar*)dst;
  if(n == 0 || s == d)
    return dst;

  /*
   * 用 x86 字符串拷贝指令替代字节循环（类似 Linux 的 bulk copy 思路）：
   * - 前向：cld; rep movsb
   * - 反向重叠：std; rep movsb; cld
   * 对帧缓冲滚屏这类大块 memmove，真机吞吐明显更高。
   */
  cnt = n;
  if(s < d && s + n > d){
    s += n - 1;
    d += n - 1;
    asm volatile("std; rep movsb; cld"
                 : "=S" (s), "=D" (d), "=c" (cnt)
                 : "0" (s), "1" (d), "2" (cnt)
                 : "memory", "cc");
  } else {
    asm volatile("cld; rep movsb"
                 : "=S" (s), "=D" (d), "=c" (cnt)
                 : "0" (s), "1" (d), "2" (cnt)
                 : "memory", "cc");
  }

  return dst;
}

// memcpy exists to placate GCC.  Use memmove.
void*
memcpy(void *dst, const void *src, uint n)
{
  return memmove(dst, src, n);
}

int
strncmp(const char *p, const char *q, uint n)
{
  while(n > 0 && *p && *p == *q)
    n--, p++, q++;
  if(n == 0)
    return 0;
  return (uchar)*p - (uchar)*q;
}

char*
strncpy(char *s, const char *t, int n)
{
  char *os;
  
  os = s;
  while(n-- > 0 && (*s++ = *t++) != 0)
    ;
  while(n-- > 0)
    *s++ = 0;
  return os;
}

// Like strncpy but guaranteed to NUL-terminate.
char*
safestrcpy(char *s, const char *t, int n)
{
  char *os;
  
  os = s;
  if(n <= 0)
    return os;
  while(--n > 0 && (*s++ = *t++) != 0)
    ;
  *s = 0;
  return os;
}

int
strlen(const char *s)
{
  int n;

  for(n = 0; s[n]; n++)
    ;
  return n;
}


#include "string.h"

void *
memchr(const void *s, int c, size_t n)
{
  const unsigned char *p = s;
  unsigned char uc = (unsigned char)c;

  while(n > 0){
    if(*p == uc)
      return (void*)p;
    p++;
    n--;
  }
  return 0;
}

int
strcoll(const char *a, const char *b)
{
  return strcmp(a, b);
}

int
strncmp(const char *a, const char *b, size_t n)
{
  unsigned char u1, u2;

  while(n > 0){
    u1 = (unsigned char)*a++;
    u2 = (unsigned char)*b++;
    if(u1 != u2)
      return (int)u1 - (int)u2;
    if(u1 == 0)
      return 0;
    n--;
  }
  return 0;
}

char *
strncpy(char *d, const char *s, size_t n)
{
  char *od = d;

  while(n > 0){
    if((*d++ = *s++) == 0){
      while(--n > 0)
        *d++ = 0;
      break;
    }
    n--;
  }
  return od;
}

char *
strrchr(const char *s, int c)
{
  const char *last = 0;

  for(;;){
    if(*s == (char)c)
      last = s;
    if(*s == 0)
      break;
    s++;
  }
  return (char*)last;
}

char *
strstr(const char *h, const char *n)
{
  size_t i;
  char c0;

  c0 = n[0];
  if(c0 == 0)
    return (char*)h;
  for(; *h; h++){
    if(*h != c0)
      continue;
    for(i = 0; n[i] && h[i] == n[i]; i++)
      ;
    if(n[i] == 0)
      return (char*)h;
  }
  return 0;
}

char *
strpbrk(const char *s, const char *accept)
{
  const char *p;

  for(; *s; s++){
    for(p = accept; *p; p++){
      if(*s == *p)
        return (char*)s;
    }
  }
  return 0;
}

size_t
strspn(const char *s, const char *accept)
{
  const char *p;
  size_t n;

  for(n = 0; *s; s++, n++){
    int ok = 0;
    for(p = accept; *p; p++){
      if(*p == *s){
        ok = 1;
        break;
      }
    }
    if(!ok)
      break;
  }
  return n;
}

size_t
strcspn(const char *s, const char *reject)
{
  const char *t;
  const char *p;

  for(t = s; *t; t++){
    for(p = reject; *p; p++){
      if(*p == *t)
        return (size_t)(t - s);
    }
  }
  return (size_t)(t - s);
}

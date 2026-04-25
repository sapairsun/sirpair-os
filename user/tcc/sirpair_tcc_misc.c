#include "types.h"
#include "user.h"

struct timeval {
  long tv_sec;
  long tv_usec;
};

int
sirpair_open_impl(const char *path, int flags)
{
  return open((char *)path, flags);
}

int
gettimeofday(struct timeval *tv, void *tz)
{
  (void)tz;
  if(tv){
    int u = uptime();
    tv->tv_sec = u;
    tv->tv_usec = 0;
  }
  return 0;
}

double strtod(const char *nptr, char **endptr);

long long
strtoll(const char *nptr, char **endptr, int base)
{
  int neg;
  unsigned long long v;
  const char *p;

  if(!nptr)
    return 0;
  p = nptr;
  while(*p == ' ' || *p == '\t')
    p++;
  neg = 0;
  if(*p == '-'){
    neg = 1;
    p++;
  } else if(*p == '+')
    p++;
  if(base == 0){
    if(*p == '0'){
      p++;
      if(*p == 'x' || *p == 'X'){
        base = 16;
        p++;
      } else
        base = 8;
    } else
      base = 10;
  }
  v = 0;
  while(*p){
    int d = -1;
    if(*p >= '0' && *p <= '9')
      d = *p - '0';
    else if(*p >= 'a' && *p <= 'z')
      d = *p - 'a' + 10;
    else if(*p >= 'A' && *p <= 'Z')
      d = *p - 'A' + 10;
    if(d < 0 || d >= base)
      break;
    v = v * (unsigned long long)base + (unsigned long long)d;
    p++;
  }
  if(endptr)
    *endptr = (char*)p;
  if(neg)
    return (long long)(0ULL - v);
  return (long long)v;
}

long double
strtold(const char *nptr, char **endptr)
{
  double d;

  d = strtod(nptr, endptr);
  return (long double)d;
}

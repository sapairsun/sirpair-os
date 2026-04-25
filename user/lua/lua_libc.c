#include "types.h"
#include "user.h"
#include "errno.h"
#include "stdlib.h"
#include "string.h"
#include "ctype.h"
#include "limits.h"
#include "printf.h"

void
abort(void)
{
  exit(1);
}

int
abs(int j)
{
  return j < 0 ? -j : j;
}

long
labs(long j)
{
  return j < 0 ? -j : j;
}

char *
getenv(const char *name)
{
  (void)name;
  return 0;
}

int
system(const char *cmd)
{
  if(cmd == 0)
    return 0;
  errno = EINVAL;
  return -1;
}

int
remove(const char *path)
{
  if(unlink((char*)path) < 0)
    return -1;
  return 0;
}

int
rename(const char *oldpath, const char *newpath)
{
  if(link((char*)oldpath, (char*)newpath) < 0)
    return -1;
  if(unlink((char*)oldpath) < 0)
    return -1;
  return 0;
}

long
strtol(const char *nptr, char **endptr, int base)
{
  const char *s;
  int neg;
  unsigned long acc, lim;
  int c, cutlim;
  int any;

  s = nptr;
  while(isspace((unsigned char)*s))
    s++;
  neg = 0;
  if(*s == '-'){
    neg = 1;
    s++;
  } else if(*s == '+')
    s++;

  if((base == 0 || base == 16) && *s == '0' && (s[1] == 'x' || s[1] == 'X')){
    base = 16;
    s += 2;
  }
  if(base == 0){
    if(*s == '0')
      base = 8;
    else
      base = 10;
  }

  acc = 0;
  any = 0;
  lim = (unsigned long)LONG_MAX;
  if(neg)
    lim = (unsigned long)(-(LONG_MIN + 1L)) + 1UL;
  cutlim = (int)(lim % (unsigned long)base);
  lim /= (unsigned long)base;

  for(;;){
    c = (unsigned char)*s;
    if(isdigit(c))
      c -= '0';
    else if(isalpha(c))
      c = tolower(c) - 'a' + 10;
    else
      break;
    if(c >= base)
      break;
    if(any < 0)
      ;
    else if(acc > lim || (acc == lim && (unsigned)c > (unsigned)cutlim)){
      any = -1;
      errno = ERANGE;
      if(neg)
        acc = (unsigned long)LONG_MIN;
      else
        acc = (unsigned long)LONG_MAX;
    } else {
      any = 1;
      acc *= (unsigned long)base;
      acc += (unsigned long)c;
    }
    s++;
  }

  if(any == 0){
    if(endptr)
      *endptr = (char*)nptr;
    return 0;
  }
  if(any < 0)
    ;
  else if(neg)
    acc = (unsigned long)-(long)acc;
  if(endptr)
    *endptr = (char*)s;
  return (long)acc;
}

unsigned long
strtoul(const char *nptr, char **endptr, int base)
{
  const char *s;
  int neg;
  unsigned long acc, lim;
  int c, cutlim;
  int any;

  s = nptr;
  while(isspace((unsigned char)*s))
    s++;
  neg = 0;
  if(*s == '-'){
    neg = 1;
    s++;
  } else if(*s == '+')
    s++;

  if((base == 0 || base == 16) && *s == '0' && (s[1] == 'x' || s[1] == 'X')){
    base = 16;
    s += 2;
  }
  if(base == 0){
    if(*s == '0')
      base = 8;
    else
      base = 10;
  }

  acc = 0;
  any = 0;
  lim = ULONG_MAX / (unsigned long)base;
  cutlim = (int)(ULONG_MAX % (unsigned long)base);

  for(;;){
    c = (unsigned char)*s;
    if(isdigit(c))
      c -= '0';
    else if(isalpha(c))
      c = tolower(c) - 'a' + 10;
    else
      break;
    if(c >= base)
      break;
    if(any < 0)
      ;
    else if(acc > lim || (acc == lim && (unsigned)c > (unsigned)cutlim)){
      any = -1;
      errno = ERANGE;
      acc = ULONG_MAX;
    } else {
      any = 1;
      acc *= (unsigned long)base;
      acc += (unsigned long)c;
    }
    s++;
  }

  if(any == 0){
    if(endptr)
      *endptr = (char*)nptr;
    return 0;
  }
  if(any < 0){
    if(endptr)
      *endptr = (char*)s;
    return ULONG_MAX;
  }
  if(neg)
    acc = 0UL - acc;
  if(endptr)
    *endptr = (char*)s;
  return acc;
}

static double
pow10i(int e)
{
  double x;
  int i;

  x = 1.0;
  if(e >= 0){
    for(i = 0; i < e && i < 400; i++)
      x *= 10.0;
  } else {
    for(i = 0; i < -e && i < 400; i++)
      x /= 10.0;
  }
  return x;
}

double
strtod(const char *nptr, char **endptr)
{
  const char *s;
  int neg;
  double val;
  int saw;
  const char *aftere;

  s = nptr;
  while(isspace((unsigned char)*s))
    s++;
  neg = 0;
  if(*s == '-'){
    neg = 1;
    s++;
  } else if(*s == '+')
    s++;

  val = 0.0;
  saw = 0;
  while(isdigit((unsigned char)*s)){
    val = val * 10.0 + (double)(*s - '0');
    s++;
    saw = 1;
  }
  if(*s == '.'){
    s++;
    {
      double frac = 1.0;
      while(isdigit((unsigned char)*s)){
        frac *= 0.1;
        val += (double)(*s - '0') * frac;
        s++;
        saw = 1;
      }
    }
  }

  if((*s == 'e' || *s == 'E') && saw){
    int esign = 1;
    int e = 0;

    aftere = s;
    s++;
    if(*s == '-'){
      esign = -1;
      s++;
    } else if(*s == '+')
      s++;
    if(!isdigit((unsigned char)*s))
      s = aftere;
    else {
      while(isdigit((unsigned char)*s)){
        int d = *s - '0';
        if(e < 1000000)
          e = e * 10 + d;
        s++;
      }
      e *= esign;
      val *= pow10i(e);
    }
  }

  if(!saw){
    if(endptr)
      *endptr = (char*)nptr;
    return 0.0;
  }
  if(endptr)
    *endptr = (char*)s;
  return neg ? -val : val;
}

float
strtof(const char *nptr, char **endptr)
{
  return (float)strtod(nptr, endptr);
}

long
atol(const char *s)
{
  return strtol(s, 0, 10);
}

static void
swapb(unsigned char *a, unsigned char *b, size_t size)
{
  size_t i;
  unsigned char t;

  for(i = 0; i < size; i++){
    t = a[i];
    a[i] = b[i];
    b[i] = t;
  }
}

void
qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *))
{
  unsigned char *b;
  size_t i;

  b = base;
  if(nmemb < 2 || size == 0)
    return;
  for(i = 1; i < nmemb; i++){
    size_t j = i;
    while(j > 0 && compar(b + (j - 1) * size, b + j * size) > 0){
      swapb(b + (j - 1) * size, b + j * size, size);
      j--;
    }
  }
}

char *
strerror(int errnum)
{
  static char buf[48];

  snprintf_(buf, sizeof buf, "errno %d", errnum);
  return buf;
}

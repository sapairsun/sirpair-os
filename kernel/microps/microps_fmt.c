#include "types.h"
#include "defs.h"
#include "microps_kernel.h"
#include <stdarg.h>

static char *
fmt_pad_x(char *s, char *end, unsigned val, int width, int upper)
{
  char dig[] = "0123456789abcdef";
  int i;
  char c;

  if(width <= 0)
    width = 1;
  if(width > 8)
    width = 8;
  for(i = width - 1; i >= 0; i--){
    c = dig[(val >> (i * 4)) & 0xf];
    if(upper && c >= 'a')
      c -= 'a' - 'A';
    if(s < end - 1)
      *s++ = c;
    else
      break;
  }
  return s;
}

static char *
fmt_uint10(char *s, char *end, unsigned val)
{
  char tmp[16];
  int i;

  i = 0;
  do {
    tmp[i++] = '0' + (val % 10u);
    val /= 10u;
  } while(val && i < (int)sizeof(tmp));
  while(i > 0){
    if(s < end - 1)
      *s++ = tmp[--i];
    else
      break;
  }
  return s;
}

static char *
fmt_int10(char *s, char *end, int val)
{
  unsigned u;

  if(val < 0){
    if(s < end - 1)
      *s++ = '-';
    u = (unsigned)(-(long)val);
  } else
    u = (unsigned)val;
  return fmt_uint10(s, end, u);
}

static char *
fmt_long10(char *s, char *end, long val)
{
  unsigned long u;

  if(val < 0){
    if(s < end - 1)
      *s++ = '-';
    u = (unsigned long)(-val);
  } else
    u = (unsigned long)val;
  return fmt_uint10(s, end, (unsigned)u);
}

int
vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
  const char *f;
  char *s, *end;

  if(!buf || size == 0)
    return 0;
  s = buf;
  end = buf + size;
  for(f = fmt; *f && s < end - 1; f++){
    int width;
    int longmod;
    int sizemod;

    if(*f != '%'){
      *s++ = *f;
      continue;
    }
    f++;
    width = 0;
    while(*f >= '0' && *f <= '9'){
      width = width * 10 + (*f - '0');
      f++;
    }
    longmod = 0;
    sizemod = 0;
    if(*f == 'l'){
      longmod = 1;
      f++;
    } else if(*f == 'z'){
      sizemod = 1;
      f++;
    }
    switch(*f){
    case '\0':
      goto done;
    case 's': {
      const char *str = va_arg(ap, const char *);

      if(!str)
        str = "(null)";
      while(*str && s < end - 1)
        *s++ = *str++;
      break;
    }
    case 'd':
    case 'i':
      if(longmod)
        s = fmt_long10(s, end, va_arg(ap, long));
      else if(sizemod)
        s = fmt_int10(s, end, (int)va_arg(ap, ssize_t));
      else
        s = fmt_int10(s, end, va_arg(ap, int));
      break;
    case 'u':
      if(longmod)
        s = fmt_uint10(s, end, (unsigned)va_arg(ap, unsigned long));
      else if(sizemod)
        s = fmt_uint10(s, end, (unsigned)va_arg(ap, size_t));
      else
        s = fmt_uint10(s, end, va_arg(ap, unsigned));
      break;
    case 'x':
    case 'X': {
      unsigned xv = va_arg(ap, unsigned);
      int w;

      w = width;
      if(w == 0){
        int nz;

        for(nz = 7; nz > 0; nz--)
          if((xv >> (nz * 4)) & 0xf)
            break;
        w = nz + 1;
      }
      s = fmt_pad_x(s, end, xv, w, *f == 'X');
      break;
    }
    case 'c':
      if(s < end - 1)
        *s++ = (char)va_arg(ap, int);
      break;
    case 'p': {
      void *pv = va_arg(ap, void *);

      if(s < end - 2){
        *s++ = '0';
        *s++ = 'x';
      }
      s = fmt_pad_x(s, end, (unsigned)(unsigned long)pv, 8, 0);
      break;
    }
    case '%':
      if(s < end - 1)
        *s++ = '%';
      break;
    default:
      if(s < end - 1)
        *s++ = *f;
      break;
    }
  }
done:
  *s = '\0';
  return (int)(s - buf);
}

int
snprintf(char *buf, size_t size, const char *fmt, ...)
{
  va_list ap;
  int n;

  va_start(ap, fmt);
  n = vsnprintf(buf, size, fmt, ap);
  va_end(ap);
  return n;
}

long
strtol(const char *nptr, char **endptr, int base)
{
  long v;
  int neg;
  const char *p;

  if(!nptr){
    if(endptr)
      *endptr = (char *)nptr;
    return 0;
  }
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
    base = 10;
    if(*p == '0'){
      p++;
      if(*p == 'x' || *p == 'X'){
        p++;
        base = 16;
      } else
        base = 8;
    }
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
    v = v * base + d;
    p++;
  }
  if(endptr)
    *endptr = (char *)p;
  return neg ? -v : v;
}

char *
strrchr(const char *s, int c)
{
  const char *last;

  last = 0;
  if(!s)
    return 0;
  while(*s){
    if(*s == (char)c)
      last = s;
    s++;
  }
  if(c == '\0')
    return (char *)s;
  return (char *)last;
}

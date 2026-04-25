#include "types.h"
#include "user.h"
#include <stdarg.h>
#include "mp_printf.h"

int
vsnprintf(char *s, size_t n, const char *fmt, va_list ap)
{
  return vsnprintf_(s, n, fmt, ap);
}

int
snprintf(char *s, size_t n, const char *fmt, ...)
{
  va_list ap;
  int r;

  va_start(ap, fmt);
  r = vsnprintf_(s, n, fmt, ap);
  va_end(ap);
  return r;
}

void
bs_printf(int fd, const char *fmt, ...)
{
  char b[1024];
  va_list ap;
  int n;

  va_start(ap, fmt);
  n = vsnprintf_(b, sizeof b, fmt, ap);
  va_end(ap);
  if(n > (int)sizeof b)
    n = sizeof b;
  if(n > 0)
    write(fd, b, n);
}

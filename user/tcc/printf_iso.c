/*
 * TinyCC 与 ulib 的 printf(int fd, ...) 同名冲突：本文件仅用于链接 _tcc，
 * 提供 ISO 语义的 printf，供 libtcc 中 TCCSYM(printf) 取址。
 */
#include "types.h"
#include "stdio.h"
#include <stdarg.h>

extern void sirpair_stdio_init(void);

int
printf(const char *fmt, ...)
{
  static int init;
  va_list ap;
  int r;

  if(!init){
    sirpair_stdio_init();
    init = 1;
  }
  va_start(ap, fmt);
  r = vfprintf(stdout, fmt, ap);
  va_end(ap);
  return r;
}

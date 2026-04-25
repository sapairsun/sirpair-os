#ifndef BEANSTALKD_SIRPAIR_SHIM_H
#define BEANSTALKD_SIRPAIR_SHIM_H

#include "types.h"
#include "user.h"
#include <stdarg.h>
#include <stddef.h>

int vsnprintf_(char *buffer, size_t count, const char *format, va_list va);

int vsnprintf(char *s, size_t n, const char *fmt, va_list ap);
int snprintf(char *s, size_t n, const char *fmt, ...);
void bs_printf(int fd, const char *fmt, ...);

#define sprintf(buf, fmt, ...) snprintf(buf, 4096, fmt, __VA_ARGS__)

#endif

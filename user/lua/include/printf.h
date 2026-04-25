#ifndef SIRPAIR_PRINTF_H
#define SIRPAIR_PRINTF_H

#include <stdarg.h>
#include <stddef.h>

void _putchar(char c);

int snprintf_(char *buf, size_t n, const char *fmt, ...);
int vsnprintf_(char *buf, size_t n, const char *fmt, va_list ap);
int sprintf_(char *buf, const char *fmt, ...);

#endif

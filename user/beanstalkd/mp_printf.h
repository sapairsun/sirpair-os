/* mpaland printf：仅声明 vsnprintf_，避免与 Sirpair printf 宏冲突 */
#ifndef MP_PRINTF_WRAP_H
#define MP_PRINTF_WRAP_H

#include <stdarg.h>
#include <stddef.h>

int vsnprintf_(char *buffer, size_t count, const char *format, va_list va);

#endif

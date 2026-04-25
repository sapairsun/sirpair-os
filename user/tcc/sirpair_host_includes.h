/*
 * Sirpair 用户态下编译 TCC 时的宿主 C 库入口（复用 user/lua 下的头与实现）。
 */
#ifndef SIRPAIR_HOST_INCLUDES_H
#define SIRPAIR_HOST_INCLUDES_H

#define CONFIG_TCC_STATIC 1

#include "types.h"
#include "stat.h"
#include "fcntl.h"

#ifndef O_BINARY
#define O_BINARY 0
#endif
#ifndef O_CREAT
#define O_CREAT O_CREATE
#endif

#include <stddef.h>
#include <stdarg.h>

#include <limits.h>
#include <float.h>
#include <errno.h>

#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <signal.h>
#include <setjmp.h>
#include <time.h>
#include <math.h>
#include <stdio.h>

int printf(const char *fmt, ...);

#include "user.h"

int sirpair_open_impl(const char *path, int flags);
#define open(path, flags, ...) sirpair_open_impl((const char *)(path), (flags))

struct timeval {
  long tv_sec;
  long tv_usec;
};

int gettimeofday(struct timeval *tv, void *tz);

long long strtoll(const char *nptr, char **endptr, int base);
long double strtold(const char *nptr, char **endptr);

#endif

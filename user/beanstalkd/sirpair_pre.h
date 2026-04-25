/* 由 Makefile 对 beanstalkd 各模块 -include，在 dat.h 之前注入 */
#ifndef SIRPAIR_BEANSTALKD_PRE_H
#define SIRPAIR_BEANSTALKD_PRE_H

#include "types.h"
#include <stdint.h>
#ifndef PRIu64
#define PRIu64 "llu"
#endif
#ifndef PRId64
#define PRId64 "lld"
#endif
#include "stat.h"
#include "fcntl.h"
#include "user.h"

#ifndef O_CREAT
#define O_CREAT O_CREATE
#endif

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

extern int errno;
char *strerror(int);
void *calloc(size_t, size_t);
unsigned long strtoul(const char *nptr, char **endptr, int base);

#ifndef EAGAIN
#define EAGAIN 11
#endif
#ifndef EWOULDBLOCK
#define EWOULDBLOCK EAGAIN
#endif
#ifndef EINTR
#define EINTR 4
#endif

struct iovec {
  void *iov_base;
  size_t iov_len;
};

/* prot.c 等省略 <string.h>/<inttypes.h>，符号见 libc_compat.c */
void *memchr(const void *s, int c, size_t n);
int strncmp(const char *s1, const char *s2, size_t n);
char *strncpy(char *dst, const char *src, size_t n);
size_t strspn(const char *s, const char *accept);
uintmax_t strtoumax(const char *nptr, char **endptr, int base);

#endif

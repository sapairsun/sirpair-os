#ifndef SIRPAIR_ERRNO_H
#define SIRPAIR_ERRNO_H

extern int errno;

#define EINTR 4

/* 与用户态 / Lua 桩一致（勿与 include 中其它头冲突） */
#define ENOENT 2
#define EEXIST 17
#define EINVAL 22
#define ERANGE 34
#define ENOMEM 12
#define EDOM 33
#define ETIMEDOUT 110

#endif

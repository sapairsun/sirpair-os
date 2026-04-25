/*
 * Sirpair 内核中编译 microps 时的公共桩与宏（无 libc stdio）。
 */
#ifndef MICROPS_KERNEL_H
#define MICROPS_KERNEL_H

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;

#define UINT16_MAX 0xffff
#define UINT32_MAX 0xffffffffu

typedef unsigned int size_t;
typedef int ssize_t;
typedef long ptrdiff_t;

#ifndef NULL
#define NULL ((void *)0)
#endif

#define flockfile(x) ((void)(x))
#define funlockfile(x) ((void)(x))
#define stderr ((void *)0)
#define fprintf(...) ((void)0)
#define vfprintf(...) ((void)0)

#ifndef isascii
#define isascii(c) ((unsigned)(c) < 128u)
#endif
#ifndef isprint
#define isprint(c) ((c) >= 0x20 && (c) <= 0x7e)
#endif

struct timeval {
  long tv_sec;
  long tv_usec;
};

#define timersub(a, b, res)                                                  \
  do {                                                                       \
    (res)->tv_sec = (a)->tv_sec - (b)->tv_sec;                               \
    (res)->tv_usec = (a)->tv_usec - (b)->tv_usec;                            \
    if((res)->tv_usec < 0) {                                                 \
      (res)->tv_sec--;                                                       \
      (res)->tv_usec += 1000000;                                             \
    }                                                                        \
  } while(0)

#define timerclear(tvp)                                                      \
  do {                                                                       \
    (tvp)->tv_sec = (tvp)->tv_usec = 0;                                      \
  } while(0)

#define timeval_add_usec(x, y)                                               \
  do {                                                                       \
    (x)->tv_sec += (long)((y) / 1000000);                                    \
    (x)->tv_usec += (long)((y) % 1000000);                                   \
    if((x)->tv_usec >= 1000000) {                                            \
      (x)->tv_sec += 1;                                                      \
      (x)->tv_usec -= 1000000;                                               \
    }                                                                        \
  } while(0)

#define timercmp(a, b, op)                                                   \
  (((a)->tv_sec == (b)->tv_sec) ? ((a)->tv_usec op (b)->tv_usec)             \
                                : ((a)->tv_sec op (b)->tv_sec))

#endif

#ifndef MICROPS_UTIL_H
#define MICROPS_UTIL_H

#include <stdarg.h>
#include "microps_kernel.h"

#ifndef MAX
#define MAX(x, y) ((x) > (y) ? (x) : (y))
#endif
#ifndef MIN
#define MIN(x, y) ((x) < (y) ? (x) : (y))
#endif

#define countof(x) ((sizeof(x) / sizeof(*(x))))
#define tailof(x) ((x) + countof(x))
#define indexof(x, y) (((uintptr_t)(y) - (uintptr_t)(x)) / sizeof(*(y)))

#define errorf(...) lprintf(0, 'E', __FILE__, __LINE__, __func__, __VA_ARGS__)
#define warnf(...) lprintf(0, 'W', __FILE__, __LINE__, __func__, __VA_ARGS__)
#define infof(...) lprintf(0, 'I', __FILE__, __LINE__, __func__, __VA_ARGS__)
#define debugf(...) lprintf(0, 'D', __FILE__, __LINE__, __func__, __VA_ARGS__)

#ifdef HEXDUMP
#define debugdump(...) hexdump(0, __VA_ARGS__)
#else
#define debugdump(...)
#endif

extern int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
extern int snprintf(char *buf, size_t size, const char *fmt, ...);
extern long strtol(const char *nptr, char **endptr, int base);
extern char *strrchr(const char *s, int c);

int lprintf(void *fp, int level, const char *file, int line, const char *func,
            const char *fmt, ...);
void hexdump(void *fp, const void *data, size_t size);

struct queue_entry;

struct queue_head {
  struct queue_entry *head;
  struct queue_entry *tail;
  unsigned int num;
};

void queue_init(struct queue_head *queue);
void *queue_push(struct queue_head *queue, void *data);
void *queue_pop(struct queue_head *queue);
void *queue_peek(struct queue_head *queue);
void queue_foreach(struct queue_head *queue, void (*func)(void *arg, void *data),
                   void *arg);

uint16_t hton16(uint16_t h);
uint16_t ntoh16(uint16_t n);
uint32_t hton32(uint32_t h);
uint32_t ntoh32(uint32_t n);
uint16_t cksum16(uint16_t *addr, uint16_t count, uint32_t init);

#ifndef uintptr_t
typedef unsigned int uintptr_t;
#endif

#endif

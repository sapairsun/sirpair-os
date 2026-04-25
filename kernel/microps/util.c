#include "types.h"
#include "defs.h"
#include "microps_kernel.h"
#include <stdarg.h>

#include "platform.h"
#include "util.h"

int
lprintf(void *fp, int level, const char *file, int line, const char *func,
        const char *fmt, ...)
{
  char buf[512];
  va_list ap;

  (void)fp;
  /*
   * 关闭高频调试级别输出，避免在网络热点路径上深调用链叠加导致内核栈压力过大。
   * 仍保留 I/W/E 级别，保证可观测性。
   */
  if(level == 'D')
    return 0;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  cprintf("[mps %c] %s: %s (%s:%d)\n", level, func, buf, file, line);
  return 0;
}

void
hexdump(void *fp, const void *data, size_t size)
{
  (void)fp;
  (void)data;
  (void)size;
}

struct queue_entry {
  struct queue_entry *next;
  void *data;
};

void
queue_init(struct queue_head *queue)
{
  queue->head = 0;
  queue->tail = 0;
  queue->num = 0;
}

void *
queue_push(struct queue_head *queue, void *data)
{
  struct queue_entry *entry;

  if(!queue)
    return 0;
  entry = memory_alloc(sizeof(*entry));
  if(!entry)
    return 0;
  entry->next = 0;
  entry->data = data;
  if(queue->tail)
    queue->tail->next = entry;
  queue->tail = entry;
  if(!queue->head)
    queue->head = entry;
  queue->num++;
  return data;
}

void *
queue_pop(struct queue_head *queue)
{
  struct queue_entry *entry;
  void *data;

  if(!queue || !queue->head)
    return 0;
  entry = queue->head;
  queue->head = entry->next;
  if(!queue->head)
    queue->tail = 0;
  queue->num--;
  data = entry->data;
  memory_free(entry);
  return data;
}

void *
queue_peek(struct queue_head *queue)
{
  if(!queue || !queue->head)
    return 0;
  return queue->head->data;
}

void
queue_foreach(struct queue_head *queue, void (*func)(void *arg, void *data),
              void *arg)
{
  struct queue_entry *entry;

  if(!queue || !func)
    return;
  for(entry = queue->head; entry; entry = entry->next)
    func(arg, entry->data);
}

#ifndef __BIG_ENDIAN
#define __BIG_ENDIAN 4321
#endif
#ifndef __LITTLE_ENDIAN
#define __LITTLE_ENDIAN 1234
#endif

static int endian;

static int
byteorder(void)
{
  uint32_t x = 0x00000001;

  return *(uint8_t *)&x ? __LITTLE_ENDIAN : __BIG_ENDIAN;
}

static uint16_t
byteswap16(uint16_t v)
{
  return (uint16_t)((v & 0x00ff) << 8 | (v & 0xff00) >> 8);
}

static uint32_t
byteswap32(uint32_t v)
{
  return (uint32_t)((v & 0x000000ffU) << 24 | (v & 0x0000ff00U) << 8 |
                    (v & 0x00ff0000U) >> 8 | (v & 0xff000000U) >> 24);
}

uint16_t
hton16(uint16_t h)
{
  if(!endian)
    endian = byteorder();
  return endian == __LITTLE_ENDIAN ? byteswap16(h) : h;
}

uint16_t
ntoh16(uint16_t n)
{
  if(!endian)
    endian = byteorder();
  return endian == __LITTLE_ENDIAN ? byteswap16(n) : n;
}

uint32_t
hton32(uint32_t h)
{
  if(!endian)
    endian = byteorder();
  return endian == __LITTLE_ENDIAN ? byteswap32(h) : h;
}

uint32_t
ntoh32(uint32_t n)
{
  if(!endian)
    endian = byteorder();
  return endian == __LITTLE_ENDIAN ? byteswap32(n) : n;
}

uint16_t
cksum16(uint16_t *addr, uint16_t count, uint32_t init)
{
  uint32_t sum;

  sum = init;
  while(count > 1){
    sum += *(addr++);
    count -= 2;
  }
  if(count > 0)
    sum += *(uint8_t *)addr;
  while(sum >> 16)
    sum = (sum & 0xffff) + (sum >> 16);
  return ~(uint16_t)sum;
}

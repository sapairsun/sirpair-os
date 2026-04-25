/*
 * 兼容端口指针设备（第二端口 / 常见为 IRQ12）。
 * 外接纯 USB 鼠标需 USB HID，当前内核未实现；真机触控板与小红点多为 PS/2。
 */

#include "types.h"
#include "x86.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "mouse_if.h"

#define PS2_DATA 0x60
#define PS2_CMD  0x64

#define MOUSE_RING 64

static struct spinlock mouselock;
static int mouse_lock_inited;
static struct mouse_event ring[MOUSE_RING];
static uint ring_r;
static uint ring_w;
static int mouse_ready;

static void
wait_ib_empty(void)
{
  int i;
  for(i = 0; i < 100000; i++){
    if((inb(PS2_CMD) & 2) == 0)
      return;
  }
}

static void
mouse_write_aux(uchar v)
{
  wait_ib_empty();
  outb(PS2_CMD, 0xD4);
  wait_ib_empty();
  outb(PS2_DATA, v);
}

static int
mouse_read_ack(int ms)
{
  int t;
  uchar st;
  for(t = 0; t < ms * 1000; t++){
    st = inb(PS2_CMD);
    if(st & 1){
      uchar d = inb(PS2_DATA);
      if(d == 0xFA)
        return 0;
    }
    microdelay(1);
  }
  return -1;
}

static void
ring_push(int dx, int dy, int btn)
{
  uint nw;
  if(!mouse_ready)
    return;
  acquire(&mouselock);
  nw = (ring_w + 1) % MOUSE_RING;
  if(nw == ring_r){
    ring_r = (ring_r + 1) % MOUSE_RING;
  }
  ring[ring_w].dx = dx;
  ring[ring_w].dy = dy;
  ring[ring_w].buttons = btn;
  ring_w = nw;
  release(&mouselock);
}

void
mouse_byte(uchar b)
{
  static uchar pkt[3];
  static int idx;
  int dx, dy, btn;

  if(idx == 0){
    if((b & 0x8) == 0)
      return;
  }
  pkt[idx++] = b;
  if(idx < 3)
    return;
  idx = 0;
  btn = pkt[0] & 7;
  dx = (char)pkt[1];
  dy = (char)pkt[2];
  dy = -dy;
  ring_push(dx, dy, btn);
}

void
mouse_intr(void)
{
  /*
   * IRQ12 仅对应第二 PS/2 端口（触控板/小红点）。部分机型（如 Ivy Bridge 代 ThinkPad）
   * 的 8042 在读状态寄存器时未必置位 0x20（辅助口标志），若据此丢弃字节会导致
   * X230i 等真机完全收不到移动/按键；故在专用中断里直接消费输出缓冲中的数据。
   */
  while(inb(PS2_CMD) & 1){
    uchar d = inb(PS2_DATA);

    mouse_byte(d);
  }
}

int
mouse_hw_init(void)
{
  uchar cfg;
  int i;

  if(mouse_ready)
    return 0;

  if(!mouse_lock_inited){
    initlock(&mouselock, "mouse");
    mouse_lock_inited = 1;
  }
  ring_r = ring_w = 0;

  wait_ib_empty();
  outb(PS2_CMD, 0xA8);

  while(inb(PS2_CMD) & 1)
    (void)inb(PS2_DATA);

  wait_ib_empty();
  outb(PS2_CMD, 0x20);
  for(i = 0; i < 100000; i++){
    if(inb(PS2_CMD) & 1)
      break;
  }
  if((inb(PS2_CMD) & 1) == 0)
    return -1;
  cfg = inb(PS2_DATA);
  cfg |= 0x02;
  cfg &= ~0x20;

  wait_ib_empty();
  outb(PS2_CMD, 0x60);
  wait_ib_empty();
  outb(PS2_DATA, cfg);

  mouse_write_aux(0xFF);
  if(mouse_read_ack(250) < 0)
    return -1;
  microdelay(100000);

  while(inb(PS2_CMD) & 1)
    (void)inb(PS2_DATA);

  mouse_write_aux(0xF6);
  if(mouse_read_ack(250) < 0)
    return -1;
  while(inb(PS2_CMD) & 1)
    (void)inb(PS2_DATA);

  mouse_write_aux(0xF4);
  if(mouse_read_ack(250) < 0)
    return -1;

  picenable(12);
  if(ismp)
    ioapicenable(12, mpbcpu());

  mouse_ready = 1;
  return 0;
}

int
mouse_pop(struct mouse_event *ev)
{
  if(!mouse_lock_inited)
    return 0;
  acquire(&mouselock);
  if(ring_r == ring_w){
    release(&mouselock);
    return 0;
  }
  *ev = ring[ring_r];
  ring_r = (ring_r + 1) % MOUSE_RING;
  release(&mouselock);
  return 1;
}

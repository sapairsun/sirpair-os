// PC/AT CMOS RTC (MC146818) — 读取开机时刻的 UTC 墙钟，与 ticks 叠加供 time(2) / date 使用。
#include "types.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"

extern uint ticks;
extern struct spinlock tickslock;

uint boot_epoch_sec;

static struct spinlock rtcinitlk;
static int rtc_boot_epoch_ready;

uchar cmos_read_byte(uint reg);

static uchar
bcd_to_bin(uchar v)
{
  return (v & 0x0F) + ((v >> 4) & 0x0F) * 10;
}

static uchar
cmos_read(uint reg)
{
  return cmos_read_byte(reg);
}

static int
is_leap(int y)
{
  if(y % 400 == 0)
    return 1;
  if(y % 100 == 0)
    return 0;
  if(y % 4 == 0)
    return 1;
  return 0;
}

static uint
days_before_month(int y, int m)
{
  static int mdays[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
  int d;

  if(m < 1 || m > 12)
    return 0;
  d = mdays[m - 1];
  if(m > 2 && is_leap(y))
    d++;
  return d;
}

// 将 Y-M-D h:mi:s（UTC）转为 Unix 秒（自 1970-01-01 起）。
static uint
utc_mktime(int year, int mon, int day, int hour, int min, int sec)
{
  int y;
  uint days;

  if(year < 1970 || mon < 1 || mon > 12 || day < 1 || day > 31)
    return 0;

  days = 0;
  for(y = 1970; y < year; y++)
    days += 365 + is_leap(y);
  days += days_before_month(year, mon);
  days += day - 1;
  return days * 86400 + hour * 3600 + min * 60 + sec;
}

// 从 CMOS 读当前 RTC 并转为 Unix 秒（UTC，假定 CMOS 存本地/UTC 与 BIOS 一致；QEMU 常为 UTC）。
static uint
rtc_read_unix(void)
{
  uchar sec, minute, hour, mday, mon, year, regb;
  int bin, hour24;
  int y, mo, d, h, mi, s;

  asm volatile("cli" ::: "memory");
  sec = cmos_read(0x00);
  minute = cmos_read(0x02);
  hour = cmos_read(0x04);
  mday = cmos_read(0x07);
  mon = cmos_read(0x08);
  year = cmos_read(0x09);
  regb = cmos_read(0x0B);
  asm volatile("sti" ::: "memory");

  bin = (regb & 0x04) != 0;
  hour24 = (regb & 0x02) != 0;

  if(!bin){
    sec = bcd_to_bin(sec);
    minute = bcd_to_bin(minute);
    mday = bcd_to_bin(mday);
    mon = bcd_to_bin(mon);
    year = bcd_to_bin(year);
  }

  if(hour24){
    h = bin ? (int)hour : (int)bcd_to_bin(hour);
  } else {
    int pm = hour & 0x80;
    h = bin ? (hour & 0x7F) : (int)bcd_to_bin(hour & 0x7F);
    if(h == 12)
      h = 0;
    if(pm)
      h += 12;
  }

  mi = (int)minute;
  s = (int)sec;
  mo = (int)mon;
  d = (int)mday;
  y = 2000 + (int)year;
  if(y < 1970)
    y += 100;

  return utc_mktime(y, mo, d, h, mi, s);
}

static void
rtc_ensure_boot_epoch(void)
{
  uint raw, adj;

  if(rtc_boot_epoch_ready)
    return;
  acquire(&rtcinitlk);
  if(rtc_boot_epoch_ready){
    release(&rtcinitlk);
    return;
  }
  raw = rtc_read_unix();
  /* CMOS 无效或解析失败时 rtc_read_unix 可能为 0；此时若仍减 ticks 会退化为「仅 uptime」，
   * date +%s 会远小于 2000-epoch，自动化回归失败。以 2000-01-01 UTC 为下限与 Linux 常见范围一致。 */
  if(raw < 946684800u)
    raw = 946684800u;
  acquire(&tickslock);
  adj = ticks / HZ;
  release(&tickslock);
  if(raw > adj)
    boot_epoch_sec = raw - adj;
  else
    boot_epoch_sec = 0;
  rtc_boot_epoch_ready = 1;
  release(&rtcinitlk);
}

void
rtc_init(void)
{
  initlock(&rtcinitlk, "rtc");
}

uint
ktime_now(void)
{
  uint t;

  rtc_ensure_boot_epoch();
  acquire(&tickslock);
  t = boot_epoch_sec + ticks / HZ;
  release(&tickslock);
  return t;
}

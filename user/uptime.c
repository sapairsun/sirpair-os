#include "types.h"
#include "user.h"
#include "param.h"

// Print two decimal digits for 0..99 (hours/minutes/seconds/centiseconds)
static void
print02(int fd, int val)
{
  if(val < 10)
    printf(fd, "0%d", val);
  else
    printf(fd, "%d", val);
}

int
main(int argc, char *argv[])
{
  int ticks, secs, mins, hrs;
  int frac;

  /*
   * 单次 exec 内测两次 ticks 间隔（约 2s @100Hz），供自动化回归使用。
   * 在 QEMU 下首次运行用户程序后偶发 EHCI 复位，若再 exec 第二次进程，可能无法从 USB 盘加载；
   * 故避免「uptime; sleep; uptime」两次 exec。
   */
  if(argc >= 2 && strcmp(argv[1], "--drift") == 0){
    int t0, t1;

    t0 = uptime();
    while(uptime() - t0 < 200)
      ;
    t1 = uptime();
    printf(1, "uptime-drift: ticks %d | ticks %d | delta %d\n", t0, t1, t1 - t0);
    exit(0);
  }

  ticks = uptime();
  secs = ticks / HZ;
  frac = ticks % HZ;
  hrs = secs / 3600;
  mins = (secs % 3600) / 60;
  secs = secs % 60;

  printf(1, "up ");
  print02(1, hrs);
  printf(1, ":");
  print02(1, mins);
  printf(1, ":");
  print02(1, secs);
  printf(1, ".");
  print02(1, frac);
  printf(1, " | ticks %d\n", ticks);
  exit(0);
}

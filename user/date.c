#include "types.h"
#include "user.h"

struct tm {
  int tm_sec;
  int tm_min;
  int tm_hour;
  int tm_mday;
  int tm_mon;
  int tm_year;
  int tm_wday;
};

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

static int
dimy(int y, int m)
{
  static int mdays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  int d;

  if(m < 1 || m > 12)
    return 31;
  d = mdays[m - 1];
  if(m == 2 && is_leap(y))
    d++;
  return d;
}

static void
unix_to_tm(int t, struct tm *tm)
{
  int days = t / 86400;
  int sod = t % 86400;
  int y = 1970;
  int mon;

  tm->tm_sec = sod % 60;
  tm->tm_min = (sod / 60) % 60;
  tm->tm_hour = sod / 3600;

  for(;;){
    int d = 365 + is_leap(y);
    if(days < d)
      break;
    days -= d;
    y++;
  }
  tm->tm_year = y;
  mon = 1;
  for(;;){
    int dim = dimy(y, mon);
    if(days < dim)
      break;
    days -= dim;
    mon++;
  }
  tm->tm_mon = mon - 1;
  tm->tm_mday = days + 1;
  tm->tm_wday = ((t / 86400) + 4) % 7;
}

static void
print02(int fd, int v)
{
  if(v < 10)
    printf(fd, "0%d", v);
  else
    printf(fd, "%d", v);
}

static void
print_default(int fd, struct tm *tm)
{
  static char *wd[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  static char *mn[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                         "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

  printf(fd, "%s %s ", wd[tm->tm_wday], mn[tm->tm_mon]);
  if(tm->tm_mday < 10)
    printf(fd, " ");
  printf(fd, "%d ", tm->tm_mday);
  print02(fd, tm->tm_hour);
  printf(fd, ":");
  print02(fd, tm->tm_min);
  printf(fd, ":");
  print02(fd, tm->tm_sec);
  printf(fd, " UTC %04d\n", tm->tm_year);
}

static void
strftime_fmt(int fd, char *fmt, struct tm *tm, int t)
{
  static char *wd[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  static char *wdf[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
  static char *mn[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  static char *mnf[] = {"January", "February", "March", "April", "May", "June",
                        "July", "August", "September", "October", "November", "December"};
  int i;
  int hour12;
  int pm;

  for(i = 0; fmt[i]; i++){
    if(fmt[i] != '%'){
      printf(fd, "%c", fmt[i]);
      continue;
    }
    i++;
    if(fmt[i] == 0){
      printf(fd, "%%");
      break;
    }
    switch(fmt[i]){
    case '%':
      printf(fd, "%%");
      break;
    case 'a':
      printf(fd, "%s", wd[tm->tm_wday]);
      break;
    case 'A':
      printf(fd, "%s", wdf[tm->tm_wday]);
      break;
    case 'b':
      printf(fd, "%s", mn[tm->tm_mon]);
      break;
    case 'B':
      printf(fd, "%s", mnf[tm->tm_mon]);
      break;
    case 'd':
      print02(fd, tm->tm_mday);
      break;
    case 'e':
      if(tm->tm_mday < 10)
        printf(fd, " ");
      printf(fd, "%d", tm->tm_mday);
      break;
    case 'H':
      print02(fd, tm->tm_hour);
      break;
    case 'I':
      hour12 = tm->tm_hour % 12;
      if(hour12 == 0)
        hour12 = 12;
      print02(fd, hour12);
      break;
    case 'm':
      print02(fd, tm->tm_mon + 1);
      break;
    case 'M':
      print02(fd, tm->tm_min);
      break;
    case 'n':
      printf(fd, "\n");
      break;
    case 'p':
      pm = tm->tm_hour >= 12;
      printf(fd, "%s", pm ? "PM" : "AM");
      break;
    case 'S':
      print02(fd, tm->tm_sec);
      break;
    case 's':
      printf(fd, "%d", t);
      break;
    case 'T':
      print02(fd, tm->tm_hour);
      printf(fd, ":");
      print02(fd, tm->tm_min);
      printf(fd, ":");
      print02(fd, tm->tm_sec);
      break;
    case 'u':
      printf(fd, "%d", tm->tm_wday == 0 ? 7 : tm->tm_wday);
      break;
    case 'w':
      printf(fd, "%d", tm->tm_wday);
      break;
    case 'y':
      print02(fd, tm->tm_year % 100);
      break;
    case 'Y':
      printf(fd, "%d", tm->tm_year);
      break;
    case 'Z':
      printf(fd, "UTC");
      break;
    case 't':
      printf(fd, "\t");
      break;
    default:
      printf(fd, "%%%c", fmt[i]);
      break;
    }
  }
}

int
main(int argc, char *argv[])
{
  int t;
  struct tm tm;
  char *fmt;
  int i;

  /*
   * 单次 exec 内完成原 date-regress 脚本的多次校验（默认行、两次 epoch、ISO 日期）。
   * QEMU 在首次运行用户程序后可能复位 EHCI，第二次从 USB 映像 exec 会失败，故不能依赖多条命令。
   */
  if(argc >= 2 && strcmp(argv[1], "--regress") == 0){
    int t0, t1;
    struct tm tm1;
    int u0;

    t0 = time(0);
    unix_to_tm(t0, &tm);
    print_default(1, &tm);
    printf(1, "REG-EPOCH1 %d\n", t0);
    u0 = uptime();
    while(uptime() - u0 < 150)
      ;
    t1 = time(0);
    printf(1, "REG-EPOCH2 %d\n", t1);
    unix_to_tm(t1, &tm1);
    strftime_fmt(1, "%Y-%m-%d", &tm1, t1);
    printf(1, "\n");
    exit(0);
  }

  t = time(0);
  unix_to_tm(t, &tm);

  fmt = 0;
  for(i = 1; i < argc; i++){
    if(strcmp(argv[i], "-u") == 0)
      continue;
    if(argv[i][0] == '+'){
      fmt = argv[i] + 1;
      continue;
    }
    printf(2, "date: invalid argument\n");
    exit(0);
  }

  if(fmt)
    strftime_fmt(1, fmt, &tm, t);
  else
    print_default(1, &tm);
  exit(0);
}

#include "types.h"
#include "user.h"
#include "time.h"
#include "stdio.h"
#include "string.h"

static int
isleap(int y)
{
  return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

static int
yday_from_ymd(int y, int mon, int mday)
{
  static const int d[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
  int yd;

  yd = d[mon] + mday - 1;
  if(mon > 1 && isleap(y))
    yd++;
  return yd;
}

static void
utc_from_epoch(int t, struct tm *tm)
{
  int days, rem;
  long epoch_days;
  int y, mon, leap;
  int yday;
  static const int ml[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

  days = t / 86400;
  rem = t % 86400;
  if(rem < 0){
    rem += 86400;
    days--;
  }
  epoch_days = days;
  tm->tm_sec = rem % 60;
  tm->tm_min = (rem / 60) % 60;
  tm->tm_hour = rem / 3600;

  y = 1970;
  for(;;){
    int diy = isleap(y) ? 366 : 365;
    if(days < diy)
      break;
    days -= diy;
    y++;
  }
  tm->tm_year = y - 1900;
  leap = isleap(y);
  for(mon = 0; mon < 12; mon++){
    int dim = ml[mon];
    if(mon == 1 && leap)
      dim = 29;
    if(days < dim){
      tm->tm_mon = mon;
      tm->tm_mday = days + 1;
      break;
    }
    days -= dim;
  }
  yday = yday_from_ymd(y, tm->tm_mon, tm->tm_mday);
  tm->tm_yday = yday;
  {
    int w = (int)((epoch_days + 4) % 7);
    if(w < 0)
      w += 7;
    tm->tm_wday = w;
  }
  tm->tm_isdst = 0;
}

static struct tm gmb;
static struct tm locb;

struct tm *
gmtime(const time_t *tp)
{
  if(tp == 0)
    return 0;
  utc_from_epoch(*tp, &gmb);
  return &gmb;
}

struct tm *
localtime(const time_t *tp)
{
  if(tp == 0)
    return 0;
  utc_from_epoch(*tp, &locb);
  return &locb;
}

clock_t
clock(void)
{
  return (clock_t)uptime();
}

double
difftime(time_t t1, time_t t0)
{
  return (double)(t1 - t0);
}

time_t
mktime(struct tm *tm)
{
  (void)tm;
  return (time_t)-1;
}

static void
append_str(char *dst, size_t *pos, size_t max, const char *s)
{
  while(*s && *pos + 1 < max){
    dst[(*pos)++] = *s++;
  }
}

static void
append_int2(char *dst, size_t *pos, size_t max, int v)
{
  char b[8];
  int i, n;

  n = v;
  if(n < 0)
    n = 0;
  if(n > 99)
    n = 99;
  b[0] = '0' + n / 10;
  b[1] = '0' + n % 10;
  b[2] = 0;
  for(i = 0; b[i] && *pos + 1 < max; i++)
    dst[(*pos)++] = b[i];
}

static void
append_int4(char *dst, size_t *pos, size_t max, int y)
{
  char b[8];
  int t, i, k;

  t = y;
  if(t < 0)
    t = 0;
  for(k = 0; k < 4; k++){
    b[3 - k] = '0' + (t % 10);
    t /= 10;
  }
  b[4] = 0;
  for(i = 0; i < 4 && *pos + 1 < max; i++)
    dst[(*pos)++] = b[i];
}

static const char *wday_name[] =
  {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char *mon_name[] =
  {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

size_t
strftime(char *s, size_t max, const char *fmt, const struct tm *tm)
{
  size_t pos;
  const char *p;

  if(s == 0 || max == 0 || tm == 0)
    return 0;
  pos = 0;
  for(p = fmt; *p && pos + 1 < max; p++){
    if(*p != '%'){
      s[pos++] = *p;
      continue;
    }
    p++;
    if(*p == 0)
      break;
    switch(*p){
    case 'Y':
      append_int4(s, &pos, max, tm->tm_year + 1900);
      break;
    case 'm':
      append_int2(s, &pos, max, tm->tm_mon + 1);
      break;
    case 'd':
      append_int2(s, &pos, max, tm->tm_mday);
      break;
    case 'H':
      append_int2(s, &pos, max, tm->tm_hour);
      break;
    case 'M':
      append_int2(s, &pos, max, tm->tm_min);
      break;
    case 'S':
      append_int2(s, &pos, max, tm->tm_sec);
      break;
    case 'w':
      if(pos + 1 < max)
        s[pos++] = '0' + (tm->tm_wday % 7);
      break;
    case 'j':
      append_int2(s, &pos, max, tm->tm_yday + 1);
      break;
    case 'a':
      if(tm->tm_wday >= 0 && tm->tm_wday <= 6)
        append_str(s, &pos, max, wday_name[tm->tm_wday]);
      break;
    case 'b':
      if(tm->tm_mon >= 0 && tm->tm_mon <= 11)
        append_str(s, &pos, max, mon_name[tm->tm_mon]);
      break;
    case 'c':
      append_str(s, &pos, max, wday_name[tm->tm_wday % 7]);
      append_str(s, &pos, max, " ");
      append_str(s, &pos, max, mon_name[tm->tm_mon % 12]);
      append_str(s, &pos, max, " ");
      append_int2(s, &pos, max, tm->tm_mday);
      append_str(s, &pos, max, " ");
      append_int2(s, &pos, max, tm->tm_hour);
      append_str(s, &pos, max, ":");
      append_int2(s, &pos, max, tm->tm_min);
      append_str(s, &pos, max, ":");
      append_int2(s, &pos, max, tm->tm_sec);
      append_str(s, &pos, max, " ");
      append_int4(s, &pos, max, tm->tm_year + 1900);
      break;
    case '%':
      s[pos++] = '%';
      break;
    default:
      s[pos++] = '%';
      if(pos + 1 < max)
        s[pos++] = *p;
      break;
    }
  }
  s[pos] = 0;
  return pos;
}

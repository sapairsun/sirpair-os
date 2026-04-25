#ifndef SIRPAIR_TIME_H
#define SIRPAIR_TIME_H

#include <stddef.h>

#ifndef SIRPAIR_TIME_T_DEFINED
#define SIRPAIR_TIME_T_DEFINED
typedef int time_t;
#endif
typedef int clock_t;

struct tm {
  int tm_sec;
  int tm_min;
  int tm_hour;
  int tm_mday;
  int tm_mon;
  int tm_year;
  int tm_wday;
  int tm_yday;
  int tm_isdst;
};

#define CLOCKS_PER_SEC 100

time_t time(time_t *tp);
struct tm *gmtime(const time_t *tp);
struct tm *localtime(const time_t *tp);
clock_t clock(void);
double difftime(time_t t1, time_t t0);
time_t mktime(struct tm *tm);
size_t strftime(char *s, size_t max, const char *fmt, const struct tm *tm);

#endif

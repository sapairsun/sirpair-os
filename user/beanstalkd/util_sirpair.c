#include "dat.h"
#include "beanstalkd_sirpair_shim.h"
#include "mp_printf.h"
#include "types.h"
#include "user.h"
#include <stdarg.h>
#include <stdint.h>
#include <string.h>

const char *progname;

static void
vwarnx(const char *err, const char *fmt, va_list args)
{
  char b[512];

  vsnprintf_(b, sizeof b, fmt, args);
  if(err)
    bs_printf(2, "%s: %s: %s\n", progname, b, err);
  else
    bs_printf(2, "%s: %s\n", progname, b);
}

void
warn(const char *fmt, ...)
{
  va_list args;
  char *e;

  e = strerror(errno);
  va_start(args, fmt);
  vwarnx(e, fmt, args);
  va_end(args);
}

void
warnx(const char *fmt, ...)
{
  va_list args;

  va_start(args, fmt);
  vwarnx(0, fmt, args);
  va_end(args);
}

char *
fmtalloc(char *fmt, ...)
{
  int n;
  char *buf;
  va_list ap;

  va_start(ap, fmt);
  n = vsnprintf_(0, 0, fmt, ap) + 1;
  va_end(ap);
  buf = malloc(n);
  if(buf){
    va_start(ap, fmt);
    vsnprintf_(buf, n, fmt, ap);
    va_end(ap);
  }
  return buf;
}

void *
zalloc(int n)
{
  void *p;

  p = malloc(n);
  if(p)
    memset(p, 0, n);
  return p;
}

static size_t
parse_size_t(char *str)
{
  uint64 n;
  char *p;

  while(*str == ' ')
    str++;
  n = 0;
  p = str;
  while(*p >= '0' && *p <= '9'){
    n = n * 10 + (uint64)(*p - '0');
    p++;
  }
  if(p == str || *p){
    warnx("invalid size: %s", str);
    exit(5);
  }
  return (size_t)n;
}

static void usage(int code) __attribute__((noreturn));

static char *
flagusage(const char *flag)
{
  warnx("flag requires an argument: %s", flag);
  usage(5);
  return 0;
}

static void
usage(int code)
{
  bs_printf(2,
         "usage: %s [options]\n"
         " -l ADDR  listen on unix:path (default unix:/bs)\n"
         " -p PORT  ignored (sirpair: Unix domain sockets only)\n"
         " -z BYTES max job size\n"
         " -V       verbose\n"
         " -v       version\n"
         " -h       help\n",
         progname);
  exit(code);
}

void
optparse(Server *s, char **argv)
{
  int64 ms;
  char *arg, *tmp;
#define EARGF(x) (*arg ? (tmp = arg, arg = "", tmp) : *argv ? *argv++ : (x))

  while((arg = *argv++) && *arg++ == '-' && *arg){
    char c;
    while((c = *arg++)){
      switch(c){
      case 'p':
        s->port = EARGF(flagusage("-p"));
        break;
      case 'l':
        s->addr = EARGF(flagusage("-l"));
        break;
      case 'z':
        job_data_size_limit = parse_size_t(EARGF(flagusage("-z")));
        if(job_data_size_limit > JOB_DATA_SIZE_LIMIT_MAX){
          warnx("maximum job size was set to %d", JOB_DATA_SIZE_LIMIT_MAX);
          job_data_size_limit = JOB_DATA_SIZE_LIMIT_MAX;
        }
        break;
      case 's':
        s->wal.filesize = (int)parse_size_t(EARGF(flagusage("-s")));
        break;
      case 'f':
        ms = (int64)parse_size_t(EARGF(flagusage("-f")));
        s->wal.syncrate = ms * 1000000;
        s->wal.wantsync = 1;
        break;
      case 'F':
        s->wal.wantsync = 0;
        break;
      case 'b':
        s->wal.dir = EARGF(flagusage("-b"));
        s->wal.use = 1;
        break;
      case 'h':
        usage(0);
      case 'v':
        bs_printf(1, "beanstalkd %s\n", version);
        exit(0);
      case 'V':
        verbose++;
        break;
      default:
        warnx("unknown flag");
        usage(5);
      }
    }
  }
  if(arg){
    warnx("unknown argument");
    usage(5);
  }
}

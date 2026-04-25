#include "types.h"
#include "stat.h"
#include "user.h"
#include "param.h"
#include "pinfo.h"

static void
printpad(char *s, int width)
{
  int i, len;
  len = strlen(s);
  if(len > width)
    len = width;
  write(1, s, len);
  for(i = len; i < width; i++)
    write(1, " ", 1);
}

static void
printintpad(int v, int width)
{
  char buf[16];
  int i, j, len, neg;
  uint x;

  neg = 0;
  if(v < 0){
    neg = 1;
    x = (uint)(-v);
  } else {
    x = (uint)v;
  }

  i = 0;
  do{
    buf[i++] = '0' + (x % 10);
    x /= 10;
  }while(x);
  if(neg)
    buf[i++] = '-';

  len = i;
  for(j = len; j < width; j++)
    write(1, " ", 1);
  while(i > 0){
    i--;
    write(1, &buf[i], 1);
  }
}

static void
print_time_hms(int sec)
{
  int h, m, s;
  h = sec / 3600;
  m = (sec % 3600) / 60;
  s = sec % 60;
  printf(1, "%d%d:%d%d:%d%d",
         (h / 10) % 10, h % 10,
         (m / 10) % 10, m % 10,
         (s / 10) % 10, s % 10);
}

static char
state_char(int st)
{
  switch(st){
  case PS_RUNNING:  return 'R';
  case PS_RUNNABLE: return 'R';
  case PS_SLEEPING: return 'S';
  case PS_ZOMBIE:   return 'Z';
  case PS_EMBRYO:   return 'I';
  default:          return 'S';
  }
}

static void
print_header(struct pinfo *ps, int n, int upsec)
{
  int i, running, sleeping, zombie;
  running = sleeping = zombie = 0;
  for(i = 0; i < n; i++){
    if(ps[i].state == PS_RUNNING || ps[i].state == PS_RUNNABLE)
      running++;
    else if(ps[i].state == PS_SLEEPING || ps[i].state == PS_EMBRYO)
      sleeping++;
    else if(ps[i].state == PS_ZOMBIE)
      zombie++;
  }

  printf(1, "top - ");
  print_time_hms(upsec);
  printf(1, " up %d min,  1 user,  load average: 0.00, 0.00, 0.00\n", upsec / 60);
  printf(1, "Tasks: %d total, %d running, %d sleeping, 0 stopped, %d zombie\n",
         n, running, sleeping, zombie);
  printf(1, "%%Cpu(s):  0.0 us,  0.0 sy,  0.0 ni, 100.0 id,  0.0 wa,  0.0 hi,  0.0 si,  0.0 st\n");
  printf(1, "MiB Mem :  0.0 total,  0.0 free,  0.0 used,  0.0 buff/cache\n");
  printf(1, "MiB Swap:  0.0 total,  0.0 free,  0.0 used.  0.0 avail Mem\n");
  printf(1, "\n");
  printf(1, "  PID USER      PR  NI    VIRT    RES    SHR S  %%CPU  %%MEM     TIME+ COMMAND\n");
}

static void
print_one(struct pinfo *p)
{
  char s[2];
  uint virt_k = p->sz / 1024;
  s[0] = state_char(p->state);
  s[1] = 0;

  printintpad(p->pid, 5);
  write(1, " ", 1);
  printpad("root", 8);
  printintpad(20, 3);
  write(1, " ", 1);
  printintpad(0, 3);
  write(1, " ", 1);
  printintpad(virt_k, 7);
  write(1, " ", 1);
  printintpad(0, 6);
  write(1, " ", 1);
  printintpad(0, 6);
  write(1, " ", 1);
  printpad(s, 1);
  write(1, " ", 1);
  printpad("0.0", 5);
  write(1, " ", 1);
  printpad("0.0", 5);
  write(1, " ", 1);
  printpad("0:00.00", 9);
  write(1, " ", 1);
  printf(1, "%s\n", p->name);
}

static void
usage(void)
{
  printf(2, "usage: top [-n count] [-d delay_sec]\n");
}

int
main(int argc, char *argv[])
{
  struct pinfo ps[NPROC];
  int i, j, n, loops, delay_sec, upsec;

  loops = 1;
  delay_sec = 1;

  for(i = 1; i < argc; i++){
    if(strcmp(argv[i], "-n") == 0){
      if(i + 1 >= argc){
        usage();
        exit(0);
      }
      loops = atoi(argv[++i]);
    } else if(strcmp(argv[i], "-d") == 0){
      if(i + 1 >= argc){
        usage();
        exit(0);
      }
      delay_sec = atoi(argv[++i]);
    } else {
      usage();
      exit(0);
    }
  }

  if(loops <= 0)
    loops = 1;
  if(delay_sec <= 0)
    delay_sec = 1;

  for(i = 0; i < loops; i++){
    n = getprocs(ps, NPROC);
    if(n < 0){
      printf(2, "top: getprocs failed\n");
      exit(0);
    }
    upsec = uptime() / HZ;

    printf(1, "\033[2J\033[H");
    print_header(ps, n, upsec);
    for(j = 0; j < n; j++)
      print_one(&ps[j]);

    if(i + 1 < loops)
      sleep(delay_sec * HZ);
  }

  exit(0);
}

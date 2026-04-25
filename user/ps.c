#include "types.h"
#include "stat.h"
#include "user.h"
#include "param.h"
#include "pinfo.h"

// Print string left-aligned, padded to given width
static void
printpad(int fd, char *s, int width)
{
  int len = strlen(s);
  int i;
  write(fd, s, len);
  for(i = len; i < width; i++)
    write(fd, " ", 1);
}

// Print integer left-aligned, padded to given width
static void
printintpad(int fd, int val, int width)
{
  char buf[16];
  int i, len;
  uint v;

  if(val < 0){
    buf[0] = '-';
    v = -val;
    i = 1;
  } else {
    v = val;
    i = 0;
  }

  int start = i;
  do {
    buf[i++] = '0' + (v % 10);
    v /= 10;
  } while(v != 0);
  buf[i] = 0;
  len = i;

  // Reverse digit portion
  int left = start, right = len - 1;
  while(left < right){
    char tmp = buf[left];
    buf[left] = buf[right];
    buf[right] = tmp;
    left++;
    right--;
  }

  write(fd, buf, len);
  for(i = len; i < width; i++)
    write(fd, " ", 1);
}

static char*
statename(int state)
{
  switch(state){
  case PS_EMBRYO:   return "EMBRYO";
  case PS_SLEEPING: return "SLEEP";
  case PS_RUNNABLE: return "READY";
  case PS_RUNNING:  return "RUN";
  case PS_ZOMBIE:   return "ZOMBIE";
  default:          return "???";
  }
}

int
main(int argc, char *argv[])
{
  struct pinfo pinfos[NPROC];
  int n, i;

  n = getprocs(pinfos, NPROC);
  if(n < 0){
    printf(2, "ps: getprocs failed\n");
    exit(0);
  }

  // Print header
  printpad(1, "PID", 6);
  printpad(1, "PPID", 6);
  printpad(1, "CPU", 5);
  printpad(1, "STATE", 9);
  printpad(1, "SIZE", 10);
  printf(1, "NAME\n");

  for(i = 0; i < n; i++){
    printintpad(1, pinfos[i].pid, 6);
    printintpad(1, pinfos[i].ppid, 6);
    printintpad(1, pinfos[i].cpu_id, 5);
    printpad(1, statename(pinfos[i].state), 9);
    printintpad(1, pinfos[i].sz, 10);
    printf(1, "%s\n", pinfos[i].name);
  }

  exit(0);
}

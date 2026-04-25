#include "types.h"
#include "user.h"
#include "usock.h"

#define SOCKPATH "/bs"

static char cmd[] = "stats\r\n";

static int
has_marker(const char *s, const char *needle)
{
  const char *a, *b;

  for(; *s; s++){
    for(a = s, b = needle; *b && *a == *b; a++, b++)
      ;
    if(*b == 0)
      return 1;
  }
  return 0;
}

int
main(int argc, char **argv)
{
  int pid, fd, n, r, i, j;
  char buf[512];
  char *av[2];

  (void)argc;
  (void)argv;

  pid = fork();
  if(pid < 0){
    printf(2, "bsregress: fork failed\n");
    exit(1);
  }
  if(pid == 0){
    av[0] = "beanstalkd";
    av[1] = 0;
    exec("/bin/beanstalkd", av);
    printf(2, "bsregress: exec beanstalkd failed\n");
    exit(1);
  }

  fd = -1;
  for(j = 0; j < 60; j++){
    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if(fd >= 0 && connect(fd, SOCKPATH) >= 0)
      break;
    if(fd >= 0){
      close(fd);
      fd = -1;
    }
    sleep(100);
  }
  if(fd < 0){
    printf(2, "bsregress: connect %s failed\n", SOCKPATH);
    kill(pid);
    wait();
    exit(1);
  }

  n = strlen(cmd);
  if(write(fd, cmd, n) != n){
    printf(2, "bsregress: write failed\n");
    close(fd);
    kill(pid);
    wait();
    exit(1);
  }
  n = 0;
  for(i = 0; i < (int)sizeof(buf) - 1; i++){
    r = read(fd, buf + i, 1);
    if(r <= 0)
      break;
    n = i + 1;
    if(n >= 4 && buf[n - 4] == '\r' && buf[n - 3] == '\n' && buf[n - 2] == '\r'
       && buf[n - 1] == '\n')
      break;
  }
  buf[n] = 0;
  if(!has_marker(buf, "current-jobs-ready")){
    printf(2, "bsregress: unexpected reply\n");
    close(fd);
    kill(pid);
    wait();
    exit(1);
  }
  close(fd);
  printf(1, "BSTEST_OK\n");
  kill(pid);
  wait();
  exit(0);
}

#include "types.h"
#include "user.h"
#include "usock.h"

#define SOCKPATH "/bs"

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
  int fd, n, r, i;
  char buf[512];
  static char cmd[] = "stats\r\n";

  (void)argc;
  (void)argv;

  fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if(fd < 0){
    printf(2, "bstest: socket failed\n");
    exit(1);
  }
  if(connect(fd, SOCKPATH) < 0){
    printf(2, "bstest: connect %s failed\n", SOCKPATH);
    close(fd);
    exit(1);
  }
  n = strlen(cmd);
  if(write(fd, cmd, n) != n){
    printf(2, "bstest: write failed\n");
    close(fd);
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
    printf(2, "bstest: unexpected reply\n");
    close(fd);
    exit(1);
  }
  printf(1, "BSTEST_OK\n");
  close(fd);
  exit(0);
}

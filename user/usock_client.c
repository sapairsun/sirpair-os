#include "types.h"
#include "user.h"
#include "usock.h"

#define PATH "/dev/usock_all"

static int
recv_line(int fd, char *buf, int max)
{
  int n = 0;
  char ch;
  int r;

  if(max <= 1)
    return -1;
  while(n < max - 1){
    r = recv(fd, &ch, 1);
    if(r < 0)
      return -1;
    if(r == 0)
      break;
    if(ch == '\n')
      break;
    buf[n++] = ch;
  }
  buf[n] = 0;
  return n;
}

int
main(int argc, char **argv)
{
  int fd, i, n;
  char *msgs[5] = { "hello\n", "x\n", "hello\n", "y\n", "z\n" };
  char line[128];

  if(argc != 1){
    printf(2, "usage: usock_client\n");
    exit(0);
  }

  fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if(fd < 0){
    printf(2, "usock_client: socket failed\n");
    exit(0);
  }
  if(connect(fd, PATH) < 0){
    printf(2, "usock_client: connect %s failed\n", PATH);
    close(fd);
    exit(0);
  }

  for(i = 0; i < 5; i++){
    n = strlen(msgs[i]);
    if(send(fd, msgs[i], n) != n){
      printf(2, "usock_client: send failed at %d\n", i);
      close(fd);
      exit(0);
    }
    if(strcmp(msgs[i], "hello\n") == 0){
      n = recv_line(fd, line, sizeof(line));
      if(n <= 0){
        printf(2, "usock_client: recv failed at %d\n", i);
        close(fd);
        exit(0);
      }
      printf(1, "usock_client recv: %s\n", line);
    }
  }

  close(fd);
  exit(0);
}

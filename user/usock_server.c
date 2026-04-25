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
  int lfd, cfd;
  char line[128];
  int n;

  if(argc != 1){
    printf(2, "usage: usock_server\n");
    exit(0);
  }

  lfd = socket(AF_UNIX, SOCK_STREAM, 0);
  if(lfd < 0){
    printf(2, "usock_server: socket failed\n");
    exit(0);
  }
  if(bind(lfd, PATH) < 0){
    printf(2, "usock_server: bind %s failed\n", PATH);
    close(lfd);
    exit(0);
  }
  if(listen(lfd, 4) < 0){
    printf(2, "usock_server: listen failed\n");
    close(lfd);
    exit(0);
  }

  while(1){
    cfd = accept(lfd);
    if(cfd < 0){
      // accept usually fails only when the process is being killed.
      break;
    }

    while(1){
      n = recv_line(cfd, line, sizeof(line));
      if(n < 0){
        printf(2, "usock_server: recv failed\n");
        break;
      }
      if(n == 0)
        break;
      if(strcmp(line, "hello") == 0){
        if(send(cfd, "world\n", 6) != 6){
          printf(2, "usock_server: send failed\n");
          break;
        }
      }
    }
    close(cfd);
  }

  close(lfd);
  exit(0);
}

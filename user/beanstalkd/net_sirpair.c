#include "dat.h"
#include "types.h"
#include "user.h"
#include "usock.h"
#include <string.h>

int
set_nonblocking(int fd)
{
  (void)fd;
  return 0;
}

int
make_server_socket(char *host, char *port)
{
  int fd;
  char *path;

  (void)port;
  if(!host || strncmp(host, "unix:", 5) != 0){
    return -1;
  }
  path = host + 5;
  fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if(fd < 0)
    return -1;
  unlink(path);
  if(bind(fd, path) < 0){
    close(fd);
    return -1;
  }
  if(listen(fd, 16) < 0){
    close(fd);
    return -1;
  }
  return fd;
}

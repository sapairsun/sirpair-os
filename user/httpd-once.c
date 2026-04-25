#include "types.h"
#include "user.h"
#include "usock.h"

static void
append_port(char *addr, int port)
{
  char rev[16];
  int i, k, t;
  i = strlen(addr);
  addr[i++] = ':';
  k = 0;
  t = port;
  if(t == 0)
    rev[k++] = '0';
  else
    while(t > 0 && k < 15){
      rev[k++] = '0' + (t % 10);
      t /= 10;
    }
  while(k > 0)
    addr[i++] = rev[--k];
  addr[i] = 0;
}

static int
send_all(int fd, char *buf, int n)
{
  int off, r;
  off = 0;
  while(off < n){
    r = send(fd, buf + off, n - off);
    if(r <= 0)
      return -1;
    off += r;
  }
  return 0;
}

int
main(int argc, char **argv)
{
  int lfd, cfd, port, n, body_len;
  char addr[80];
  char reqbuf[256];
  char rsp[512];
  char lenbuf[16];
  char *body;
  int rpos, i;

  if(argc < 3){
    printf(2, "usage: httpd-once <ip> <port> [body]\n");
    exit(1);
  }
  port = atoi(argv[2]);
  if(port <= 0 || port > 65535){
    printf(2, "httpd-once: invalid port\n");
    exit(1);
  }
  body = argc >= 4 ? argv[3] : "ok\n";
  body_len = strlen(body);

  strcpy(addr, argv[1]);
  append_port(addr, port);

  lfd = socket(AF_INET, SOCK_STREAM, 0);
  if(lfd < 0){
    printf(2, "httpd-once: socket failed\n");
    exit(1);
  }
  if(bind(lfd, addr) < 0){
    printf(2, "httpd-once: bind failed\n");
    close(lfd);
    exit(1);
  }
  if(listen(lfd, 1) < 0){
    printf(2, "httpd-once: listen failed\n");
    close(lfd);
    exit(1);
  }
  cfd = accept(lfd);
  close(lfd);
  if(cfd < 0){
    printf(2, "httpd-once: accept failed\n");
    exit(1);
  }

  // 只要收到任意请求数据就返回响应，避免等待完整头导致阻塞。
  n = recv(cfd, reqbuf, sizeof(reqbuf));
  (void)n;

  // 组装 HTTP/1.0 响应。
  rpos = 0;
  strcpy(rsp, "HTTP/1.0 200 OK\r\nContent-Length: ");
  rpos = strlen(rsp);
  {
    char rev[16];
    int k = 0;
    int t = body_len;
    if(t == 0)
      rev[k++] = '0';
    else
      while(t > 0 && k < 15){
        rev[k++] = '0' + (t % 10);
        t /= 10;
      }
    i = 0;
    while(k > 0)
      lenbuf[i++] = rev[--k];
    lenbuf[i] = 0;
  }
  for(i = 0; lenbuf[i] && rpos < (int)sizeof(rsp) - 1; i++)
    rsp[rpos++] = lenbuf[i];
  if(rpos + 4 >= (int)sizeof(rsp)){
    close(cfd);
    exit(1);
  }
  rsp[rpos++] = '\r';
  rsp[rpos++] = '\n';
  rsp[rpos++] = '\r';
  rsp[rpos++] = '\n';
  rsp[rpos] = 0;

  if(send_all(cfd, rsp, rpos) < 0){
    close(cfd);
    exit(1);
  }
  if(body_len > 0 && send_all(cfd, body, body_len) < 0){
    close(cfd);
    exit(1);
  }
  close(cfd);
  exit(0);
}

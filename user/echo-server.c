/*
 * echo-server tcp|udp <IPv4> <端口>
 * 按换行分包：TCP 流式逐字节收满一行再回显；UDP 每收到一个数据报原样回显。
 */
#include "types.h"
#include "user.h"
#include "usock.h"

static void
append_port(char *addr, int port)
{
  char *p;
  char rev[8];
  int k, t;

  p = addr + strlen(addr);
  *p++ = ':';
  k = 0;
  t = port;
  if(t == 0)
    rev[k++] = '0';
  else
    while(t > 0 && k < 7){
      rev[k++] = '0' + (t % 10);
      t /= 10;
    }
  while(k > 0 && p < addr + 79)
    *p++ = rev[--k];
  *p = 0;
}

static void
tcp_line_echo(int fd)
{
  char buf[512];
  int i, n;
  char c;
  int skip_lf;

  i = 0;
  skip_lf = 0;
  for(;;){
    /*
     * 不要用 fdready 再 recv：用户态 fdready 与 TCP 收包可能短暂不一致，
     * 会长期 sleep(1) 不读数据，telnet 已发出「nihao+回车」却得不到回显。
     * 直接阻塞 recv 与内核 fileread 一致。
     */
    n = recv(fd, &c, 1);
    if(n <= 0)
      break;
    /* telnet/终端常以 \r 结束一行；若仅等待 \n 则永无回显 */
    if(skip_lf){
      if(c == '\n' && i == 0){
        skip_lf = 0;
        continue;
      }
      skip_lf = 0;
    }
    if(c == '\r'){
      /*
       * 回显须带换行：否则对端仅收到「行内容」无 \n，telnet 按行打 "> " 时无法开始新行，
       * 第二行起会与上行粘在一起显示错乱。可选的 \n 仍由 skip_lf 吞掉。
       */
      if(send(fd, buf, i) != i)
        break;
      if(send(fd, "\n", 1) != 1)
        break;
      i = 0;
      skip_lf = 1;
      continue;
    }
    if(c == '\n'){
      if(i < (int)sizeof(buf) - 1)
        buf[i++] = c;
      if(send(fd, buf, i) != i)
        break;
      i = 0;
      continue;
    }
    if(i < (int)sizeof(buf) - 1)
      buf[i++] = c;
  }
}

static void
run_tcp(char *ip, int port)
{
  char addr[80];
  int ls, cfd;

  if(strlen(ip) >= sizeof(addr) - 8){
    printf(2, "echo-server: address too long\n");
    exit(1);
  }
  strcpy(addr, ip);
  append_port(addr, port);
  ls = socket(AF_INET, SOCK_STREAM, 0);
  if(ls < 0){
    printf(2, "echo-server: socket failed\n");
    exit(1);
  }
  if(bind(ls, addr) < 0){
    printf(2, "echo-server: bind %s failed\n", addr);
    close(ls);
    exit(1);
  }
  if(listen(ls, 5) < 0){
    printf(2, "echo-server: listen failed\n");
    close(ls);
    exit(1);
  }
  /*
   * 单进程串行处理连接，不再 fork。
   * 原先每接受一个连接就 fork 子进程，ps 会看到两个 echo-server，易被误认为异常；
   * 且多进程与本机 TCP 槽位实现叠加时增加排错成本。
   * 代价：同一时刻只服务一个客户端，对回显验证与常见用法足够。
   */
  for(;;){
    cfd = accept(ls);
    if(cfd < 0)
      exit(0);
    tcp_line_echo(cfd);
    close(cfd);
  }
}

static void
run_udp(char *ip, int port)
{
  char addr[80];
  int s;
  char buf[1400];
  char src_ip[4];
  int src_port;
  int n;

  if(strlen(ip) >= sizeof(addr) - 8){
    printf(2, "echo-server: address too long\n");
    exit(1);
  }
  strcpy(addr, ip);
  append_port(addr, port);
  s = socket(AF_INET, SOCK_DGRAM, 0);
  if(s < 0){
    printf(2, "echo-server: socket failed\n");
    exit(1);
  }
  if(bind(s, addr) < 0){
    printf(2, "echo-server: bind %s failed\n", addr);
    close(s);
    exit(1);
  }
  for(;;){
    while(fdready(s, 0) <= 0)
      sleep(1);
    n = recvfrom(s, buf, sizeof(buf), src_ip, &src_port);
    if(n <= 0)
      continue;
    if(sendto(s, buf, n, src_ip, src_port) != n)
      continue;
  }
}

int
main(int argc, char **argv)
{
  int port;

  if(argc != 4){
    printf(2, "usage: echo-server tcp|udp <IPv4> <port>\n");
    exit(1);
  }
  port = atoi(argv[3]);
  if(port <= 0 || port > 65535){
    printf(2, "echo-server: invalid port\n");
    exit(1);
  }
  if(strcmp(argv[1], "tcp") == 0)
    run_tcp(argv[2], port);
  else if(strcmp(argv[1], "udp") == 0)
    run_udp(argv[2], port);
  else{
    printf(2, "echo-server: first arg must be tcp or udp\n");
    exit(1);
  }
  exit(0);
}

/*
 * 回归用：udp_line_client <IPv4> <端口> <一行文本，可含空格>
 * 发送带换行的一行 UDP 数据报，再 recvfrom 打印应答（供 echo-server udp 自测）。
 */
#include "types.h"
#include "user.h"
#include "usock.h"

static int
parse_ipv4(const char *s, unsigned char *o)
{
  int i, v, ndig;
  const char *p = s;

  for(i = 0; i < 4; i++){
    v = 0;
    ndig = 0;
    while(*p >= '0' && *p <= '9'){
      v = v * 10 + (*p - '0');
      if(v > 255)
        return -1;
      p++;
      ndig++;
    }
    if(ndig == 0)
      return -1;
    o[i] = (unsigned char)v;
    if(i < 3){
      if(*p != '.')
        return -1;
      p++;
    }
  }
  return *p == 0 ? 0 : -1;
}

int
main(int argc, char **argv)
{
  int sock;
  int port;
  char msg[512];
  char rbuf[1400];
  char src_ip[4];
  int src_port;
  int ml, n, i;
  unsigned char dip[4];

  if(argc < 4){
    printf(2, "usage: udp_line_client <IPv4> <port> <text>\n");
    exit(1);
  }
  if(parse_ipv4(argv[1], dip) < 0){
    printf(2, "udp_line_client: invalid address\n");
    exit(1);
  }
  port = atoi(argv[2]);
  if(port <= 0 || port > 65535){
    printf(2, "udp_line_client: invalid port\n");
    exit(1);
  }
  ml = 0;
  msg[0] = 0;
  for(i = 3; i < argc; i++){
    int j, L;
    if(i > 3){
      if(ml + 1 >= (int)sizeof(msg)){
        printf(2, "udp_line_client: text too long\n");
        exit(1);
      }
      msg[ml++] = ' ';
      msg[ml] = 0;
    }
    L = strlen(argv[i]);
    if(ml + L >= (int)sizeof(msg)){
      printf(2, "udp_line_client: text too long\n");
      exit(1);
    }
    for(j = 0; j < L; j++)
      msg[ml++] = argv[i][j];
    msg[ml] = 0;
  }
  if(ml == 0 || msg[ml - 1] != '\n'){
    if(ml + 1 >= (int)sizeof(msg)){
      printf(2, "udp_line_client: text too long\n");
      exit(1);
    }
    msg[ml++] = '\n';
    msg[ml] = 0;
  }

  sock = socket(AF_INET, SOCK_DGRAM, 0);
  if(sock < 0){
    printf(2, "udp_line_client: socket failed\n");
    exit(1);
  }
  if(bind(sock, "0.0.0.0:0") < 0){
    printf(2, "udp_line_client: bind failed\n");
    close(sock);
    exit(1);
  }
  if(sendto(sock, msg, ml, (char*)dip, port) != ml){
    printf(2, "udp_line_client: sendto failed\n");
    close(sock);
    exit(1);
  }
  while(fdready(sock, 0) <= 0)
    sleep(1);
  n = recvfrom(sock, rbuf, sizeof(rbuf), src_ip, &src_port);
  close(sock);
  if(n <= 0){
    printf(2, "udp_line_client: recvfrom failed\n");
    exit(1);
  }
  if(write(1, rbuf, n) != n)
    exit(1);
  exit(0);
}

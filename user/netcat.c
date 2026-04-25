/*
 * netcat — 行为子集，参考 thirdparty/netcat-0.7.1 的常见用法在本系统上可实现的部分。
 * 本内核：AF_INET 支持 TCP 客户端与 bind/listen/accept；-s 与 -l、-p 同时出现时在本机指定地址与端口上监听 TCP。
 */
#include "types.h"
#include "user.h"
#include "usock.h"

/*
 * server_session：为 1 表示 listen 并已 accept 的服务端会话。
 * shell 后台任务将 stdin 置为 /null，read 立即 EOF；若按普通中继则误退出并关掉连接。
 * 服务端应在 stdin EOF 后仅停止向对端发送，仍接收对端数据，直至对端关闭。
 */
static void
relay(int sock, int server_session)
{
  char buf[256];
  int n;
  int stdin_dead;

  stdin_dead = 0;
  for(;;){
    /* 见 telnet：内层排空 sock 再读 stdin，避免对端快速 FIN 时 send 失败 */
    {
      int sock_done;

      sock_done = 0;
      while(fdready(sock, 0) != 0){
        n = recv(sock, buf, sizeof(buf));
        if(n <= 0){
          sock_done = 1;
          break;
        }
        if(write(1, buf, n) != n){
          sock_done = 1;
          break;
        }
      }
      if(sock_done)
        break;
    }
    if(!stdin_dead && fdready(0, 0) > 0){
      n = read(0, buf, sizeof(buf));
      if(n <= 0){
        if(server_session){
          stdin_dead = 1;
          continue;
        }
        break;
      }
      if(send(sock, buf, n) != n)
        break;
    }
    if(fdready(sock, 0) <= 0 && (stdin_dead || fdready(0, 0) <= 0))
      sleep(1);
  }
  close(sock);
}

/* 在 addr 末尾追加 :<十进制端口> */
static void
append_tcp_port(char *addr, int port)
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

/* 生成 /nc-<端口>，与仅 -l -p（无 -s）时 Unix 监听配套 */
static void
path_from_port(int port, char *out, int olen)
{
  int i, k, p;
  char rev[8];

  if(port <= 0 || port > 65535 || olen < 16){
    out[0] = 0;
    return;
  }
  strcpy(out, "/nc-");
  i = strlen(out);
  k = 0;
  p = port;
  while(p > 0 && k < 6){
    rev[k++] = '0' + (p % 10);
    p /= 10;
  }
  while(k > 0 && i < olen - 1)
    out[i++] = rev[--k];
  out[i] = 0;
}

static void
usage(void)
{
  printf(2,
    "usage:\n"
    "  netcat <dotted IPv4> [port]     TCP connect (default port 31337)\n"
    "  netcat -U <unix-path>           Unix domain stream\n"
    "  netcat -l -p <port>             listen on /nc-<port> (Unix)\n"
    "  netcat -l -s <IPv4> -p <port>   listen TCP on address and port\n"
    "  netcat -l -U <unix-path>        listen on path (Unix)\n"
  );
}

int
main(int argc, char **argv)
{
  int i;
  int listen_mode = 0;
  int lport = -1;
  char *upath = 0;
  char *bind_ip = 0;
  char *host = 0;
  int tcpport = 31337;
  int sock;
  char addr[80];
  char pathbuf[USOCK_PATH_MAX];
  int lfd, cfd;

  for(i = 1; i < argc; i++){
    if(strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0){
      usage();
      exit(0);
    }
    if(strcmp(argv[i], "-l") == 0){
      listen_mode = 1;
      continue;
    }
    if(strcmp(argv[i], "-p") == 0){
      if(i + 1 >= argc){
        printf(2, "netcat: -p needs a port\n");
        exit(1);
      }
      lport = atoi(argv[++i]);
      if(lport <= 0 || lport > 65535){
        printf(2, "netcat: invalid port\n");
        exit(1);
      }
      continue;
    }
    if(strcmp(argv[i], "-s") == 0){
      if(i + 1 >= argc){
        printf(2, "netcat: -s needs an address\n");
        exit(1);
      }
      bind_ip = argv[++i];
      continue;
    }
    if(strcmp(argv[i], "-U") == 0){
      if(i + 1 >= argc){
        printf(2, "netcat: -U needs a path\n");
        exit(1);
      }
      upath = argv[++i];
      if(strlen(upath) >= sizeof(pathbuf)){
        printf(2, "netcat: path too long\n");
        exit(1);
      }
      continue;
    }
    if(argv[i][0] == '-'){
      printf(2, "netcat: unknown option %s\n", argv[i]);
      usage();
      exit(1);
    }
    if(host == 0)
      host = argv[i];
    else{
      tcpport = atoi(argv[i]);
      if(tcpport <= 0 || tcpport > 65535){
        printf(2, "netcat: invalid port\n");
        exit(1);
      }
    }
  }

  if(upath && host){
    printf(2, "netcat: cannot use host and -U path together\n");
    exit(1);
  }

  if(listen_mode){
    if(upath){
      strcpy(pathbuf, upath);
      lfd = socket(AF_UNIX, SOCK_STREAM, 0);
      if(lfd < 0){
        printf(2, "netcat: socket failed\n");
        exit(1);
      }
      if(bind(lfd, pathbuf) < 0){
        printf(2, "netcat: bind %s failed\n", pathbuf);
        close(lfd);
        exit(1);
      }
      if(listen(lfd, 4) < 0){
        printf(2, "netcat: listen failed\n");
        close(lfd);
        exit(1);
      }
      cfd = accept(lfd);
      close(lfd);
      if(cfd < 0){
        printf(2, "netcat: accept failed\n");
        exit(1);
      }
      relay(cfd, 1);
      exit(0);
    }
    if(bind_ip && lport > 0){
      if(strlen(bind_ip) >= sizeof(addr) - 8){
        printf(2, "netcat: bind address too long\n");
        exit(1);
      }
      strcpy(addr, bind_ip);
      append_tcp_port(addr, lport);
      lfd = socket(AF_INET, SOCK_STREAM, 0);
      if(lfd < 0){
        printf(2, "netcat: socket failed\n");
        exit(1);
      }
      if(bind(lfd, addr) < 0){
        printf(2, "netcat: bind %s failed\n", addr);
        close(lfd);
        exit(1);
      }
      if(listen(lfd, 4) < 0){
        printf(2, "netcat: listen failed\n");
        close(lfd);
        exit(1);
      }
      cfd = accept(lfd);
      close(lfd);
      if(cfd < 0){
        printf(2, "netcat: accept failed\n");
        exit(1);
      }
      relay(cfd, 1);
      exit(0);
    }
    if(lport > 0){
      path_from_port(lport, pathbuf, sizeof(pathbuf));
      if(pathbuf[0] == 0){
        printf(2, "netcat: invalid listen path\n");
        exit(1);
      }
      lfd = socket(AF_UNIX, SOCK_STREAM, 0);
      if(lfd < 0){
        printf(2, "netcat: socket failed\n");
        exit(1);
      }
      if(bind(lfd, pathbuf) < 0){
        printf(2, "netcat: bind %s failed\n", pathbuf);
        close(lfd);
        exit(1);
      }
      if(listen(lfd, 4) < 0){
        printf(2, "netcat: listen failed\n");
        close(lfd);
        exit(1);
      }
      cfd = accept(lfd);
      close(lfd);
      if(cfd < 0){
        printf(2, "netcat: accept failed\n");
        exit(1);
      }
      relay(cfd, 1);
      exit(0);
    }
    printf(2, "netcat: listen needs -p <port> (optional -s for TCP) or -U <path>\n");
    exit(1);
  }

  if(upath){
    sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if(sock < 0){
      printf(2, "netcat: socket failed\n");
      exit(1);
    }
    if(connect(sock, upath) < 0){
      printf(2, "netcat: connect failed\n");
      close(sock);
      exit(1);
    }
    relay(sock, 0);
    exit(0);
  }

  if(host == 0){
    usage();
    exit(1);
  }
  if(strlen(host) >= sizeof(addr) - 10){
    printf(2, "netcat: hostname too long\n");
    exit(1);
  }
  strcpy(addr, host);
  append_tcp_port(addr, tcpport);

  sock = socket(AF_INET, SOCK_STREAM, 0);
  if(sock < 0){
    printf(2, "netcat: socket failed\n");
    exit(1);
  }
  if(connect(sock, addr) < 0){
    printf(2, "netcat: connect failed\n");
    close(sock);
    exit(1);
  }
  relay(sock, 0);
  exit(0);
}

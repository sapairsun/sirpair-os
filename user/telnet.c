#include "types.h"
#include "user.h"
#include "usock.h"

/*
 * 单进程轮询：stdin EOF 时勿退出（与 netcat 一致），仍 recv 直至对端关闭。
 * ^C（0x03）立即 close(sock)，单 fd 上发出 FIN，配合内核在监听槽 refcnt>1 时对 FIN 恢复 LISTEN，
 * 才能再次 connect。fork 双 fd 共套接字时子进程先关仍不足以发 FIN，父进程 recv 会死锁。
 *
 * 提示符：
 * - 输出侧仅在「新行」首字节前写一次 "> "，避免同一行被拆包时在行中插入提示符；
 * - On input side, write one "> " when connected, then append the next "> " after each full echoed line.
 *   控制台是行模式，若等 fdready(0) 才打输入提示符，会晚于用户按回车，显示成「第二行错乱」。
 */
static void
relay(int sock)
{
  char buf[256];
  int n;
  int stdin_dead;
  int out_line_start;
  int pending_cr;

  stdin_dead = 0;
  out_line_start = 1;
  pending_cr = 0;
  if(write(1, "> ", 2) != 2)
    return;

  for(;;){
    /*
     * fdready>0：rx 有数据；fdready<0：槽位已释放，须 recv 得 EOF(0)。
     * 内层先排空套接字再读 stdin：否则对端在回显后很快 FIN 时，可能在同一次外层迭代的
     * recv 与 send(stdin) 之间把连接置为不可用，表现为 send 失败（telnet-regress 全场景）。
     */
    {
      int sock_done;
      int show_input_prompt;

      sock_done = 0;
      show_input_prompt = 0;
      while(fdready(sock, 0) != 0){
        int k;
        n = recv(sock, buf, sizeof(buf));
        if(n < 0){
          sock_done = 1;
          break;
        }
        if(n == 0){
          sock_done = 1;
          break;
        }
        k = 0;
        while(k < n){
          if(pending_cr){
            if(buf[k] == '\n'){
              if(write(1, &buf[k], 1) != 1){
                sock_done = 1;
                break;
              }
              k++;
              pending_cr = 0;
              out_line_start = 1;
              continue;
            }
            pending_cr = 0;
            out_line_start = 1;
          }
          if(out_line_start){
            if(write(1, "> ", 2) != 2){
              sock_done = 1;
              break;
            }
            out_line_start = 0;
          }
          if(write(1, &buf[k], 1) != 1){
            sock_done = 1;
            break;
          }
          if(buf[k] == '\n'){
            out_line_start = 1;
            show_input_prompt = 1;
          } else if(buf[k] == '\r'){
            pending_cr = 1;
          }
          k++;
        }
        if(sock_done)
          break;
      }
      if(!sock_done && show_input_prompt && !stdin_dead){
        if(write(1, "> ", 2) != 2)
          sock_done = 1;
      }
      if(sock_done)
        break;
    }
    if(!stdin_dead && fdready(0, 0) > 0){
      n = read(0, buf, sizeof(buf));
      if(n < 0)
        break;
      if(n == 0){
        stdin_dead = 1;
        continue;
      }
      if(n == 1 && (uchar)buf[0] == 0x03){
        close(sock);
        exit(0);
      }
      if(send(sock, buf, n) != n)
        break;
    }
    if(fdready(sock, 0) <= 0 && (stdin_dead || fdready(0, 0) <= 0))
      sleep(1);
  }
  close(sock);
  exit(0);
}

int
main(int argc, char **argv)
{
  int sock;
  char addr[72];
  char *p;

  if(argc < 2 || argc > 3){
    printf(2, "usage: telnet host [port]\n");
    exit(1);
  }
  if(strlen(argv[1]) >= sizeof(addr) - 8){
    printf(2, "telnet: host too long\n");
    exit(1);
  }
  strcpy(addr, argv[1]);
  if(argc == 3){
    if(atoi(argv[2]) <= 0 || atoi(argv[2]) > 65535){
      printf(2, "telnet: bad port\n");
      exit(1);
    }
    p = addr + strlen(addr);
    *p++ = ':';
    strcpy(p, argv[2]);
  }

  sock = socket(AF_INET, SOCK_STREAM, 0);
  if(sock < 0){
    printf(2, "telnet: socket failed\n");
    exit(1);
  }
  if(connect(sock, addr) < 0){
    printf(2, "telnet: connect failed\n");
    close(sock);
    exit(1);
  }
  relay(sock);
  exit(0);
}

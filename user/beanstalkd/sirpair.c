/*
 * 替代 linux.c：用 fdready 轮询实现 sockinit/sockwant/socknext。
 */
#include "dat.h"
#include "types.h"
#include "user.h"

#define MAXSOCK 128

static Socket *socks[MAXSOCK];
static char sock_rw[MAXSOCK];
static int nsock;

int
sockinit(void)
{
  nsock = 0;
  return 0;
}

static int
findslot(Socket *s)
{
  int i;
  for(i = 0; i < nsock; i++){
    if(socks[i] == s)
      return i;
  }
  return -1;
}

int
sockwant(Socket *s, int rw)
{
  int i;

  if(rw == 0){
    i = findslot(s);
    if(i >= 0){
      socks[i] = socks[nsock - 1];
      sock_rw[i] = sock_rw[nsock - 1];
      nsock--;
    }
    s->added = 0;
    return 0;
  }
  i = findslot(s);
  if(i < 0){
    if(nsock >= MAXSOCK)
      return -1;
    i = nsock++;
    socks[i] = s;
    s->added = 1;
  }
  sock_rw[i] = (char)rw;
  return 0;
}

static int
fdready_sock(Socket *s, int rw)
{
  if(rw == 'r'){
    if(fdready(s->fd, 0))
      return 1;
    return 0;
  }
  if(rw == 'w'){
    if(fdready(s->fd, 1))
      return 1;
    return 0;
  }
  if(rw == 'h')
    return 0;
  return 0;
}

int
socknext(Socket **s, int64 timeout)
{
  int64 start, deadline;
  int i;

  if(timeout < 0)
    timeout = 0;
  start = nanoseconds();
  deadline = start + timeout;
  for(;;){
    for(i = 0; i < nsock; i++){
      Socket *so = socks[i];
      if(!so)
        continue;
      if(sock_rw[i] == 'r' && fdready_sock(so, 'r')){
        *s = so;
        return 'r';
      }
      if(sock_rw[i] == 'w' && fdready_sock(so, 'w')){
        *s = so;
        return 'w';
      }
    }
    if(timeout == 0)
      return 0;
    if(nanoseconds() >= deadline)
      return 0;
    sleep(1);
  }
}

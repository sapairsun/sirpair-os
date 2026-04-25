#include "types.h"
#include "user.h"
#include "usock.h"

int
main(void)
{
  int srv, cli, conn;
  int pid;
  char buf[32];
  int n;
  int ok;

  srv = socket(AF_UNIX, SOCK_STREAM, 0);
  if(srv < 0){
    printf(2, "usocktest: socket(server) failed\n");
    exit(0);
  }
  if(bind(srv, "/dev/usock0") < 0 || listen(srv, 4) < 0){
    printf(2, "usocktest: bind/listen failed\n");
    exit(0);
  }

  pid = fork();
  if(pid < 0){
    printf(2, "usocktest: fork failed\n");
    exit(0);
  }

  if(pid == 0){
    cli = socket(AF_UNIX, SOCK_STREAM, 0);
    if(cli < 0){
      printf(2, "usocktest: socket(client) failed\n");
      exit(0);
    }
    if(connect(cli, "/dev/usock0") < 0){
      printf(2, "usocktest: connect failed\n");
      exit(0);
    }
    if(send(cli, "ping", 4) != 4){
      printf(2, "usocktest: send failed\n");
      exit(0);
    }
    memset(buf, 0, sizeof(buf));
    n = recv(cli, buf, 4);
    ok = (n == 4 && buf[0] == 'p' && buf[1] == 'o' && buf[2] == 'n' && buf[3] == 'g');
    if(!ok){
      printf(2, "usocktest: recv mismatch\n");
      exit(0);
    }
    printf(1, "usocktest: client ok\n");
    close(cli);
    exit(0);
  }

  conn = accept(srv);
  if(conn < 0){
    printf(2, "usocktest: accept failed\n");
    exit(0);
  }
  memset(buf, 0, sizeof(buf));
  n = recv(conn, buf, 4);
  ok = (n == 4 && buf[0] == 'p' && buf[1] == 'i' && buf[2] == 'n' && buf[3] == 'g');
  if(!ok){
    printf(2, "usocktest: server recv mismatch\n");
    exit(0);
  }
  if(send(conn, "pong", 4) != 4){
    printf(2, "usocktest: server send failed\n");
    exit(0);
  }
  printf(1, "usocktest: server ok\n");
  close(conn);
  close(srv);
  wait();
  printf(1, "usocktest: pass\n");
  exit(0);
}

#include "dat.h"
#include "beanstalkd_sirpair_shim.h"
#include "types.h"
#include "user.h"

extern struct Server srv;

int
main(int argc, char **argv)
{
  int r;

  (void)argc;
  progname = argv[0];
  optparse(&srv, argv + 1);
  if(srv.addr == 0)
    srv.addr = "unix:/bs";
  r = make_server_socket(srv.addr, srv.port);
  if(r < 0){
    bs_printf(2, "beanstalkd: make_server_socket failed\n");
    exit(111);
  }
  srv.sock.fd = r;
  prot_init();
  srv_acquire_wal(&srv);
  srvserve(&srv);
  exit(0);
}

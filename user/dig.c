#include "types.h"
#include "user.h"

static void
print_ip(uint ip)
{
  int a = (ip >> 24) & 0xFF;
  int b = (ip >> 16) & 0xFF;
  int c = (ip >> 8) & 0xFF;
  int d = ip & 0xFF;
  printf(1, "%d.%d.%d.%d", a, b, c, d);
}

int
main(int argc, char **argv)
{
  uint ip;

  if(argc != 2){
    printf(2, "usage: dig <domain>\n");
    exit(0);
  }
  if(dig(argv[1], &ip) < 0){
    printf(2, "dig: resolve failed: %s\n", argv[1]);
    exit(0);
  }

  printf(1, "%s A ", argv[1]);
  print_ip(ip);
  printf(1, "\n");
  exit(0);
}

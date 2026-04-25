#include "types.h"
#include "user.h"

int
main(int argc, char *argv[])
{
  printf(1, "Rebooting...\n");
  reboot();
  printf(1, "reboot: failed\n");
  exit(0);
}

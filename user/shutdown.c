#include "types.h"
#include "user.h"

int
main(int argc, char *argv[])
{
  printf(1, "Shutting down...\n");
  shutdown();
  printf(1, "shutdown: failed\n");
  exit(0);
}

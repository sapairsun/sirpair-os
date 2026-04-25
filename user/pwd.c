#include "types.h"
#include "user.h"

int
main(int argc, char *argv[])
{
  char path[512];

  if(argc > 1){
    printf(2, "usage: pwd\n");
    exit(0);
  }

  if(getcwd(path, sizeof(path)) < 0){
    printf(2, "pwd: getcwd failed\n");
    exit(0);
  }

  printf(1, "%s\n", path);
  exit(0);
}

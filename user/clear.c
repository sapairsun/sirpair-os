#include "types.h"
#include "user.h"

int
main(int argc, char *argv[])
{
  if(argc > 1){
    printf(2, "usage: clear\n");
    exit(0);
  }

  // ANSI clear screen + cursor home
  printf(1, "\033[2J\033[H");
  exit(0);
}

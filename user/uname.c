#include "types.h"
#include "user.h"
#include "param.h"

int
main(int argc, char *argv[])
{
  if(argc > 1 && strcmp(argv[1], "-a") == 0){
    printf(1, "sirpair sirpair 0.1.0 sirpair-USB i686 i386\n");
  } else {
    printf(1, "sirpair\n");
  }
  exit(0);
}

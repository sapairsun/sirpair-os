#include "types.h"
#include "stat.h"
#include "fcntl.h"
#include "user.h"

int
main(int argc, char *argv[])
{
  int i;
  int fd;

  if(argc < 2){
    printf(2, "Usage: touch files...\n");
    exit(0);
  }

  for(i = 1; i < argc; i++){
    fd = open(argv[i], O_CREATE | O_RDWR);
    if(fd < 0){
      printf(2, "touch: %s failed to create or open\n", argv[i]);
      break;
    }
    close(fd);
  }

  exit(0);
}

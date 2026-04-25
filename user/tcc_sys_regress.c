#include "types.h"
#include "stat.h"
#include "user.h"

static char *srcs[] = {
  "/home/t01.c", "/home/t02.c", "/home/t03.c", "/home/t04.c",
  "/home/t05.c", "/home/t06.c", "/home/t07.c", "/home/t08.c",
};

static char *dsts[] = {
  "/home/t01.o", "/home/t02.o", "/home/t03.o", "/home/t04.o",
  "/home/t05.o", "/home/t06.o", "/home/t07.o", "/home/t08.o",
};

int
main(int argc, char *argv[])
{
  int i, pid;
  struct stat st;
  char *av[] = { "tcc", "-c", "-o", 0, 0, 0 };

  (void)argc;
  (void)argv;

  for(i = 0; i < 8; i++){
    unlink(dsts[i]);
    av[3] = dsts[i];
    av[4] = srcs[i];
    if((pid = fork()) == 0){
      exec("tcc", av);
      printf(2, "tcc_sys_regress: exec tcc failed\n");
      exit(1);
    }
    if(pid < 0){
      printf(2, "tcc_sys_regress: fork failed\n");
      exit(1);
    }
    wait();
    if(stat(dsts[i], &st) < 0 || st.size == 0){
      printf(2, "tcc_sys_regress: missing or empty output %s\n", dsts[i]);
      exit(1);
    }
    printf(1, "TCCSYS%02d\n", i + 1);
  }
  exit(0);
}

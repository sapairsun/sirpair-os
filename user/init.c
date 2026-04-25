// init: The initial user-level program

#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

char *argv[] = { "/bin/sh", 0 };

int
main(void)
{
  int pid, wpid;

  if(open("console", O_RDWR) < 0){
    mknod("console", 1, 1);
    open("console", O_RDWR);
  }
  dup(0);  // stdout
  dup(0);  // stderr

  // Always start user space from root directory.
  chdir("/");

  // 空设备：供 shell 将后台任务 stdin 与控制台分离（避免与前台 sh 竞争 read(0)）
  {
    int fd;

    fd = open("null", O_RDONLY);
    if(fd < 0){
      /* 主设备号 2：include/file.h 中 DEVNULL，与内核 nullinit 一致 */
      if(mknod("null", 2, 0) < 0)
        ;
    } else
      close(fd);
  }

  for(;;){
    printf(1, "init: starting sh \033[32m[OK]\033[0m\n");
    pid = fork();
    if(pid < 0){
      printf(1, "init: fork failed \033[31m[FAILED]\033[0m\n");
      exit(0);
    }
    if(pid == 0){
      exec("/bin/sh", argv);
      printf(1, "init: exec sh failed \033[31m[FAILED]\033[0m\n");
      exit(0);
    }
    /* 回收过继到 init 的后台子进程（如 sh 对 cmd & 先 fork 再 exit 的中间子进程），
     * 与应长期存活的 /bin/sh 区分；勿打印告警：属正常孤儿回收而非错误。 */
    while((wpid=wait()) >= 0 && wpid != pid)
      ;
  }
}

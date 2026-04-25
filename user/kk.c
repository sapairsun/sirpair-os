#include <stdio.h>
#include <stdlib.h>

/*
 * Sirpair 无 C 运行时：exec 把栈上“返回地址”置为 0xffffffff，从 main 直接 return
 * 会跳到该地址并 trap 14。须调用 exit，与其它用户程序一致。
 */
int
main(void)
{
  fprintf(stdout, "OK");
  exit(0);
}

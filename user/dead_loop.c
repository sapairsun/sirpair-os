#include "types.h"
#include "user.h"
#include "param.h"

int
main(void)
{
  int i;

  for(;;){
    // 每轮先做 1000 次空转，再睡眠 1 秒，循环不退出。
    for(i = 0; i < 1000; i++)
      asm volatile("");
    sleep(HZ);
  }
}

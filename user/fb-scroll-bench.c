#include "types.h"
#include "user.h"
#include "param.h"

/*
 * 超一屏滚动压测：
 * 连续打印大量行，统计打印阶段消耗的 ticks。
 * 用于对比不同帧缓冲滚屏策略在真机上的吞吐差异。
 */
int
main(int argc, char *argv[])
{
  int i, n, t0, t1, dt;
  int us_per_line;

  n = 2400;
  if(argc >= 2){
    n = atoi(argv[1]);
    if(n <= 0)
      n = 2400;
    if(n > 20000)
      n = 20000;
  }

  printf(1, "fb-scroll-bench: start lines=%d\n", n);
  t0 = uptime();
  for(i = 0; i < n; i++){
    printf(1, "line %d : 0123456789 abcdefghijklmnopqrstuvwxyz ABCDEFGHIJKLMNOPQRSTUVWXYZ\n", i);
  }
  t1 = uptime();
  dt = t1 - t0;
  if(dt <= 0)
    dt = 1;
  us_per_line = (dt * (1000000 / HZ)) / n;
  printf(1, "fb-scroll-bench: lines %d | delta %d ticks | us_per_line %d\n",
         n, dt, us_per_line);
  exit(0);
}

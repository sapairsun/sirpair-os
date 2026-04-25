#include "types.h"
#include "user.h"

extern unsigned char _binary_gui_demo_raw_start[];
extern unsigned char _binary_gui_demo_raw_end[];

int
main(void)
{
  int len = (int)(_binary_gui_demo_raw_end - _binary_gui_demo_raw_start);

  if(gui((void*)_binary_gui_demo_raw_start, len) < 0){
    printf(2, "gui: graphics mode or render failed\n");
    exit(0);
  }

  // 保持进程存活，避免回到文本 shell 后图形页立即被覆盖。
  for(;;)
    sleep(1000);
}

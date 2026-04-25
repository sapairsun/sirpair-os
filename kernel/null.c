// 空设备：读立即返回 0（EOF），写丢弃数据。供 shell 后台任务重定向 stdin，避免与控制台竞争输入。

#include "types.h"
#include "defs.h"
#include "fs.h"
#include "file.h"

int
nullread(struct inode *ip, char *dst, int n)
{
  iunlock(ip);
  (void)dst;
  (void)n;
  ilock(ip);
  return 0;
}

int
nullwrite(struct inode *ip, char *buf, int n)
{
  iunlock(ip);
  (void)buf;
  ilock(ip);
  return n;
}

void
nullinit(void)
{
  devsw[DEVNULL].read = nullread;
  devsw[DEVNULL].write = nullwrite;
}

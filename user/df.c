#include "types.h"
#include "user.h"
#include "fs.h"

static void
print_uint_r(int fd, uint v, int width)
{
  char tmp[16];
  int i, n, pad;
  uint x;

  x = v;
  i = 0;
  do {
    tmp[i++] = '0' + (x % 10);
    x /= 10;
  } while (x != 0 && i < (int)sizeof(tmp));
  n = i;
  for (pad = width - n; pad > 0; pad--)
    write(fd, " ", 1);
  while (--i >= 0)
    write(fd, &tmp[i], 1);
}

int
main(int argc, char *argv[])
{
  struct statfs st;
  uint total1k, used1k, avail1k, usedblk;
  int pct;

  (void)argv;
  if (argc > 1) {
    printf(2, "usage: df\n");
    exit(1);
  }

  if (statfs(&st) < 0) {
    printf(2, "df: statfs failed\n");
    exit(1);
  }

  if (st.f_blocks == 0) {
    printf(2, "df: invalid superblock\n");
    exit(1);
  }

  total1k = (st.f_blocks * 512U) / 1024U;
  usedblk = st.f_blocks - st.f_bfree;
  used1k = (usedblk * 512U) / 1024U;
  avail1k = (st.f_bavail * 512U) / 1024U;
  pct = (int)((100U * usedblk) / st.f_blocks);

  printf(1, "Filesystem     1K-blocks      Used Available Use%% Mounted on\n");
  write(1, "/dev/root      ", 15);
  print_uint_r(1, total1k, 12);
  write(1, " ", 1);
  print_uint_r(1, used1k, 12);
  write(1, " ", 1);
  print_uint_r(1, avail1k, 12);
  printf(1, " %d%% /\n", pct);
  exit(0);
}

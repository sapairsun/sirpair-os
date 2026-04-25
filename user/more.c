#include "types.h"
#include "stat.h"
#include "fcntl.h"
#include "user.h"

#define BUF_SZ 512
#define PAGE_LINES 23
#define READ_MAX (4 * 1024 * 1024)

/* 与 kernel/kbd.h 中 extended 键一致，供 read(/console) 返回 */
#define KEY_UP  0xE2
#define KEY_DN  0xE3

/* 必须用 /console：相对路径在 /bin 等目录下会失败；管道场景下 fd0 是管道读端 */
static int
open_console_tty(void)
{
  int fd;

  fd = open("/console", O_RDONLY);
  if(fd < 0)
    printf(2, "more: cannot open /console\n");
  return fd;
}

/* 与 vi 一致：关回显 + 原始输入，单键可读且方向键不刷屏 */
static void
set_input_echo(int on)
{
  if(on){
    printf(1, "\033[901h");
    printf(1, "\033[902l");
  } else {
    printf(1, "\033[901l");
    printf(1, "\033[902h");
  }
}

static char *
read_all(int fd, int *plen)
{
  char *buf;
  int tot, cap, n;
  char tmp[BUF_SZ];

  buf = 0;
  tot = 0;
  cap = 0;
  while((n = read(fd, tmp, sizeof(tmp))) > 0){
    if(tot + n > READ_MAX)
      n = READ_MAX - tot;
    if(n <= 0)
      break;
    if(tot + n > cap){
      cap = cap ? cap * 2 : 8192;
      while(tot + n > cap)
        cap *= 2;
      if(cap > READ_MAX)
        cap = READ_MAX;
      buf = realloc(buf, cap);
      if(buf == 0)
        return 0;
    }
    memmove(buf + tot, tmp, n);
    tot += n;
  }
  if(n < 0){
    free(buf);
    *plen = 0;
    return 0;
  }
  if(buf == 0){
    buf = malloc(1);
    if(buf == 0)
      return 0;
    tot = 0;
  }
  buf = realloc(buf, tot + 1);
  if(buf == 0)
    return 0;
  buf[tot] = 0;
  *plen = tot;
  return buf;
}

/* 将 blob 按 \\n 切分为行指针（原地写 0），返回行数；失败返回 -1 */
static int
split_lines(char *blob, int len, char ***outlines)
{
  int i, nlines, idx;
  char **lines;

  *outlines = 0;
  if(len <= 0)
    return 0;

  nlines = 1;
  for(i = 0; i < len; i++){
    if(blob[i] == '\n')
      nlines++;
  }

  lines = malloc(sizeof(char*) * nlines);
  if(lines == 0)
    return -1;

  idx = 0;
  lines[idx++] = blob;
  for(i = 0; i < len; i++){
    if(blob[i] == '\n'){
      blob[i] = 0;
      lines[idx++] = blob + i + 1;
    }
  }
  *outlines = lines;
  return nlines;
}

static void
redraw_window(char **lines, int nlines, int first)
{
  int i, end;

  printf(1, "\033[2J\033[H");
  end = first + PAGE_LINES;
  if(end > nlines)
    end = nlines;
  for(i = first; i < end; i++)
    printf(1, "%s\n", lines[i]);
}

static int
pager(int fd, int ttyfd)
{
  char *blob;
  int len;
  char **lines;
  int nlines;
  int first, maxfirst;
  /* 须用无符号字节：方向键为 0xE2/0xE3，有符号 char 会与 KEY_* 比较恒假 */
  uchar uc;

  blob = read_all(fd, &len);
  if(blob == 0)
    return -1;

  nlines = split_lines(blob, len, &lines);
  if(nlines < 0){
    free(blob);
    return -1;
  }
  if(nlines == 0){
    free(lines);
    free(blob);
    return 0;
  }

  if(nlines <= PAGE_LINES){
    int i;

    for(i = 0; i < nlines; i++)
      printf(1, "%s\n", lines[i]);
    free(lines);
    free(blob);
    return 0;
  }

  maxfirst = nlines - PAGE_LINES;
  first = 0;

  set_input_echo(0);
  redraw_window(lines, nlines, first);

  /*
   * 方向键：内核 PS/2 路径为单字节 KEY_UP/KEY_DN；串口或部分终端为 ANSI
   * ESC [ A/B（或带数字前缀的 CSI），与 vi.c、game.c 一致须同时支持。
   */
  {
    int esc_state; /* 0 普通 1 已见 esc 2 csi 内 3 已见 esc O */

    esc_state = 0;
    for(;;){
      if(read(ttyfd, &uc, 1) != 1)
        break;
      if(uc == 'q' || uc == 'Q')
        break;

      if(esc_state == 0){
        if(uc == '\n' || uc == '\r' || uc == KEY_DN){
          if(first < maxfirst){
            first++;
            redraw_window(lines, nlines, first);
          }
        } else if(uc == KEY_UP){
          if(first > 0){
            first--;
            redraw_window(lines, nlines, first);
          }
        } else if(uc == 0x1b)
          esc_state = 1;
        continue;
      }

      if(esc_state == 1){
        if(uc == '[')
          esc_state = 2;
        else if(uc == 'O')
          esc_state = 3;
        else
          esc_state = 0;
        continue;
      }

      if(esc_state == 2){
        /* CSI：跳过参数，直到 A(上) B(下) */
        if(uc == 'A'){
          if(first > 0){
            first--;
            redraw_window(lines, nlines, first);
          }
        } else if(uc == 'B'){
          if(first < maxfirst){
            first++;
            redraw_window(lines, nlines, first);
          }
        } else if((uc >= '0' && uc <= '9') || uc == ';' || uc == '?')
          continue;
        esc_state = 0;
        continue;
      }

      if(esc_state == 3){
        if(uc == 'A'){
          if(first > 0){
            first--;
            redraw_window(lines, nlines, first);
          }
        } else if(uc == 'B'){
          if(first < maxfirst){
            first++;
            redraw_window(lines, nlines, first);
          }
        }
        esc_state = 0;
        continue;
      }

      esc_state = 0;
    }
  }

  set_input_echo(1);
  free(lines);
  free(blob);
  return 0;
}

int
main(int argc, char *argv[])
{
  int i, fd, rc = 0, ttyfd;

  if(argc == 1){
    ttyfd = open_console_tty();
    if(ttyfd < 0)
      exit(0);
    if(pager(0, ttyfd) < 0){
      printf(2, "more: read error\n");
      close(ttyfd);
      exit(0);
    }
    close(ttyfd);
    exit(0);
  }

  for(i = 1; i < argc; i++){
    ttyfd = open_console_tty();
    if(ttyfd < 0){
      rc = -1;
      continue;
    }
    fd = open(argv[i], O_RDONLY);
    if(fd < 0){
      printf(2, "more: cannot open %s\n", argv[i]);
      close(ttyfd);
      rc = -1;
      continue;
    }
    if(pager(fd, ttyfd) < 0){
      printf(2, "more: read error %s\n", argv[i]);
      close(fd);
      close(ttyfd);
      rc = -1;
      continue;
    }
    close(fd);
    close(ttyfd);
  }

  if(rc < 0)
    exit(0);
  exit(0);
}

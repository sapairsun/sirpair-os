/*
 * Terminal vi for Sirpair: read/write/printf and ANSI escapes (ported from vi.c).
 */

#include "types.h"
#include "stat.h"
#include "fcntl.h"
#include "user.h"

#define VI_MAX_LINES    200
#define VI_LINE_SIZE    256
/* set nu 时左侧行号区：3 位数字 + 1 空格，与正文列对齐 */
#define VI_NU_GUTTER    4
#define VI_FILENAME_MAX 256
#define VI_FILE_IO_MAX  16384

#define VI_MODE_NORMAL  0
#define VI_MODE_INSERT  1
#define VI_MODE_COMMAND 2

#define KEY_ESC         27
#define K_UP            300
#define K_DOWN          301
#define K_LEFT          302
#define K_RIGHT         303
#define K_HOME          304
#define K_END           305
#define K_PAGEUP        306
#define K_PAGEDN        307
#define K_DELETE        308

/*
 * 与 include/kbd.h 中 KEY_* 一致：PS/2 方向键等由内核转为单字节传入，
 * 并非串口终端常见的 ESC [ 序列，read_key 须同时识别两类来源。
 */
#define KBD_KEY_HOME    0xE0
#define KBD_KEY_END     0xE1
#define KBD_KEY_UP      0xE2
#define KBD_KEY_DOWN    0xE3
#define KBD_KEY_LEFT    0xE4
#define KBD_KEY_RIGHT   0xE5
#define KBD_KEY_PGUP    0xE6
#define KBD_KEY_PGDN    0xE7
#define KBD_KEY_DEL     0xE9

#define C_TEXT   "\033[0;37m"
#define C_STATUS "\033[0;46m\033[30m"
#define C_CMD    "\033[1;37m"
#define C_MSG    "\033[1;33m"
#define C_ERR    "\033[1;31m"
#define C_TILDE  "\033[0;34m"
#define C_LINENO "\033[32m"
#define C_RESET  "\033[0m"

static char lines[VI_MAX_LINES][VI_LINE_SIZE];
static int line_count;
static int cx, cy;
static int scroll_y;
static int mode;
static int modified;
static char filename[VI_FILENAME_MAX];
static char statusmsg[80];
static int status_is_err;
static char cmdline[80];
static int cmdpos;
static int running;
static int last_key;
static uchar file_buf[VI_FILE_IO_MAX];
static uchar save_buf[VI_FILE_IO_MAX];
static int opt_nu;
/* 动态屏幕尺寸（基于 consize 系统调用） */
static int vi_screen_rows = 20;  /* 文本区，不含状态行与命令行 */
static int vi_screen_cols = 80;
/* read_key：ESC 后若非 CSI（非 [），第二字节需留给下一次调用，否则 : 等键会丢失 */
static int read_key_pending = -1;

static void
clear_screen(void)
{
  printf(1, "\033[2J\033[H");
}

static void
vi_refresh_screen_size(void)
{
  int rows, cols;

  rows = 25;
  cols = 80;
  if(consize(&rows, &cols) < 0){
    rows = 25;
    cols = 80;
  }
  if(rows < 6)
    rows = 6;
  if(cols < 20)
    cols = 20;

  /*
   * 预留 2 行：状态栏 + 命令/消息行；再减 1 行作为换行预算：
   * 每帧共输出 (vi_screen_rows + 2) 个换行；若该值等于 consize 行数，
   * 内核 cgaputc 会在最后一换行时滚屏，首行正文被卷出（表现为「少第一行」）。
   * 故编辑区 = 总行数 - 2 - 1 = 总行数 - 3。
   */
  vi_screen_rows = rows - 3;
  if(vi_screen_rows < 3)
    vi_screen_rows = 3;
  vi_screen_cols = cols;
}

static void
set_input_echo(int on)
{
  /*
   * 901: 回显开关（见 kernel/console.c）
   * 902: 原始输入；关回显时必须开 902h，否则 consoleread 只在换行时唤醒，
   *      单键 read 会一直阻塞（表现为 e/i 无法进入插入态）。
   */
  if(on){
    printf(1, "\033[901h");
    printf(1, "\033[902l");
  } else {
    printf(1, "\033[901l");
    printf(1, "\033[902h");
  }
}

static void
strncpy0(char *d, const char *s, int n)
{
  int i;

  if(n <= 0)
    return;
  for(i = 0; i < n - 1 && s[i]; i++)
    d[i] = s[i];
  d[i] = '\0';
}

static void
itoa10(int v, char *out, int omax)
{
  char tmp[16];
  int i, j, neg;
  uint u;

  if(omax < 2){
    out[0] = 0;
    return;
  }
  neg = 0;
  if(v < 0){
    neg = 1;
    u = (uint)(-v);
  } else
    u = (uint)v;
  i = 0;
  do{
    tmp[i++] = '0' + (u % 10);
    u /= 10;
  }while(u > 0 && i < (int)sizeof(tmp));
  j = 0;
  if(neg)
    out[j++] = '-';
  while(i > 0 && j < omax - 1)
    out[j++] = tmp[--i];
  out[j] = '\0';
}

static void
line_append(char *dst, char *src)
{
  int ld, i;

  ld = strlen(dst);
  for(i = 0; src[i] && ld + i < VI_LINE_SIZE - 1; i++)
    dst[ld + i] = src[i];
  dst[ld + i] = '\0';
}

/*
 * 与 kernel/console.c consputc 中 \t 一致：按 8 列制表位展开宽度。
 * 用于光标列与屏幕绘制对齐（缓冲区一字节 \t 在屏上占多列）。
 */
static int
vi_visual_col_from_buffer(int row, int buf_col)
{
  const char *s;
  int i, vis, len;

  if(row < 0 || row >= line_count)
    return 0;
  s = lines[row];
  len = strlen(s);
  if(buf_col > len)
    buf_col = len;
  vis = 0;
  for(i = 0; i < buf_col; i++){
    if(s[i] == '\t'){
      int n = 8 - (vis % 8);
      if(n == 0)
        n = 8;
      vis += n;
    } else
      vis++;
  }
  return vis;
}

/*
 * CSI：已读完 ESC [，继续读到最终字节 0x40–0x7E（ECMA-48），避免残留字节被当成可打印字符。
 * 方向键带修饰（如 ESC [ 1 ; 5 A）仍映射为方向键。
 */
static int
read_csi_after_lbracket(void)
{
  uchar c, fin;
  uchar prefix[62];
  int plen, i, np, v, d, j;
  int params[8];

  plen = 0;
  for(;;){
    if(read(0, &c, 1) != 1)
      return KEY_ESC;
    if(c >= 0x40 && c <= 0x7E){
      fin = c;
      break;
    }
    if(plen >= (int)sizeof(prefix))
      return KEY_ESC;
    prefix[plen++] = c;
  }

  if(fin == 'A')
    return K_UP;
  if(fin == 'B')
    return K_DOWN;
  if(fin == 'C')
    return K_RIGHT;
  if(fin == 'D')
    return K_LEFT;

  /* 仅数字与分号的参数前缀，供 H/f/~ 判断；含 ? 等则视为非光标序列 */
  np = 0;
  v = 0;
  d = 0;
  for(i = 0; i < plen; i++){
    c = prefix[i];
    if(c >= '0' && c <= '9'){
      v = v * 10 + (c - '0');
      d = 1;
    } else if(c == ';' || c == ':'){
      if(np < 8){
        if(d)
          params[np++] = v;
        else
          params[np++] = 0;
      }
      v = 0;
      d = 0;
    } else
      return KEY_ESC;
  }
  if(d && np < 8)
    params[np++] = v;

  if(fin == 'H' || fin == 'f'){
    /* 无参数或单参数 1：部分终端的 Home；两参数为光标定位，忽略 */
    if(np == 0)
      return K_HOME;
    if(np == 1 && params[0] == 1)
      return K_HOME;
    return KEY_ESC;
  }
  if(fin == 'F')
    return K_END;

  if(fin == '~'){
    for(j = 0; j < np; j++){
      if(params[j] == 5)
        return K_PAGEUP;
      if(params[j] == 6)
        return K_PAGEDN;
      if(params[j] == 3)
        return K_DELETE;
    }
    return KEY_ESC;
  }
  return KEY_ESC;
}

static int
read_key(void)
{
  uchar c, c2, c3;

  if(read_key_pending >= 0){
    c = read_key_pending;
    read_key_pending = -1;
  } else if(read(0, &c, 1) != 1)
    return -1;

  switch(c){
  case KBD_KEY_HOME:
    return K_HOME;
  case KBD_KEY_END:
    return K_END;
  case KBD_KEY_UP:
    return K_UP;
  case KBD_KEY_DOWN:
    return K_DOWN;
  case KBD_KEY_LEFT:
    return K_LEFT;
  case KBD_KEY_RIGHT:
    return K_RIGHT;
  case KBD_KEY_PGUP:
    return K_PAGEUP;
  case KBD_KEY_PGDN:
    return K_PAGEDN;
  case KBD_KEY_DEL:
    return K_DELETE;
  default:
    break;
  }

  if(c != KEY_ESC)
    return c;
  if(read(0, &c2, 1) != 1)
    return KEY_ESC;
  /*
   * 应用光标键模式（SS3）：ESC O A/B/C/D，否则方向键第二字节被当成普通字符插入。
   */
  if(c2 == 'O'){
    if(read(0, &c3, 1) != 1)
      return KEY_ESC;
    if(c3 == 'A')
      return K_UP;
    if(c3 == 'B')
      return K_DOWN;
    if(c3 == 'C')
      return K_RIGHT;
    if(c3 == 'D')
      return K_LEFT;
    if(c3 == 'H')
      return K_HOME;
    if(c3 == 'F')
      return K_END;
    read_key_pending = c3;
    return KEY_ESC;
  }
  if(c2 != '['){
    read_key_pending = c2;
    return KEY_ESC;
  }
  return read_csi_after_lbracket();
}

static void
vi_set_status(const char *msg)
{
  strncpy0(statusmsg, msg, sizeof(statusmsg));
  status_is_err = 0;
}

static void
vi_set_error(const char *msg)
{
  strncpy0(statusmsg, msg, sizeof(statusmsg));
  status_is_err = 1;
}

static void
vi_print_lineno_gutter(int lineno)
{
  char nb[8];
  int nd, i;

  itoa10(lineno, nb, sizeof(nb));
  nd = strlen(nb);
  printf(1, "%s", C_LINENO);
  for(i = 0; i < VI_NU_GUTTER - 1 - nd; i++)
    printf(1, " ");
  printf(1, "%s ", nb);
  printf(1, "%s", C_RESET);
}

static void
vi_draw_screen(void)
{
  int y, fr, len, i;
  char num[16];
  int screen_y, screen_x;
  int text_w;

  text_w = opt_nu ? (vi_screen_cols - VI_NU_GUTTER) : vi_screen_cols;
  if(text_w < 8)
    text_w = 8;

  clear_screen();

  for(y = 0; y < vi_screen_rows; y++){
    fr = scroll_y + y;
    if(fr < line_count){
      len = strlen(lines[fr]);
      if(len > text_w)
        len = text_w;
      if(opt_nu)
        vi_print_lineno_gutter(fr + 1);
      for(i = 0; i < len; i++)
        printf(1, "%c", lines[fr][i]);
      printf(1, "\n");
    } else {
      if(opt_nu){
        int j;
        for(j = 0; j < VI_NU_GUTTER; j++)
          printf(1, " ");
      }
      printf(1, "%s~\033[0m\n", C_TILDE);
    }
  }

  printf(1, "%s", C_STATUS);
  if(mode == VI_MODE_INSERT)
    printf(1, " -- INSERT -- ");
  else
    printf(1, " ");

  if(filename[0]){
    const char *name = filename;
    char *sl;

    sl = strchr(filename, '/');
    if(sl && sl[1])
      name = sl + 1;
    printf(1, "%s", name);
  } else
    printf(1, "[No Name]");

  if(modified)
    printf(1, " [+]");

  itoa10(cy + 1, num, sizeof(num));
  printf(1, "  Ln %s/", num);
  itoa10(line_count, num, sizeof(num));
  printf(1, "%s", num);
  printf(1, ", Col ");
  itoa10(vi_visual_col_from_buffer(cy, cx) + 1, num, sizeof(num));
  printf(1, "%s", num);
  printf(1, "%s\n", C_RESET);

  if(mode == VI_MODE_COMMAND){
    printf(1, "%s:%s%s\n", C_CMD, cmdline, C_RESET);
    screen_x = 1 + cmdpos + 1;
    if(screen_x < 1)
      screen_x = 1;
    printf(1, "\033[%d;%dH", vi_screen_rows + 2, screen_x);
  } else if(statusmsg[0]){
    if(status_is_err)
      printf(1, "%s%s%s\n", C_ERR, statusmsg, C_RESET);
    else
      printf(1, "%s%s%s\n", C_MSG, statusmsg, C_RESET);
  } else
    printf(1, "\n");

  if(mode != VI_MODE_COMMAND){
    screen_y = cy - scroll_y;
    screen_x = vi_visual_col_from_buffer(cy, cx) + 1;
    if(opt_nu)
      screen_x += VI_NU_GUTTER;
    if(screen_y >= 0 && screen_y < vi_screen_rows){
      if(screen_x < 1)
        screen_x = 1;
      if(screen_x > vi_screen_cols)
        screen_x = vi_screen_cols;
      printf(1, "\033[%d;%dH", screen_y + 1, screen_x);
    }
  }
}

static void
vi_scroll(void)
{
  if(cy < scroll_y)
    scroll_y = cy;
  if(cy >= scroll_y + vi_screen_rows)
    scroll_y = cy - vi_screen_rows + 1;
  if(scroll_y < 0)
    scroll_y = 0;
}

static void
vi_clamp_cursor(void)
{
  int line_len;

  if(cy < 0)
    cy = 0;
  if(line_count > 0 && cy >= line_count)
    cy = line_count - 1;
  if(line_count == 0)
    cy = 0;

  line_len = strlen(lines[cy]);

  if(mode == VI_MODE_NORMAL){
    if(line_len > 0 && cx >= line_len)
      cx = line_len - 1;
    if(line_len == 0)
      cx = 0;
  } else {
    if(cx > line_len)
      cx = line_len;
  }
  if(cx < 0)
    cx = 0;
}

static void
vi_insert_char(char c)
{
  int len;
  int i;

  if(cy >= VI_MAX_LINES)
    return;
  len = strlen(lines[cy]);
  if(len >= VI_LINE_SIZE - 2)
    return;
  for(i = len; i >= cx; i--)
    lines[cy][i + 1] = lines[cy][i];
  lines[cy][cx] = c;
  cx++;
  modified = 1;
}

static void
vi_delete_char_at(int row, int col)
{
  int len;
  int i;

  if(row < 0 || row >= line_count)
    return;
  len = strlen(lines[row]);
  if(col < 0 || col >= len)
    return;
  for(i = col; i < len; i++)
    lines[row][i] = lines[row][i + 1];
  modified = 1;
}

static void
vi_join_line_with_next(int row)
{
  int cur_len, next_len;
  int i;

  if(row < 0 || row >= line_count - 1)
    return;
  cur_len = strlen(lines[row]);
  next_len = strlen(lines[row + 1]);
  if(cur_len + next_len >= VI_LINE_SIZE - 1)
    return;
  line_append(lines[row], lines[row + 1]);
  for(i = row + 1; i < line_count - 1; i++)
    strcpy(lines[i], lines[i + 1]);
  line_count--;
  modified = 1;
}

static void
vi_backspace(void)
{
  if(cx > 0){
    vi_delete_char_at(cy, cx - 1);
    cx--;
  } else if(cy > 0){
    int prev_len;

    prev_len = strlen(lines[cy - 1]);
    cy--;
    cx = prev_len;
    vi_join_line_with_next(cy);
  }
}

static void
vi_insert_newline(void)
{
  int i;

  if(line_count >= VI_MAX_LINES)
    return;
  for(i = line_count; i > cy + 1; i--)
    strcpy(lines[i], lines[i - 1]);
  line_count++;
  strcpy(lines[cy + 1], lines[cy] + cx);
  lines[cy][cx] = '\0';
  cy++;
  cx = 0;
  modified = 1;
}

static void
vi_delete_line(void)
{
  int i;

  if(line_count == 1){
    lines[0][0] = '\0';
    cx = 0;
    modified = 1;
    return;
  }
  for(i = cy; i < line_count - 1; i++)
    strcpy(lines[i], lines[i + 1]);
  line_count--;
  if(cy >= line_count)
    cy = line_count - 1;
  modified = 1;
}

static void
vi_open_line_below(void)
{
  int i;

  if(line_count >= VI_MAX_LINES)
    return;
  for(i = line_count; i > cy + 1; i--)
    strcpy(lines[i], lines[i - 1]);
  line_count++;
  lines[cy + 1][0] = '\0';
  cy++;
  cx = 0;
  mode = VI_MODE_INSERT;
  modified = 1;
}

static void
vi_open_line_above(void)
{
  int i;

  if(line_count >= VI_MAX_LINES)
    return;
  for(i = line_count; i > cy; i--)
    strcpy(lines[i], lines[i - 1]);
  line_count++;
  lines[cy][0] = '\0';
  cx = 0;
  mode = VI_MODE_INSERT;
  modified = 1;
}

static int
vi_load_file(const char *path)
{
  int fd, n, i, col, bytes_read;
  char line[VI_LINE_SIZE];

  line_count = 0;
  col = 0;
  fd = open((char*)path, O_RDONLY);
  if(fd < 0){
    line_count = 1;
    lines[0][0] = '\0';
    modified = 0;
    vi_set_status("[New File]");
    return 0;
  }

  bytes_read = 0;
  while((n = read(fd, file_buf, sizeof(file_buf))) > 0){
    bytes_read += n;
    for(i = 0; i < n && line_count < VI_MAX_LINES; i++){
      if(file_buf[i] == '\n'){
        line[col] = '\0';
        strcpy(lines[line_count], line);
        line_count++;
        col = 0;
      } else if(file_buf[i] == '\r')
        continue;
      else if(file_buf[i] == '\t'){
        int s, t;

        s = 4 - (col % 4);
        for(t = 0; t < s && col < VI_LINE_SIZE - 1; t++)
          line[col++] = ' ';
      } else {
        if(col < VI_LINE_SIZE - 1)
          line[col++] = file_buf[i];
      }
    }
  }
  close(fd);
  if(n < 0){
    vi_set_error("Read failed");
    return -1;
  }

  if(col > 0 || line_count == 0){
    line[col] = '\0';
    strcpy(lines[line_count], line);
    line_count++;
  }
  if(line_count == 0){
    line_count = 1;
    lines[0][0] = '\0';
  }
  modified = 0;
  vi_set_status("Loaded");
  (void)bytes_read;
  return 0;
}

static int
vi_save_file(void)
{
  int fd, i, len;
  uint pos;

  if(!filename[0]){
    vi_set_error("No file name");
    return -1;
  }

  pos = 0;
  for(i = 0; i < line_count; i++){
    len = strlen(lines[i]);
    if(pos + (uint)len + 2 > sizeof(save_buf)){
      vi_set_error("File too large to save");
      return -1;
    }
    memmove(save_buf + pos, lines[i], len);
    pos += len;
    save_buf[pos++] = '\n';
  }

  fd = open(filename, O_CREATE | O_WRONLY | O_TRUNC);
  if(fd < 0){
    vi_set_error("Cannot open for write");
    return -1;
  }
  if((int)pos > 0 && write(fd, save_buf, (int)pos) != (int)pos){
    close(fd);
    vi_set_error("Write failed");
    return -1;
  }
  close(fd);
  modified = 0;
  vi_set_status("Written");
  return 0;
}

static int
vi_save_as(const char *new_path)
{
  strncpy0(filename, new_path, sizeof(filename));
  return vi_save_file();
}

static void
vi_process_normal(int key)
{
  int len;

  statusmsg[0] = '\0';

  switch(key){
  case 'h':
  case K_LEFT:
    if(cx > 0)
      cx--;
    break;
  case 'j':
  case K_DOWN:
    if(cy < line_count - 1)
      cy++;
    break;
  case 'k':
  case K_UP:
    if(cy > 0)
      cy--;
    break;
  case 'l':
  case K_RIGHT:
    len = strlen(lines[cy]);
    if(len > 0 && cx < len - 1)
      cx++;
    break;
  case '0':
  case K_HOME:
    cx = 0;
    break;
  case '$':
  case K_END:
    len = strlen(lines[cy]);
    cx = (len > 0) ? len - 1 : 0;
    break;
  case '^':
    len = strlen(lines[cy]);
    cx = 0;
    while(cx < len && (lines[cy][cx] == ' ' || lines[cy][cx] == '\t'))
      cx++;
    break;
  case 'G':
    if(line_count > 0)
      cy = line_count - 1;
    break;
  case 'g':
    if(last_key == 'g'){
      cy = 0;
      last_key = 0;
      return;
    }
    break;
  case 'w':
    len = strlen(lines[cy]);
    while(cx < len && lines[cy][cx] != ' ')
      cx++;
    while(cx < len && lines[cy][cx] == ' ')
      cx++;
    if(cx >= len && cy < line_count - 1){
      cy++;
      cx = 0;
      len = strlen(lines[cy]);
      while(cx < len && lines[cy][cx] == ' ')
        cx++;
    }
    break;
  case 'b':
    if(cx == 0 && cy > 0){
      cy--;
      cx = strlen(lines[cy]);
    }
    if(cx > 0)
      cx--;
    while(cx > 0 && lines[cy][cx] == ' ')
      cx--;
    while(cx > 0 && lines[cy][cx - 1] != ' ')
      cx--;
    break;
  case K_PAGEUP:
    cy -= vi_screen_rows;
    if(cy < 0)
      cy = 0;
    scroll_y -= vi_screen_rows;
    if(scroll_y < 0)
      scroll_y = 0;
    break;
  case K_PAGEDN:
    cy += vi_screen_rows;
    if(cy >= line_count)
      cy = line_count - 1;
    break;
  case 'i':
  case 'e':
    /* 'e' enters insert at cursor */
    mode = VI_MODE_INSERT;
    break;
  case 'I':
    mode = VI_MODE_INSERT;
    len = strlen(lines[cy]);
    cx = 0;
    while(cx < len && (lines[cy][cx] == ' ' || lines[cy][cx] == '\t'))
      cx++;
    break;
  case 'a':
    mode = VI_MODE_INSERT;
    len = strlen(lines[cy]);
    if(len > 0)
      cx++;
    break;
  case 'A':
    mode = VI_MODE_INSERT;
    cx = strlen(lines[cy]);
    break;
  case 'o':
    vi_open_line_below();
    break;
  case 'O':
    vi_open_line_above();
    break;
  case 'x':
    len = strlen(lines[cy]);
    if(len > 0 && cx < len){
      vi_delete_char_at(cy, cx);
      len = strlen(lines[cy]);
      if(len > 0 && cx >= len)
        cx = len - 1;
      if(len == 0)
        cx = 0;
    }
    break;
  case 'X':
    if(cx > 0){
      vi_delete_char_at(cy, cx - 1);
      cx--;
    }
    break;
  case 'd':
    if(last_key == 'd'){
      vi_delete_line();
      last_key = 0;
      return;
    }
    break;
  case 'J':
    if(cy < line_count - 1){
      len = strlen(lines[cy]);
      if(len > 0 && len < VI_LINE_SIZE - 2){
        lines[cy][len] = ' ';
        lines[cy][len + 1] = '\0';
      }
      cx = strlen(lines[cy]);
      vi_join_line_with_next(cy);
    }
    break;
  case 'r': {
    int rkey;

    set_input_echo(1);
    rkey = read_key();
    set_input_echo(0);
    if(rkey >= 32 && rkey < 127){
      len = strlen(lines[cy]);
      if(cx < len){
        lines[cy][cx] = (char)rkey;
        modified = 1;
      }
    }
    break;
  }
  case 'Z':
    if(last_key == 'Z'){
      if(modified)
        vi_save_file();
      running = 0;
      last_key = 0;
      return;
    }
    break;
  case ':':
    mode = VI_MODE_COMMAND;
    cmdpos = 0;
    cmdline[0] = '\0';
    break;
  case KEY_ESC:
    statusmsg[0] = '\0';
    break;
  default:
    break;
  }
  last_key = key;
}

static int
cmdline_is_all_digits(const char *p)
{
  if(!p || !p[0])
    return 0;
  for(; *p; p++){
    if(*p < '0' || *p > '9')
      return 0;
  }
  return 1;
}

static int
cmdline_to_positive_int(const char *p)
{
  int n;

  n = 0;
  while(*p >= '0' && *p <= '9'){
    int d;

    d = *p - '0';
    if(n > (2147483647 - d) / 10)
      return 2147483647;
    n = n * 10 + d;
    p++;
  }
  return n;
}

static void
vi_goto_line_1based(int ln)
{
  if(line_count < 1)
    return;
  if(ln < 1)
    ln = 1;
  if(ln > line_count)
    ln = line_count;
  cy = ln - 1;
  cx = 0;
}

static void
vi_process_insert(int key)
{
  statusmsg[0] = '\0';

  switch(key){
  case KEY_ESC:
    mode = VI_MODE_NORMAL;
    if(cx > 0)
      cx--;
    break;
  case '\b':
  case 0x7f:
    vi_backspace();
    break;
  case K_DELETE: {
    int len;

    len = strlen(lines[cy]);
    if(cx < len)
      vi_delete_char_at(cy, cx);
    else if(cy < line_count - 1)
      vi_join_line_with_next(cy);
    break;
  }
  case '\r':
  case '\n':
    vi_insert_newline();
    break;
  case K_UP:
    if(cy > 0)
      cy--;
    break;
  case K_DOWN:
    if(cy < line_count - 1)
      cy++;
    break;
  case K_LEFT:
    if(cx > 0)
      cx--;
    break;
  case K_RIGHT: {
    int len;

    len = strlen(lines[cy]);
    if(cx < len)
      cx++;
    break;
  }
  case K_HOME:
    cx = 0;
    break;
  case K_END:
    cx = strlen(lines[cy]);
    break;
  case '\t':
    vi_insert_char(' ');
    vi_insert_char(' ');
    vi_insert_char(' ');
    vi_insert_char(' ');
    break;
  default:
    if(key >= 32 && key < 127)
      vi_insert_char((char)key);
    break;
  }
}

static void
vi_execute_command(void)
{
  char *p;
  mode = VI_MODE_NORMAL;
  if(cmdpos == 0)
    return;
  cmdline[cmdpos] = '\0';

  p = cmdline;
  while(*p == ' ')
    p++;

  /*
   * :N（N 为正整数）跳转到第 N 行（1 起计）；超过末行则落在末行。
   * 命令缓冲区不含前导 ':'，仅数字。
   */
  if(cmdline_is_all_digits(p)){
    int n;

    n = cmdline_to_positive_int(p);
    if(n <= 0){
      vi_set_error("Invalid line");
      return;
    }
    vi_goto_line_1based(n);
    statusmsg[0] = '\0';
    return;
  }

  if(p[0] == 's' && p[1] == 'e' && p[2] == 't' && p[3] == ' '){
    p += 4;
    while(*p == ' ')
      p++;
    if(strcmp(p, "nu") == 0 || strcmp(p, "number") == 0){
      opt_nu = 1;
      return;
    }
    if(strcmp(p, "nonu") == 0 || strcmp(p, "nonumber") == 0){
      opt_nu = 0;
      return;
    }
    vi_set_error("Unknown set option");
    return;
  }

  if(strcmp(p, "q") == 0){
    if(modified)
      vi_set_error("No write since last change (use :q! to force)");
    else
      running = 0;
  } else if(strcmp(p, "q!") == 0){
    running = 0;
  } else if(strcmp(p, "w") == 0){
    vi_save_file();
  } else if(p[0] == 'w' && p[1] == ' '){
    char *new_name;
    int j;

    new_name = p + 2;
    while(*new_name == ' ')
      new_name++;
    if(*new_name){
      for(j = 0; new_name[j] && j < (int)sizeof(filename) - 1; j++)
        ;
      if(j < (int)sizeof(filename) - 1)
        vi_save_as(new_name);
      else
        vi_set_error("File name too long");
    } else
      vi_set_error("File name required");
  } else if(strcmp(p, "wq") == 0 || strcmp(p, "x") == 0){
    if(vi_save_file() == 0)
      running = 0;
  } else if(p[0] == 'w' && p[1] == 'q' && p[2] == ' '){
    char *new_name;

    new_name = p + 3;
    while(*new_name == ' ')
      new_name++;
    if(*new_name){
      if(vi_save_as(new_name) == 0)
        running = 0;
    } else
      vi_set_error("File name required");
  } else
    vi_set_error("Not an editor command");
}

static void
vi_process_command(int key)
{
  switch(key){
  case KEY_ESC:
    mode = VI_MODE_NORMAL;
    statusmsg[0] = '\0';
    break;
  case '\r':
  case '\n':
    vi_execute_command();
    break;
  case '\b':
  case 0x7f:
    if(cmdpos > 0){
      cmdpos--;
      cmdline[cmdpos] = '\0';
    } else
      mode = VI_MODE_NORMAL;
    break;
  default:
    if(key >= 32 && key < 127 && cmdpos < 78){
      cmdline[cmdpos++] = (char)key;
      cmdline[cmdpos] = '\0';
    }
    break;
  }
}

int
main(int argc, char *argv[])
{
  memset(lines, 0, sizeof(lines));
  line_count = 1;
  lines[0][0] = '\0';
  cx = cy = scroll_y = 0;
  mode = VI_MODE_NORMAL;
  running = 1;
  modified = 0;
  last_key = 0;
  filename[0] = '\0';
  statusmsg[0] = '\0';
  opt_nu = 0;

  if(argc > 2){
    printf(2, "usage: vi [file]\n");
    exit(0);
  }

  if(argc == 2){
    if(strlen(argv[1]) >= VI_FILENAME_MAX){
      printf(2, "vi: file name too long\n");
      exit(0);
    }
    strncpy0(filename, argv[1], sizeof(filename));
    if(vi_load_file(argv[1]) < 0){
      line_count = 1;
      lines[0][0] = '\0';
    }
    /*
     * 打开文件后始终从首行首列显示，避免历史状态导致首屏从第 2 行起。
     */
    cy = 0;
    cx = 0;
    scroll_y = 0;
  } else
    vi_set_status("[New File]");

  vi_refresh_screen_size();
  set_input_echo(0);
  while(running){
    vi_clamp_cursor();
    vi_scroll();
    vi_draw_screen();

    switch(mode){
    case VI_MODE_NORMAL:
      vi_process_normal(read_key());
      break;
    case VI_MODE_INSERT:
      vi_process_insert(read_key());
      break;
    case VI_MODE_COMMAND:
      vi_process_command(read_key());
      break;
    }
  }
  set_input_echo(1);
  clear_screen();
  printf(1, "\033[0m");
  exit(0);
}

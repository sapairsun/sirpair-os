#include "types.h"
#include "stat.h"
#include "user.h"
#include <stdarg.h>

static void
putc(int fd, char c)
{
  write(fd, &c, 1);
}

static int
slen(char *s)
{
  int n = 0;
  while(s[n])
    n++;
  return n;
}

static void
printint(int fd, int xx, int base, int sgn)
{
  static char digits[] = "0123456789ABCDEF";
  char buf[16];
  int i, neg;
  uint x;

  neg = 0;
  if(sgn && xx < 0){
    neg = 1;
    x = -xx;
  } else {
    x = xx;
  }

  i = 0;
  do{
    buf[i++] = digits[x % base];
  }while((x /= base) != 0);
  if(neg)
    buf[i++] = '-';

  while(--i >= 0)
    putc(fd, buf[i]);
}

/* 将无符号十进制写入临时缓冲，返回位数（反向存储，与 printint 类似） */
static int
uint_to_dec_buf(uint v, char *buf)
{
  int i = 0;
  if(v == 0){
    buf[i++] = '0';
    return i;
  }
  while(v){
    buf[i++] = '0' + (v % 10);
    v /= 10;
  }
  return i;
}

static void
print_uint_dec_padded(int fd, uint v, int width, int zpad)
{
  char buf[16];
  int n, pad;
  char pch;

  n = uint_to_dec_buf(v, buf);
  pad = width > n ? width - n : 0;
  pch = zpad ? '0' : ' ';
  while(pad--)
    putc(fd, pch);
  while(--n >= 0)
    putc(fd, buf[n]);
}

static void
print_int_dec_padded(int fd, int xx, int width, int zpad)
{
  char buf[16];
  int n, neg, pad;
  uint x;

  neg = 0;
  if(xx < 0){
    neg = 1;
    x = -(uint)xx;
  } else {
    x = xx;
  }
  n = uint_to_dec_buf(x, buf);
  pad = width > (n + (neg ? 1 : 0)) ? width - (n + (neg ? 1 : 0)) : 0;
  if(!neg){
    while(pad--)
      putc(fd, zpad ? '0' : ' ');
    while(--n >= 0)
      putc(fd, buf[n]);
    return;
  }
  if(zpad){
    putc(fd, '-');
    while(pad--)
      putc(fd, '0');
    while(--n >= 0)
      putc(fd, buf[n]);
  } else {
    while(pad--)
      putc(fd, ' ');
    putc(fd, '-');
    while(--n >= 0)
      putc(fd, buf[n]);
  }
}

static void
print_hex_padded(int fd, uint v, int width, int zpad)
{
  static char d[] = "0123456789abcdef";
  char buf[16];
  int n, i, pad;
  char pch;

  n = 0;
  if(v == 0)
    buf[n++] = '0';
  else
    while(v && n < 16){
      buf[n++] = d[v & 0xf];
      v >>= 4;
    }
  pad = width > n ? width - n : 0;
  pch = zpad ? '0' : ' ';
  while(pad--)
    putc(fd, pch);
  for(i = n - 1; i >= 0; i--)
    putc(fd, buf[i]);
}

static void
print_str_padded(int fd, char *s, int width, int left)
{
  int len, pad;

  if(s == 0)
    s = "(null)";
  len = slen(s);
  if(width <= 0 || len >= width){
    while(*s)
      putc(fd, *s++);
    return;
  }
  pad = width - len;
  if(left){
    while(*s)
      putc(fd, *s++);
    while(pad--)
      putc(fd, ' ');
  } else {
    while(pad--)
      putc(fd, ' ');
    while(*s)
      putc(fd, *s++);
  }
}

/*
 * 支持常用子集：%d %u %x %p %s %c %%，
 * 以及宽度与对齐：%6d %5u %08x %02x %-7s %14s 等（与 readelf/objdump 输出一致）。
 *
 * 可变参数须用 stdarg：旧实现用 (uint*)&fmt+1 依赖栈布局，在 -O 或边界情况下
 * 可能与实参错位，破坏调用方栈并在 ret 处缺页（如 eip≈0x1d77）。
 */
void
printf(int fd, char *fmt, ...)
{
  va_list ap;
  int i, c, left, zf, width;

  va_start(ap, fmt);
  i = 0;
  while(fmt[i]){
    c = fmt[i] & 0xff;
    if(c != '%'){
      putc(fd, c);
      i++;
      continue;
    }
    i++;
    left = 0;
    if(fmt[i] == '-'){
      left = 1;
      i++;
    }
    zf = 0;
    if(fmt[i] == '0'){
      zf = 1;
      i++;
    }
    width = 0;
    while(fmt[i] >= '0' && fmt[i] <= '9'){
      width = width * 10 + (fmt[i] - '0');
      i++;
    }
    c = fmt[i] & 0xff;
    if(c == 0){
      putc(fd, '%');
      break;
    }
    i++;
    if(c == 'd'){
      if(width > 0)
        print_int_dec_padded(fd, va_arg(ap, int), width, zf);
      else
        printint(fd, va_arg(ap, int), 10, 1);
    } else if(c == 'u'){
      if(width > 0)
        print_uint_dec_padded(fd, va_arg(ap, uint), width, zf);
      else
        printint(fd, (int)va_arg(ap, uint), 10, 0);
    } else if(c == 'x' || c == 'p'){
      if(width > 0)
        print_hex_padded(fd, va_arg(ap, uint), width, zf);
      else
        printint(fd, va_arg(ap, uint), 16, 0);
    } else if(c == 's'){
      print_str_padded(fd, va_arg(ap, char*), width, left);
    } else if(c == 'c'){
      putc(fd, va_arg(ap, int));
    } else if(c == '%'){
      putc(fd, '%');
    } else {
      putc(fd, '%');
      putc(fd, c);
    }
  }
  va_end(ap);
}

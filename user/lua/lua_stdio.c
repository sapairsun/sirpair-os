#include "types.h"
#include "stat.h"
#include "fcntl.h"
#include "user.h"
#include "stdio.h"
#include "printf.h"
#include "string.h"
#include <stdarg.h>

FILE __sirpair_stdin_obj;
FILE __sirpair_stdout_obj;
FILE __sirpair_stderr_obj;

static void
discard_bytes(int fd, long n)
{
  char buf[256];
  long r;

  while(n > 0){
    r = n > (long)sizeof(buf) ? (long)sizeof(buf) : n;
    r = read(fd, buf, (int)r);
    if(r <= 0)
      break;
    n -= r;
  }
}

static int
mode_flags(const char *mode)
{
  if(mode[0] == 'r' && mode[1] == '\0')
    return O_RDONLY;
  if(mode[0] == 'w' && mode[1] == '\0')
    return O_WRONLY | O_CREATE | O_TRUNC;
  if(mode[0] == 'a' && mode[1] == '\0')
    return O_WRONLY | O_CREATE;
  if(mode[0] == 'r' && mode[1] == '+' && mode[2] == '\0')
    return O_RDWR;
  if(mode[0] == 'w' && mode[1] == '+' && mode[2] == '\0')
    return O_RDWR | O_CREATE | O_TRUNC;
  if(mode[0] == 'a' && mode[1] == '+' && mode[2] == '\0')
    return O_RDWR | O_CREATE;
  if(mode[0] == 'r' && mode[1] == 'b' && mode[2] == '\0')
    return O_RDONLY;
  if(mode[0] == 'w' && mode[1] == 'b' && mode[2] == '\0')
    return O_WRONLY | O_CREATE | O_TRUNC;
  return O_RDONLY;
}

static void
file_init(FILE *f, int fd, const char *path, int fl)
{
  struct stat st;

  f->fd = fd;
  f->pos = 0;
  f->eof = 0;
  f->err = 0;
  f->has_unget = 0;
  f->path[0] = 0;
  if(path){
    int i;
    for(i = 0; i < 127 && path[i]; i++)
      f->path[i] = path[i];
    f->path[i] = 0;
  }
  f->size = -1;
  if(fd >= 0 && (fl & 3) != O_WRONLY){
    if(fstat(fd, &st) >= 0)
      f->size = st.size;
  }
}

void
sirpair_stdio_init(void)
{
  file_init(&__sirpair_stdin_obj, 0, 0, O_RDONLY);
  file_init(&__sirpair_stdout_obj, 1, 0, O_WRONLY);
  file_init(&__sirpair_stderr_obj, 2, 0, O_WRONLY);
}

FILE *
fopen(const char *path, const char *mode)
{
  static FILE pool[16];
  static int init;
  int fl;
  int fd;
  int i;

  if(!init){
    for(i = 0; i < 16; i++)
      pool[i].fd = -1;
    sirpair_stdio_init();
    init = 1;
  }

  fl = mode_flags(mode);
  fd = open((char*)path, fl);
  if(fd < 0)
    return 0;

  for(i = 0; i < 16; i++){
    if(pool[i].fd < 0){
      file_init(&pool[i], fd, path, fl);
      return &pool[i];
    }
  }
  close(fd);
  return 0;
}

FILE *
fdopen(int fd, const char *mode)
{
  static FILE pool[16];
  static int init;
  int fl;
  int i;

  if(!init){
    for(i = 0; i < 16; i++)
      pool[i].fd = -1;
    sirpair_stdio_init();
    init = 1;
  }

  if(fd < 0)
    return 0;
  fl = mode_flags(mode);
  for(i = 0; i < 16; i++){
    if(pool[i].fd < 0){
      file_init(&pool[i], fd, 0, fl);
      return &pool[i];
    }
  }
  return 0;
}

FILE *
freopen(const char *path, const char *mode, FILE *f)
{
  int fl;
  int fd;

  if(f == 0)
    return fopen(path, mode);
  if(f->fd >= 3)
    close(f->fd);
  f->fd = -1;
  fl = mode_flags(mode);
  fd = open((char*)path, fl);
  if(fd < 0)
    return 0;
  file_init(f, fd, path, fl);
  return f;
}

int
fclose(FILE *f)
{
  if(f == 0 || f->fd < 0)
    return -1;
  if(f->fd >= 3)
    close(f->fd);
  f->fd = -1;
  return 0;
}

size_t
fread(void *p, size_t sz, size_t n, FILE *f)
{
  char *bp;
  size_t need, tot;
  int r;

  if(f == 0 || sz == 0 || n == 0)
    return 0;
  bp = p;
  need = sz * n;
  tot = 0;
  while(tot < need){
    if(f->has_unget){
      *bp++ = (char)f->unget;
      f->has_unget = 0;
      tot++;
      continue;
    }
    r = read(f->fd, bp, (int)(need - tot));
    if(r <= 0){
      if(r == 0)
        f->eof = 1;
      else
        f->err = 1;
      break;
    }
    f->pos += r;
    bp += r;
    tot += (size_t)r;
  }
  return tot / sz;
}

size_t
fwrite(const void *p, size_t sz, size_t n, FILE *f)
{
  const char *bp;
  size_t need, tot;
  int r;

  if(f == 0 || sz == 0 || n == 0)
    return 0;
  bp = p;
  need = sz * n;
  tot = 0;
  while(tot < need){
    r = write(f->fd, (void*)(bp + tot), (int)(need - tot));
    if(r <= 0){
      f->err = 1;
      break;
    }
    f->pos += r;
    tot += (size_t)r;
  }
  return tot / sz;
}

int
fseek(FILE *f, long off, int whence)
{
  long target;
  struct stat st;

  if(f == 0 || f->fd < 0)
    return -1;
  f->has_unget = 0;

  if(whence == SEEK_CUR)
    target = f->pos + off;
  else if(whence == SEEK_END){
    if(f->size < 0 && fstat(f->fd, &st) >= 0)
      f->size = st.size;
    if(f->size < 0)
      return -1;
    target = f->size + off;
  } else
    target = off;

  if(target < 0)
    return -1;

  if(target >= f->pos){
    discard_bytes(f->fd, target - f->pos);
    f->pos = target;
    return 0;
  }

  if(f->path[0] == 0)
    return -1;
  close(f->fd);
  f->fd = open(f->path, O_RDONLY);
  if(f->fd < 0)
    return -1;
  f->pos = 0;
  f->eof = 0;
  if(fstat(f->fd, &st) >= 0)
    f->size = st.size;
  discard_bytes(f->fd, target);
  f->pos = target;
  return 0;
}

long
ftell(FILE *f)
{
  if(f == 0)
    return -1;
  return f->pos;
}

void
rewind(FILE *f)
{
  (void)fseek(f, 0, SEEK_SET);
  if(f)
    f->eof = 0;
}

int
fflush(FILE *f)
{
  (void)f;
  return 0;
}

void
clearerr(FILE *f)
{
  if(f == 0)
    return;
  f->eof = 0;
  f->err = 0;
}

int
setvbuf(FILE *f, char *buf, int mode, size_t size)
{
  (void)f;
  (void)buf;
  (void)mode;
  (void)size;
  return 0;
}

int
feof(FILE *f)
{
  return f && f->eof;
}

int
ferror(FILE *f)
{
  return f && f->err;
}

int
fileno(FILE *f)
{
  if(f == 0)
    return -1;
  return f->fd;
}

int
fgetc(FILE *f)
{
  unsigned char c;
  int r;

  if(f == 0)
    return -1;
  if(f->has_unget){
    f->has_unget = 0;
    return f->unget & 0xff;
  }
  r = read(f->fd, &c, 1);
  if(r == 1){
    f->pos++;
    return c;
  }
  if(r == 0)
    f->eof = 1;
  else
    f->err = 1;
  return -1;
}

int
ungetc(int c, FILE *f)
{
  if(f == 0 || c < 0)
    return -1;
  f->unget = c;
  f->has_unget = 1;
  return c;
}

char *
fgets(char *s, int n, FILE *f)
{
  int i;
  int c;

  if(s == 0 || n <= 0 || f == 0)
    return 0;
  for(i = 0; i < n - 1;){
    c = fgetc(f);
    if(c < 0){
      if(i == 0)
        return 0;
      break;
    }
    s[i++] = (char)c;
    if(c == '\n')
      break;
  }
  s[i] = 0;
  return s;
}

int
fputc(int c, FILE *f)
{
  unsigned char uc;
  int r;

  if(f == 0)
    return -1;
  uc = (unsigned char)c;
  r = write(f->fd, &uc, 1);
  if(r != 1){
    f->err = 1;
    return -1;
  }
  f->pos++;
  return (unsigned char)c;
}

int
fputs(const char *s, FILE *f)
{
  int n;

  if(s == 0 || f == 0)
    return -1;
  n = strlen((char*)s);
  if((int)fwrite(s, 1, (uint)n, f) != n)
    return -1;
  return 0;
}

int
vfprintf(FILE *f, const char *fmt, va_list ap)
{
  char buf[4096];
  int n;

  if(f == 0)
    return -1;
  n = vsnprintf_(buf, sizeof buf, fmt, ap);
  if(n > 0){
    int w = n;
    if(w >= (int)sizeof buf)
      w = (int)sizeof buf - 1;
    write(f->fd, buf, w);
  }
  return n;
}

int
fprintf(FILE *f, const char *fmt, ...)
{
  va_list ap;
  int r;

  va_start(ap, fmt);
  r = vfprintf(f, fmt, ap);
  va_end(ap);
  return r;
}

static unsigned
tmpnam_counter(void)
{
  static unsigned c;
  return c++;
}

char *
tmpnam(char *buf)
{
  static char s[L_tmpnam];
  char *dst;
  unsigned n, v, i;

  dst = buf ? buf : s;
  v = tmpnam_counter();
  n = 0;
  dst[n++] = 'l';
  dst[n++] = 'u';
  dst[n++] = 'a';
  for(i = 0; i < 4; i++){
    dst[n++] = '0' + (v % 10);
    v /= 10;
  }
  dst[n++] = '.';
  dst[n++] = 't';
  dst[n++] = 'm';
  dst[n++] = 'p';
  dst[n] = 0;
  return dst;
}

FILE *
tmpfile(void)
{
  char buf[L_tmpnam];

  return fopen(tmpnam(buf), "w+");
}

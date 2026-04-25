#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"

// ANSI color escape sequences
#define COLOR_RESET   "\033[0m"
#define COLOR_BLUE    "\033[1;34m"   // Bright blue for directories
#define COLOR_GREEN   "\033[0;32m"   // Green for regular files
#define COLOR_YELLOW  "\033[1;33m"   // Yellow for devices

char*
fmtname(char *path)
{
  static char buf[DIRSIZ+1];
  char *p;

  // Find first character after last slash.
  for(p=path+strlen(path); p >= path && *p != '/'; p--)
    ;
  p++;

  // Return blank-padded name.
  if(strlen(p) >= DIRSIZ)
    return p;
  memmove(buf, p, strlen(p));
  memset(buf+strlen(p), ' ', DIRSIZ-strlen(p));
  buf[DIRSIZ] = 0;
  return buf;
}

// Print string left-aligned, padded to given width with spaces
static void
printpad(int fd, char *s, int width)
{
  int len = strlen(s);
  int i;
  write(fd, s, len);
  for(i = len; i < width; i++)
    write(fd, " ", 1);
}

// Print integer left-aligned, padded to given width with spaces
static void
printintpad(int fd, int val, int width)
{
  char buf[16];
  int i, len;
  uint v;

  if(val < 0){
    buf[0] = '-';
    v = -val;
    i = 1;
  } else {
    v = val;
    i = 0;
  }

  // Convert to string (reversed)
  int start = i;
  do {
    buf[i++] = '0' + (v % 10);
    v /= 10;
  } while(v != 0);
  buf[i] = 0;
  len = i;

  // Reverse the digit portion
  int left = start, right = len - 1;
  while(left < right){
    char tmp = buf[left];
    buf[left] = buf[right];
    buf[right] = tmp;
    left++;
    right--;
  }

  write(fd, buf, len);
  for(i = len; i < width; i++)
    write(fd, " ", 1);
}

// Column widths
#define COL_TYPE  9
#define COL_NAME  (DIRSIZ+1)
#define COL_INO   6
#define COL_NLINK 6
// SIZE is last column, no padding needed

// Print column header
void
printheader(void)
{
  printpad(1, "NAME", COL_NAME);
  printpad(1, "TYPE", COL_TYPE);
  printpad(1, "INO", COL_INO);
  printpad(1, "NLINK", COL_NLINK);
  printf(1, "SIZE\n");
}

// Print a single entry with type indicator and color
void
printentry(char *path, struct stat *st)
{
  char *color;
  char *type;

  switch(st->type){
  case T_DIR:
    color = COLOR_BLUE;
    type = "[DIR]";
    break;
  case T_FILE:
    color = COLOR_GREEN;
    type = "[FILE]";
    break;
  case T_DEV:
    color = COLOR_YELLOW;
    type = "[DEV]";
    break;
  default:
    color = COLOR_RESET;
    type = "[???]";
    break;
  }

  printf(1, "%s", color);
  printpad(1, fmtname(path), COL_NAME);
  printpad(1, type, COL_TYPE);
  printintpad(1, st->ino, COL_INO);
  printintpad(1, st->nlink, COL_NLINK);
  printf(1, "%d%s\n", st->size, COLOR_RESET);
}

void
ls(char *path)
{
  char buf[512], *p;
  int fd;
  struct dirent de;
  struct stat st;

  if((fd = open(path, 0)) < 0){
    printf(2, "ls: cannot open %s\n", path);
    return;
  }

  if(fstat(fd, &st) < 0){
    printf(2, "ls: cannot stat %s\n", path);
    close(fd);
    return;
  }

  switch(st.type){
  case T_FILE:
    printheader();
    printentry(path, &st);
    break;

  case T_DIR:
    if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf){
      printf(1, "ls: path too long\n");
      break;
    }
    printheader();
    strcpy(buf, path);
    p = buf+strlen(buf);
    *p++ = '/';
    while(read(fd, &de, sizeof(de)) == sizeof(de)){
      if(de.inum == 0)
        continue;
      memmove(p, de.name, DIRSIZ);
      p[DIRSIZ] = 0;
      if(stat(buf, &st) < 0){
        printf(1, "ls: cannot stat %s\n", buf);
        continue;
      }
      printentry(buf, &st);
    }
    break;
  }
  close(fd);
}

int
main(int argc, char *argv[])
{
  int i;

  if(argc < 2){
    ls(".");
    exit(0);
  }
  for(i=1; i<argc; i++)
    ls(argv[i]);
  exit(0);
}

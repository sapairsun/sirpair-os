#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"

#define PATH_MAX 128

static void
strip_trailing_slash(char *s)
{
  int n;

  n = strlen(s);
  while(n > 1 && s[n - 1] == '/')
    s[--n] = 0;
}

static char*
base_name(char *path)
{
  char *p, *last;

  last = path;
  for(p = path; *p; p++){
    if(*p == '/')
      last = p + 1;
  }
  return last;
}

static int
path_join(char *out, int outsiz, char *dir, char *name)
{
  int dl, nl;

  strip_trailing_slash(dir);
  dl = strlen(dir);
  nl = strlen(name);
  if(dl + 1 + nl + 1 > outsiz)
    return -1;
  if(dl == 0){
    strcpy(out, name);
    return 0;
  }
  memmove(out, dir, dl);
  out[dl] = '/';
  strcpy(out + dl + 1, name);
  return 0;
}

static int
same_inode(struct stat *a, struct stat *b)
{
  return a->dev == b->dev && a->ino == b->ino;
}

static int
mv_one(char *src, char *dst)
{
  struct stat st_src, st_dst, st_t;
  char target[PATH_MAX];
  char src_copy[PATH_MAX];
  char *base;

  if(strlen(src) >= sizeof(src_copy)){
    printf(2, "mv: path too long\n");
    return -1;
  }
  strcpy(src_copy, src);
  strip_trailing_slash(src_copy);

  if(stat(src, &st_src) < 0){
    printf(2, "mv: cannot stat %s\n", src);
    return -1;
  }
  if(st_src.type == T_DIR){
    printf(2, "mv: cannot move directory %s\n", src);
    return -1;
  }

  if(stat(dst, &st_dst) == 0){
    if(st_dst.type == T_DIR){
      base = base_name(src_copy);
      if(*base == 0){
        printf(2, "mv: invalid source path %s\n", src);
        return -1;
      }
      if(strlen(base) >= DIRSIZ){
        printf(2, "mv: name too long\n");
        return -1;
      }
      if(path_join(target, sizeof(target), dst, base) < 0){
        printf(2, "mv: path too long\n");
        return -1;
      }
      if(stat(target, &st_t) == 0){
        if(same_inode(&st_src, &st_t))
          return 0;
        if(st_t.type == T_DIR){
          printf(2, "mv: cannot overwrite directory %s\n", target);
          return -1;
        }
        if(unlink(target) < 0){
          printf(2, "mv: cannot remove %s\n", target);
          return -1;
        }
      }
      if(link(src, target) < 0){
        printf(2, "mv: cannot move to %s\n", target);
        return -1;
      }
      if(unlink(src) < 0){
        printf(2, "mv: cannot remove %s\n", src);
        return -1;
      }
      return 0;
    }
    if(same_inode(&st_src, &st_dst))
      return 0;
    if(unlink(dst) < 0){
      printf(2, "mv: cannot remove %s\n", dst);
      return -1;
    }
    if(link(src, dst) < 0){
      printf(2, "mv: cannot move to %s\n", dst);
      return -1;
    }
    if(unlink(src) < 0){
      printf(2, "mv: cannot remove %s\n", src);
      return -1;
    }
    return 0;
  }

  if(link(src, dst) < 0){
    printf(2, "mv: cannot move to %s\n", dst);
    return -1;
  }
  if(unlink(src) < 0){
    printf(2, "mv: cannot remove %s\n", src);
    return -1;
  }
  return 0;
}

int
main(int argc, char *argv[])
{
  struct stat st;
  int i, err;

  if(argc < 3){
    printf(2, "Usage: mv source dest\n       mv source... directory\n");
    exit(0);
  }

  err = 0;
  if(argc == 3){
    if(mv_one(argv[1], argv[2]) < 0)
      err = 1;
  } else {
    if(stat(argv[argc - 1], &st) < 0 || st.type != T_DIR){
      printf(2, "mv: target is not a directory\n");
      exit(0);
    }
    for(i = 1; i < argc - 1; i++){
      if(mv_one(argv[i], argv[argc - 1]) < 0)
        err = 1;
    }
  }
  if(err)
    exit(1);
  exit(0);
}

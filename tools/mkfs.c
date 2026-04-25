#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>

#define stat sirpair_stat  // avoid clash with host struct stat
#include "types.h"
#include "fs.h"
#include "stat.h"
#include "param.h"

int nblocks = 0;
int nlog = LOGSIZE;
int ninodes = 1024;
int size = FS_SIZE;

int fsfd;
struct superblock sb;
char zeroes[512];
uint freeblock;
uint usedblocks;
uint bitblocks;
uint freeinode = 1;

void balloc(int);
void wsect(uint, void*);
void winode(uint, struct dinode*);
void rinode(uint inum, struct dinode *ip);
void rsect(uint sec, void *buf);
uint ialloc(ushort type);
void iappend(uint inum, void *p, int n);
uint mk_dir(uint parent, const char *name);
void dirlink_raw(uint dirino, uint inum, const char *name);
void diralign(uint inum);

// convert to intel byte order
ushort
xshort(ushort x)
{
  ushort y;
  uchar *a = (uchar*)&y;
  a[0] = x;
  a[1] = x >> 8;
  return y;
}

uint
xint(uint x)
{
  uint y;
  uchar *a = (uchar*)&y;
  a[0] = x;
  a[1] = x >> 8;
  a[2] = x >> 16;
  a[3] = x >> 24;
  return y;
}

int
main(int argc, char *argv[])
{
  int i, cc, fd;
  uint rootino, inum;
  uint homeino, binino, devino, libino;
  struct dirent de;
  char buf[512];

  if(argc < 2){
    fprintf(stderr, "Usage: mkfs fs.img files...\n");
    exit(1);
  }

  assert((512 % sizeof(struct dinode)) == 0);
  assert((512 % sizeof(struct dirent)) == 0);

  fsfd = open(argv[1], O_RDWR|O_CREAT|O_TRUNC, 0666);
  if(fsfd < 0){
    perror(argv[1]);
    exit(1);
  }

  bitblocks = size/(512*8) + 1;
  usedblocks = NINODEBLOCKS(ninodes) + 3 + bitblocks;
  nblocks = size - usedblocks - nlog;
  if(nblocks <= 0){
    fprintf(stderr, "mkfs: invalid geometry: size=%d used=%u log=%d\n",
            size, usedblocks, nlog);
    exit(1);
  }

  sb.size = xint(size);
  sb.nblocks = xint(nblocks); // so whole disk is size sectors
  sb.ninodes = xint(ninodes);
  sb.nlog = xint(nlog);

  freeblock = usedblocks;

  printf("used %d (bit %d ninode %d) free %u log %u total %d\n", usedblocks,
         bitblocks, (int)NINODEBLOCKS(ninodes), freeblock, nlog, nblocks+usedblocks+nlog);

  assert(nblocks + usedblocks + nlog == size);

  for(i = 0; i < nblocks + usedblocks + nlog; i++)
    wsect(i, zeroes);

  memset(buf, 0, sizeof(buf));
  memmove(buf, &sb, sizeof(sb));
  wsect(1, buf);

  rootino = ialloc(T_DIR);
  assert(rootino == ROOTINO);

  bzero(&de, sizeof(de));
  de.inum = xshort(rootino);
  strcpy(de.name, ".");
  iappend(rootino, &de, sizeof(de));

  bzero(&de, sizeof(de));
  de.inum = xshort(rootino);
  strcpy(de.name, "..");
  iappend(rootino, &de, sizeof(de));

  homeino = mk_dir(rootino, "home");
  binino = mk_dir(rootino, "bin");
  devino = mk_dir(rootino, "dev");
  libino = mk_dir(rootino, "lib");
  (void)devino;
  (void)libino;

  /*
   * TinyCC 在映像内的资源根：/tcc/include、/tcc/lib（与 CONFIG_TCCDIR=/tcc 一致）。
   * 避免在 Sirpair 上再去打开不存在的 /usr/lib/crt*.o 与 /usr/include。
   */
  {
    uint tccroot, tccinc, tcclib;
    static const struct {
      const char *name;
      const char *host;
    } tcc_hdrs[] = {
      { "assert.h", "user/lua/include/assert.h" },
      { "ctype.h", "user/lua/include/ctype.h" },
      { "errno.h", "user/lua/include/errno.h" },
      { "float.h", "user/lua/include/float.h" },
      { "limits.h", "user/lua/include/limits.h" },
      { "locale.h", "user/lua/include/locale.h" },
      { "math.h", "user/lua/include/math.h" },
      { "printf.h", "user/lua/include/printf.h" },
      { "setjmp.h", "user/lua/include/setjmp.h" },
      { "signal.h", "user/lua/include/signal.h" },
      { "stdio.h", "user/lua/include/stdio.h" },
      { "stdlib.h", "user/lua/include/stdlib.h" },
      { "string.h", "user/lua/include/string.h" },
      { "time.h", "user/lua/include/time.h" },
      { "stdarg.h", "thirdparty/tcc-0.9.25/include/stdarg.h" },
      { "stdbool.h", "thirdparty/tcc-0.9.25/include/stdbool.h" },
      { "stddef.h", "user/tcc/include/stddef.h" },
      { "tcclib.h", "thirdparty/tcc-0.9.25/include/tcclib.h" },
    };
    static const struct {
      const char *name;
      const char *host;
    } tcc_libs[] = {
      { "libtcc1_rt.o", "build/libtcc1_rt.o" },
      { "libsirpairrt.a", "build/libsirpairrt.a" },
    };
    int ti;

    tccroot = mk_dir(rootino, "tcc");
    tccinc = mk_dir(tccroot, "include");
    tcclib = mk_dir(tccroot, "lib");

    for(ti = 0; ti < (int)(sizeof(tcc_hdrs) / sizeof(tcc_hdrs[0])); ti++){
      if((fd = open(tcc_hdrs[ti].host, 0)) < 0){
        perror(tcc_hdrs[ti].host);
        exit(1);
      }
      inum = ialloc(T_FILE);
      dirlink_raw(tccinc, inum, tcc_hdrs[ti].name);
      while((cc = read(fd, buf, sizeof(buf))) > 0)
        iappend(inum, buf, cc);
      close(fd);
    }
    for(ti = 0; ti < (int)(sizeof(tcc_libs) / sizeof(tcc_libs[0])); ti++){
      if((fd = open(tcc_libs[ti].host, 0)) < 0){
        perror(tcc_libs[ti].host);
        exit(1);
      }
      inum = ialloc(T_FILE);
      dirlink_raw(tcclib, inum, tcc_libs[ti].name);
      while((cc = read(fd, buf, sizeof(buf))) > 0)
        iappend(inum, buf, cc);
      close(fd);
    }
    if((fd = open("user/kk.c", 0)) >= 0){
      inum = ialloc(T_FILE);
      dirlink_raw(homeino, inum, "kk.c");
      while((cc = read(fd, buf, sizeof(buf))) > 0)
        iappend(inum, buf, cc);
      close(fd);
    } else {
      fprintf(stderr, "mkfs: warning: user/kk.c not found, skipping /home/kk.c\n");
    }
    diralign(tccroot);
    diralign(tccinc);
    diralign(tcclib);
  }

  for(i = 2; i < argc; i++){
    char *name, *target;
    uint parent;

    if((fd = open(argv[i], 0)) < 0){
      perror(argv[i]);
      exit(1);
    }

    // Get basename: strip directory prefix (e.g., "docs/README" -> "README")
    name = strrchr(argv[i], '/');
    if(name)
      name++;  // skip the '/'
    else
      name = argv[i];

    // Skip leading _ in name when writing to file system.
    // The binaries are named _rm, _cat, etc. to keep the
    // build operating system from trying to execute them
    // in place of system binaries like rm and cat.
    if(name[0] == '_')
      target = name + 1;
    else
      target = name;

    if(name[0] == '_'){
      if(strcmp(target, "init") == 0)
        parent = rootino;   // kernel bootstrap requires /init
      else
        parent = binino;
    } else {
      parent = homeino;
    }

    inum = ialloc(T_FILE);

    dirlink_raw(parent, inum, target);

    while((cc = read(fd, buf, sizeof(buf))) > 0)
      iappend(inum, buf, cc);

    close(fd);
  }

  diralign(rootino);
  diralign(homeino);
  diralign(binino);
  diralign(devino);
  diralign(libino);

  balloc(usedblocks);

  exit(0);
}

void
dirlink_raw(uint dirino, uint inum, const char *name)
{
  struct dirent de;

  bzero(&de, sizeof(de));
  de.inum = xshort(inum);
  strncpy(de.name, name, DIRSIZ);
  iappend(dirino, &de, sizeof(de));
}

uint
mk_dir(uint parent, const char *name)
{
  uint inum;
  struct dirent de;

  inum = ialloc(T_DIR);
  dirlink_raw(parent, inum, name);

  bzero(&de, sizeof(de));
  de.inum = xshort(inum);
  strcpy(de.name, ".");
  iappend(inum, &de, sizeof(de));

  bzero(&de, sizeof(de));
  de.inum = xshort(parent);
  strcpy(de.name, "..");
  iappend(inum, &de, sizeof(de));
  return inum;
}

void
diralign(uint inum)
{
  uint off;
  struct dinode din;

  rinode(inum, &din);
  off = xint(din.size);
  off = ((off / BSIZE) + 1) * BSIZE;
  din.size = xint(off);
  winode(inum, &din);
}

void
wsect(uint sec, void *buf)
{
  if(lseek(fsfd, sec * 512L, 0) != sec * 512L){
    perror("lseek");
    exit(1);
  }
  if(write(fsfd, buf, 512) != 512){
    perror("write");
    exit(1);
  }
}

uint
i2b(uint inum)
{
  return (inum / IPB) + 2;
}

void
winode(uint inum, struct dinode *ip)
{
  char buf[512];
  uint bn;
  struct dinode *dip;

  bn = i2b(inum);
  rsect(bn, buf);
  dip = ((struct dinode*)buf) + (inum % IPB);
  *dip = *ip;
  wsect(bn, buf);
}

void
rinode(uint inum, struct dinode *ip)
{
  char buf[512];
  uint bn;
  struct dinode *dip;

  bn = i2b(inum);
  rsect(bn, buf);
  dip = ((struct dinode*)buf) + (inum % IPB);
  *ip = *dip;
}

void
rsect(uint sec, void *buf)
{
  if(lseek(fsfd, sec * 512L, 0) != sec * 512L){
    perror("lseek");
    exit(1);
  }
  if(read(fsfd, buf, 512) != 512){
    perror("read");
    exit(1);
  }
}

uint
ialloc(ushort type)
{
  uint inum = freeinode++;
  struct dinode din;

  bzero(&din, sizeof(din));
  din.type = xshort(type);
  din.nlink = xshort(1);
  din.size = xint(0);
  winode(inum, &din);
  return inum;
}

void
balloc(int used)
{
  uchar buf[512];
  int bi;
  uint b;
  uint off;

  printf("balloc: first %d blocks have been allocated\n", used);
  assert((uint)used <= size);
  for(bi = 0; bi < bitblocks; bi++){
    memset(buf, 0, sizeof(buf));
    off = (uint)bi * BPB;
    if(off >= (uint)used)
      break;
    for(b = 0; b < BPB && off + b < (uint)used; b++)
      buf[b/8] |= (0x1 << (b%8));
    printf("balloc: write bitmap block at sector %d\n", (int)NINODEBLOCKS(ninodes) + 3 + bi);
    wsect(NINODEBLOCKS(ninodes) + 3 + bi, buf);
  }
}

#define min(a, b) ((a) < (b) ? (a) : (b))

void
iappend(uint inum, void *xp, int n)
{
  char *p = (char*)xp;
  uint fbn, off, n1;
  struct dinode din;
  char buf[512];
  uint dindirect[NINDIRECT];
  uint indirect[NINDIRECT];
  uint x, x2;

  rinode(inum, &din);

  off = xint(din.size);
  while(n > 0){
    fbn = off / 512;
    assert(fbn < MAXFILE);
    if(fbn < NDIRECT){
      if(xint(din.addrs[fbn]) == 0){
        din.addrs[fbn] = xint(freeblock++);
        usedblocks++;
      }
      x = xint(din.addrs[fbn]);
    } else if(fbn < NDIRECT + NINDIRECT){
      if(xint(din.addrs[NDIRECT]) == 0){
        // printf("allocate indirect block\n");
        din.addrs[NDIRECT] = xint(freeblock++);
        usedblocks++;
      }
      // printf("read indirect block\n");
      rsect(xint(din.addrs[NDIRECT]), (char*)indirect);
      if(indirect[fbn - NDIRECT] == 0){
        indirect[fbn - NDIRECT] = xint(freeblock++);
        usedblocks++;
        wsect(xint(din.addrs[NDIRECT]), (char*)indirect);
      }
      x = xint(indirect[fbn-NDIRECT]);
    } else {
      uint bn = fbn - NDIRECT - NINDIRECT;
      uint idx1 = bn / NINDIRECT;
      uint idx2 = bn % NINDIRECT;
      assert(bn < NDINDIRECT);

      if(xint(din.addrs[NDIRECT+1]) == 0){
        din.addrs[NDIRECT+1] = xint(freeblock++);
        usedblocks++;
      }
      rsect(xint(din.addrs[NDIRECT+1]), (char*)dindirect);
      if(dindirect[idx1] == 0){
        dindirect[idx1] = xint(freeblock++);
        usedblocks++;
        wsect(xint(din.addrs[NDIRECT+1]), (char*)dindirect);
      }
      x2 = xint(dindirect[idx1]);
      rsect(x2, (char*)indirect);
      if(indirect[idx2] == 0){
        indirect[idx2] = xint(freeblock++);
        usedblocks++;
        wsect(x2, (char*)indirect);
      }
      x = xint(indirect[idx2]);
    }
    n1 = min(n, (fbn + 1) * 512 - off);
    rsect(x, buf);
    bcopy(p, buf + off - (fbn * 512), n1);
    wsect(x, buf);
    n -= n1;
    off += n1;
    p += n1;
  }
  din.size = xint(off);
  winode(inum, &din);
}

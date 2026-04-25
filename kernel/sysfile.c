//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//

#include "types.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "mmu.h"
#include "proc.h"
#include "fs.h"
#include "file.h"
#include "fcntl.h"
#include "usock.h"

// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
static int
argfd(int n, int *pfd, struct file **pf)
{
  int fd;
  struct file *f;

  if(argint(n, &fd) < 0)
    return -1;
  if(fd < 0 || fd >= NOFILE || (f=proc->ofile[fd]) == 0)
    return -1;
  if(pfd)
    *pfd = fd;
  if(pf)
    *pf = f;
  return 0;
}

/* 形如 a.b.c.d 或 a.b.c.d:端口（与 Linux telnet 一致，默认端口 23） */
static int
parse_ipv4_port(const char *s, uchar ip[4], int *port_out)
{
  int i, v, ndig;
  const char *p = s;
  int port = 23;

  for(i = 0; i < 4; i++){
    v = 0;
    ndig = 0;
    while(*p >= '0' && *p <= '9'){
      v = v * 10 + (*p - '0');
      if(v > 255)
        return -1;
      p++;
      ndig++;
    }
    if(ndig == 0)
      return -1;
    ip[i] = (uchar)v;
    if(i < 3){
      if(*p != '.')
        return -1;
      p++;
    }
  }
  if(*p == ':'){
    p++;
    v = 0;
    ndig = 0;
    while(*p >= '0' && *p <= '9'){
      v = v * 10 + (*p - '0');
      if(v > 65535)
        return -1;
      p++;
      ndig++;
    }
    if(ndig == 0)
      return -1;
    port = v;
  } else if(*p != 0)
    return -1;
  *port_out = port;
  return 0;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
static int
fdalloc(struct file *f)
{
  int fd;

  for(fd = 0; fd < NOFILE; fd++){
    if(proc->ofile[fd] == 0){
      proc->ofile[fd] = f;
      return fd;
    }
  }
  return -1;
}

int
sys_dup(void)
{
  struct file *f;
  int fd;
  
  if(argfd(0, 0, &f) < 0)
    return -1;
  if((fd=fdalloc(f)) < 0)
    return -1;
  filedup(f);
  return fd;
}

int
sys_read(void)
{
  struct file *f;
  int n;
  char *p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argptr(1, &p, n) < 0)
    return -1;
  return fileread(f, p, n);
}

int
sys_write(void)
{
  struct file *f;
  int n;
  char *p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argptr(1, &p, n) < 0)
    return -1;
  return filewrite(f, p, n);
}

int
sys_close(void)
{
  int fd;
  struct file *f;
  
  if(argfd(0, &fd, &f) < 0)
    return -1;
  proc->ofile[fd] = 0;
  fileclose(f);
  return 0;
}

int
sys_fstat(void)
{
  struct file *f;
  struct stat *st;
  
  if(argfd(0, 0, &f) < 0 || argptr(1, (void*)&st, sizeof(*st)) < 0)
    return -1;
  return filestat(f, st);
}

int
sys_lseek(void)
{
  struct file *f;
  int off, whence;

  if(argfd(0, 0, &f) < 0 || argint(1, &off) < 0 || argint(2, &whence) < 0)
    return -1;
  return fileseek(f, off, whence);
}

// Create the path new as a link to the same inode as old.
int
sys_link(void)
{
  char name[DIRSIZ], *new, *old;
  struct inode *dp, *ip;

  if(argstr(0, &old) < 0 || argstr(1, &new) < 0)
    return -1;
  if((ip = namei(old)) == 0)
    return -1;

  begin_trans();

  ilock(ip);
  if(ip->type == T_DIR){
    iunlockput(ip);
    commit_trans();
    return -1;
  }

  ip->nlink++;
  iupdate(ip);
  iunlock(ip);

  if((dp = nameiparent(new, name)) == 0)
    goto bad;
  ilock(dp);
  if(dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0){
    iunlockput(dp);
    goto bad;
  }
  iunlockput(dp);
  iput(ip);

  commit_trans();

  return 0;

bad:
  ilock(ip);
  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);
  commit_trans();
  return -1;
}

// Is the directory dp empty except for "." and ".." ?
static int
isdirempty(struct inode *dp)
{
  int off;
  struct dirent de;

  for(off=2*sizeof(de); off<dp->size; off+=sizeof(de)){
    if(readi(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
      panic("isdirempty: readi");
    if(de.inum != 0)
      return 0;
  }
  return 1;
}

//PAGEBREAK!
int
sys_unlink(void)
{
  struct inode *ip, *dp;
  struct dirent de;
  char name[DIRSIZ], *path;
  uint off;

  if(argstr(0, &path) < 0)
    return -1;
  if((dp = nameiparent(path, name)) == 0)
    return -1;

  begin_trans();

  ilock(dp);

  // Cannot unlink "." or "..".
  if(namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
    goto bad;

  if((ip = dirlookup(dp, name, &off)) == 0)
    goto bad;
  ilock(ip);

  if(ip->nlink < 1)
    panic("unlink: nlink < 1");
  if(ip->type == T_DIR && !isdirempty(ip)){
    iunlockput(ip);
    goto bad;
  }

  memset(&de, 0, sizeof(de));
  if(writei(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
    panic("unlink: writei");
  if(ip->type == T_DIR){
    dp->nlink--;
    iupdate(dp);
  }
  iunlockput(dp);

  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);

  commit_trans();

  return 0;

bad:
  iunlockput(dp);
  commit_trans();
  return -1;
}

static struct inode*
create(char *path, short type, short major, short minor)
{
  uint off;
  struct inode *ip, *dp;
  char name[DIRSIZ];

  if((dp = nameiparent(path, name)) == 0)
    return 0;
  ilock(dp);

  if((ip = dirlookup(dp, name, &off)) != 0){
    iunlockput(dp);
    ilock(ip);
    if(type == T_FILE && ip->type == T_FILE)
      return ip;
    iunlockput(ip);
    return 0;
  }

  if((ip = ialloc(dp->dev, type)) == 0)
    panic("create: ialloc");

  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  iupdate(ip);

  if(type == T_DIR){  // Create . and .. entries.
    dp->nlink++;  // for ".."
    iupdate(dp);
    // No ip->nlink++ for ".": avoid cyclic ref count.
    if(dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
      panic("create dots");
  }

  if(dirlink(dp, name, ip->inum) < 0)
    panic("create: dirlink");

  iunlockput(dp);

  return ip;
}

int
sys_open(void)
{
  char *path;
  int fd, omode;
  int trunc_trans = 0;
  struct file *f;
  struct inode *ip;

  if(argstr(0, &path) < 0 || argint(1, &omode) < 0)
    return -1;
  if(omode & O_CREATE){
    begin_trans();
    ip = create(path, T_FILE, 0, 0);
    commit_trans();
    if(ip == 0)
      return -1;
  } else {
    if(omode & O_TRUNC){
      begin_trans();
      trunc_trans = 1;
    }
    if((ip = namei(path)) == 0)
    {
      if(trunc_trans)
        commit_trans();
      return -1;
    }
    ilock(ip);
    if(ip->type == T_DIR && omode != O_RDONLY){
      iunlockput(ip);
      if(trunc_trans)
        commit_trans();
      return -1;
    }
    if((omode & O_TRUNC) && ip->type == T_FILE){
      itrunc(ip);
      iupdate(ip);
    }
  }

  if((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0){
    if(f)
      fileclose(f);
    iunlockput(ip);
    if(trunc_trans)
      commit_trans();
    return -1;
  }
  iunlock(ip);
  if(trunc_trans)
    commit_trans();

  f->type = FD_INODE;
  f->ip = ip;
  f->off = (omode & O_APPEND) ? ip->size : 0;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);
  return fd;
}

int
sys_mkdir(void)
{
  char *path;
  struct inode *ip;

  begin_trans();
  if(argstr(0, &path) < 0 || (ip = create(path, T_DIR, 0, 0)) == 0){
    commit_trans();
    return -1;
  }
  iunlockput(ip);
  commit_trans();
  return 0;
}

int
sys_mknod(void)
{
  struct inode *ip;
  char *path;
  int len;
  int major, minor;
  
  begin_trans();
  if((len=argstr(0, &path)) < 0 ||
     argint(1, &major) < 0 ||
     argint(2, &minor) < 0 ||
     (ip = create(path, T_DEV, major, minor)) == 0){
    commit_trans();
    return -1;
  }
  iunlockput(ip);
  commit_trans();
  return 0;
}

int
sys_chdir(void)
{
  char *path;
  struct inode *ip;
  int ret = -1;

  begin_trans();
  if(argstr(0, &path) < 0)
    goto out;
  if((ip = namei(path)) == 0)
    goto out;
  ilock(ip);
  if(ip->type != T_DIR){
    iunlockput(ip);
    goto out;
  }
  iunlock(ip);
  iput(proc->cwd);
  proc->cwd = ip;
  ret = 0;

out:
  commit_trans();
  return ret;
}

int
sys_getcwd(void)
{
  char *ubuf;
  int size;
  int depth, i, j, pos;
  uint off;
  struct inode *ip, *dp;
  struct dirent de;
  char comps[64][DIRSIZ + 1];
  char name[DIRSIZ + 1];
  char kbuf[512];
  int ret = -1;

  begin_trans();
  if(argint(1, &size) < 0 || size <= 0)
    goto out;
  if(argptr(0, &ubuf, size) < 0)
    goto out;

  depth = 0;
  ip = idup(proc->cwd);
  while(1){
    ilock(ip);
    if(ip->inum == ROOTINO){
      iunlock(ip);
      iput(ip);
      break;
    }

    dp = dirlookup(ip, "..", 0);
    if(dp == 0){
      iunlockput(ip);
      goto out;
    }

    ilock(dp);
    name[0] = 0;
    for(off = 0; off < dp->size; off += sizeof(de)){
      if(readi(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
        panic("getcwd read");
      if(de.inum == ip->inum){
        memmove(name, de.name, DIRSIZ);
        name[DIRSIZ] = 0;
        break;
      }
    }
    iunlock(dp);
    iunlockput(ip);

    if(name[0] == 0){
      iput(dp);
      goto out;
    }
    if(depth >= NELEM(comps)){
      iput(dp);
      goto out;
    }
    safestrcpy(comps[depth], name, sizeof(comps[depth]));
    depth++;
    ip = dp;
  }

  if(depth == 0){
    if(size < 2)
      goto out;
    ubuf[0] = '/';
    ubuf[1] = 0;
    ret = 0;
    goto out;
  }

  pos = 0;
  for(i = depth - 1; i >= 0; i--){
    if(pos >= (int)sizeof(kbuf) - 1)
      goto out;
    kbuf[pos++] = '/';
    for(j = 0; comps[i][j] && j < DIRSIZ; j++){
      if(pos >= (int)sizeof(kbuf) - 1)
        goto out;
      kbuf[pos++] = comps[i][j];
    }
  }
  kbuf[pos] = 0;

  if(pos + 1 > size)
    goto out;
  memmove(ubuf, kbuf, pos + 1);
  ret = 0;

out:
  commit_trans();
  return ret;
}

int
sys_socket(void)
{
  int domain, type, protocol;
  int fd;
  struct file *f;
  int slot;
  static int socket_dbg;

  if(argint(0, &domain) < 0 || argint(1, &type) < 0 || argint(2, &protocol) < 0)
    return -1;
  if(protocol != 0)
    return -1;

  if(domain == AF_INET){
    if(!net_is_available())
      return -1;
    if(type == SOCK_STREAM){
      slot = net_tcp_user_alloc();
      if(slot < 0)
        return -1;
      if((f = filealloc()) == 0){
        net_tcp_user_close(slot);
        return -1;
      }
      if((fd = fdalloc(f)) < 0){
        fileclose(f);
        net_tcp_user_close(slot);
        return -1;
      }
      f->type = FD_TCPSOCK;
      f->readable = 1;
      f->writable = 1;
      f->tcp_slot = slot;
      f->tcp_listen = 0;
      if(socket_dbg < 8){
        cprintf("sys_socket dbg: fd=%d slot=%d domain=%d type=%d\n", fd, slot, domain, type);
        socket_dbg++;
      }
      return fd;
    }
    if(type == SOCK_DGRAM){
      slot = net_udp_user_alloc();
      if(slot < 0)
        return -1;
      if((f = filealloc()) == 0){
        net_udp_user_close(slot);
        return -1;
      }
      if((fd = fdalloc(f)) < 0){
        fileclose(f);
        net_udp_user_close(slot);
        return -1;
      }
      f->type = FD_UDPSOCK;
      f->readable = 1;
      f->writable = 1;
      f->udp_slot = slot;
      return fd;
    }
    return -1;
  }

  if(domain != AF_UNIX)
    return -1;
  if(type != SOCK_STREAM)
    return -1;

  if((f = filealloc()) == 0)
    return -1;
  if((fd = fdalloc(f)) < 0){
    fileclose(f);
    return -1;
  }

  f->type = FD_USOCK;
  f->readable = 1;
  f->writable = 1;
  f->usock_state = USOCK_INIT;
  f->usock_lid = -1;
  f->usock_rx = 0;
  f->usock_tx = 0;
  return fd;
}

int
sys_bind(void)
{
  struct file *f;
  char *path;

  if(argfd(0, 0, &f) < 0 || argstr(1, &path) < 0)
    return -1;
  if(f->type == FD_TCPSOCK)
    return net_tcp_user_bind(f->tcp_slot, path);
  if(f->type == FD_UDPSOCK)
    return net_udp_user_bind(f->udp_slot, path);
  return usock_bind(f, path);
}

int
sys_listen(void)
{
  struct file *f;
  int backlog;
  int r;

  if(argfd(0, 0, &f) < 0 || argint(1, &backlog) < 0)
    return -1;
  if(f->type == FD_TCPSOCK){
    r = net_tcp_user_listen(f->tcp_slot);
    if(r == 0)
      f->tcp_listen = 1;
    return r;
  }
  return usock_listen(f, backlog);
}

int
sys_connect(void)
{
  struct file *f;
  char *path;
  uchar ip[4];
  int port;
  static int connect_dbg;

  if(argfd(0, 0, &f) < 0 || argstr(1, &path) < 0)
    return -1;
  if(f->type == FD_TCPSOCK){
    if(connect_dbg < 8){
      cprintf("sys_connect dbg: slot=%d path=%s\n", f->tcp_slot, path);
      connect_dbg++;
    }
    if(parse_ipv4_port(path, ip, &port) < 0)
      return -1;
    return net_tcp_user_connect(f->tcp_slot, ip, (ushort)port);
  }
  return usock_connect(f, path);
}

int
sys_accept(void)
{
  struct file *lf, *nf;
  int fd;

  if(argfd(0, 0, &lf) < 0)
    return -1;
  if(lf->type == FD_TCPSOCK){
    if(net_tcp_user_accept(lf, &nf) < 0)
      return -1;
    if((fd = fdalloc(nf)) < 0){
      fileclose(nf);
      return -1;
    }
    return fd;
  }
  if(usock_accept(lf, &nf) < 0)
    return -1;
  if((fd = fdalloc(nf)) < 0){
    fileclose(nf);
    return -1;
  }
  return fd;
}

int
sys_send(void)
{
  struct file *f;
  int n;
  char *p;
  static int send_sys_dbg;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argptr(1, &p, n) < 0)
    return -1;
  if(send_sys_dbg < 12){
    cprintf("sys_send dbg: ftype=%d n=%d tcp_slot=%d usock_state=%d\n",
            f->type, n, f->tcp_slot, f->usock_state);
    send_sys_dbg++;
  }
  return filewrite(f, p, n);
}

int
sys_recv(void)
{
  struct file *f;
  int n;
  char *p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argptr(1, &p, n) < 0)
    return -1;
  return fileread(f, p, n);
}

int
sys_recvfrom(void)
{
  struct file *f;
  int n;
  char *buf;
  char *src_ip;
  int *src_port;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argptr(1, &buf, n) < 0)
    return -1;
  if(argptr(3, &src_ip, 4) < 0 || argptr(4, (void*)&src_port, sizeof(int)) < 0)
    return -1;
  if(f->type != FD_UDPSOCK)
    return -1;
  return net_udp_user_recvfrom(f->udp_slot, buf, n, (uchar*)src_ip, src_port);
}

int
sys_sendto(void)
{
  struct file *f;
  int n;
  char *buf;
  char *dst_ip;
  int dst_port;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argptr(1, &buf, n) < 0)
    return -1;
  if(argptr(3, &dst_ip, 4) < 0 || argint(4, &dst_port) < 0)
    return -1;
  if(f->type != FD_UDPSOCK)
    return -1;
  if(dst_port < 0 || dst_port > 65535)
    return -1;
  return net_udp_user_sendto(f->udp_slot, buf, n, (uchar*)dst_ip, (ushort)dst_port);
}

int
sys_exec(void)
{
  char *path, *argv[MAXARG];
  int i;
  uint uargv, uarg;

  if(argstr(0, &path) < 0 || argint(1, (int*)&uargv) < 0){
    return -1;
  }
  memset(argv, 0, sizeof(argv));
  for(i=0;; i++){
    if(i >= NELEM(argv))
      return -1;
    if(fetchint(proc, uargv+4*i, (int*)&uarg) < 0)
      return -1;
    if(uarg == 0){
      argv[i] = 0;
      break;
    }
    if(fetchstr(proc, uarg, &argv[i]) < 0)
      return -1;
  }
  return exec(path, argv);
}

int
sys_pipe(void)
{
  int *fd;
  struct file *rf, *wf;
  int fd0, fd1;

  if(argptr(0, (void*)&fd, 2*sizeof(fd[0])) < 0)
    return -1;
  if(pipealloc(&rf, &wf) < 0)
    return -1;
  fd0 = -1;
  if((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0){
    if(fd0 >= 0)
      proc->ofile[fd0] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  fd[0] = fd0;
  fd[1] = fd1;
  return 0;
}

int
sys_statfs(void)
{
  struct statfs *st;

  if(argptr(0, (void*)&st, sizeof(*st)) < 0)
    return -1;
  fillstatfs(ROOTDEV, st);
  return 0;
}

int
sys_fdready(void)
{
  struct file *f;
  int forwrite;

  if(argfd(0, 0, &f) < 0 || argint(1, &forwrite) < 0)
    return -1;
  return filefdready(f, forwrite);
}

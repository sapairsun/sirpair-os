//
// File descriptors
//

#include "types.h"
#include "defs.h"
#include "param.h"
#include "fs.h"
#include "file.h"
#include "stat.h"
#include "spinlock.h"

struct devsw devsw[NDEV];
struct {
  struct spinlock lock;
  struct file file[NFILE];
} ftable;

void
fileinit(void)
{
  initlock(&ftable.lock, "ftable");
}

// Allocate a file structure.
struct file*
filealloc(void)
{
  struct file *f;

  acquire(&ftable.lock);
  for(f = ftable.file; f < ftable.file + NFILE; f++){
    if(f->ref == 0){
      f->ref = 1;
      f->tcp_slot = -1;
      f->udp_slot = -1;
      f->tcp_listen = 0;
      release(&ftable.lock);
      return f;
    }
  }
  release(&ftable.lock);
  return 0;
}

// Increment ref count for file f.
struct file*
filedup(struct file *f)
{
  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("filedup");
  f->ref++;
  release(&ftable.lock);
  return f;
}

// Close file f.  (Decrement ref count, close when reaches 0.)
void
fileclose(struct file *f)
{
  struct file ff;

  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("fileclose");
  if(--f->ref > 0){
    release(&ftable.lock);
    return;
  }
  ff = *f;
  f->ref = 0;
  f->type = FD_NONE;
  f->pipe = 0;
  f->ip = 0;
  f->off = 0;
  f->usock_state = USOCK_INIT;
  f->usock_lid = -1;
  f->usock_rx = 0;
  f->usock_tx = 0;
  release(&ftable.lock);
  
  if(ff.type == FD_PIPE)
    pipeclose(ff.pipe, ff.writable);
  else if(ff.type == FD_INODE){
    begin_trans();
    iput(ff.ip);
    commit_trans();
  } else if(ff.type == FD_TCPSOCK){
    net_tcp_user_unref(ff.tcp_slot,
        ff.tcp_listen ? TCP_UNREF_CLOSE_LISTEN : TCP_UNREF_CLOSE_ACCEPTED);
  } else if(ff.type == FD_UDPSOCK){
    net_udp_user_unref(ff.udp_slot);
  } else if(ff.type == FD_USOCK){
    usock_fileclose(&ff);
  }
}

// Get metadata about file f.
int
filestat(struct file *f, struct stat *st)
{
  if(f->type == FD_INODE){
    ilock(f->ip);
    stati(f->ip, st);
    iunlock(f->ip);
    return 0;
  }
  return -1;
}

// Read from file f.
int
fileread(struct file *f, char *addr, int n)
{
  int r;

  if(f->readable == 0)
    return -1;
  if(f->type == FD_PIPE)
    return piperead(f->pipe, addr, n);
  if(f->type == FD_USOCK){
    if(f->usock_state != USOCK_CONN || f->usock_rx == 0)
      return -1;
    return fileread(f->usock_rx, addr, n);
  }
  if(f->type == FD_TCPSOCK){
    if(f->tcp_slot < 0)
      return -1;
    return net_tcp_user_read(f->tcp_slot, addr, n, f->tcp_listen);
  }
  if(f->type == FD_UDPSOCK)
    return -1;
  if(f->type == FD_INODE){
    ilock(f->ip);
    if((r = readi(f->ip, addr, f->off, n)) > 0)
      f->off += r;
    iunlock(f->ip);
    return r;
  }
  panic("fileread");
}

//PAGEBREAK!
// Write to file f.
int
filewrite(struct file *f, char *addr, int n)
{
  int r;

  if(f->writable == 0)
    return -1;
  if(f->type == FD_PIPE)
    return pipewrite(f->pipe, addr, n);
  if(f->type == FD_USOCK){
    if(f->usock_state != USOCK_CONN || f->usock_tx == 0)
      return -1;
    return filewrite(f->usock_tx, addr, n);
  }
  if(f->type == FD_TCPSOCK){
    if(f->tcp_slot < 0)
      return -1;
    return net_tcp_user_write(f->tcp_slot, addr, n, f->tcp_listen);
  }
  if(f->type == FD_UDPSOCK)
    return -1;
  if(f->type == FD_INODE){
    // write a few blocks at a time to avoid exceeding
    // the maximum log transaction size, including
    // i-node, indirect block, allocation blocks,
    // and 2 blocks of slop for non-aligned writes.
    // this really belongs lower down, since writei()
    // might be writing a device like the console.
    int max = ((LOGSIZE-1-1-2) / 2) * 512;
    int i = 0;
    while(i < n){
      int n1 = n - i;
      if(n1 > max)
        n1 = max;

      begin_trans();
      ilock(f->ip);
      if ((r = writei(f->ip, addr + i, f->off, n1)) > 0)
        f->off += r;
      iunlock(f->ip);
      commit_trans();

      if(r < 0)
        break;
      if(r != n1)
        panic("short filewrite");
      i += r;
    }
    return i == n ? n : -1;
  }
  panic("filewrite");
}

int
filefdready(struct file *f, int forwrite)
{
  if(f == 0)
    return -1;
  if(f->type == FD_PIPE){
    if(forwrite)
      return pipecanwrite(f->pipe);
    return pipecanread(f->pipe);
  }
  if(f->type == FD_USOCK && f->usock_state == USOCK_CONN){
    if(forwrite)
      return filefdready(f->usock_tx, 1);
    return filefdready(f->usock_rx, 0);
  }
  if(f->type == FD_USOCK && f->usock_state == USOCK_LISTEN){
    if(forwrite)
      return 0;
    return usock_listencanaccept(f);
  }
  if(f->type == FD_TCPSOCK){
    if(f->tcp_slot < 0)
      return -1;
    return net_tcp_user_fdready(f->tcp_slot, forwrite, f->tcp_listen);
  }
  if(f->type == FD_UDPSOCK){
    if(f->udp_slot < 0)
      return -1;
    return net_udp_user_fdready(f->udp_slot, forwrite);
  }
  if(f->type == FD_INODE){
    if(forwrite)
      return f->writable;
    ilock(f->ip);
    if(f->ip->type == T_DEV && f->ip->major == CONSOLE){
      int r;
      r = consolecanread();
      iunlock(f->ip);
      return r;
    }
    if(f->ip->type == T_DEV && f->ip->major == DEVNULL){
      iunlock(f->ip);
      return 1;
    }
    iunlock(f->ip);
    return f->readable;
  }
  return -1;
}

int
fileseek(struct file *f, int offset, int whence)
{
  struct stat st;

  if(f->type != FD_INODE)
    return -1;
  ilock(f->ip);
  if(whence == 0){ /* SEEK_SET */
    if(offset < 0){
      iunlock(f->ip);
      return -1;
    }
    f->off = (uint)offset;
  } else if(whence == 1){ /* SEEK_CUR */
    if((int)f->off + offset < 0){
      iunlock(f->ip);
      return -1;
    }
    f->off = (uint)((int)f->off + offset);
  } else if(whence == 2){ /* SEEK_END */
    stati(f->ip, &st);
    if((int)st.size + offset < 0){
      iunlock(f->ip);
      return -1;
    }
    f->off = st.size + (uint)offset;
  } else {
    iunlock(f->ip);
    return -1;
  }
  iunlock(f->ip);
  return (int)f->off;
}


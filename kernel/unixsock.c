#include "types.h"
#include "defs.h"
#include "param.h"
#include "mmu.h"
#include "fs.h"
#include "proc.h"
#include "spinlock.h"
#include "file.h"
#include "usock.h"

#define NUSOCK 32
#define USOCK_PENDING_MAX 16

struct usock_listener {
  struct spinlock lock;
  int used;
  int listening;
  int backlog;
  char path[USOCK_PATH_MAX];
  struct file *pending[USOCK_PENDING_MAX];
  int head;
  int tail;
  int cnt;
};

static struct spinlock usock_lock;
static struct usock_listener listeners[NUSOCK];

static void
clear_listener_pending(struct usock_listener *l)
{
  struct file *pending[USOCK_PENDING_MAX];
  int n = 0;
  int i;

  acquire(&l->lock);
  while(l->cnt > 0){
    pending[n++] = l->pending[l->head];
    l->pending[l->head] = 0;
    l->head = (l->head + 1) % USOCK_PENDING_MAX;
    l->cnt--;
  }
  l->head = l->tail = 0;
  wakeup(l);
  release(&l->lock);

  for(i = 0; i < n; i++)
    fileclose(pending[i]);
}

void
usockinit(void)
{
  int i, j;
  initlock(&usock_lock, "usock");
  for(i = 0; i < NUSOCK; i++){
    initlock(&listeners[i].lock, "usockl");
    listeners[i].used = 0;
    listeners[i].listening = 0;
    listeners[i].backlog = 0;
    listeners[i].path[0] = 0;
    listeners[i].head = listeners[i].tail = listeners[i].cnt = 0;
    for(j = 0; j < USOCK_PENDING_MAX; j++)
      listeners[i].pending[j] = 0;
  }
}

static int
find_listener_locked(char *path)
{
  int i;
  for(i = 0; i < NUSOCK; i++){
    if(listeners[i].used && strncmp(listeners[i].path, path, USOCK_PATH_MAX) == 0)
      return i;
  }
  return -1;
}

int
usock_bind(struct file *f, char *path)
{
  int i;
  int len;

  if(f == 0 || f->type != FD_USOCK || f->usock_state != USOCK_INIT || f->usock_lid >= 0)
    return -1;

  len = strlen(path);
  if(len <= 0 || len >= USOCK_PATH_MAX)
    return -1;

  acquire(&usock_lock);
  if(find_listener_locked(path) >= 0){
    release(&usock_lock);
    return -1;
  }

  for(i = 0; i < NUSOCK; i++){
    if(!listeners[i].used){
      listeners[i].used = 1;
      listeners[i].listening = 0;
      listeners[i].backlog = 0;
      safestrcpy(listeners[i].path, path, USOCK_PATH_MAX);
      listeners[i].head = listeners[i].tail = listeners[i].cnt = 0;
      release(&usock_lock);
      f->usock_lid = i;
      f->usock_state = USOCK_BOUND;
      return 0;
    }
  }

  release(&usock_lock);
  return -1;
}

int
usock_listen(struct file *f, int backlog)
{
  struct usock_listener *l;

  if(f == 0 || f->type != FD_USOCK || f->usock_state != USOCK_BOUND || f->usock_lid < 0)
    return -1;

  if(backlog <= 0)
    backlog = 1;
  if(backlog > USOCK_PENDING_MAX)
    backlog = USOCK_PENDING_MAX;

  l = &listeners[f->usock_lid];
  acquire(&l->lock);
  if(!l->used){
    release(&l->lock);
    return -1;
  }
  l->listening = 1;
  l->backlog = backlog;
  wakeup(l);
  release(&l->lock);
  f->usock_state = USOCK_LISTEN;
  return 0;
}

static struct file*
make_endpoint(struct file *rx, struct file *tx)
{
  struct file *f = filealloc();
  if(f == 0)
    return 0;
  f->type = FD_USOCK;
  f->readable = 1;
  f->writable = 1;
  f->usock_state = USOCK_CONN;
  f->usock_lid = -1;
  f->usock_rx = rx;
  f->usock_tx = tx;
  return f;
}

int
usock_connect(struct file *f, char *path)
{
  int lid;
  struct usock_listener *l;
  struct file *c2s_r = 0, *c2s_w = 0, *s2c_r = 0, *s2c_w = 0;
  struct file *server_ep = 0;

  if(f == 0 || f->type != FD_USOCK || f->usock_state != USOCK_INIT || f->usock_lid >= 0)
    return -1;
  if(strlen(path) <= 0 || strlen(path) >= USOCK_PATH_MAX)
    return -1;

  acquire(&usock_lock);
  lid = find_listener_locked(path);
  release(&usock_lock);
  if(lid < 0)
    return -1;

  l = &listeners[lid];
  if(pipealloc(&c2s_r, &c2s_w) < 0)
    goto bad;
  if(pipealloc(&s2c_r, &s2c_w) < 0)
    goto bad;
  server_ep = make_endpoint(c2s_r, s2c_w);
  if(server_ep == 0)
    goto bad;

  acquire(&l->lock);
  if(!l->used || !l->listening || l->cnt >= l->backlog){
    release(&l->lock);
    goto bad;
  }
  l->pending[l->tail] = server_ep;
  l->tail = (l->tail + 1) % USOCK_PENDING_MAX;
  l->cnt++;
  wakeup(l);
  release(&l->lock);

  f->readable = 1;
  f->writable = 1;
  f->usock_state = USOCK_CONN;
  f->usock_rx = s2c_r;
  f->usock_tx = c2s_w;
  return 0;

bad:
  if(server_ep)
    fileclose(server_ep);
  else {
    if(c2s_r) fileclose(c2s_r);
    if(c2s_w) fileclose(c2s_w);
    if(s2c_r) fileclose(s2c_r);
    if(s2c_w) fileclose(s2c_w);
  }
  return -1;
}

int
usock_listencanaccept(struct file *f)
{
  struct usock_listener *l;
  int n;

  if(f == 0 || f->type != FD_USOCK || f->usock_state != USOCK_LISTEN || f->usock_lid < 0)
    return -1;

  l = &listeners[f->usock_lid];
  acquire(&l->lock);
  n = (l->used && l->listening && l->cnt > 0) ? 1 : 0;
  release(&l->lock);
  return n;
}

int
usock_accept(struct file *f, struct file **out)
{
  struct usock_listener *l;
  struct file *ep;

  if(f == 0 || out == 0)
    return -1;
  if(f->type != FD_USOCK || f->usock_state != USOCK_LISTEN || f->usock_lid < 0)
    return -1;

  l = &listeners[f->usock_lid];
  acquire(&l->lock);
  while(l->used && l->listening && l->cnt == 0){
    if(proc->killed){
      release(&l->lock);
      return -1;
    }
    sleep(l, &l->lock);
  }
  if(!l->used || !l->listening || l->cnt <= 0){
    release(&l->lock);
    return -1;
  }

  ep = l->pending[l->head];
  l->pending[l->head] = 0;
  l->head = (l->head + 1) % USOCK_PENDING_MAX;
  l->cnt--;
  release(&l->lock);

  *out = ep;
  return 0;
}

void
usock_fileclose(struct file *f)
{
  struct usock_listener *l;
  int lid;

  if(f == 0 || f->type != FD_USOCK)
    return;

  if(f->usock_state == USOCK_CONN){
    if(f->usock_rx){
      fileclose(f->usock_rx);
      f->usock_rx = 0;
    }
    if(f->usock_tx){
      fileclose(f->usock_tx);
      f->usock_tx = 0;
    }
    f->usock_state = USOCK_INIT;
    return;
  }

  if((f->usock_state == USOCK_BOUND || f->usock_state == USOCK_LISTEN) && f->usock_lid >= 0){
    lid = f->usock_lid;
    acquire(&usock_lock);
    if(lid < 0 || lid >= NUSOCK || !listeners[lid].used){
      release(&usock_lock);
      return;
    }
    l = &listeners[lid];
    l->used = 0;
    l->listening = 0;
    l->backlog = 0;
    l->path[0] = 0;
    release(&usock_lock);
    clear_listener_pending(l);
    f->usock_lid = -1;
    f->usock_state = USOCK_INIT;
  }
}

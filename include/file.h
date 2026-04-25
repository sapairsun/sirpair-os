struct file {
  enum { FD_NONE, FD_PIPE, FD_INODE, FD_USOCK, FD_TCPSOCK, FD_UDPSOCK } type;
  int ref; // reference count
  char readable;
  char writable;
  struct pipe *pipe;
  struct inode *ip;
  uint off;
  int usock_state;
  int usock_lid;
  struct file *usock_rx;
  struct file *usock_tx;
  int tcp_slot;
  char tcp_listen; /* IPv4 TCP：listen 套接字为 1，仅用于 accept，不可读写到连接数据 */
  int udp_slot;
};

#define USOCK_INIT   0
#define USOCK_BOUND  1
#define USOCK_LISTEN 2
#define USOCK_CONN   3


// in-memory copy of an inode
struct inode {
  uint dev;           // Device number
  uint inum;          // Inode number
  int ref;            // Reference count
  int flags;          // I_BUSY, I_VALID

  short type;         // copy of disk inode
  short major;
  short minor;
  short nlink;
  uint size;
  uint addrs[NDIRECT+2];
};
#define I_BUSY 0x1
#define I_VALID 0x2

// table mapping major device number to
// device functions
struct devsw {
  int (*read)(struct inode*, char*, int);
  int (*write)(struct inode*, char*, int);
};

extern struct devsw devsw[];

#define CONSOLE 1
#define DEVNULL 2

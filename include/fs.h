// On-disk file system format. 
// Both the kernel and user programs use this header file.

// Block 0 is unused.
// Block 1 is super block.
// Blocks 2 through sb.ninodes/IPB hold inodes.
// Then free bitmap blocks holding sb.size bits.
// Then sb.nblocks data blocks.
// Then sb.nlog log blocks.

#define ROOTINO 1  // root i-number
#define BSIZE 512  // block size

// File system super block
struct superblock {
  uint size;         // Size of file system image (blocks)
  uint nblocks;      // Number of data blocks
  uint ninodes;      // Number of inodes.
  uint nlog;         // Number of log blocks
};

#define NDIRECT 11
#define NINDIRECT (BSIZE / sizeof(uint))
#define NDINDIRECT (NINDIRECT * NINDIRECT)
#define MAXFILE (NDIRECT + NINDIRECT + NDINDIRECT)

// On-disk inode structure
struct dinode {
  short type;           // File type
  short major;          // Major device number (T_DEV only)
  short minor;          // Minor device number (T_DEV only)
  short nlink;          // Number of links to inode in file system
  uint size;            // Size of file (bytes)
  uint addrs[NDIRECT+2];   // Data block addresses (direct + indirect + double-indirect)
};

// Inodes per block.
#define IPB           (BSIZE / sizeof(struct dinode))

#define NINODEBLOCKS(ninodes) (((ninodes) + IPB - 1) / IPB)

// Block containing inode i
#define IBLOCK(i)     ((i) / IPB + 2)

// Bitmap bits per block
#define BPB           (BSIZE*8)

// Block containing bit for block b
#define BBLOCK(b, ninodes) (b/BPB + NINODEBLOCKS(ninodes) + 3)

// Directory is a file containing a sequence of dirent structures.
#define DIRSIZ 14

struct dirent {
  ushort inum;
  char name[DIRSIZ];
};

// statfs(2) 风格（与 Linux struct statfs 常用字段对齐；Sirpair 仅实现根设备）
struct statfs {
  uint f_bsize;   // 块大小（字节），固定为 BSIZE
  uint f_blocks;  // 数据块总数（与 superblock.nblocks 一致）
  uint f_bfree;   // 空闲数据块数
  uint f_bavail;  // 非超级用户可用块（与 f_bfree 相同）
  uint f_files;   // inode 槽位总数（ninodes）
  uint f_ffree;   // 空闲 inode 数（盘上 type==0，不含 0 号槽）
};


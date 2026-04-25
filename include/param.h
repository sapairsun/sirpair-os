// 系统时钟滴答/秒（与内核 LAPIC 定时器标定目标一致；用户态 uptime 等须与此一致）
#define HZ           100

#define NPROC        64  // maximum number of processes
#define KSTACKSIZE 4096  // size of per-process kernel stack
#define NCPU          8  // maximum number of CPUs（ThinkPad X220 为四逻辑处理器；Makefile 中 make qemu 默认 CPUS=4）
#define NOFILE       16  // open files per process
#define NFILE       100  // open files per system
#define NBUF         10  // size of disk block cache
#define NINODE       50  // maximum number of active i-nodes
#define NDEV         10  // maximum major device number
#define ROOTDEV       1  // device number of file system root disk
#define MAXARG       32  // max exec arguments
// exec() 中用户栈：1 页 guard（PTE_U 清除）+ USTACK_PAGES 页可访问栈。
// Lua 的 luaL_loadfilex 等含 ~4KB 局部缓冲 + 栈探测，单页栈会在 orl (%esp) 处缺页。
#define USTACK_PAGES 16
#define LOGSIZE      10  // max data sectors in on-disk log

// 合并映像布局: sirpair-kernel.img
// 扇区 0:                引导扇区 (MBR)
// 扇区 1 ~ FSOFF-1:     内核 ELF + 填充
// 扇区 FSOFF ~ FSOFF+FSSIZE-1: 文件系统
#define FS_SECTOR_OFFSET 10000  // 文件系统在合并映像中的起始扇区
#define FS_SIZE          65536  // 文件系统大小(扇区数, 32MiB)

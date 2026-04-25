// Routines to let C code use special x86 instructions.

static inline uchar
inb(ushort port)
{
  uchar data;

  asm volatile("in %1,%0" : "=a" (data) : "d" (port));
  return data;
}

static inline ushort
inw(ushort port)
{
  ushort data;

  asm volatile("in %1,%0" : "=a" (data) : "d" (port));
  return data;
}

static inline uint
inl(ushort port)
{
  uint data;

  asm volatile("inl %1,%0" : "=a" (data) : "d" (port));
  return data;
}

static inline void
outl(ushort port, uint data)
{
  asm volatile("outl %0,%1" : : "a" (data), "d" (port));
}

static inline void
insl(int port, void *addr, int cnt)
{
  asm volatile("cld; rep insl" :
               "=D" (addr), "=c" (cnt) :
               "d" (port), "0" (addr), "1" (cnt) :
               "memory", "cc");
}

static inline void
outb(ushort port, uchar data)
{
  asm volatile("out %0,%1" : : "a" (data), "d" (port));
}

static inline void
outw(ushort port, ushort data)
{
  asm volatile("out %0,%1" : : "a" (data), "d" (port));
}

static inline void
outsl(int port, const void *addr, int cnt)
{
  asm volatile("cld; rep outsl" :
               "=S" (addr), "=c" (cnt) :
               "d" (port), "0" (addr), "1" (cnt) :
               "cc");
}

static inline void
stosb(void *addr, int data, int cnt)
{
  asm volatile("cld; rep stosb" :
               "=D" (addr), "=c" (cnt) :
               "0" (addr), "1" (cnt), "a" (data) :
               "memory", "cc");
}

static inline void
stosl(void *addr, int data, int cnt)
{
  asm volatile("cld; rep stosl" :
               "=D" (addr), "=c" (cnt) :
               "0" (addr), "1" (cnt), "a" (data) :
               "memory", "cc");
}

struct segdesc;

static inline void
lgdt(struct segdesc *p, int size)
{
  volatile ushort pd[3];

  pd[0] = size-1;
  pd[1] = (uint)p;
  pd[2] = (uint)p >> 16;

  asm volatile("lgdt (%0)" : : "r" (pd));
}

struct gatedesc;

static inline void
lidt(struct gatedesc *p, int size)
{
  volatile ushort pd[3];

  pd[0] = size-1;
  pd[1] = (uint)p;
  pd[2] = (uint)p >> 16;

  asm volatile("lidt (%0)" : : "r" (pd));
}

static inline void
ltr(ushort sel)
{
  asm volatile("ltr %0" : : "r" (sel));
}

static inline uint
readeflags(void)
{
  uint eflags;
  asm volatile("pushfl; popl %0" : "=r" (eflags));
  return eflags;
}

static inline void
loadgs(ushort v)
{
  asm volatile("movw %0, %%gs" : : "r" (v));
}

static inline void
cli(void)
{
  asm volatile("cli");
}

static inline void
sti(void)
{
  asm volatile("sti");
}

static inline uint
xchg(volatile uint *addr, uint newval)
{
  uint result;
  
  // The + in "+m" denotes a read-modify-write operand.
  asm volatile("lock; xchgl %0, %1" :
               "+m" (*addr), "=a" (result) :
               "1" (newval) :
               "cc");
  return result;
}

static inline uint
rcr2(void)
{
  uint val;
  asm volatile("movl %%cr2,%0" : "=r" (val));
  return val;
}

static inline void
lcr3(uint val) 
{
  asm volatile("movl %0,%%cr3" : : "r" (val));
}

static inline uint
rcr3(void)
{
  uint val;
  asm volatile("movl %%cr3,%0" : "=r" (val));
  return val;
}

// 与 PCI 网卡 DMA 的写回缓存一致性：CPU 写入的发送缓冲须回写到内存后设备才能读到；
// 设备写入的接收描述符/缓冲须在 CPU 读前使缓存行失效，否则真机上会出现 DD 位不更新、乱码类型字段等。
#define CACHE_LINE_SIZE 64

static inline void
mfence(void)
{
  asm volatile("mfence" ::: "memory");
}

static inline void
clflush_line(void *p)
{
  asm volatile("clflush (%0)" :: "r" (p) : "memory");
}

static inline void
clflush_range(void *p, uint len)
{
  uchar *start;
  uchar *a;
  uchar *end;

  if(len == 0)
    return;
  start = (uchar*)p;
  end = start + len;
  a = (uchar*)((uint)start & ~(CACHE_LINE_SIZE - 1));
  for(; a < end; a += CACHE_LINE_SIZE)
    clflush_line(a);
}

// 整处理器写回并无效化缓存；用于少数网卡/南桥上仅靠按行刷新仍无法让 PCI 直接内存访问看到发送缓冲的情况（代价高，仅在对症路径调用）。
static inline void
wbinvd(void)
{
  asm volatile("wbinvd" ::: "memory");
}

//PAGEBREAK: 36
// Layout of the trap frame built on the stack by the
// hardware and by trapasm.S, and passed to trap().
struct trapframe {
  // registers as pushed by pusha
  uint edi;
  uint esi;
  uint ebp;
  uint oesp;      // useless & ignored
  uint ebx;
  uint edx;
  uint ecx;
  uint eax;

  // rest of trap frame
  ushort gs;
  ushort padding1;
  ushort fs;
  ushort padding2;
  ushort es;
  ushort padding3;
  ushort ds;
  ushort padding4;
  uint trapno;

  // below here defined by x86 hardware
  uint err;
  uint eip;
  ushort cs;
  ushort padding5;
  uint eflags;

  // below here only when crossing rings, such as from user to kernel
  uint esp;
  ushort ss;
  ushort padding6;
};

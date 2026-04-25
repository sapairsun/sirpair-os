#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"


static void startothers(void);
static void mpmain(void)  __attribute__((noreturn));
static void print_boot_banner(void);
static void print_boot_summary(void);
static void boot_stage(const char *msg);
static uint detect_phystop(void);
extern pde_t *kpgdir;
extern char end[]; // first address after kernel loaded from ELF file

// Detected physical memory top (set at boot, used by vm.c, kalloc.c)
uint phystop;

// AP synchronization: APs wait in mpmain until BSP sets this to 1.
// This ensures all boot output (boot[1]-boot[7]) is printed before
// any AP enters the scheduler, preventing race conditions on real HW.
static volatile int ap_go = 0;

// Bootstrap processor starts running C code here.
// Allocate a real stack and switch to it, first
// doing some setup required for memory allocator to work.
int
main(void)
{
  kinit1(end, P2V(16*1024*1024)); // phys page allocator (16MB, matches entrypgdir)
  phystop = detect_phystop();     // detect actual physical memory
  /*
   * setupkvm→acquire→pushcli 会访问 per-cpu 的 cpu 指针（经 %gs）。
   * 必须在 seginit() 建立 GDT 与 loadgs(SEG_KCPU) 之后再 kvmalloc。
   */
  seginit();
  setupkvm_lock_init();          // 先于 setupkvm/kvmalloc，避免未初始化锁
  kvmalloc();      // kernel page table
  uartearlyinit(); // early serial output for SMP diagnostics
  cprintf("Sirpair OS Booting...");
  boot_ok();
  mpinit();        // collect info about this machine
  lapicinit(mpbcpu());
  picinit();       // interrupt controller
  ioapicinit();    // another interrupt controller
  consoleinit();   // I/O devices & their interrupts
  nullinit();      // /null 设备（后台任务 stdin）
  uartinit();      // serial port

  boot_stage("Physical page allocator (first chunk) and RAM top ready");
  boot_stage("Kernel page tables and KVA map established");
  boot_stage("Early UART output available");
  boot_stage("MP tables and local interrupt controller initialized");
  boot_stage("Segments, PICs and I/O APIC initialized");
  boot_stage("Console, null device and UART driver ready");

  // Print boot banner (after console/UART init so it shows everywhere)
  print_boot_banner();

  pinit();         // process table
  boot_stage("Process table initialized");
  tvinit();        // trap vectors
  idtinit();       // 加载 IDT：须早于 net_dhcp_acquire 等依赖 ticks 的路径，否则 LAPIC 定时器无法累加 ticks
  boot_stage("Trap vectors installed");
  rtc_init();      // 仅 initlock，墙钟在首次 ktime_now/sys_time 时惰性读 CMOS（避免 QEMU 早期访问 0x70/0x71 异常）
  boot_stage("RTC interface registered");
  binit();         // buffer cache
  boot_stage("Buffer cache ready");
  fileinit();      // file table
  boot_stage("File descriptor table ready");
  usockinit();     // unix domain socket table
  boot_stage("Socket table ready");
  iinit();         // inode cache
  boot_stage("Inode cache ready");

  boot_stage("Scanning PCI and USB mass storage (may take a while)...");
  diskinit();      // disk (USB mass storage init)

  boot_stage("Initializing wired NIC and network stack (may take a while)...");
  /*
   * net_dhcp_acquire 依赖 ticks（LAPIC 定时器中断 → trap）。须在 netinit 前开中断，
   * 否则 IF=0 时 ticks 不前进，DHCP 会无限阻塞直至外层测试超时。
   */
  sti();
  netinit();       // 有线网卡：真机 82579LM；QEMU 为 e1000e 设备
  sirpair_microps_boot_phase(net_dhcp_acquire() == 0); /* DHCP 用 net.c；成功后收发包走 microps */
  boot_stage("Network stack initialized");

  // Initialize filesystem log BEFORE starting APs.
  // initlog() reads the log header from USB disk (via bread/usb_rw).
  // Moving it here ensures all boot-time USB I/O is single-threaded.
  boot_stage("Reading superblock and recovering filesystem log (disk I/O)...");
  initlog();
  boot_stage("Journaling filesystem metadata ready");

  if(!ismp){
    timerinit();   // uniprocessor timer
    boot_stage("Uniprocessor timer enabled");
  } else
    boot_stage("Multiprocessor: timer from local APIC");
  boot_stage("Starting application processors...");
  startothers();   // start other processors
  kinit2(P2V(16*1024*1024), P2V(phystop)); // must come after startothers()
  boot_stage("Extended physical page pool ready");

  /* 帧缓冲页表映射依赖 kinit2 扩展后的物理页池 */
  boot_stage("Mapping VRAM and initializing framebuffer (full-screen clear; long black screen is normal)...");
  console_fbinit();
  boot_stage("Framebuffer console active");

  // Print boot summary (all subsystems initialized)
  print_boot_summary();

  boot_stage("Creating first user process (/init)...");
  userinit();      // first user process（磁盘已在 diskinit 末尾切为 USB 轮询）

  // Release APs into scheduler AFTER all BSP output is done.
  ap_go = 1;
  mpmain();
}

/* 串口可见的启动阶段提示，便于定位黑屏或长时间停顿发生在哪一步 */
static void
boot_stage(const char *msg)
{
  cprintf("boot: %s", msg);
  boot_ok();
}

// Print boot banner with OS name and hardware info
static void
print_boot_banner(void)
{
  // CPU identification via CPUID
  uint eax, ebx, ecx, edx;
  char brand[49];
  int i;

  // Get CPU brand string via CPUID(0x80000002-4) if supported
  asm volatile("cpuid"
    : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
    : "a"(0x80000000));
  if(eax >= 0x80000004){
    for(i = 0; i < 3; i++){
      asm volatile("cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(0x80000002 + i));
      memmove(brand + i*16 + 0,  &eax, 4);
      memmove(brand + i*16 + 4,  &ebx, 4);
      memmove(brand + i*16 + 8,  &ecx, 4);
      memmove(brand + i*16 + 12, &edx, 4);
    }
    brand[48] = 0;
    // Skip leading spaces
    char *b = brand;
    while(*b == ' ') b++;
    cprintf("CPU: %s", b);
    boot_ok();
  }
  cprintf("Mem: %d MB", phystop / (1024 * 1024));
  boot_ok();
}

// Print boot summary after all subsystems initialized
static void
print_boot_summary(void)
{
  cprintf("SMP: %d core(s)", ncpu);
  boot_ok();
  cprintf("Disk: USB (EHCI)");
  boot_ok();
}

// Other CPUs jump here from entryother.S.
static void
mpenter(void)
{
  switchkvm(); 
  seginit();
  lapicinit(cpunum());
  mpmain();
}

// Common CPU setup code.
static void
mpmain(void)
{
  idtinit();       // load idt register
  xchg(&cpu->started, 1); // tell startothers() we're up

  // APs: wait for BSP to finish all boot initialization before entering
  // the scheduler. This prevents APs from picking up initcode and doing
  // USB I/O or triggering hardware exceptions while BSP is still printing.
  // BSP skips this (ap_go is set by BSP in main() before calling mpmain).
  // BSP 的本地 APIC ID 未必等于 mpbcpu() 下标，须用 per-cpu 指针判断。
  if(cpu != &cpus[mpbcpu()]){
    while(ap_go == 0)
      asm volatile("pause");
  }

  scheduler();     // start running processes
}

// E820 memory map entry (24 bytes, filled by bootloader at physical 0x8000)
struct e820entry {
  uint base_lo;
  uint base_hi;
  uint len_lo;
  uint len_hi;
  uint type;
  uint acpi;
};

#define E820_MAP_ADDR  0x8000   // Physical address where bootloader stores E820 map
#define E820_USABLE    1        // Usable RAM

// Detect physical memory via E820 map (stored by bootloader at 0x8000).
// Falls back to CMOS if E820 is unavailable.
static uint
detect_phystop(void)
{
  // --- Method 1: E820 memory map from bootloader ---
  // The bootloader stores: uint32 count at 0x8000, then 24-byte entries at 0x8004
  uint *e820_count = (uint*)P2V(E820_MAP_ADDR);
  struct e820entry *entries = (struct e820entry*)P2V(E820_MAP_ADDR + 4);
  uint count = *e820_count;
  uint mem = 0;
  uint i;

  if(count > 0 && count <= 32){
    // Find highest usable memory address
    for(i = 0; i < count; i++){
      if(entries[i].type == E820_USABLE && entries[i].base_hi == 0){
        uint end = entries[i].base_lo + entries[i].len_lo;
        // Only consider if no overflow and within 32-bit range
        if(end > entries[i].base_lo && end > mem)
          mem = end;
      }
    }
  }

  // --- Method 2: CMOS fallback (if E820 failed or returned 0) ---
  if(mem == 0){
    uint ext_low_kb, ext_high_64k;

    // Read extended memory below 16 MB (in KB) from CMOS
    outb(0x70, 0x17);
    ext_low_kb = inb(0x71);
    outb(0x70, 0x18);
    ext_low_kb |= (uint)inb(0x71) << 8;

    // Read extended memory above 16 MB (in 64 KB blocks) from CMOS
    outb(0x70, 0x34);
    ext_high_64k = inb(0x71);
    outb(0x70, 0x35);
    ext_high_64k |= (uint)inb(0x71) << 8;

    // Total = 1 MB base + extended below 16 MB + extended above 16 MB
    mem = 0x100000 + ext_low_kb * 1024 + ext_high_64k * 65536;
  }

  // --- Fallback: if nothing works, assume 256 MB ---
  if(mem <= 0x100000)
    mem = 256 * 1024 * 1024;

  // Cap at PHYSTOP (virtual address space limit = DEVSPACE - KERNBASE)
  if(mem > PHYSTOP)
    mem = PHYSTOP;

  // Align down to page boundary
  mem = mem & ~(PGSIZE - 1);

  return mem;
}

pde_t entrypgdir[];  // For entry.S

// Start the non-boot (AP) processors.
static void
startothers(void)
{
  extern uchar _binary_entryother_start[], _binary_entryother_size[];
  uchar *code;
  struct cpu *c;
  char *stack;
  int ap_started = 0;

  if(ncpu <= 1){
    cprintf("smp: single CPU, no AP to start");
    boot_ok();
    return;
  }
  cprintf("smp: starting %d AP(s)...", ncpu - 1);
  boot_ok();

  // Write entry code to unused memory at 0x7000.
  // The linker has placed the image of entryother.S in
  // _binary_entryother_start.
  code = p2v(0x7000);
  memmove(code, _binary_entryother_start, (uint)_binary_entryother_size);

  for(c = cpus; c < cpus+ncpu; c++){
    if(c == cpus+cpunum())  // We've started already.
      continue;

    // Tell entryother.S what stack to use, where to enter, and what 
    // pgdir to use. We cannot use kpgdir yet, because the AP processor
    // is running in low  memory, so we use entrypgdir for the APs too.
    stack = kalloc();
    *(void**)(code-4) = stack + KSTACKSIZE;
    *(void**)(code-8) = mpenter;
    *(int**)(code-12) = (void *) v2p(entrypgdir);

    lapicstartap(c->id, v2p(code));

    // Wait for AP to finish mpmain() - with timeout to prevent hang
    // on real hardware if AP fails to start.
    {
      volatile int timeout;
      for(timeout = 0; timeout < 50000000; timeout++){
        if(c->started != 0)
          break;
        asm volatile("pause");
      }
      if(c->started == 0){
        // Free the stack we allocated for this AP
        kfree(stack);
        continue;
      }
    }
    ap_started++;
  }
}

// Boot page table used in entry.S and entryother.S.
// Page directories (and page tables), must start on a page boundary,
// hence the "__aligned__" attribute.  
// Use PTE_PS in page directory entry to enable 4Mbyte pages.
//
// CRITICAL: Map 16MB (not just 4MB) at KERNBASE.
// With phystop=1792MB and DEVSPACE=0xF0000000, kvmalloc needs ~514
// page tables, consuming most of kinit1's pool (only 736 pages from
// 4MB). Extending to 16MB gives kinit1 ~3800 pages, enough for
// kvmalloc + ACPI mapping + USB init + AP stacks on real hardware.
__attribute__((__aligned__(PGSIZE)))
pde_t entrypgdir[NPDENTRIES] = {
  // Map VA's [0, 4MB) to PA's [0, 4MB) - identity map for boot transition
  [0] = (0) | PTE_P | PTE_W | PTE_PS,
  // Map VA's [KERNBASE, KERNBASE+16MB) to PA's [0, 16MB)
  [KERNBASE>>PDXSHIFT]     = (0)         | PTE_P | PTE_W | PTE_PS,
  [(KERNBASE>>PDXSHIFT)+1] = (0x400000)  | PTE_P | PTE_W | PTE_PS,
  [(KERNBASE>>PDXSHIFT)+2] = (0x800000)  | PTE_P | PTE_W | PTE_PS,
  [(KERNBASE>>PDXSHIFT)+3] = (0xC00000)  | PTE_P | PTE_W | PTE_PS,
};

//PAGEBREAK!
// Blank page.


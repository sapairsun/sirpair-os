struct buf;
struct context;
struct file;
struct inode;
struct pinfo;
struct pipe;
struct proc;
struct spinlock;
struct stat;
struct statfs;
struct superblock;
struct net_stats;
struct net_cfg;

#include "mouse_if.h"

// bio.c
void            binit(void);
struct buf*     bread(uint, uint);
void            brelse(struct buf*);
void            bwrite(struct buf*);

// console.c
void            consoleinit(void);
void            console_fbinit(void);
void            console_cursor_tick(void);
void            early_uart_mirror(int);
int             consolecanread(void);

// null.c
void            nullinit(void);
void            cprintf(char*, ...);
void            uartcprintf(char*, ...);
void            boot_ok(void);
void            boot_fail(void);
void            consoleintr(int(*)(void));
void            panic(char*) __attribute__((noreturn));

// exec.c
int             exec(char*, char**);

// file.c
struct file*    filealloc(void);
void            fileclose(struct file*);
struct file*    filedup(struct file*);
void            fileinit(void);
int             fileread(struct file*, char*, int n);
int             filestat(struct file*, struct stat*);
int             filewrite(struct file*, char*, int n);
int             fileseek(struct file*, int, int);
int             filefdready(struct file*, int forwrite);

// pipe.c
int             pipecanread(struct pipe*);
int             pipecanwrite(struct pipe*);

// unixsock.c
void            usockinit(void);
int             usock_bind(struct file*, char*);
int             usock_listen(struct file*, int);
int             usock_connect(struct file*, char*);
int             usock_accept(struct file*, struct file**);
int             usock_listencanaccept(struct file*);
void            usock_fileclose(struct file*);

// fs.c
void            readsb(int dev, struct superblock *sb);
int             dirlink(struct inode*, char*, uint);
struct inode*   dirlookup(struct inode*, char*, uint*);
struct inode*   ialloc(uint, short);
struct inode*   idup(struct inode*);
void            iinit(void);
void            ilock(struct inode*);
void            iput(struct inode*);
void            iunlock(struct inode*);
void            iunlockput(struct inode*);
void            itrunc(struct inode*);
void            iupdate(struct inode*);
int             namecmp(const char*, const char*);
struct inode*   namei(char*);
struct inode*   nameiparent(char*, char*);
int             readi(struct inode*, char*, uint, uint);
void            stati(struct inode*, struct stat*);
int             writei(struct inode*, char*, uint, uint);
void            fillstatfs(int, struct statfs*);

// disk.c (disk driver - USB only)
void            diskinit(void);
void            diskrw(struct buf*);

// ioapic.c
void            ioapicenable(int irq, int cpu);
void            ioapicenable_level(int irq, int cpu);
void            ioapicdisable(int irq);
extern uchar    ioapicid;
void            ioapicinit(void);

// kalloc.c
char*           kalloc(void);
void            kfree(char*);
void            kinit1(void*, void*);
void            kinit2(void*, void*);
int             kfreepages(void);

// main.c
extern uint     phystop;          // detected physical memory top

// kbd.c
void            kbdintr(void);

// lapic.c
int             cpunum(void);
extern volatile uint*    lapic;
void            lapiceoi(void);
void            lapicinit(int);
void            lapicstartap(uchar, uint);
void            microdelay(int);

// log.c
void            initlog(void);
void            log_write(struct buf*);
void            begin_trans();
void            commit_trans();

// mp.c
extern int      ismp;
int             mpbcpu(void);
void            mpinit(void);
void            mpstartthem(void);
void*           acpi_map_phys(uint pa);

// picirq.c
void            picenable(int);
void            picinit(void);

// pipe.c
int             pipealloc(struct file**, struct file**);
void            pipeclose(struct pipe*, int);
int             piperead(struct pipe*, char*, int);
int             pipewrite(struct pipe*, char*, int);

//PAGEBREAK: 16
// proc.c
struct proc*    copyproc(struct proc*);
void            exit(void);
int             fork(void);
int             growproc(int);
int             kill(int);
void            setfgpid(int);
int             killfgproc(void);
int             proc_is_killed_current(void);
void            pinit(void);
void            procdump(void);
void            reboot_prepare_cpus(void);
void            reboot_halt_if_requested(void);
int             rebooting(void);
void            scheduler(void) __attribute__((noreturn));
void            sched(void);
void            sleep(void*, struct spinlock*);
void            userinit(void);
int             wait(void);
void            wakeup(void*);
void            yield(void);
int             getprocs(struct pinfo*, int);
int             pgdir_user_pt_shared_with_other(pde_t *owner, uint pde_pa);
int             proc_user_pt_page_in_use(uint pde_pa);

// swtch.S
void            swtch(struct context**, struct context*);

// spinlock.c
void            acquire(struct spinlock*);
void            getcallerpcs(void*, uint*);
int             holding(struct spinlock*);
void            initlock(struct spinlock*, char*);
void            release(struct spinlock*);
void            pushcli(void);
void            popcli(void);

// string.c
int             memcmp(const void*, const void*, uint);
void*           memmove(void*, const void*, uint);
void*           memset(void*, int, uint);
char*           safestrcpy(char*, const char*, int);
int             strlen(const char*);
int             strncmp(const char*, const char*, uint);
char*           strncpy(char*, const char*, int);

// syscall.c
int             argint(int, int*);
int             argptr(int, char**, int);
int             argstr(int, char**);
int             fetchint(struct proc*, uint, int*);
int             fetchstr(struct proc*, uint, char**);
void            syscall(void);

// timer.c
void            timerinit(void);

// rtc.c
void            rtc_init(void);
uint            ktime_now(void);

// trap.c
void            idtinit(void);
extern uint     ticks;
void            tvinit(void);
extern struct spinlock tickslock;

// uart.c
void            uartearlyinit(void);
void            uartinit(void);
void            uartintr(void);
void            uartputc(int);

// vm.c
void            seginit(void);
void            setupkvm_lock_init(void);
void            kvmalloc(void);
void            vmenable(void);
pde_t*          setupkvm(void);
pde_t*          setupkvm_impl(void);
void            setupkvm_lock_acquire(void);
void            setupkvm_lock_release(void);
char*           uva2ka(pde_t*, char*);
int             allocuvm(pde_t*, uint, uint);
int             allocuvm_nolock(pde_t*, uint, uint);
int             deallocuvm(pde_t*, uint, uint);
int             deallocuvm_nolock(pde_t*, uint, uint);
void            freevm(pde_t*);
void            inituvm(pde_t*, char*, uint);
int             loaduvm(pde_t*, char*, struct inode*, uint, uint);
pde_t*          copyuvm(pde_t*, uint);
void            switchuvm(struct proc*);
void            switchkvm(void);
int             copyout(pde_t*, uint, void*, uint);
void            clearpteu(pde_t *pgdir, char *uva);
int             mappages(pde_t*, void*, uint, uint, int);
void            vm_dump_user_mapping(pde_t *pgdir, uint va, const char *tag);

// pci.c
void            pciinit(void);

// mouse.c
void            mouse_intr(void);
int             mouse_hw_init(void);
int             mouse_pop(struct mouse_event*);

// net.c
void            netinit(void);
int             net_is_available(void);
int             net_get_irq(void);
void            net_get_mac(uchar mac[6]);
void            net_get_ipv4(uchar ip[4]);
void            net_get_dhcp_cfg(uchar gw[4], uchar mask[4], uchar dns[4], uint *lease_sec, int *ok);
void            net_get_cfg(struct net_cfg *cfg);
void            net_get_stats(struct net_stats *st);
int             net_tx_raw(void *data, int len);
void            net_intr(void);
void            net_poll(void);
int             net_dhcp_acquire(void);
int             net_ping_interrupted(void);
int             net_ping(uint dst_ip, int count);
int             net_dns_query(const char *name, uint *out_ip);
int             net_tcp_user_alloc(void);
int             net_tcp_user_bind(int slot, char *path);
int             net_tcp_user_listen(int slot);
int             net_tcp_user_accept(struct file *lf, struct file **out);
int             net_tcp_user_connect(int slot, uchar ip[4], ushort port);
void            net_tcp_user_close(int slot);
/* net_tcp_user_unref kind：仅关闭 accept 返回的连接 fd 时可能发 FIN 并回到 LISTEN */
#define TCP_UNREF_CLOSE_ACCEPTED 0
#define TCP_UNREF_CLOSE_LISTEN   1
#define TCP_UNREF_ACCEPT_FAIL    2
void            net_tcp_user_unref(int slot, int kind);
int             net_tcp_user_read(int slot, char *buf, int n, int listen_fd);
int             net_tcp_user_write(int slot, char *buf, int n, int listen_fd);
int             net_tcp_user_fdready(int slot, int forwrite, int listen_fd);
int             net_udp_user_alloc(void);
void            net_udp_user_unref(int slot);
void            net_udp_user_close(int slot);
int             net_udp_user_bind(int slot, char *path);
int             net_udp_user_recvfrom(int slot, char *buf, int n, uchar src_ip[4], int *src_port);
int             net_udp_user_sendto(int slot, char *buf, int n, uchar dst_ip[4], ushort dst_port);
int             net_udp_user_fdready(int slot, int forwrite);

// usb.c
int             usbinit(void);
int             usb_is_available(void);
void            usb_rw(struct buf*);
void            usb_intr(void);
int             usb_get_irq(void);
uint            usb_get_capacity(void);
uint            usb_get_nports(void);
uint            usb_get_ehci_base(void);
void            usb_disable_interrupts(void);
void            usb_halt_controller(void);

// USB interrupt synchronization (for timer-based timeout wakeup)
extern volatile int usb_xfer_done;
extern volatile int usb_waiting;

// kmalloc.c + microps TCP/IP（与 kalloc 页分配器并存的小块堆）
void            kmalloc_init(void);
void            *kmalloc(uint);
void            kmfree(void*);
void            sirpair_microps_selftest(void);
void            sirpair_microps_boot_phase(int dhcp_ok);
void            sirpair_microps_poll_timers(void);
void            sirpair_net_cfg_loopback(struct net_cfg *cfg);

// number of elements in fixed-size array
#define NELEM(x) (sizeof(x)/sizeof((x)[0]))

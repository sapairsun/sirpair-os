#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
#include "pinfo.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static int
pgdir_share_user_pt(pde_t *a, pde_t *b)
{
  uint i;
  uint user_pde_top;

  if(a == 0 || b == 0)
    return 0;
  user_pde_top = PDX(KERNBASE);
  for(i = 0; i < user_pde_top; i++){
    pde_t pa = a[i];
    pde_t pb = b[i];
    if((pa & PTE_P) == 0 || (pb & PTE_P) == 0)
      continue;
    if(PTE_ADDR(pa) != 0 && PTE_ADDR(pa) == PTE_ADDR(pb))
      return 1;
  }
  return 0;
}

int
pgdir_user_pt_shared_with_other(pde_t *owner, uint pde_pa)
{
  struct proc *p;
  uint i;
  uint user_pde_top;

  if(owner == 0 || pde_pa == 0)
    return 0;
  user_pde_top = PDX(KERNBASE);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED || p->pgdir == 0 || p->pgdir == owner)
      continue;
    for(i = 0; i < user_pde_top; i++){
      if((p->pgdir[i] & PTE_P) == 0)
        continue;
      if(PTE_ADDR(p->pgdir[i]) == pde_pa)
        return 1;
    }
  }
  return 0;
}

int
proc_user_pt_page_in_use(uint pde_pa)
{
  struct proc *p;
  uint i;
  uint user_pde_top;

  if(pde_pa == 0)
    return 0;
  user_pde_top = PDX(KERNBASE);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED || p->pgdir == 0)
      continue;
    for(i = 0; i < user_pde_top; i++){
      pde_t pde = p->pgdir[i];
      if((pde & PTE_P) == 0)
        continue;
      if(PTE_ADDR(pde) == pde_pa)
        return 1;
    }
  }
  return 0;
}

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

static volatile uint reboot_request = 0;
static volatile uint reboot_ack[NCPU];
static int console_fgpid = 0;

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

int
rebooting(void)
{
  return reboot_request != 0;
}

void
reboot_halt_if_requested(void)
{
  if(reboot_request == 0 || cpu->id == mpbcpu())
    return;

  reboot_ack[cpu->id] = 1;
  cli();
  for(;;)
    asm volatile("hlt");
}

void
reboot_prepare_cpus(void)
{
  int i;
  int timeout;

  for(i = 0; i < NCPU; i++)
    reboot_ack[i] = 0;

  reboot_ack[cpu->id] = 1;
  reboot_request = 1;

  // Give APs a short window to notice the reboot request in either the
  // scheduler loop or timer interrupt path and park themselves.
  for(timeout = 0; timeout < 2000000; timeout++){
    int all_acked = 1;
    for(i = 0; i < ncpu; i++){
      if(i == cpu->id)
        continue;
      if(cpus[i].started && reboot_ack[i] == 0){
        all_acked = 0;
        break;
      }
    }
    if(all_acked)
      break;
    asm volatile("pause");
  }
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;
  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;
  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;
  
  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;
  
  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];
  
  p = allocproc();
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  p->state = RUNNABLE;
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  
  sz = proc->sz;
  /*
   * 与 exec/copyuvm 的 allocuvm 串行：定时器抢占下若 proc->sz 尚未提交而再次
   * growproc，或与其它路径交错修改同一 pgdir，可能对已映射 VA 再次 mappages 触发 remap。
   */
  setupkvm_lock_acquire();
  if(n > 0){
    uint newsz = sz + (uint)n;
    if(newsz < sz || newsz >= KERNBASE){
      setupkvm_lock_release();
      return -1;
    }
    if((sz = allocuvm_nolock(proc->pgdir, sz, newsz)) == 0){
      setupkvm_lock_release();
      return -1;
    }
  } else if(n < 0){
    /*
     * 收缩用户空间：必须用无符号差，禁止 newsz==0。
     * 若允许收缩到 0，deallocuvm_nolock 成功时返回 0（新大小），
     * 旧代码把「返回值 0」误判为失败且不更新 proc->sz，但页已被释放，
     * 进程继续跑用户态会在任意代码地址（如 sh 中 wait 桩的 ret）缺页。
     * n 为最小负整数时 -n 在 C 中无定义，须拒绝。
     */
    if(n == -2147483647 - 1){
      setupkvm_lock_release();
      return -1;
    }
    uint absn = (uint)(-n);
    if(sz <= absn){
      setupkvm_lock_release();
      return -1;
    }
    {
      uint newsz = sz - absn;
      sz = deallocuvm_nolock(proc->pgdir, sz, newsz);
    }
  }
  proc->sz = sz;
  setupkvm_lock_release();
  switchuvm(proc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;

  // Allocate process.
  if((np = allocproc()) == 0)
    return -1;

  // Copy process state from p.
  if((np->pgdir = copyuvm(proc->pgdir, proc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  if(np->pgdir == proc->pgdir){
    cprintf("[proc] fatal: fork got same pgdir as parent pid=%d parent=%d pgdir=%p\n",
            np->pid, proc->pid, np->pgdir);
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  if(pgdir_share_user_pt(proc->pgdir, np->pgdir)){
    cprintf("[proc] fatal: fork detected shared user page tables with parent pid=%d parent=%d child_pgdir=%p parent_pgdir=%p\n",
            np->pid, proc->pid, np->pgdir, proc->pgdir);
    freevm(np->pgdir);
    np->pgdir = 0;
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  if(strncmp(proc->name, "sh", 16) == 0 || strncmp(proc->name, "init", 16) == 0){
    uartcprintf("[proc dbg] fork parent(pid=%d,name=%s,pgdir=%p,sz=0x%x) -> child(pid=%d,pgdir=%p)\n",
                proc->pid, proc->name, proc->pgdir, proc->sz, np->pid, np->pgdir);
  }
  np->sz = proc->sz;
  np->parent = proc;
  *np->tf = *proc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(proc->ofile[i])
      np->ofile[i] = filedup(proc->ofile[i]);
  np->cwd = idup(proc->cwd);
 
  pid = np->pid;
  np->state = RUNNABLE;
  safestrcpy(np->name, proc->name, sizeof(proc->name));
  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *p;
  int fd;

  if(proc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(proc->ofile[fd]){
      fileclose(proc->ofile[fd]);
      proc->ofile[fd] = 0;
    }
  }

  begin_trans();
  iput(proc->cwd);
  commit_trans();
  proc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(proc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == proc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  proc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;

  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for zombie children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != proc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pde_t *zpgdir;
        pid = p->pid;
        if(proc && strncmp(proc->name, "sh", 16) == 0)
          vm_dump_user_mapping(proc->pgdir, 0x1dcf, "wait-parent-before-freevm");
        if(pid == 2 || strncmp(p->name, "dig", 16) == 0 || strncmp(p->name, "sh", 16) == 0){
          uartcprintf("[proc dbg] wait reap child(pid=%d,name=%s,pgdir=%p,sz=0x%x,parent=%d)\n",
                      pid, p->name, p->pgdir, p->sz, proc->pid);
        }
        kfree(p->kstack);
        p->kstack = 0;
        zpgdir = p->pgdir;
        if(zpgdir == proc->pgdir){
          cprintf("[proc] fatal: wait child pgdir equals parent, skip free parent=%d child=%d pgdir=%p\n",
                  proc->pid, pid, zpgdir);
          zpgdir = 0;
        }
        if(zpgdir && pgdir_share_user_pt(proc->pgdir, zpgdir)){
          cprintf("[proc] fatal: wait detected parent/child shared user page tables, skip free parent=%d child=%d pgdir=%p\n",
                  proc->pid, pid, zpgdir);
          zpgdir = 0;
        }
        if(zpgdir && (((uint)zpgdir % PGSIZE) != 0 || (uint)zpgdir < KERNBASE)){
          cprintf("[proc] fatal: wait invalid child pgdir pointer, skip free child=%d pgdir=%p\n",
                  pid, zpgdir);
          zpgdir = 0;
        }
        if(zpgdir)
          freevm(zpgdir);
        if(proc && strncmp(proc->name, "sh", 16) == 0)
          vm_dump_user_mapping(proc->pgdir, 0x1dcf, "wait-parent-after-freevm");
        p->pgdir = 0;
        p->state = UNUSED;
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || proc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(proc, &ptable.lock);  //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;

  for(;;){
    reboot_halt_if_requested();

    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      proc = p;
      switchuvm(p);
      p->cpuid = cpu->id;
      p->state = RUNNING;
      swtch(&cpu->scheduler, proc->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      proc = 0;
    }
    release(&ptable.lock);

  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state.
void
sched(void)
{
  int intena;

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(cpu->ncli != 1)
    panic("sched locks");
  if(proc->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = cpu->intena;
  swtch(&proc->context, cpu->scheduler);
  cpu->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  proc->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  if(proc == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }

  // Go to sleep.
  proc->chan = chan;
  proc->state = SLEEPING;
  sched();

  // Tidy up.
  proc->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

void
setfgpid(int pid)
{
  acquire(&ptable.lock);
  console_fgpid = pid;
  release(&ptable.lock);
}

int
killfgproc(void)
{
  int pid;

  acquire(&ptable.lock);
  pid = console_fgpid;
  release(&ptable.lock);

  if(pid > 0)
    return kill(pid);
  return -1;
}

int
proc_is_killed_current(void)
{
  return (proc && proc->killed) ? 1 : 0;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];
  
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}

// Get information about all active processes.
// Returns the number of processes copied into the pinfo array.
int
getprocs(struct pinfo *pinfos, int max)
{
  struct proc *p;
  int count = 0;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC] && count < max; p++){
    if(p->state == UNUSED)
      continue;
    pinfos[count].pid = p->pid;
    pinfos[count].ppid = (p->parent) ? p->parent->pid : 0;
    pinfos[count].state = p->state;
    pinfos[count].cpu_id = p->cpuid;
    pinfos[count].sz = p->sz;
    safestrcpy(pinfos[count].name, p->name, sizeof(pinfos[count].name));
    count++;
  }
  release(&ptable.lock);
  return count;
}
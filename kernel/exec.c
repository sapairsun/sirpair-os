#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "defs.h"
#include "x86.h"
#include "elf.h"

static int
pgdir_share_user_pt_page(pde_t *a, pde_t *b)
{
  uint i, top;

  if(a == 0 || b == 0)
    return 0;
  top = PDX(KERNBASE);
  for(i = 0; i < top; i++){
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
exec(char *path, char **argv)
{
  char *s, *last;
  const char *fail_reason;
  int i, off;
  uint argc, sz, sp, ustack[3+MAXARG+1];
  struct elfhdr elf;
  struct inode *ip;
  struct proghdr ph;
  pde_t *pgdir, *oldpgdir;

  fail_reason = "unknown";
  if((ip = namei(path)) == 0)
    return -1;
  ilock(ip);
  pgdir = 0;

  // Check ELF header
  if(readi(ip, (char*)&elf, 0, sizeof(elf)) < sizeof(elf))
    { fail_reason = "read ELF header failed"; goto bad; }
  if(elf.magic != ELF_MAGIC)
    { fail_reason = "bad ELF magic"; goto bad; }

  /*
   * setupkvm_impl 须持 setupkvm_lock；allocuvm 内部自洽持同一锁。
   * loaduvm 内 readi 可能阻塞，故须在释放锁之后调用。
   */
  setupkvm_lock_acquire();
  if((pgdir = setupkvm_impl()) == 0){
    setupkvm_lock_release();
    fail_reason = "setupkvm failed";
    goto bad;
  }
  setupkvm_lock_release();

  // Load program into memory.
  sz = 0;
  for(i=0, off=elf.phoff; i<elf.phnum; i++, off+=sizeof(ph)){
    if(readi(ip, (char*)&ph, off, sizeof(ph)) != sizeof(ph))
      { fail_reason = "read program header failed"; goto bad; }
    if(ph.type != ELF_PROG_LOAD)
      continue;
    if(ph.memsz < ph.filesz)
      { fail_reason = "invalid segment size"; goto bad; }
    {
      uint segend = ph.vaddr + ph.memsz;
      if(segend < ph.vaddr || segend > KERNBASE)
        { fail_reason = "segment address out of range"; goto bad; }
    }
    if((sz = allocuvm(pgdir, sz, ph.vaddr + ph.memsz)) == 0)
      { fail_reason = "alloc segment memory failed"; goto bad; }
    if(loaduvm(pgdir, (char*)ph.vaddr, ip, ph.off, ph.filesz) < 0)
      { fail_reason = "load segment failed"; goto bad; }
  }
  iunlockput(ip);
  ip = 0;

  // Allocate guard + USTACK_PAGES at the next page boundary.
  // Make the first page inaccessible (PTE_U cleared).  Rest is user stack.
  sz = PGROUNDUP(sz);
  {
    uint stackbytes = (USTACK_PAGES + 1) * PGSIZE;
    uint newsz = sz + stackbytes;
    if(newsz < sz || newsz >= KERNBASE)
      { fail_reason = "invalid user stack address"; goto bad; }
    if((sz = allocuvm(pgdir, sz, newsz)) == 0)
      { fail_reason = "alloc user stack failed"; goto bad; }
  }
  clearpteu(pgdir, (char*)(sz - (USTACK_PAGES + 1)*PGSIZE));
  sp = sz;

  // Push argument strings, prepare rest of stack in ustack.
  for(argc = 0; argv[argc]; argc++) {
    if(argc >= MAXARG)
      { fail_reason = "too many arguments"; goto bad; }
    sp = (sp - (strlen(argv[argc]) + 1)) & ~3;
    if(copyout(pgdir, sp, argv[argc], strlen(argv[argc]) + 1) < 0)
      { fail_reason = "copy argument string failed"; goto bad; }
    ustack[3+argc] = sp;
  }
  ustack[3+argc] = 0;

  ustack[0] = 0xffffffff;  // fake return PC
  ustack[1] = argc;
  ustack[2] = sp - (argc+1)*4;  // argv pointer

  sp -= (3+argc+1) * 4;
  if(copyout(pgdir, sp, ustack, (3+argc+1)*4) < 0)
    { fail_reason = "copy argument stack failed"; goto bad; }

  // Save program name for debugging.
  for(last=s=path; *s; s++)
    if(*s == '/')
      last = s+1;
  safestrcpy(proc->name, last, sizeof(proc->name));

  // Commit to the user image.
  oldpgdir = proc->pgdir;
  if(strncmp(proc->name, "sh", 16) == 0 || strncmp(proc->name, "dig", 16) == 0){
    uartcprintf("[proc dbg] before exec switch pid=%d name=%s oldpgdir=%p oldsz=0x%x newpgdir=%p newsz=0x%x\n",
                proc->pid, proc->name, oldpgdir, proc->sz, pgdir, sz);
  }
  proc->pgdir = pgdir;
  proc->sz = sz;
  proc->tf->eip = elf.entry;  // main
  proc->tf->esp = sp;
  switchuvm(proc);
  if(oldpgdir == proc->pgdir){
    cprintf("[proc] fatal: exec old/new pgdir identical, skip freevm pid=%d pgdir=%p\n",
            proc->pid, oldpgdir);
  } else if(proc->parent && proc->parent->pgdir &&
            pgdir_share_user_pt_page(oldpgdir, proc->parent->pgdir)){
    cprintf("[proc] fatal: exec oldpgdir shares user page-table pages with parent, skip freevm pid=%d parent=%d oldpgdir=%p parent_pgdir=%p\n",
            proc->pid, proc->parent->pid, oldpgdir, proc->parent->pgdir);
  } else {
    freevm(oldpgdir);
  }
  return 0;

 bad:
  if(strncmp(path, "dig", 16) == 0 || strncmp(path, "/bin/dig", 16) == 0 || strncmp(path, "/echo", 16) == 0){
    uartcprintf("[proc dbg] exec failed pid=%d name=%s path=%s reason=%s\n",
                proc ? proc->pid : -1, proc ? proc->name : "?", path, fail_reason);
  }
  if(pgdir)
    freevm(pgdir);
  if(ip)
    iunlockput(ip);
  return -1;
}

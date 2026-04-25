/*
 * objdump — 与 GNU objdump 常用选项一致：-f -h -x -t -s -d [-M att]
 */
#include "types.h"
#include "stat.h"
#include "fcntl.h"
#include "user.h"
#include "elf32.h"
#include "disasm_i386.h"

struct odflags {
  int f, h, x, t, s, d;
  int matt;
};

static int
xread(int fd, void *buf, int n)
{
  int n0, tot;
  char *p;

  p = (char*)buf;
  tot = 0;
  while(tot < n){
    n0 = read(fd, p + tot, n - tot);
    if(n0 <= 0)
      return -1;
    tot += n0;
  }
  return 0;
}

static int
read_file(const char *path, char **out, int *outsz)
{
  int fd;
  struct stat st;
  char *buf;

  fd = open((char*)path, O_RDONLY);
  if(fd < 0)
    return -1;
  if(fstat(fd, &st) < 0){
    close(fd);
    return -1;
  }
  if(st.size <= 0 || st.size > 1024*1024*8){
    close(fd);
    return -1;
  }
  buf = malloc(st.size);
  if(buf == 0){
    close(fd);
    return -1;
  }
  if(xread(fd, buf, st.size) < 0){
    free(buf);
    close(fd);
    return -1;
  }
  close(fd);
  *out = buf;
  *outsz = st.size;
  return 0;
}

static const char*
ptype(uint t)
{
  switch(t){
  case PT_NULL: return "NULL";
  case PT_LOAD: return "LOAD";
  case PT_DYNAMIC: return "DYNAMIC";
  case PT_INTERP: return "INTERP";
  case PT_NOTE: return "NOTE";
  case PT_PHDR: return "PHDR";
  default: return "UNKNOWN";
  }
}

static void
dump_filehdr(int fd, const char *path, struct Elf32_Ehdr *eh)
{
  printf(fd, "\n%s:     file format elf32-i386\n", path);
  printf(fd, "architecture: i386, flags 0x%08x:\n", eh->e_flags);
  printf(fd, "HAS_SYMS\n");
  printf(fd, "start address 0x%08x\n\n", eh->e_entry);
}

static int
log2_align(uint a)
{
  int p;
  if(a == 0)
    return 0;
  p = 0;
  while(a > 1){
    a >>= 1;
    p++;
  }
  return p;
}

static void
dump_sections_hdr(int fd, struct Elf32_Ehdr *eh, struct Elf32_Shdr *sh,
                    const char *shstr, char *data, int sz)
{
  int i;

  (void)data;
  printf(fd, "Sections:\n");
  printf(fd, "Idx Name          Size      VMA       LMA       File off  Algn\n");
  for(i = 0; i < eh->e_shnum; i++){
    if(sh[i].sh_offset + sh[i].sh_size > sz)
      continue;
    printf(fd, "%3d %-13s %08x  %08x  %08x  %08x  2**%d\n",
           i, shstr + sh[i].sh_name, sh[i].sh_size, sh[i].sh_addr,
           sh[i].sh_addr, sh[i].sh_offset,
           log2_align(sh[i].sh_addralign));
  }
}

static void
dump_symbols(int fd, const char *path, struct Elf32_Ehdr *eh, struct Elf32_Shdr *sh,
             const char *shstr, char *data, int sz)
{
  int i, j;
  struct Elf32_Sym *syms;
  const char *st;
  int nsym, stsz;

  (void)shstr;
  printf(fd, "\n%s:     file format elf32-i386\n", path);
  printf(fd, "SYMBOL TABLE:\n");
  for(i = 0; i < eh->e_shnum; i++){
    if(sh[i].sh_type != SHT_SYMTAB && sh[i].sh_type != SHT_DYNSYM)
      continue;
    if(sh[i].sh_offset + sh[i].sh_size > sz)
      continue;
    if(sh[i].sh_link >= eh->e_shnum)
      continue;
    if(sh[sh[i].sh_link].sh_type != SHT_STRTAB)
      continue;
    syms = (struct Elf32_Sym*)(data + sh[i].sh_offset);
    nsym = sh[i].sh_size / sizeof(struct Elf32_Sym);
    st = data + sh[sh[i].sh_link].sh_offset;
    stsz = sh[sh[i].sh_link].sh_size;
    for(j = 0; j < nsym; j++){
      const char *nm = "";
      if(syms[j].st_name < stsz)
        nm = st + syms[j].st_name;
      printf(fd, "%08x l    O %08x %08x %s\n",
             j, syms[j].st_value, syms[j].st_size, nm);
    }
  }
}

static void
dump_hex_section(int fd, const char *name, uchar *sec, uint off, uint vma, uint len)
{
  uint i, j;
  printf(fd, "Contents of section %s:\n", name);
  for(i = 0; i < len; i += 16){
    printf(fd, " %08x ", vma + i);
    for(j = 0; j < 16 && i + j < len; j++){
      printf(fd, "%02x", sec[off + i + j]);
      if(j % 4 == 3)
        printf(fd, " ");
    }
    printf(fd, "\n");
  }
}

static void
dump_disasm(int fd, struct Elf32_Ehdr *eh, struct Elf32_Shdr *sh,
            const char *shstr, char *data, int sz)
{
  int i;
  uchar *sec;
  uint pos, va, left, step;

  for(i = 0; i < eh->e_shnum; i++){
    if(!(sh[i].sh_flags & SHF_ALLOC) || !(sh[i].sh_flags & SHF_EXECINSTR))
      continue;
    if(sh[i].sh_type == SHT_NOBITS)
      continue;
    if(sh[i].sh_offset + sh[i].sh_size > sz)
      continue;
    sec = (uchar*)(data + sh[i].sh_offset);
    printf(fd, "\nDisassembly of section %s:\n", shstr + sh[i].sh_name);
    pos = 0;
    va = sh[i].sh_addr;
    while(pos < sh[i].sh_size){
      left = sh[i].sh_size - pos;
      step = disasm_insn_print(fd, sec + pos, left, va + pos);
      if(step < 1)
        step = 1;
      pos += step;
    }
  }
}

static void
dump_programs(int fd, struct Elf32_Ehdr *eh, struct Elf32_Phdr *ph, int sz)
{
  int i;
  if(eh->e_phoff + eh->e_phnum * sizeof(struct Elf32_Phdr) > sz)
    return;
  printf(fd, "Program Header:\n");
  for(i = 0; i < eh->e_phnum; i++){
    printf(fd, "    %s off 0x%08x vaddr 0x%08x paddr 0x%08x align 2**%d\n",
           ptype(ph[i].p_type), ph[i].p_offset, ph[i].p_vaddr,
           ph[i].p_paddr, log2_align(ph[i].p_align));
  }
}

static void
process(const char *path, struct odflags *of)
{
  char *data;
  int sz;
  struct Elf32_Ehdr *eh;
  struct Elf32_Shdr *sh;
  struct Elf32_Phdr *ph;
  const char *shstr;
  int i;

  if(read_file(path, &data, &sz) < 0){
    printf(2, "objdump: cannot read '%s'\n", path);
    return;
  }
  if(sz < (int)sizeof(struct Elf32_Ehdr)){
    printf(2, "objdump: '%s' too small\n", path);
    free(data);
    return;
  }
  eh = (struct Elf32_Ehdr*)data;
  if(eh->e_ident[0] != ELFMAG0 || eh->e_ident[1] != ELFMAG1 ||
     eh->e_ident[2] != ELFMAG2 || eh->e_ident[3] != ELFMAG3){
    printf(2, "objdump: '%s' not an ELF file\n", path);
    free(data);
    return;
  }

  if(of->f)
    dump_filehdr(1, path, eh);

  sh = 0;
  shstr = 0;
  if(eh->e_shnum && eh->e_shoff + eh->e_shnum * sizeof(struct Elf32_Shdr) <= sz){
    sh = (struct Elf32_Shdr*)(data + eh->e_shoff);
    if(eh->e_shstrndx < eh->e_shnum &&
       sh[eh->e_shstrndx].sh_offset + sh[eh->e_shstrndx].sh_size <= sz)
      shstr = data + sh[eh->e_shstrndx].sh_offset;
  }

  ph = 0;
  if(eh->e_phnum && eh->e_phoff + eh->e_phnum * sizeof(struct Elf32_Phdr) <= sz)
    ph = (struct Elf32_Phdr*)(data + eh->e_phoff);

  if((of->h || of->x || of->t || of->s || of->d) && sh && shstr){
    if(of->x && ph)
      dump_programs(1, eh, ph, sz);
    if(of->h || of->x)
      dump_sections_hdr(1, eh, sh, shstr, data, sz);
    if(of->t)
      dump_symbols(1, path, eh, sh, shstr, data, sz);
    if(of->s){
      for(i = 0; i < eh->e_shnum; i++){
        if(sh[i].sh_type == SHT_NOBITS)
          continue;
        if(sh[i].sh_offset + sh[i].sh_size > sz)
          continue;
        dump_hex_section(1, shstr + sh[i].sh_name,
                         (uchar*)data, sh[i].sh_offset, sh[i].sh_addr, sh[i].sh_size);
      }
    }
    if(of->d){
      if(of->matt)
        printf(1, "\n(att syntax)\n");
      dump_disasm(1, eh, sh, shstr, data, sz);
    }
  } else if(of->h || of->x || of->t || of->s || of->d){
    printf(2, "objdump: missing section headers in '%s'\n", path);
  }

  free(data);
}

static void
usage(void)
{
  printf(2, "Usage: objdump [-f] [-h] [-x] [-t] [-s] [-d] [-M att] <elf-file>...\n");
  exit(1);
}

int
main(int argc, char **argv)
{
  struct odflags of;
  int i, ff;

  memset(&of, 0, sizeof(of));
  ff = -1;
  for(i = 1; i < argc; i++){
    if(argv[i][0] != '-'){
      ff = i;
      break;
    }
    if(strcmp(argv[i], "-f") == 0) of.f = 1;
    else if(strcmp(argv[i], "-h") == 0) of.h = 1;
    else if(strcmp(argv[i], "-x") == 0) of.x = 1;
    else if(strcmp(argv[i], "-t") == 0) of.t = 1;
    else if(strcmp(argv[i], "-s") == 0) of.s = 1;
    else if(strcmp(argv[i], "-d") == 0) of.d = 1;
    else if(strcmp(argv[i], "-M") == 0 && i + 1 < argc){
      i++;
      if(strcmp(argv[i], "att") == 0)
        of.matt = 1;
    } else {
      printf(2, "objdump: unknown option '%s'\n", argv[i]);
      usage();
    }
  }
  if(ff < 0)
    usage();
  if(!of.f && !of.h && !of.x && !of.t && !of.s && !of.d){
    printf(2, "objdump: at least one of -f -h -x -t -s -d is required\n");
    exit(1);
  }
  for(i = ff; i < argc; i++)
    process(argv[i], &of);
  exit(0);
}

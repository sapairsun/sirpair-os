/*
 * readelf — 显示 ELF32 信息，常用选项与 GNU readelf 一致：-h -l -S -s -r -d -a -W
 */
#include "types.h"
#include "stat.h"
#include "fcntl.h"
#include "user.h"
#include "elf32.h"

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

static void
print_hex(const uchar *p, int n)
{
  int i;
  for(i = 0; i < n; i++){
    printf(1, "%s%02x", i ? " " : "", p[i]);
  }
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
  case PT_SHLIB: return "SHLIB";
  case PT_PHDR: return "PHDR";
  default: return "UNKNOWN";
  }
}

static const char*
shtype(uint t)
{
  switch(t){
  case SHT_NULL: return "NULL";
  case SHT_PROGBITS: return "PROGBITS";
  case SHT_SYMTAB: return "SYMTAB";
  case SHT_STRTAB: return "STRTAB";
  case SHT_RELA: return "RELA";
  case SHT_HASH: return "HASH";
  case SHT_DYNAMIC: return "DYNAMIC";
  case SHT_NOTE: return "NOTE";
  case SHT_NOBITS: return "NOBITS";
  case SHT_REL: return "REL";
  case SHT_SHLIB: return "SHLIB";
  case SHT_DYNSYM: return "DYNSYM";
  default: return "UNKNOWN";
  }
}

static const char*
reloc_type_386(uchar t)
{
  switch(t){
  case R_386_NONE: return "R_386_NONE";
  case R_386_32: return "R_386_32";
  case R_386_PC32: return "R_386_PC32";
  case R_386_GOT32: return "R_386_GOT32";
  case R_386_PLT32: return "R_386_PLT32";
  case R_386_COPY: return "R_386_COPY";
  case R_386_GLOB_DAT: return "R_386_GLOB_DAT";
  case R_386_JMP_SLOT: return "R_386_JMP_SLOT";
  case R_386_RELATIVE: return "R_386_RELATIVE";
  case R_386_GOTOFF: return "R_386_GOTOFF";
  case R_386_GOTPC: return "R_386_GOTPC";
  case R_386_32PLT: return "R_386_32PLT";
  default: return "UNKNOWN";
  }
}

static void
dump_symtab(int wide, struct Elf32_Sym *syms, int nsym, const char *strtab, int strsz,
            const char *shstr, const struct Elf32_Shdr *shs, int shnum)
{
  int i;
  for(i = 0; i < nsym; i++){
    struct Elf32_Sym *s = syms + i;
    const char *name = "";
    const char *bind, *type, *ndx;
    char ndxbuf[16];

    if(s->st_name < strsz && strtab)
      name = strtab + s->st_name;
    bind = (s->st_info >> 4) == STB_LOCAL ? "LOCAL" :
           (s->st_info >> 4) == STB_GLOBAL ? "GLOBAL" : "WEAK";
    type = (s->st_info & 0xf) == STT_NOTYPE ? "NOTYPE" :
           (s->st_info & 0xf) == STT_OBJECT ? "OBJECT" :
           (s->st_info & 0xf) == STT_FUNC ? "FUNC" :
           (s->st_info & 0xf) == STT_SECTION ? "SECTION" :
           (s->st_info & 0xf) == STT_FILE ? "FILE" : "NOTYPE";
    if(s->st_shndx == SHN_UNDEF)
      ndx = "UND";
    else if(s->st_shndx == SHN_ABS)
      ndx = "ABS";
    else if(s->st_shndx == SHN_COMMON)
      ndx = "COM";
    else {
      ndx = ndxbuf;
      if(s->st_shndx < shnum && shstr && shs){
        uint k = shs[s->st_shndx].sh_name;
        uint j = 0;
        while(j < sizeof(ndxbuf)-1 && shstr[k+j]){
          ndxbuf[j] = shstr[k+j];
          j++;
        }
        ndxbuf[j] = 0;
      } else {
        ndxbuf[0] = '?';
        ndxbuf[1] = 0;
      }
    }
    if(wide)
      printf(1, "  %6d: %08x %5u %-7s %-7s %-7s %4s %s\n",
             i, s->st_value, s->st_size, type, bind, "DEFAULT", ndx, name);
    else
      printf(1, "  %6d: %08x %5u %-7s %-7s %4s %s\n",
             i, s->st_value, s->st_size, type, bind, ndx, name);
  }
}

static void
do_rel_section(const char *data, int sz, struct Elf32_Shdr *shs, int shnum,
               const char *shstr, int idx)
{
  struct Elf32_Rel *rel;
  struct Elf32_Sym *symtab;
  const char *symstr;
  int i, nrel, nsym;
  uint symtab_sh, str_sh;

  (void)sz;
  if(shs[idx].sh_offset + shs[idx].sh_size > sz)
    return;
  symtab_sh = shs[idx].sh_link;
  if(symtab_sh >= shnum)
    return;
  if(shs[symtab_sh].sh_type != SHT_SYMTAB && shs[symtab_sh].sh_type != SHT_DYNSYM)
    return;
  str_sh = shs[symtab_sh].sh_link;
  if(str_sh >= shnum || shs[str_sh].sh_type != SHT_STRTAB)
    return;
  symtab = (struct Elf32_Sym*)(data + shs[symtab_sh].sh_offset);
  nsym = shs[symtab_sh].sh_size / sizeof(struct Elf32_Sym);
  symstr = data + shs[str_sh].sh_offset;
  nrel = shs[idx].sh_size / sizeof(struct Elf32_Rel);
  rel = (struct Elf32_Rel*)(data + shs[idx].sh_offset);
  printf(1, "\nRelocation section '%s' at offset 0x%x contains %d entries:\n",
         shstr + shs[idx].sh_name, shs[idx].sh_offset, nrel);
  printf(1, " Offset     Info    Type            Sym.Value  Sym. Name\n");
  for(i = 0; i < nrel; i++){
    uint symix = ELF32_R_SYM(rel[i].r_info);
    uchar typ = ELF32_R_TYPE(rel[i].r_info);
    const char *sn = "";
    if(symix < nsym && symtab[symix].st_name < shs[str_sh].sh_size)
      sn = symstr + symtab[symix].st_name;
    printf(1, "%08x  %08x %-16s %08x  %s\n",
           rel[i].r_offset, rel[i].r_info, reloc_type_386(typ),
           symix < nsym ? symtab[symix].st_value : 0, sn);
  }
}

static void
process_file(const char *path, int f_h, int f_l, int f_S, int f_s, int f_r, int f_d, int wide)
{
  char *data;
  int sz;
  struct Elf32_Ehdr *eh;
  struct Elf32_Phdr *ph;
  struct Elf32_Shdr *sh;
  const char *shstr;
  int i;

  if(read_file(path, &data, &sz) < 0){
    printf(2, "readelf: cannot read '%s'\n", path);
    return;
  }
  if(sz < (int)sizeof(struct Elf32_Ehdr)){
    printf(2, "readelf: '%s' too small\n", path);
    free(data);
    return;
  }
  eh = (struct Elf32_Ehdr*)data;
  if(eh->e_ident[0] != ELFMAG0 || eh->e_ident[1] != ELFMAG1 ||
     eh->e_ident[2] != ELFMAG2 || eh->e_ident[3] != ELFMAG3){
    printf(2, "readelf: '%s' not an ELF file\n", path);
    free(data);
    return;
  }

  if(f_h){
    printf(1, "ELF Header:\n");
    printf(1, "  Magic:   ");
    print_hex(eh->e_ident, EI_NIDENT);
    printf(1, "\n");
    printf(1, "  Class:                             ELF32\n");
    printf(1, "  Data:                              2's complement, little endian\n");
    printf(1, "  Version:                           %d (current)\n", eh->e_ident[6]);
    printf(1, "  OS/ABI:                            UNIX - System V\n");
    printf(1, "  ABI Version:                       0\n");
    printf(1, "  Type:                              ");
    if(eh->e_type == ET_EXEC) printf(1, "EXEC (Executable file)\n");
    else if(eh->e_type == ET_REL) printf(1, "REL (Relocatable file)\n");
    else if(eh->e_type == ET_DYN) printf(1, "DYN (Shared object file)\n");
    else printf(1, "Unknown\n");
    printf(1, "  Machine:                           Intel 80386\n");
    printf(1, "  Version:                           0x%x\n", eh->e_version);
    printf(1, "  Entry point address:               0x%x\n", eh->e_entry);
    printf(1, "  Start of program headers:          %d (bytes into file)\n", eh->e_phoff);
    printf(1, "  Start of section headers:          %d (bytes into file)\n", eh->e_shoff);
    printf(1, "  Flags:                             0x%x\n", eh->e_flags);
    printf(1, "  Size of this header:               %d (bytes)\n", eh->e_ehsize);
    printf(1, "  Size of program headers:           %d (bytes)\n", eh->e_phentsize);
    printf(1, "  Number of program headers:         %d\n", eh->e_phnum);
    printf(1, "  Size of section headers:           %d (bytes)\n", eh->e_shentsize);
    printf(1, "  Number of section headers:         %d\n", eh->e_shnum);
    printf(1, "  Section header string table index: %d\n", eh->e_shstrndx);
  }

  if(f_l && eh->e_phnum > 0 && eh->e_phoff + eh->e_phnum * sizeof(struct Elf32_Phdr) <= sz){
    printf(1, "\nProgram Headers:\n");
    printf(1, "  Type           Offset   VirtAddr   PhysAddr   FileSiz MemSiz  Flg Align\n");
    ph = (struct Elf32_Phdr*)(data + eh->e_phoff);
    for(i = 0; i < eh->e_phnum; i++){
      char fg[4];
      int p = 0;
      if(ph[i].p_flags & PF_R) fg[p++] = 'R';
      if(ph[i].p_flags & PF_W) fg[p++] = 'W';
      if(ph[i].p_flags & PF_X) fg[p++] = 'E';
      fg[p] = 0;
      printf(1, "  %-14s 0x%06x 0x%08x 0x%08x 0x%05x 0x%05x %s 0x%x\n",
             ptype(ph[i].p_type), ph[i].p_offset, ph[i].p_vaddr, ph[i].p_paddr,
             ph[i].p_filesz, ph[i].p_memsz, fg, ph[i].p_align);
    }
  }

  if(eh->e_shnum == 0 || eh->e_shoff + eh->e_shnum * sizeof(struct Elf32_Shdr) > sz){
    if(f_S || f_s || f_r)
      printf(2, "readelf: warning: no section headers in '%s'\n", path);
    free(data);
    return;
  }
  sh = (struct Elf32_Shdr*)(data + eh->e_shoff);
  if(eh->e_shstrndx >= eh->e_shnum){
    free(data);
    return;
  }
  if(sh[eh->e_shstrndx].sh_offset + sh[eh->e_shstrndx].sh_size > sz){
    free(data);
    return;
  }
  shstr = data + sh[eh->e_shstrndx].sh_offset;

  if(f_S){
    printf(1, "Section Headers:\n");
    printf(1, "  [Nr] Name              Type            Addr     Off    Size   ES Flg Lk Inf Al\n");
    for(i = 0; i < eh->e_shnum; i++){
      char flg[4];
      int p = 0;
      if(sh[i].sh_flags & SHF_WRITE) flg[p++] = 'W';
      if(sh[i].sh_flags & SHF_ALLOC) flg[p++] = 'A';
      if(sh[i].sh_flags & SHF_EXECINSTR) flg[p++] = 'X';
      flg[p] = 0;
      printf(1, "  [%2d] %-16s %-15s %08x %06x %06x %02x %3s %2d %3d %2d\n",
             i, shstr + sh[i].sh_name, shtype(sh[i].sh_type),
             sh[i].sh_addr, sh[i].sh_offset, sh[i].sh_size,
             sh[i].sh_entsize, flg, sh[i].sh_link, sh[i].sh_info, sh[i].sh_addralign);
    }
    printf(1, "Key to Flags:\n");
    printf(1, "  W (write), A (alloc), X (execute)\n");
  }

  if(f_s){
    for(i = 0; i < eh->e_shnum; i++){
      if(sh[i].sh_type != SHT_SYMTAB && sh[i].sh_type != SHT_DYNSYM)
        continue;
      if(sh[i].sh_offset + sh[i].sh_size > sz)
        continue;
      if(sh[i].sh_link >= eh->e_shnum)
        continue;
      if(sh[sh[i].sh_link].sh_type != SHT_STRTAB)
        continue;
      {
        struct Elf32_Sym *syms = (struct Elf32_Sym*)(data + sh[i].sh_offset);
        int nsym = sh[i].sh_size / sizeof(struct Elf32_Sym);
        const char *st = data + sh[sh[i].sh_link].sh_offset;
        int stsz = sh[sh[i].sh_link].sh_size;
        printf(1, "\nSymbol table '%s' contains %d entries:\n", shstr + sh[i].sh_name, nsym);
        if(wide)
          printf(1, "   Num:    Value  Size Type    Bind   Vis      Ndx Name\n");
        else
          printf(1, "   Num:    Value  Size Type    Bind   Ndx Name\n");
        dump_symtab(wide, syms, nsym, st, stsz, shstr, sh, eh->e_shnum);
      }
    }
  }

  if(f_r){
    for(i = 0; i < eh->e_shnum; i++){
      if(sh[i].sh_type != SHT_REL)
        continue;
      do_rel_section(data, sz, sh, eh->e_shnum, shstr, i);
    }
  }

  if(f_d){
    printf(1, "\nThere is no dynamic section in this file.\n");
  }

  free(data);
}

struct rf {
  int h, l, S, s, r, d, a, W;
};

static void
parse_flags(int argc, char **argv, struct rf *f, int *first_file)
{
  int i;
  memset(f, 0, sizeof(*f));
  *first_file = -1;
  for(i = 1; i < argc; i++){
    if(argv[i][0] != '-'){
      *first_file = i;
      break;
    }
    if(strcmp(argv[i], "-h") == 0) f->h = 1;
    else if(strcmp(argv[i], "-l") == 0) f->l = 1;
    else if(strcmp(argv[i], "-S") == 0) f->S = 1;
    else if(strcmp(argv[i], "-s") == 0) f->s = 1;
    else if(strcmp(argv[i], "-r") == 0) f->r = 1;
    else if(strcmp(argv[i], "-d") == 0) f->d = 1;
    else if(strcmp(argv[i], "-a") == 0) f->a = 1;
    else if(strcmp(argv[i], "-W") == 0) f->W = 1;
    else {
      printf(2, "readelf: unknown option '%s'\n", argv[i]);
      printf(2, "Usage: readelf [-a|-h|-l|-S|-s|-r|-d] [-W] <elf-file>...\n");
      exit(1);
    }
  }
  if(f->a){
    f->h = f->l = f->S = f->s = f->r = 1;
  }
}

int
main(int argc, char **argv)
{
  struct rf f;
  int i, ff;

  if(argc < 2){
    printf(2, "Usage: readelf [-a|-h|-l|-S|-s|-r|-d] [-W] <elf-file>...\n");
    exit(1);
  }
  parse_flags(argc, argv, &f, &ff);
  if(ff < 0){
    printf(2, "readelf: missing elf-file argument\n");
    exit(1);
  }
  if(!f.h && !f.l && !f.S && !f.s && !f.r && !f.d && !f.a){
    printf(2, "readelf: one of -a -h -l -S -s -r -d is required\n");
    exit(1);
  }
  for(i = ff; i < argc; i++){
    if(argc > ff + 1)
      printf(1, "File: %s\n", argv[i]);
    process_file(argv[i], f.h, f.l, f.S, f.s, f.r, f.d, f.W);
  }
  exit(0);
}

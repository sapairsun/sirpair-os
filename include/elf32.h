/* ELF32 定义（用户态 readelf/objdump 与内核 exec 所用布局一致，小端序） */
#ifndef SIRPAIR_ELF32_H
#define SIRPAIR_ELF32_H

#include "types.h"

#define EI_NIDENT 16

#define ELFMAG0 0x7f
#define ELFMAG1 'E'
#define ELFMAG2 'L'
#define ELFMAG3 'F'

#define ELFCLASS32 1
#define ELFDATA2LSB 1
#define EV_CURRENT 1

#define ET_NONE   0
#define ET_REL    1
#define ET_EXEC   2
#define ET_DYN    3

#define EM_NONE  0
#define EM_386   3

#define PT_NULL    0
#define PT_LOAD    1
#define PT_DYNAMIC 2
#define PT_INTERP  3
#define PT_NOTE    4
#define PT_SHLIB   5
#define PT_PHDR    6

#define PF_X 1
#define PF_W 2
#define PF_R 4

#define SHT_NULL          0
#define SHT_PROGBITS      1
#define SHT_SYMTAB        2
#define SHT_STRTAB        3
#define SHT_RELA          4
#define SHT_HASH          5
#define SHT_DYNAMIC       6
#define SHT_NOTE          7
#define SHT_NOBITS        8
#define SHT_REL           9
#define SHT_SHLIB         10
#define SHT_DYNSYM        11

#define SHF_WRITE     0x1
#define SHF_ALLOC     0x2
#define SHF_EXECINSTR 0x4

#define STB_LOCAL  0
#define STB_GLOBAL 1
#define STB_WEAK   2

#define STT_NOTYPE  0
#define STT_OBJECT  1
#define STT_FUNC    2
#define STT_SECTION 3
#define STT_FILE    4

#define STV_DEFAULT   0
#define STV_INTERNAL  1
#define STV_HIDDEN    2
#define STV_PROTECTED 3

#define SHN_UNDEF     0
#define SHN_ABS       0xfff1
#define SHN_COMMON    0xfff2

#define R_386_NONE           0
#define R_386_32             1
#define R_386_PC32           2
#define R_386_GOT32          3
#define R_386_PLT32          4
#define R_386_COPY           5
#define R_386_GLOB_DAT       6
#define R_386_JMP_SLOT       7
#define R_386_RELATIVE       8
#define R_386_GOTOFF         9
#define R_386_GOTPC          10
#define R_386_32PLT          11

struct Elf32_Ehdr {
  uchar e_ident[EI_NIDENT];
  ushort e_type;
  ushort e_machine;
  uint e_version;
  uint e_entry;
  uint e_phoff;
  uint e_shoff;
  uint e_flags;
  ushort e_ehsize;
  ushort e_phentsize;
  ushort e_phnum;
  ushort e_shentsize;
  ushort e_shnum;
  ushort e_shstrndx;
};

struct Elf32_Phdr {
  uint p_type;
  uint p_offset;
  uint p_vaddr;
  uint p_paddr;
  uint p_filesz;
  uint p_memsz;
  uint p_flags;
  uint p_align;
};

struct Elf32_Shdr {
  uint sh_name;
  uint sh_type;
  uint sh_flags;
  uint sh_addr;
  uint sh_offset;
  uint sh_size;
  uint sh_link;
  uint sh_info;
  uint sh_addralign;
  uint sh_entsize;
};

struct Elf32_Sym {
  uint st_name;
  uint st_value;
  uint st_size;
  uchar st_info;
  uchar st_other;
  ushort st_shndx;
};

struct Elf32_Rel {
  uint r_offset;
  uint r_info;
};

struct Elf32_Rela {
  uint r_offset;
  uint r_info;
  int r_addend;
};

#define ELF32_R_SYM(i)  ((i) >> 8)
#define ELF32_R_TYPE(i) ((uchar)(i))

#endif

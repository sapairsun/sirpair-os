#ifndef SIRPAIR_DISASM_I386_H
#define SIRPAIR_DISASM_I386_H

#include "types.h"

int disasm_insn_print(int fd, const uchar *p, int left, uint va);

#endif

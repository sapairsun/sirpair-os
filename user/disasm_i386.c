/*
 * i386 反汇编（ATT 语法，与 GNU objdump 默认风格接近）
 */
#include "types.h"
#include "user.h"

static const char *const reg32[] = {
  "%eax", "%ecx", "%edx", "%ebx", "%esp", "%ebp", "%esi", "%edi"
};

static int
modrm_total(const uchar *p, int left)
{
  int mod, rm, disp = 0;
  uchar modrm, sib;
  int base;

  if(left < 1)
    return -1;
  modrm = p[0];
  mod = (modrm >> 6) & 3;
  rm = modrm & 7;
  if(mod == 3)
    return 1;
  if(rm == 4){
    if(left < 2)
      return -1;
    sib = p[1];
    base = sib & 7;
    if(mod == 0 && base == 5)
      disp = 4;
    else if(mod == 1)
      disp = 1;
    else if(mod == 2)
      disp = 4;
    else
      disp = 0;
    return 1 + 1 + disp;
  }
  if(mod == 0 && rm == 5)
    disp = 4;
  else if(mod == 1)
    disp = 1;
  else if(mod == 2)
    disp = 4;
  else
    disp = 0;
  return 1 + disp;
}

static void
print_hex_bytes(int fd, const uchar *p, int n)
{
  int i;
  for(i = 0; i < n; i++)
    printf(fd, "%s%02x", i ? " " : "", p[i]);
}

static void
print_op_unknown(int fd, const uchar *p, int n, uint va)
{
  printf(fd, "%08x:\t", va);
  print_hex_bytes(fd, p, n);
  printf(fd, "\t.byte 0x%02x\n", p[0]);
}

/* 返回本条指令总字节数（含前缀），并输出一行反汇编到 fd */
int
disasm_insn_print(int fd, const uchar *p, int left, uint va)
{
  int i, pre = 0, olen, mlen;
  uchar op, op2;
  const uchar *q;

  i = 0;
  while(i < left && pre < 4 &&
        (p[i] == 0xf0 || p[i] == 0xf2 || p[i] == 0xf3 || p[i] == 0x66 || p[i] == 0x67)){
    i++;
    pre++;
  }
  if(i >= left)
    return 1;
  q = p + i;
  left -= i;
  if(left < 1)
    return 1;

  op = q[0];

  /* 0x0F 两字节 */
  if(op == 0x0f){
    if(left < 2){
      print_op_unknown(fd, p, i + 1, va);
      return i + 1;
    }
    op2 = q[1];
    /* 长跳转/长条件跳转 rel32 */
    if(op2 >= 0x80 && op2 <= 0x8f){
      int rel32;
      uint tgt;
      olen = i + 6;
      if(left < 6){
        print_op_unknown(fd, p, i + left, va);
        return i + (left > 0 ? left : 1);
      }
      rel32 = (int)(q[2] | (q[3]<<8) | (q[4]<<16) | (q[5]<<24));
      tgt = va + olen + rel32;
      printf(fd, "%08x:\t", va);
      print_hex_bytes(fd, p, olen);
      printf(fd, "\tjcc    0x%x\n", tgt);
      return olen;
    }
    /* cpuid / rdtsc */
    if(op2 == 0xa2){
      olen = i + 2;
      printf(fd, "%08x:\t", va);
      print_hex_bytes(fd, p, olen);
      printf(fd, "\tcpuid\n");
      return olen;
    }
    if(op2 == 0x31){
      olen = i + 2;
      printf(fd, "%08x:\t", va);
      print_hex_bytes(fd, p, olen);
      printf(fd, "\trdtsc\n");
      return olen;
    }
    /* movzx/movsx Gv, Eb / Ev */
    if(op2 == 0xb6 || op2 == 0xb7 || op2 == 0xbe || op2 == 0xbf){
      mlen = modrm_total(q + 2, left - 2);
      if(mlen < 0)
        mlen = 0;
      olen = i + 2 + mlen;
      if(left < 2 + mlen){
        print_op_unknown(fd, p, i + 1, va);
        return i + 1;
      }
      printf(fd, "%08x:\t", va);
      print_hex_bytes(fd, p, olen);
      printf(fd, "\tmovzx/movsx ...\n");
      return olen;
    }
    /* 默认：0x0F 后跟 ModRM */
    mlen = modrm_total(q + 2, left - 2);
    if(mlen < 0)
      mlen = 1;
    olen = i + 2 + mlen;
    if(olen > i + left)
      olen = i + left;
    printf(fd, "%08x:\t", va);
    print_hex_bytes(fd, p, olen);
    printf(fd, "\t(two-byte 0x0f 0x%02x)\n", op2);
    return olen;
  }

  /* 单字节 opcode */
  if(op >= 0x50 && op <= 0x57){
    olen = i + 1;
    printf(fd, "%08x:\t", va);
    print_hex_bytes(fd, p, olen);
    printf(fd, "\tpush   %s\n", reg32[op - 0x50]);
    return olen;
  }
  if(op >= 0x58 && op <= 0x5f){
    olen = i + 1;
    printf(fd, "%08x:\t", va);
    print_hex_bytes(fd, p, olen);
    printf(fd, "\tpop    %s\n", reg32[op - 0x58]);
    return olen;
  }
  if(op == 0x90){
    olen = i + 1;
    printf(fd, "%08x:\t", va);
    print_hex_bytes(fd, p, olen);
    printf(fd, "\tnop\n");
    return olen;
  }
  if(op == 0xc3){
    olen = i + 1;
    printf(fd, "%08x:\t", va);
    print_hex_bytes(fd, p, olen);
    printf(fd, "\tret\n");
    return olen;
  }
  if(op == 0xc9){
    olen = i + 1;
    printf(fd, "%08x:\t", va);
    print_hex_bytes(fd, p, olen);
    printf(fd, "\tleave\n");
    return olen;
  }
  if(op == 0xcd){
    if(left < 2){
      print_op_unknown(fd, p, i + 1, va);
      return i + 1;
    }
    olen = i + 2;
    printf(fd, "%08x:\t", va);
    print_hex_bytes(fd, p, olen);
    printf(fd, "\tint    $0x%x\n", q[1]);
    return olen;
  }
  if(op == 0xe8){
    if(left < 5){
      print_op_unknown(fd, p, i + left, va);
      return i + (left > 0 ? left : 1);
    }
    olen = i + 5;
    printf(fd, "%08x:\t", va);
    print_hex_bytes(fd, p, olen);
    printf(fd, "\tcall   0x%x\n", va + olen + (int)(q[1] | (q[2]<<8) | (q[3]<<16) | (q[4]<<24)));
    return olen;
  }
  if(op == 0xe9){
    if(left < 5){
      print_op_unknown(fd, p, i + left, va);
      return i + (left > 0 ? left : 1);
    }
    olen = i + 5;
    printf(fd, "%08x:\t", va);
    print_hex_bytes(fd, p, olen);
    printf(fd, "\tjmp    0x%x\n", va + olen + (int)(q[1] | (q[2]<<8) | (q[3]<<16) | (q[4]<<24)));
    return olen;
  }
  if(op == 0xeb){
    if(left < 2){
      print_op_unknown(fd, p, i + 1, va);
      return i + 1;
    }
    olen = i + 2;
    printf(fd, "%08x:\t", va);
    print_hex_bytes(fd, p, olen);
    printf(fd, "\tjmp    0x%x\n", va + olen + (char)q[1]);
    return olen;
  }
  if(op >= 0x70 && op <= 0x7f){
    if(left < 2){
      print_op_unknown(fd, p, i + 1, va);
      return i + 1;
    }
    olen = i + 2;
    printf(fd, "%08x:\t", va);
    print_hex_bytes(fd, p, olen);
    printf(fd, "\tjcc    0x%x\n", va + olen + (char)q[1]);
    return olen;
  }
  if(op == 0x68){
    if(left < 5){
      print_op_unknown(fd, p, i + left, va);
      return i + (left > 0 ? left : 1);
    }
    olen = i + 5;
    printf(fd, "%08x:\t", va);
    print_hex_bytes(fd, p, olen);
    printf(fd, "\tpush   $0x%x\n", q[1] | (q[2]<<8) | (q[3]<<16) | (q[4]<<24));
    return olen;
  }
  if(op == 0x6a){
    if(left < 2){
      print_op_unknown(fd, p, i + 1, va);
      return i + 1;
    }
    olen = i + 2;
    printf(fd, "%08x:\t", va);
    print_hex_bytes(fd, p, olen);
    printf(fd, "\tpush   $0x%x\n", q[1]);
    return olen;
  }
  if(op == 0x89 || op == 0x8b || op == 0x8d || op == 0x39 || op == 0x3b || op == 0x85 || op == 0xff){
    mlen = modrm_total(q + 1, left - 1);
    if(mlen < 0)
      mlen = 1;
    olen = i + 1 + mlen;
    if(olen > i + left)
      olen = i + left;
    printf(fd, "%08x:\t", va);
    print_hex_bytes(fd, p, olen);
    if(op == 0x8d)
      printf(fd, "\tlea    ?\n");
    else if(op == 0x89)
      printf(fd, "\tmov    ?\n");
    else if(op == 0x8b)
      printf(fd, "\tmov    ?\n");
    else if(op == 0xff)
      printf(fd, "\tcall/push?\n");
    else
      printf(fd, "\tcmp/test?\n");
    return olen;
  }
  if(op == 0x83){
    mlen = modrm_total(q + 1, left - 1);
    if(mlen < 0)
      mlen = 2;
    olen = i + 1 + mlen;
    if(olen > i + left)
      olen = i + left;
    printf(fd, "%08x:\t", va);
    print_hex_bytes(fd, p, olen);
    printf(fd, "\tarith  imm8,?\n");
    return olen;
  }
  if(op == 0x81){
    mlen = modrm_total(q + 1, left - 1);
    if(mlen < 0)
      mlen = 5;
    else
      mlen += 4;
    olen = i + 1 + mlen;
    if(olen > i + left)
      olen = i + left;
    printf(fd, "%08x:\t", va);
    print_hex_bytes(fd, p, olen);
    printf(fd, "\tarith  imm32,?\n");
    return olen;
  }

  /* 默认：ModRM 类 0x00-0x3F 等 */
  if((op >= 0x00 && op <= 0x05) || (op >= 0x08 && op <= 0x0d) ||
     (op >= 0x10 && op <= 0x15) || (op >= 0x18 && op <= 0x1d) ||
     (op >= 0x20 && op <= 0x25) || (op >= 0x28 && op <= 0x2d) ||
     (op >= 0x30 && op <= 0x35) || (op >= 0x38 && op <= 0x3d)){
    mlen = modrm_total(q + 1, left - 1);
    if(mlen < 0)
      mlen = 1;
    olen = i + 1 + mlen;
    if(olen > i + left)
      olen = i + left;
    printf(fd, "%08x:\t", va);
    print_hex_bytes(fd, p, olen);
    printf(fd, "\tadd/sub/cmp/or/and ...\n");
    return olen;
  }

  if(op >= 0xb8 && op <= 0xbf){
    if(left < 5){
      print_op_unknown(fd, p, i + left, va);
      return i + (left > 0 ? left : 1);
    }
    olen = i + 5;
    printf(fd, "%08x:\t", va);
    print_hex_bytes(fd, p, olen);
    printf(fd, "\tmov    $0x%x,%s\n",
           q[1] | (q[2]<<8) | (q[3]<<16) | (q[4]<<24),
           reg32[op - 0xb8]);
    return olen;
  }

  print_op_unknown(fd, p, i + 1, va);
  return i + 1;
}

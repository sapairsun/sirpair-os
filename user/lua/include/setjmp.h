#ifndef SIRPAIR_SETJMP_H
#define SIRPAIR_SETJMP_H

typedef int jmp_buf[6];

int setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val) __attribute__((noreturn));

#endif

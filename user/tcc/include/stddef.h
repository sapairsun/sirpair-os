#ifndef SIRPAIR_TCC_STDDEF_H
#define SIRPAIR_TCC_STDDEF_H

typedef unsigned int size_t;
typedef int ptrdiff_t;
#ifndef NULL
#define NULL ((void*)0)
#endif
typedef unsigned short wchar_t;

#define offsetof(type, member) ((size_t)&((type *)0)->member)

#endif

#ifndef SIRPAIR_ASSERT_H
#define SIRPAIR_ASSERT_H

#ifdef NDEBUG
#define assert(x) ((void)0)
#else
#define assert(x) do { if (!(x)) __builtin_trap(); } while (0)
#endif

#endif

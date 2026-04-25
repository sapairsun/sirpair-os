#ifndef SIRPAIR_STDLIB_H
#define SIRPAIR_STDLIB_H

#include <stddef.h>

#define EXIT_FAILURE 1
#define EXIT_SUCCESS 0

void abort(void) __attribute__((noreturn));
void exit(int status);
void *malloc(size_t n);
void free(void *p);
void *realloc(void *p, size_t n);
void *calloc(size_t n, size_t m);

char *getenv(const char *name);
int system(const char *cmd);

long strtol(const char *nptr, char **endptr, int base);
unsigned long strtoul(const char *nptr, char **endptr, int base);
double strtod(const char *nptr, char **endptr);
float strtof(const char *nptr, char **endptr);

int atoi(const char *s);
long atol(const char *s);
int abs(int j);
long labs(long j);

typedef int (*__compar_fn_t)(const void *, const void *);
void qsort(void *base, size_t nmemb, size_t size, __compar_fn_t compar);

#endif

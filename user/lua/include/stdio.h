#ifndef SIRPAIR_STDIO_H
#define SIRPAIR_STDIO_H

#include <stddef.h>
#include <stdarg.h>
#include "printf.h"

typedef struct sirpair_FILE {
  int fd;
  long pos;
  long size;
  int eof;
  int err;
  int unget;
  int has_unget;
  char path[128];
} FILE;

extern FILE __sirpair_stdin_obj, __sirpair_stdout_obj, __sirpair_stderr_obj;
#define stdin (&__sirpair_stdin_obj)
#define stdout (&__sirpair_stdout_obj)
#define stderr (&__sirpair_stderr_obj)

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define _IONBF 0
#define _IOFBF 1
#define _IOLBF 2

#define EOF (-1)

#define BUFSIZ 4096

#define L_tmpnam 64

FILE *fopen(const char *path, const char *mode);
FILE *fdopen(int fd, const char *mode);
FILE *freopen(const char *path, const char *mode, FILE *f);
FILE *tmpfile(void);
int fclose(FILE *f);
size_t fread(void *p, size_t sz, size_t n, FILE *f);
size_t fwrite(const void *p, size_t sz, size_t n, FILE *f);
int fseek(FILE *f, long off, int whence);
long ftell(FILE *f);
void rewind(FILE *f);
int fflush(FILE *f);
void clearerr(FILE *f);
int feof(FILE *f);
int ferror(FILE *f);
int fileno(FILE *f);
int setvbuf(FILE *f, char *buf, int mode, size_t size);

int fgetc(FILE *f);
#define getc(f) fgetc(f)
char *fgets(char *s, int n, FILE *f);
int fputc(int c, FILE *f);
int fputs(const char *s, FILE *f);
int ungetc(int c, FILE *f);

int fprintf(FILE *f, const char *fmt, ...);
int vfprintf(FILE *f, const char *fmt, va_list ap);

#define snprintf snprintf_
#define vsnprintf vsnprintf_
#define sprintf sprintf_

int remove(const char *path);
int rename(const char *oldpath, const char *newpath);
char *tmpnam(char *buf);

#endif

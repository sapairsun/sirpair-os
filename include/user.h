#include "types.h"
#include "mouse_if.h"
#include <stddef.h>

#ifndef SIRPAIR_TIME_T_DEFINED
#define SIRPAIR_TIME_T_DEFINED
typedef int time_t;
#endif

struct stat;
struct statfs;
struct pinfo;
struct net_cfg;

// system calls
int fork(void);
void exit(int status) __attribute__((noreturn));
int wait(void);
int pipe(int*);
int write(int, void*, int);
int read(int, void*, int);
int lseek(int, int, int);
int close(int);
int kill(int);
int exec(char*, char**);
int open(char*, int);
int mknod(char*, short, short);
int unlink(char*);
int fstat(int fd, struct stat*);
int link(char*, char*);
int mkdir(char*);
int chdir(char*);
int dup(int);
int getpid(void);
char* sbrk(int);
int sleep(int);
int uptime(void);
time_t time(time_t *);
int statfs(struct statfs*);
int getprocs(struct pinfo*, int);
int shutdown(void);
int reboot(void);
int info(void);
/* len 为 1024*768（RGB332）或 1024*768*3（BGR 真彩色，与 VBE 帧缓冲字节序一致） */
int gui(void*, int);
int getcwd(char*, int);
int setfgpid(int);
int socket(int, int, int);
int bind(int, char*);
int listen(int, int);
int accept(int);
int connect(int, char*);
int send(int, void*, int);
int recv(int, void*, int);
int dhcp(void);
int getnetcfg(struct net_cfg*);
int ping(uint, int);
int dig(char*, uint*);
int fdready(int, int);
int recvfrom(int, void*, int, char*, int*);
int sendto(int, void*, int, char*, int);
int consize(int*, int*);
int mouse(int, struct mouse_event*, int);
int mouseinit(void);
int mousepoll(struct mouse_event*);
int guimode(int);

// ulib.c
int stat(char*, struct stat*);
char* strcpy(char*, const char*);
void *memmove(void*, const void*, uint);
char* strchr(const char*, int c);
int strcmp(const char*, const char*);
#if !defined(SIRPAIR_TCC)
void printf(int, char*, ...);
#endif
char* gets(char*, int max);
uint strlen(const char*);
void* memset(void*, int, uint);
void* memcpy(void*, const void*, uint);
int memcmp(const void*, const void*, uint);
void* malloc(size_t);
void* realloc(void*, size_t);
void free(void*);
int atoi(const char*);

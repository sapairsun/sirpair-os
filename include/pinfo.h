// Process information structure for ps command
#ifndef _PINFO_H_
#define _PINFO_H_

struct pinfo {
  int pid;           // Process ID
  int ppid;          // Parent process ID
  int state;         // Process state
  int cpu_id;        // CPU this process last ran on
  uint sz;           // Memory size (bytes)
  char name[16];     // Process name
};

// Process states (must match enum procstate in proc.h)
#define PS_UNUSED   0
#define PS_EMBRYO   1
#define PS_SLEEPING 2
#define PS_RUNNABLE 3
#define PS_RUNNING  4
#define PS_ZOMBIE   5

#endif

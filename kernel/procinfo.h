#define PROC_NAME_LEN 16

// States exposed by getprocs().  Their values match enum procstate in proc.h,
// but their names deliberately do not collide with the kernel's enum values.
#define PSTATE_UNUSED    0
#define PSTATE_EMBRYO    1
#define PSTATE_SLEEPING  2
#define PSTATE_RUNNABLE  3
#define PSTATE_RUNNING   4
#define PSTATE_ZOMBIE    5

// Public snapshot of one process.  Do not expose struct proc to user space.
struct procinfo {
  int pid;
  int ppid;
  int state;
  int sz;
  int rtime;
  int wtime;
  char name[PROC_NAME_LEN];
};

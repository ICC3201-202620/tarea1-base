// Test program for Part 1.  It is intended to be compiled after getprocs()
// has been implemented and added to user/user.h and kernel/usys.S.

#include "types.h"
#include "stat.h"
#include "user.h"
#include "procinfo.h"

#define MAX_SNAPSHOT 64

// Kept here so the test source can compile before students add the public
// prototype to user/user.h, which remains a requirement of the assignment.
int getprocs(struct procinfo *buf, int max);

static void
spin(void)
{
  volatile uint value = 0;
  int i;

  for(i = 0; i < 100000000; i++)
    value += i;
  if(value == 0)
    printf(1, "");
}

static int
find_process(struct procinfo *table, int count, int pid)
{
  int i;

  for(i = 0; i < count; i++)
    if(table[i].pid == pid)
      return i;
  return -1;
}

static void
child_cpu(int readyfd, int startfd)
{
  char token;

  write(readyfd, "C", 1);
  read(startfd, &token, 1);
  spin();
  exit();
}

static void
child_sleepy(int readyfd, int startfd)
{
  char token;

  write(readyfd, "S", 1);
  read(startfd, &token, 1);
  sleep(100);
  exit();
}

int
main(void)
{
  int ready[2], start[2];
  int cpu_pid, sleepy_pid;
  int count, i, index;
  char token;
  struct procinfo table[MAX_SNAPSHOT];

  if(getprocs(table, 0) != 0){
    printf(1, "ps_test: getprocs(buf, 0) should return 0\n");
    exit();
  }
  if(getprocs(table, -1) != -1){
    printf(1, "ps_test: getprocs(buf, -1) should return -1\n");
    exit();
  }
  if(pipe(ready) < 0 || pipe(start) < 0){
    printf(1, "ps_test: pipe failed\n");
    exit();
  }

  cpu_pid = fork();
  if(cpu_pid < 0){
    printf(1, "ps_test: fork failed\n");
    exit();
  }
  if(cpu_pid == 0){
    close(ready[0]);
    close(start[1]);
    child_cpu(ready[1], start[0]);
  }

  sleepy_pid = fork();
  if(sleepy_pid < 0){
    printf(1, "ps_test: second fork failed\n");
    exit();
  }
  if(sleepy_pid == 0){
    close(ready[0]);
    close(start[1]);
    child_sleepy(ready[1], start[0]);
  }

  close(ready[1]);
  close(start[0]);
  if(read(ready[0], &token, 1) != 1 || read(ready[0], &token, 1) != 1){
    printf(1, "ps_test: children did not synchronize\n");
    exit();
  }
  close(ready[0]);
  write(start[1], "GO", 2);
  close(start[1]);

  sleep(10);
  count = getprocs(table, MAX_SNAPSHOT);
  if(count <= 0 || count > MAX_SNAPSHOT){
    printf(1, "ps_test: invalid process count %d\n", count);
    exit();
  }

  index = find_process(table, count, getpid());
  if(index < 0){
    printf(1, "ps_test: parent is missing from snapshot\n");
    exit();
  }
  if(find_process(table, count, cpu_pid) < 0 ||
     find_process(table, count, sleepy_pid) < 0){
    printf(1, "ps_test: live child is missing from snapshot\n");
    exit();
  }
  for(i = 0; i < count; i++){
    if(table[i].state < PSTATE_EMBRYO || table[i].state > PSTATE_ZOMBIE ||
       table[i].rtime < 0 || table[i].wtime < 0){
      printf(1, "ps_test: invalid entry for pid %d\n", table[i].pid);
      exit();
    }
  }

  while(wait() > 0)
    ;
  printf(1, "ps_test: PASS\n");
  exit();
}

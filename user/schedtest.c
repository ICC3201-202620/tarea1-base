#include "types.h"
#include "stat.h"
#include "user.h"
#include "procinfo.h"

// Observation program for Part 2. It is intentionally not an automatic
// grading oracle: use its snapshots to check your scheduler's behavior.

#define MAX_SNAPSHOT 64
#define RUN_TICKS 100

int getprocs(struct procinfo *procs, int max);

static void
burn(void)
{
  volatile uint value = 1;
  int i;

  for(i = 0; i < 50000; i++)
    value = value * 1103515245 + 12345;
  if(value == 0)
    printf(1, "");
}

static void
cpu_child(int readyfd, int startfd)
{
  char start;
  uint until;

  write(readyfd, "r", 1);
  read(startfd, &start, 1);
  until = uptime() + RUN_TICKS;
  while(uptime() < until)
    burn();
  exit();
}

static void
interactive_child(int readyfd, int startfd)
{
  char start;
  uint until;

  write(readyfd, "r", 1);
  read(startfd, &start, 1);
  until = uptime() + RUN_TICKS;
  while(uptime() < until){
    burn();
    sleep(8);
  }
  exit();
}

static struct procinfo*
find_process(struct procinfo *procs, int count, int pid)
{
  int i;

  for(i = 0; i < count; i++)
    if(procs[i].pid == pid)
      return &procs[i];
  return 0;
}

static void
show_process(char *label, struct procinfo *procs, int count, int pid)
{
  struct procinfo *p;

  p = find_process(procs, count, pid);
  if(p == 0)
    printf(1, "  %s pid %d: terminado\n", label, pid);
  else
    printf(1, "  %s pid %d: prioridad %d, estado %d, cpu %d, espera %d\n",
           label, p->pid, p->priority, p->state, p->rtime, p->wtime);
}

int
main(void)
{
  int ready[2], start[2];
  int cpu_a, cpu_b, interactive;
  int i, count;
  char ready_byte;
  struct procinfo procs[MAX_SNAPSHOT];

  if(pipe(ready) < 0 || pipe(start) < 0){
    printf(1, "schedtest: no se pudieron crear pipes\n");
    exit();
  }

  cpu_a = fork();
  if(cpu_a == 0)
    cpu_child(ready[1], start[0]);
  if(cpu_a < 0){
    printf(1, "schedtest: fork fallo\n");
    exit();
  }

  cpu_b = fork();
  if(cpu_b == 0)
    cpu_child(ready[1], start[0]);
  if(cpu_b < 0){
    printf(1, "schedtest: fork fallo\n");
    exit();
  }

  interactive = fork();
  if(interactive == 0)
    interactive_child(ready[1], start[0]);
  if(interactive < 0){
    printf(1, "schedtest: fork fallo\n");
    exit();
  }

  for(i = 0; i < 3; i++)
    read(ready[0], &ready_byte, 1);
  write(start[1], "GO!", 3);

  printf(1, "schedtest: dos procesos intensivos y uno interactivo\n");
  for(i = 0; i < 8; i++){
    sleep(12);
    count = getprocs(procs, MAX_SNAPSHOT);
    if(count < 0){
      printf(1, "schedtest: getprocs fallo\n");
      break;
    }
    printf(1, "tick %d:\n", uptime());
    show_process("cpu-a", procs, count, cpu_a);
    show_process("cpu-b", procs, count, cpu_b);
    show_process("interactivo", procs, count, interactive);
  }

  wait();
  wait();
  wait();
  printf(1, "schedtest: terminado\n");
  exit();
}

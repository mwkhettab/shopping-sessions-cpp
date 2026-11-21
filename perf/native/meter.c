#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static double elapsed(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec + t.tv_nsec * 1e-9;
}
int main(int argc, char **argv) {
  double start = elapsed();
  pid_t pid = fork();
  if (pid == 0) {
    if (argc > 1) execv(argv[1], argv + 1);
    else execl("/app/shopping", "/app/shopping", (char *)0);
    _exit(127);
  }
  struct rusage usage;
  int status;
  if (pid < 0 || wait4(pid, &status, 0, &usage) < 0) return 125;
  long long peak = -1;
  FILE *f = fopen("/sys/fs/cgroup/memory.peak", "r");
  if (f) { if (fscanf(f, "%lld", &peak) != 1) peak = -1; fclose(f); }
  printf("wall %.3f s, process peak RSS %.1f MiB, cgroup peak %.1f MiB, major faults %ld, minor faults %ld\n",
         elapsed() - start, usage.ru_maxrss/1024.0, peak/1048576.0, usage.ru_majflt, usage.ru_minflt);
  return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}

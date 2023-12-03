#define _GNU_SOURCE
#include <asm/unistd.h>
#include <linux/perf_event.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static long perf_event_open(struct perf_event_attr *hw_event, pid_t pid,
                            int cpu, int group_fd, unsigned long flags) {
  int ret;

  ret = syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
  return ret;
}

int main(int argc, char **argv) {
  struct perf_event_attr pe;
  long long count;
  int fd;
  pid_t target_pid;

  if (argc != 2) {
    fprintf(stderr, "Usage: %s <pid>\n", argv[0]);
    exit(EXIT_FAILURE);
  }

  target_pid = atoi(argv[1]);

  memset(&pe, 0, sizeof(struct perf_event_attr));
  pe.type = PERF_TYPE_RAW;
  pe.size = sizeof(struct perf_event_attr);
  pe.config = 0x412e; // MEM_LOAD_RETIRED.L3_MISS
  pe.disabled = 1;
  pe.exclude_kernel = 1;
  pe.exclude_hv = 1;

  fd = perf_event_open(&pe, target_pid, -1, -1, 0);
  if (fd == -1) {
    fprintf(stderr, "Error opening event %llx for pid %d\n", pe.config,
            target_pid);
    exit(EXIT_FAILURE);
  }

  ioctl(fd, PERF_EVENT_IOC_RESET, 0);
  ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);

  // foo

  ioctl(fd, PERF_EVENT_IOC_DISABLE, 0);
  read(fd, &count, sizeof(long long));

  printf("Process %d had %lld L3 cache misses\n", target_pid, count);

  close(fd);
}

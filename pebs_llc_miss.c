#define _GNU_SOURCE
#include <asm/unistd.h>
#include <errno.h>
#include <linux/perf_event.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static long perf_event_open(struct perf_event_attr *hw_event, pid_t pid,
                            int cpu, int group_fd, unsigned long flags) {
  return syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
}

int main(int argc, char **argv) {
  struct perf_event_attr pe;
  int fd_misses, fd_accesses;
  uint64_t misses, accesses;
  ssize_t readSize;

  memset(&pe, 0, sizeof(struct perf_event_attr));
  pe.type = PERF_TYPE_HARDWARE;
  pe.size = sizeof(struct perf_event_attr);
  pe.config = PERF_COUNT_HW_CACHE_MISSES;
  pe.disabled = 1;
  pe.exclude_kernel = 1;
  pe.exclude_hv = 1;

  fd_misses = perf_event_open(&pe, 0, -1, -1, 0);
  if (fd_misses == -1) {
    fprintf(stderr, "Error opening event for cache misses: %s\n",
            strerror(errno));
    exit(EXIT_FAILURE);
  }

  pe.config = PERF_COUNT_HW_CACHE_REFERENCES;

  fd_accesses = perf_event_open(&pe, 0, -1, -1, 0);
  if (fd_accesses == -1) {
    fprintf(stderr, "Error opening event for cache accesses: %s\n",
            strerror(errno));
    close(fd_misses);
    exit(EXIT_FAILURE);
  }

  ioctl(fd_misses, PERF_EVENT_IOC_RESET, 0);
  ioctl(fd_misses, PERF_EVENT_IOC_ENABLE, 0);
  ioctl(fd_accesses, PERF_EVENT_IOC_RESET, 0);
  ioctl(fd_accesses, PERF_EVENT_IOC_ENABLE, 0);

  sleep(10);

  ioctl(fd_misses, PERF_EVENT_IOC_DISABLE, 0);
  ioctl(fd_accesses, PERF_EVENT_IOC_DISABLE, 0);

  readSize = read(fd_misses, &misses, sizeof(misses));
  if (readSize != sizeof(misses)) {
    fprintf(stderr, "Error reading cache misses\n");
  }

  readSize = read(fd_accesses, &accesses, sizeof(accesses));
  if (readSize != sizeof(accesses)) {
    fprintf(stderr, "Error reading cache accesses\n");
  }

  close(fd_misses);
  close(fd_accesses);

  printf("Cache Misses: %lu\n", misses);
  printf("Cache Accesses: %lu\n", accesses);

  if (accesses > 0) {
    double miss_rate = (double)misses / accesses;
    printf("Cache miss rate: %.2f%%\n", miss_rate * 100);
  }

  return 0;
}

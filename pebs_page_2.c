#include <asm/unistd_64.h>
#include <assert.h>
#include <errno.h>
#include <linux/hw_breakpoint.h>
#include <linux/perf_event.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#define PEBS_NPROCS 8
#define NPBUFTYPES 2
#define PERF_PAGES 2

static struct perf_event_mmap_page *perf_page[PEBS_NPROCS][NPBUFTYPES];
static int pfd[PEBS_NPROCS][NPBUFTYPES];

static long perf_event_open(struct perf_event_attr *hw_event, pid_t pid,
                            int cpu, int group_fd, unsigned long flags) {
  return syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
}

static struct perf_event_mmap_page *perf_setup(__u64 config, __u64 cpu) {
  struct perf_event_attr attr;
  memset(&attr, 0, sizeof(struct perf_event_attr));

  attr.type = PERF_TYPE_RAW;
  attr.size = sizeof(struct perf_event_attr);
  attr.config = config;
  attr.sample_type =
      PERF_SAMPLE_IP | PERF_SAMPLE_TID | PERF_SAMPLE_WEIGHT | PERF_SAMPLE_ADDR;
  attr.disabled = 0;
  attr.exclude_kernel = 1;
  attr.exclude_hv = 1;
  attr.precise_ip = 1;

  int pfd = perf_event_open(&attr, -1, cpu, -1, 0);
  if (pfd == -1) {
    fprintf(stderr, "perf_event_open failed: %s\n", strerror(errno));
    exit(EXIT_FAILURE);
  }

  size_t mmap_size = sysconf(_SC_PAGESIZE) * PERF_PAGES;
  struct perf_event_mmap_page *p = (struct perf_event_mmap_page *)mmap(
      NULL, mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED, pfd, 0);
  assert(p != MAP_FAILED);

  return p;
}

void pebs_init(void) {
  for (int i = 0; i < PEBS_NPROCS; i++) {
    perf_page[i][0] =
        perf_setup(0x1d3, i); // MEM_LOAD_L3_MISS_RETIRED.LOCAL_DRAM
  }
}

void pebs_shutdown() {
  for (int i = 0; i < PEBS_NPROCS; i++) {
    for (int j = 0; j < NPBUFTYPES; j++) {
      ioctl(pfd[i][j], PERF_EVENT_IOC_DISABLE, 0);

      if (perf_page[i][j]) {
        munmap(perf_page[i][j], sysconf(_SC_PAGESIZE) * PERF_PAGES);
      }
      if (pfd[i][j] != -1) {
        close(pfd[i][j]);
      }
    }
  }
}

int main() {
  pebs_init();

  pebs_shutdown();
  return 0;
}

#include <asm/unistd.h>
#include <linux/perf_event.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

#define SAMPLE_PERIOD 100
#define PERF_PAGES 2
#define PAGE_SIZE 4096
#define MAX_PAGES 65536

static long perf_event_open(struct perf_event_attr *hw_event, pid_t pid,
                            int cpu, int group_fd, unsigned long flags) {
  return syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
}

int main() {
  struct perf_event_attr pe;
  int fd;
  void *perf_mmap;
  uint64_t page_accesses[MAX_PAGES] = {0};

  memset(&pe, 0, sizeof(struct perf_event_attr));
  pe.type = PERF_TYPE_HARDWARE;
  pe.size = sizeof(struct perf_event_attr);
  pe.config = PERF_COUNT_HW_CACHE_MISSES;
  pe.sample_period = SAMPLE_PERIOD;
  pe.sample_type = PERF_SAMPLE_IP | PERF_SAMPLE_ADDR;
  pe.disabled = 1;
  pe.exclude_kernel = 1;
  pe.exclude_hv = 1;
  pe.precise_ip = 2;

  fd = perf_event_open(&pe, 0, -1, -1, 0);
  if (fd == -1) {
    perror("perf_event_open");
    exit(EXIT_FAILURE);
  }

  size_t mmap_size = sysconf(_SC_PAGESIZE) * PERF_PAGES;
  perf_mmap = mmap(NULL, mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (perf_mmap == MAP_FAILED) {
    perror("mmap");
    close(fd);
    exit(EXIT_FAILURE);
  }

  ioctl(fd, PERF_EVENT_IOC_RESET, 0);
  ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);

  // Replace with an actual workload
  sleep(10);

  ioctl(fd, PERF_EVENT_IOC_DISABLE, 0);

  struct perf_event_mmap_page *metadata_page = perf_mmap;
  long data_head = metadata_page->data_head;
  long data_tail = metadata_page->data_tail;

  printf("data_head: %ld, data_tail: %ld\n", data_head, data_tail);

  __sync_synchronize();

  while (data_tail < data_head) {
    struct perf_event_header *header =
        (struct perf_event_header *)((char *)perf_mmap +
                                     data_tail % (mmap_size - PAGE_SIZE));
    if (header->type == PERF_RECORD_SAMPLE) {
      uint64_t addr = *((uint64_t *)((char *)header + 8));
      uint64_t page_number = addr / PAGE_SIZE;
      if (page_number < MAX_PAGES) {
        page_accesses[page_number]++;
      }
    }
    data_tail += header->size;
  }

  metadata_page->data_tail = data_tail;

  munmap(perf_mmap, mmap_size);
  close(fd);

  for (uint64_t i = 0; i < MAX_PAGES; i++) {
    if (page_accesses[i] > 0) {
      printf("Page %lu had %lu accesses.\n", i, page_accesses[i]);
    }
  }

  return 0;
}

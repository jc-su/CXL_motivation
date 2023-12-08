#include <asm/unistd.h>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <errno.h>
#include <iostream>
#include <linux/perf_event.h>
#include <map>
#include <stdexcept>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <thread>
#include <unistd.h>
#include <vector>

constexpr int PERF_PAGES = 2;
constexpr __u64 SAMPLE_PERIOD = 1000;

class PerfEvent {
public:
  PerfEvent(__u64 config, __u64 config1, __u64 cpu, __u64 type) {
    struct perf_event_attr attr;
    memset(&attr, 0, sizeof(struct perf_event_attr));
    attr.type = PERF_TYPE_RAW;
    attr.size = sizeof(struct perf_event_attr);
    attr.config = config;
    attr.config1 = config1;
    attr.sample_period = SAMPLE_PERIOD;
    attr.sample_type = PERF_SAMPLE_IP | PERF_SAMPLE_TID | PERF_SAMPLE_WEIGHT |
                       PERF_SAMPLE_ADDR;
    attr.disabled = 1;
    attr.exclude_kernel = 1;
    attr.exclude_hv = 1;
    attr.exclude_callchain_kernel = 1;
    attr.exclude_callchain_user = 1;
    attr.precise_ip = 1;

    fd = perf_event_open(&attr, -1, cpu, -1, 0);
    if (fd == -1) {
      throw std::runtime_error("perf_event_open failed: " +
                               std::string(strerror(errno)));
    }

    size_t mmap_size = sysconf(_SC_PAGESIZE) * PERF_PAGES;
    metadata_page = static_cast<struct perf_event_mmap_page *>(
        mmap(NULL, mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
    if (metadata_page == MAP_FAILED) {
      throw std::runtime_error("mmap failed: " + std::string(strerror(errno)));
    }
  }

  ~PerfEvent() {
    munmap(metadata_page, sysconf(_SC_PAGESIZE) * PERF_PAGES);
    close(fd);
  }

  void start() {
    ioctl(fd, PERF_EVENT_IOC_RESET, 0);
    ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);
  }

  void stop() { ioctl(fd, PERF_EVENT_IOC_DISABLE, 0); }

  void read() {
    uint64_t head = metadata_page->data_head;
    uint64_t tail = metadata_page->data_tail;
    uint64_t size = head - tail;
    std::cout << "head: " << head << ", tail: " << tail << ", size: " << size
              << std::endl;
    if (size == 0) {
      return;
    }

    std::vector<struct perf_event_header> headers(size);
    std::vector<char> data(size);
    uint64_t data_offset = tail % (sysconf(_SC_PAGESIZE) * PERF_PAGES);
    uint64_t data_size = size * sizeof(struct perf_event_header);
    memcpy(headers.data(), ((char *)metadata_page) + data_offset, data_size);
    data_offset =
        (data_offset + data_size) % (sysconf(_SC_PAGESIZE) * PERF_PAGES);
    data_size = size * sizeof(char);
    memcpy(data.data(), ((char *)metadata_page) + data_offset, data_size);

    for (size_t i = 0; i < size; i++) {
      std::cout << "i: " << i << std::endl;
      struct perf_event_header *header = &headers[i];
      char *event = &data[i];
      if (header->type == PERF_RECORD_SAMPLE) {
        struct perf_event_sample *sample = (struct perf_event_sample *)event;
        uint64_t ip = sample->ip;
        uint64_t pid = sample->pid;
        uint64_t tid = sample->tid;
        uint64_t addr = sample->addr;
        uint64_t weight = sample->weight;
        std::cout << "ip: " << ip << ", pid: " << pid << ", tid: " << tid
                  << ", addr: " << addr << ", weight: " << weight << std::endl;
      }
    }

    metadata_page->data_tail += size;
  }

private:
  int fd;
  struct perf_event_mmap_page *metadata_page;

  static long perf_event_open(struct perf_event_attr *hw_event, pid_t pid,
                              int cpu, int group_fd, unsigned long flags) {
    return syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
  }
  struct perf_event_sample {
    __u64 ip;
    __u32 pid, tid;
    __u64 addr;
    __u64 weight;
  };
};

int main() {
  try {
    PerfEvent perf_event(PERF_COUNT_HW_CACHE_MISSES,
                         PERF_COUNT_HW_CACHE_REFERENCES, 0, PERF_TYPE_HW_CACHE);
    perf_event.start();
    std::this_thread::sleep_for(std::chrono::seconds(1));
    perf_event.stop();
    perf_event.read();
  } catch (const std::exception &e) {
    std::cerr << e.what() << std::endl;
    return 1;
  }
  return 0;
}
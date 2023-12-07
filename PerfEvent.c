#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sys/mman.h>
#include <system_error>
#include <thread>
#include <unistd.h>
#include <vector>

extern "C" {
#include <asm/unistd.h>
#include <linux/perf_event.h>
}

// Constants
constexpr int PEBS_NPROCS = 4;
constexpr int PERF_PAGES = 1;

// RAII wrapper for perf_event file descriptor
class PerfEvent {
public:
  PerfEvent(int cpu) {
    struct perf_event_attr attr {};
    std::memset(&attr, 0, sizeof(attr));
    attr.type = PERF_TYPE_RAW;
    attr.size = sizeof(attr);
    attr.config = 0x1d3; // MEM_LOAD_L3_MISS_RETIRED.LOCAL_DRAM
    attr.sample_type = PERF_SAMPLE_IP | PERF_SAMPLE_TID | PERF_SAMPLE_ADDR;
    attr.precise_ip = 2;

    fd = syscall(__NR_perf_event_open, &attr, -1, cpu, -1, 0);
    if (fd == -1) {
      throw std::system_error(errno, std::generic_category(),
                              "perf_event_open failed");
    }

    size_t mmap_size = sysconf(_SC_PAGESIZE) * PERF_PAGES;
    page = static_cast<perf_event_mmap_page *>(
        mmap(nullptr, mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
    if (page == MAP_FAILED) {
      throw std::system_error(errno, std::generic_category(), "mmap failed");
    }
  }

  ~PerfEvent() {
    if (page != MAP_FAILED) {
      munmap(page, sysconf(_SC_PAGESIZE) * PERF_PAGES);
    }
    if (fd != -1) {
      close(fd);
    }
  }

  perf_event_mmap_page *getPage() const { return page; }

  int getFd() const { return fd; }

private:
  int fd{-1};
  perf_event_mmap_page *page =
      reinterpret_cast<perf_event_mmap_page *>(MAP_FAILED);
};

// Thread function for scanning PEBS data
void pebs_scan_thread(int cpu, std::atomic<bool> &running,
                      perf_event_mmap_page *page) {
  std::thread::id thread_id = std::this_thread::get_id();
  printf("Thread %ld running on CPU %d\n",
         *(reinterpret_cast<long *>(&thread_id)), cpu);

  while (running) {
    char *pbuf = reinterpret_cast<char *>(page) + page->data_offset;
    std::atomic_thread_fence(std::memory_order_acquire);

    if (page->data_head == page->data_tail) {
      continue;
    }

    // Process the data...
    struct perf_event_header *header = reinterpret_cast<perf_event_header *>(
        pbuf + (page->data_tail % page->data_size));
    if (header->type == PERF_RECORD_SAMPLE) {
      struct Event {
        uint64_t ip;
        uint32_t pid, tid;
        uint64_t addr;
      } *event = reinterpret_cast<Event *>(reinterpret_cast<char *>(header) +
                                           sizeof(*header));

      printf("IP: %lx, PID: %d, TID: %d, ADDR: %lx\n", event->ip, event->pid,
             event->tid, event->addr);
    }

    page->data_tail += sizeof(*header);
  }
}

int main() {
  std::vector<PerfEvent> perfEvents;
  std::vector<std::thread> threads;
  std::atomic<bool> running{true};

  for (int i = 0; i < PEBS_NPROCS; ++i) {
    perfEvents.emplace_back(i);
  }

  for (int i = 0; i < PEBS_NPROCS; ++i) {
    threads.emplace_back(pebs_scan_thread, i, std::ref(running),
                         perfEvents[i].getPage());
  }

  // Wait for some time or some condition
  // ...

  running = false;

  for (auto &thread : threads) {
    thread.join();
  }

  return 0;
}

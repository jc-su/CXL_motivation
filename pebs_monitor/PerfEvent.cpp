#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <sys/mman.h>
#include <system_error>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

extern "C" {
#include <asm/unistd.h>
#include <linux/perf_event.h>
}

constexpr int PEBS_NPROCS = 4;
constexpr int PERF_PAGES = 1;
constexpr size_t PAGE_SIZE = 4096;

std::mutex pid_set_mutex;
std::unordered_set<uint32_t> monitored_pids;

class PerfEvent {
public:
  PerfEvent(int cpu) {
    struct perf_event_attr attr {};
    attr.type = PERF_TYPE_RAW;
    attr.size = sizeof(attr);
    attr.config = 0x1d3;
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

private:
  int fd{-1};
  perf_event_mmap_page *page =
      reinterpret_cast<perf_event_mmap_page *>(MAP_FAILED);
};

struct Event {
  uint64_t ip;
  uint32_t pid, tid;
  uint64_t addr;
};

bool is_pid_monitored(uint32_t pid) {
  std::lock_guard<std::mutex> lock(pid_set_mutex);
  return monitored_pids.find(pid) != monitored_pids.end();
}

void add_pid_to_monitor(uint32_t pid) {
  std::lock_guard<std::mutex> lock(pid_set_mutex);
  monitored_pids.insert(pid);
}

void remove_pid_from_monitor(uint32_t pid) {
  std::lock_guard<std::mutex> lock(pid_set_mutex);
  monitored_pids.erase(pid);
}

void process_perf_event(const Event &event,
                        std::unordered_map<size_t, size_t> &pageAccessCounts) {
  if (is_pid_monitored(event.pid)) {
    size_t page_number = event.addr / PAGE_SIZE;
    pageAccessCounts[page_number]++;
  }
}

void pebs_scan_thread(int cpu, std::atomic<bool> &running,
                      perf_event_mmap_page *page, std::condition_variable &cv,
                      std::mutex &mutex) {
  std::unordered_map<size_t, size_t> pageAccessCounts;

  while (running) {
    std::unique_lock<std::mutex> lock(mutex);
    cv.wait(lock, [&running, &page] {
      return !running || page->data_head != page->data_tail;
    });
    lock.unlock();

    if (!running) {
      break;
    }

    char *pbuf = reinterpret_cast<char *>(page) + page->data_offset;
    std::atomic_thread_fence(std::memory_order_acquire);

    while (page->data_head != page->data_tail) {
      struct perf_event_header *header = reinterpret_cast<perf_event_header *>(
          pbuf + (page->data_tail % page->data_size));
      if (header->type == PERF_RECORD_SAMPLE) {
        Event *event = reinterpret_cast<Event *>(
            reinterpret_cast<char *>(header) + sizeof(*header));
        process_perf_event(*event, pageAccessCounts);
      }

      page->data_tail += sizeof(*header);
    }
  }

  // hot/cold pages
  for (const auto &pair : pageAccessCounts) {
    printf("Page %zu accessed %zu times\n", pair.first, pair.second);
  }
}

int main() {
  std::vector<PerfEvent> perfEvents;
  std::vector<std::thread> threads;
  std::atomic<bool> running{true};
  std::condition_variable cv;
  std::mutex mutex;

  perfEvents.reserve(PEBS_NPROCS);
  threads.reserve(PEBS_NPROCS);

  for (int i = 0; i < PEBS_NPROCS; ++i) {
    perfEvents.emplace_back(i);
  }

  for (int i = 0; i < PEBS_NPROCS; ++i) {
    threads.emplace_back(pebs_scan_thread, i, std::ref(running),
                         perfEvents[i].getPage(), std::ref(cv),
                         std::ref(mutex));
  }

  // add PIDs to monitor
  add_pid_to_monitor(1234);
  add_pid_to_monitor(5678);

  remove_pid_from_monitor(1234);

  running = false;
  cv.notify_all();

  for (auto &thread : threads) {
    thread.join();
  }

  return 0;
}

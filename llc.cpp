#include <asm/unistd.h>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <linux/perf_event.h>
#include <stdexcept>
#include <string>
#include <sys/ioctl.h>
#include <unistd.h>
#include <thread>

class PerfEvent {
public:
  PerfEvent(uint64_t type, uint64_t config) {
    memset(&pe, 0, sizeof(struct perf_event_attr));
    pe.type = type;
    pe.size = sizeof(struct perf_event_attr);
    pe.config = config;
    pe.disabled = 1;
    pe.exclude_kernel = 1;
    pe.exclude_hv = 1;

    fd = perf_event_open(&pe, 0, -1, -1, 0);
    if (fd == -1) {
      throw std::runtime_error("Error opening performance event: " +
                               std::string(strerror(errno)));
    }
  }

  ~PerfEvent() { close(fd); }

  void start() {
    ioctl(fd, PERF_EVENT_IOC_RESET, 0);
    ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);
  }

  void stop() { ioctl(fd, PERF_EVENT_IOC_DISABLE, 0); }

  uint64_t readCounter() {
    uint64_t count;
    if (read(fd, &count, sizeof(count)) != sizeof(count)) {
      throw std::runtime_error("Error reading performance counter");
    }
    return count;
  }

private:
  int fd;
  struct perf_event_attr pe;

  static long perf_event_open(struct perf_event_attr *hw_event, pid_t pid,
                              int cpu, int group_fd, unsigned long flags) {
    return syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
  }
};

int main() {
  try {
    // Replace these with the specific hardware event types and configurations
    // for your CPU
    PerfEvent llcMisses(PERF_TYPE_HARDWARE, PERF_COUNT_HW_CACHE_LL);
    PerfEvent dtlbMisses(PERF_TYPE_HARDWARE, PERF_COUNT_HW_CACHE_DTLB);

    llcMisses.start();
    dtlbMisses.start();

    // Simulate workload for 10 seconds
    std::this_thread::sleep_for(std::chrono::seconds(10));

    llcMisses.stop();
    dtlbMisses.stop();

    uint64_t llcMissCount = llcMisses.readCounter();
    uint64_t dtlbMissCount = dtlbMisses.readCounter();

    std::cout << "LLC Misses: " << llcMissCount << "\n";
    std::cout << "DTLB Misses: " << dtlbMissCount << "\n";
  } catch (const std::exception &e) {
    std::cerr << "Exception: " << e.what() << "\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

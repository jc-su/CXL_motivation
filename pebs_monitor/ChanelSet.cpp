#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <functional>
#include <iostream>
#include <linux/perf_event.h>
#include <map>
#include <memory>
#include <set>
#include <spdlog/spdlog.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <vector>

constexpr int WAKEUP_EVENTS = 1;
constexpr unsigned long INIT_SAMPLE_PERIOD = 100000;
constexpr int PAGE_SIZE = 4096;
constexpr int RING_BUFFER_PAGES = 4;
constexpr int MMAP_SIZE = ((1 + RING_BUFFER_PAGES) * PAGE_SIZE);

#define READ_MEMORY_BARRIER() __builtin_ia32_lfence()

// Error handling utility
template <typename CleanupFunc, typename... MsgArgs>
auto Error(CleanupFunc cleanup, int ret, bool show_errstr, MsgArgs... msgs) {
  std::string errstr = (show_errstr) ? std::strerror(errno) : "";
  cleanup();
  std::fprintf(stderr, "[<%s> @ %s: %d]: ", __FUNCTION__, __FILE__, __LINE__);
  (std::fprintf(stderr, "%s", msgs), ...);
  std::fprintf(stderr, "%s\n", errstr.c_str());
  return ret;
}

static int perf_event_open(struct perf_event_attr *attr, pid_t pid, int cpu,
                           int group_fd, unsigned long flags) {
  return syscall(__NR_perf_event_open, attr, pid, cpu, group_fd, flags);
}

class Channel {
public:
  enum Type {
    // EventSel=D3H UMask=01H
    // Counter=0,1,2,3 CounterHTOff=0,1,2,3
    // PEBS:[PreciseEventingIP]
    LLC_MISSES = 0x01D3,
  };

  struct Sample {
    Type type;
    uint32_t cpu;
    uint32_t pid;
    uint32_t tid;
    uint64_t address;
  };

  Channel() : m_fd(-1), m_buffer(nullptr), m_period(0) {}

  ~Channel() { unbind(); }

  int bind(pid_t pid, Type type) {
    if (m_fd >= 0) {
      return Error([] {}, -EINVAL, false, "Channel already bound");
    }

    struct perf_event_attr attr {};
    attr.type = PERF_TYPE_RAW;
    attr.config = static_cast<uint64_t>(type);
    attr.size = sizeof(struct perf_event_attr);
    attr.sample_period = INIT_SAMPLE_PERIOD;
    attr.sample_type = PERF_SAMPLE_IDENTIFIER | PERF_SAMPLE_TID |
                       PERF_SAMPLE_ADDR | PERF_SAMPLE_CPU;
    attr.disabled = 1;
    attr.exclude_kernel = 1;
    attr.precise_ip = 3;
    attr.wakeup_events = WAKEUP_EVENTS;

    m_fd = perf_event_open(&attr, pid, -1, -1, 0);
    if (m_fd < 0) {
      return Error([] {}, -errno, true, "perf_event_open() failed");
    }

    m_buffer =
        mmap(nullptr, MMAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, m_fd, 0);
    if (m_buffer == MAP_FAILED) {
      return Error([this] { close(m_fd); }, -errno, true, "mmap() failed");
    }

    uint64_t id;
    if (ioctl(m_fd, PERF_EVENT_IOC_ID, &id) < 0) {
      return Error(
          [this] {
            munmap(m_buffer, MMAP_SIZE);
            close(m_fd);
          },
          -errno, true, "ioctl() failed");
    }

    m_pid = pid;
    m_type = type;
    m_id = id;
    return 0;
  }

  void unbind() {
    if (m_fd >= 0) {
      munmap(m_buffer, MMAP_SIZE);
      close(m_fd);
      m_fd = -1;
    }
  }

  int setPeriod(unsigned long period) {
    if (m_fd < 0) {
      return Error([] {}, -EINVAL, false, "Channel not bound");
    }

    if (m_period == period) {
      return 0; // No change
    }

    if (ioctl(m_fd, PERF_EVENT_IOC_PERIOD, &period) < 0) {
      return Error([] {}, -errno, true, "ioctl(PERF_EVENT_IOC_PERIOD) failed");
    }

    m_period = period;
    return 0;
  }

  int readSample(Sample *sample) {
    if (m_fd < 0) {
      return Error([] {}, -EINVAL, false, "Channel not bound");
    }

    auto *meta = reinterpret_cast<struct perf_event_mmap_page *>(m_buffer);
    uint64_t tail = meta->data_tail;
    uint64_t head = meta->data_head;
    READ_MEMORY_BARRIER();
    // assert(tail <= head);
    spdlog::debug("tail: {}, head: {}", tail, head);

    if (tail == head) {
      return -EAGAIN; // No data available
    }

    bool available = false;
    while (tail < head) {
      uint64_t position = tail % (PAGE_SIZE * RING_BUFFER_PAGES);
      auto *entry = reinterpret_cast<struct perf_sample *>(
          (char *)m_buffer + PAGE_SIZE + position);

      tail += entry->header.size;
      if (entry->header.type == PERF_RECORD_SAMPLE && entry->id == m_id &&
          entry->pid == m_pid) {
        sample->type = m_type;
        sample->cpu = entry->cpu;
        sample->pid = entry->pid;
        sample->tid = entry->tid;
        sample->address = entry->address;
        available = true;
        break;
      }
    }

    meta->data_tail = tail;
    return available ? 0 : -EAGAIN;
  }

  pid_t getPid() const { return m_pid; }

  Type getType() const { return m_type; }

  int getPerfFd() const { return m_fd; }

private:
  pid_t m_pid;
  Type m_type;
  int m_fd;
  uint64_t m_id;
  void *m_buffer;
  unsigned long m_period;
  struct perf_event_header {
    uint32_t type;
    uint16_t misc;
    uint16_t size;
  };

  struct perf_sample {
    struct perf_event_header header;
    uint64_t id;
    pid_t pid, tid;
    uint64_t address; // sampled instruction
    uint32_t cpu, res;
  };
};

class ChannelSet {
public:
  ChannelSet() : m_period(0), m_epollfd(-1) {}

  ~ChannelSet() { deinit(); }

  int init(const std::set<Channel::Type> &types) {
    if (m_epollfd >= 0) {
      return Error([] {}, -EINVAL, false, "ChannelSet already initialized");
    }

    if (types.empty()) {
      return Error([] {}, -EINVAL, false, "No types specified");
    }

    m_epollfd = epoll_create(1);
    if (m_epollfd < 0) {
      return Error([] {}, -errno, true, "epoll_create() failed");
    }

    m_types.assign(types.begin(), types.end());
    return 0;
  }

  void deinit() {
    if (m_epollfd >= 0) {
      close(m_epollfd);
      m_epollfd = -1;
    }
    m_entries.clear();
    m_types.clear();
  }

  int add(pid_t pid) {
    if (m_epollfd < 0) {
      return Error([] {}, -EINVAL, false, "ChannelSet not initialized");
    }

    // Check if an entry with the given pid already exists
    auto it =
        std::find_if(m_entries.begin(), m_entries.end(),
                     [pid](const Entry &entry) { return entry.pid == pid; });

    // PID already exists
    if (it != m_entries.end()) {
      return 0;
    }

    Entry entry;
    entry.pid = pid;
    entry.channels = std::make_unique<Channel[]>(m_types.size());

    for (size_t i = 0; i < m_types.size(); ++i) {
      int ret = entry.channels[i].bind(pid, m_types[i]);
      if (ret != 0) {
        return Error([this] { deinit(); }, ret, false, "Channel bind failed");
      }
    }

    m_entries.insert(std::move(entry));
    return 0;
  }

  int remove(pid_t pid) {
    if (m_epollfd < 0) {
      return Error([] {}, -EINVAL, false, "ChannelSet not initialized");
    }

    auto it = m_entries.find(Entry(pid));
    if (it != m_entries.end()) {
      m_entries.erase(it);
    }

    return 0;
  }

  int setPeriod(unsigned long period) {
    if (m_epollfd < 0) {
      return Error([] {}, -EINVAL, false, "ChannelSet not initialized");
    }

    for (auto &entry : m_entries) {
      for (size_t i = 0; i < m_types.size(); ++i) {
        int ret = entry.channels[i].setPeriod(period);
        if (ret != 0) {
          return ret;
        }
      }
    }

    m_period = period;
    return 0;
  }

  ssize_t pollSamples(int timeout, void *privdata,
                      void (*on_sample)(void *privdata,
                                        Channel::Sample *sample)) {
    if (m_epollfd < 0) {
      return Error([] {}, -EINVAL, false, "ChannelSet not initialized");
    }

    struct epoll_event events[10]; // Adjust size
    //  EPOLLIN | EPOLLHUP;

    for (auto &entry : m_entries) {
      for (size_t i = 0; i < m_types.size(); ++i) {
        struct epoll_event event {};
        event.events = EPOLLIN | EPOLLHUP;
        event.data.ptr = &entry.channels[i];
        if (epoll_ctl(m_epollfd, EPOLL_CTL_ADD, entry.channels[i].getPerfFd(),
                      &event) < 0) {
          return Error([] {}, -errno, true, "epoll_ctl failed");
        }
      }
    }
    
    int num_events = epoll_wait(m_epollfd, events, 10, timeout);
    if (num_events < 0) {
      return Error([] {}, -errno, true, "epoll_wait failed");
    }

    spdlog::debug("num_events: {}", num_events);

    ssize_t count = 0;
    for (int i = 0; i < num_events; ++i) {
      Channel *channel = static_cast<Channel *>(events[i].data.ptr);
      Channel::Sample sample;
      int ret = channel->readSample(&sample);
      if (ret == 0 && on_sample) {
        on_sample(privdata, &sample);
        ++count;
      }
    }

    return count;
  }

private:
  struct Entry {
    pid_t pid;
    std::unique_ptr<Channel[]> channels;

    Entry(pid_t pid_val = 0) : pid(pid_val) {}

    bool operator<(const Entry &other) const { return pid < other.pid; }
  };

  std::vector<Channel::Type> m_types;
  std::set<Entry> m_entries;
  unsigned long m_period;
  int m_epollfd;
};

void on_sample(void *privdata, Channel::Sample *sample) {
  std::cout << "Sample received: type = " << sample->type
            << ", CPU = " << sample->cpu << ", PID = " << sample->pid
            << ", TID = " << sample->tid << ", Address = " << std::hex
            << sample->address << std::dec << std::endl;
}

struct PageInfo {
  unsigned long address;
  unsigned int accessCount;
  time_t lastAccessTime;
  bool isHot;
};

const unsigned int HOT_ACCESS_THRESHOLD = 1;
const time_t CLASSIFICATION_PERIOD = 60;

void classifyPages(std::unordered_map<unsigned long, PageInfo> &pages) {
  for (auto &it : pages) {
    PageInfo &page = it.second;
    if (page.accessCount > HOT_ACCESS_THRESHOLD) {
      page.isHot = true;
    } else {
      page.isHot = false;
    }
    // Reset access count after classification
    page.accessCount = 0;
  }
}

int main(int argc, char *argv[]) {
  if (argc < 3) {
    std::cerr << "Usage: " << argv[0] << " <period> <pid1> <pid2> ..."
              << std::endl;
    return 1;
  }

  unsigned long period;
  if (sscanf(argv[1], "%lu", &period) != 1) {
    std::cerr << "Invalid period format." << std::endl;
    return 1;
  }

  std::set<pid_t> pids;
  for (int i = 2; i < argc; i++) {
    pid_t pid;
    if (sscanf(argv[i], "%d", &pid) != 1) {
      std::cerr << "Invalid PID format: " << argv[i] << std::endl;
      return 1;
    }
    pids.insert(pid);
  }

  ChannelSet cs;
  std::set<Channel::Type> types = {Channel::LLC_MISSES};
  if (cs.init(types) != 0) {
    std::cerr << "Failed to initialize ChannelSet." << std::endl;
    return 1;
  }

  if (cs.setPeriod(period) != 0) {
    std::cerr << "Failed to set period." << std::endl;
    return 1;
  }

  if (cs.pollSamples(0, nullptr, nullptr) < 0) {
    std::cerr << "Failed to update pids." << std::endl;
    return 1;
  }

  size_t total = 0;
  while (true) {
    ssize_t ret = cs.pollSamples(100, nullptr, on_sample);
    if (ret < 0) {
      std::cerr << "Error polling samples." << std::endl;
      return 1;
    }
    total += ret;
    std::cout << "Count: " << ret << ", Total: " << total << std::endl;
  }

  return 0;
}

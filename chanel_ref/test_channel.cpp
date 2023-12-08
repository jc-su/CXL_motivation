#include "channel.h"
#include <set>
#include <vector>
#include <unordered_map>
struct PageInfo {
    unsigned long address;
    unsigned int accessCount;
    time_t lastAccessTime;
    bool isHot;
};


const unsigned int HOT_ACCESS_THRESHOLD = 10;
const time_t CLASSIFICATION_PERIOD = 60;

void classifyPages(std::unordered_map<unsigned long, PageInfo>& pages) {
    for (auto& it : pages) {
        PageInfo& page = it.second;
        if (page.accessCount > HOT_ACCESS_THRESHOLD) {
            page.isHot = true;
        } else {
            page.isHot = false;
        }
        page.accessCount = 0;
    }
}

int main(int argc, char *argv[]) {
  unsigned long period;
  pid_t pid;
  if (argc != 3 || sscanf(argv[1], "%lu", &period) != 1 ||
      sscanf(argv[2], "%d", &pid) != 1) {
    printf("USAGE: %s <period> <pid>\n", argv[0]);
    return 1;
  }
  Channel c;
  int ret = c.bind(pid, Channel::CHANNEL_STORE);
  if (ret)
    return ret;
  ret = c.setPeriod(period);
  if (ret)
    return ret;
  while (true) {
    Channel::Sample sample;
    ret = c.readSample(&sample);
    if (ret == -EAGAIN) {
      usleep(10000);
      continue;
    } else if (ret < 0)
      return ret;
    else
      printf("type: %x, cpu: %u, pid: %u, tid: %u, address: %lx\n", sample.type,
             sample.cpu, sample.pid, sample.tid, sample.address);
  }
  return 0;
}
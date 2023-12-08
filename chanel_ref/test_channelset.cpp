#include "channelset.h"

void on_sample(void *privdata, Channel::Sample *sample) {
  printf("type: %x, cpu: %u, pid: %u, tid: %u, address: %lx\n", sample->type,
         sample->cpu, sample->pid, sample->tid, sample->address);
}

int main(int argc, char *argv[]) {
  unsigned long period;
  if (argc < 3 || sscanf(argv[1], "%lu", &period) != 1) {
  wrong_arguments:
    printf("USAGE: %s <period> <pid1> <pid2> ...\n", argv[0]);
    return 1;
  }
  std::set<pid_t> pids;
  for (int i = 2; i < argc; i++) {
    pid_t pid;
    if (sscanf(argv[i], "%d", &pid) != 1)
      goto wrong_arguments;
    pids.insert(pid);
  }
  ChannelSet cs;
  std::set<Channel::Type> types;
  types.insert(Channel::CHANNEL_LOAD);
  types.insert(Channel::CHANNEL_STORE);
  int ret = cs.init(types);
  if (ret)
    return ret;
  ret = cs.setPeriod(period);
  if (ret)
    return ret;
  ret = cs.update(pids);
  if (ret)
    return ret;
  size_t total = 0;
  while (true) {
    ssize_t ret = cs.pollSamples(1000, NULL, on_sample, NULL);
    if (ret < 0)
      return (int)ret;
    total += ret;
    printf("count: %ld, total: %lu\n", ret, total);
  }
  return 0;
}

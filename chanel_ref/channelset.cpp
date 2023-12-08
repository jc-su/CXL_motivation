#include "channelset.h"

#include <list>
#include <sys/epoll.h>

#define EPOLL_BATCH_SIZE 64

ChannelSet::ChannelSet() { m_epollfd = -1; }

ChannelSet::~ChannelSet() { deinit(); }

int ChannelSet::init(std::set<Channel::Type> &types) {
  if (m_epollfd >= 0)
    ERROR({}, -EINVAL, false, "this ChannelSet has been initialized already");
  if (types.size() == 0)
    ERROR({}, -EINVAL, false, "param <types> is empty");
  int fd = epoll_create(1);
  if (fd < 0) {
    int ret = -errno;
    ERROR({}, ret, true, "epoll_create(1) failed: ");
  }
  m_types.insert(m_types.begin(), types.begin(), types.end());
  m_period = 0;
  m_epollfd = fd;
  return 0;
}

void ChannelSet::deinit() {
  if (m_epollfd < 0)
    return;
  m_types.clear();
  for (auto it = m_entries.begin(); it != m_entries.end(); ++it)
    destroyChannels(it->channels);
  m_entries.clear();
  close(m_epollfd);
  m_epollfd = -1;
}

int ChannelSet::createChannels(Channel **pchannels, pid_t pid) {
  size_t count = m_types.size();
  auto *channels = new Channel[count];
  for (size_t i = 0; i < count; i++) {
    Channel *channel = channels + i;
    Channel::Type type = m_types[i];
    int ret = channel->bind(pid, type);
    if (ret < 0)
      ERROR(destroyChannels(channels, i), ret, false,
            "channels[%lu].bind(%d, %d) failed", i, pid, type);
    ret = channel->setPeriod(m_period);
    if (ret < 0)
      ERROR(destroyChannels(channels, i), ret, false,
            "channels[%lu].setPeriod(%lu) failed", i, m_period);
    // add channel to epoll
    struct epoll_event event;
    // if any sample available, EPOLLIN is sent, if process exits, EPOLLHUP is
    // sent.
    event.events = EPOLLIN | EPOLLHUP;
    // we can get Channal after epoll_wait()
    event.data.ptr = channel;
    int fd = channel->getPerfFd();
    ret = epoll_ctl(m_epollfd, EPOLL_CTL_ADD, fd, &event);
    if (ret)
      ERROR(destroyChannels(channels, i), ret, true,
            "epoll_ctl(%d, EPOLL_CTL_ADD, %d, &evt) failed: ", m_epollfd, fd);
  }
  (*pchannels) = channels;
  return 0;
}

void ChannelSet::destroyChannels(Channel *channels, ssize_t epoll_count) {
  // channels that added to epoll should be deleted
  size_t count = epoll_count >= 0 ? epoll_count : m_types.size();
  for (size_t i = 0; i < count; i++) {
    Channel *channel = channels + i;
    int ret = epoll_ctl(m_epollfd, EPOLL_CTL_DEL, channel->getPerfFd(), NULL);
    assert(ret == 0);
  }
  delete[] channels;
}

int ChannelSet::add(pid_t pid) {
  if (m_epollfd < 0)
    ERROR({}, -EINVAL, false, "this ChannelSet has not been initialized yet");
  Entry entry;
  entry.pid = pid;
  auto it = m_entries.find(entry);
  // already existed
  if (it != m_entries.end())
    return 0;
  int ret = createChannels(&(entry.channels), pid);
  if (ret < 0)
    ERROR({}, ret, false, "createChannels(&(entry.channels), %d) failed", pid);
  m_entries.insert(entry);
  return 0;
}

int ChannelSet::remove(pid_t pid) {
  if (m_epollfd < 0)
    ERROR({}, -EINVAL, false, "this ChannelSet has not been initialized yet");
  Entry entry;
  entry.pid = pid;
  auto it = m_entries.find(entry);
  // not existed
  if (it == m_entries.end())
    return 0;
  destroyChannels(it->channels);
  m_entries.erase(it);
  return 0;
}

int ChannelSet::update(std::set<pid_t> &pids) {
  if (m_epollfd < 0)
    ERROR({}, -EINVAL, false, "this ChannelSet has not been initialized yet");
  // now we diff the two sets
  auto pid_it = pids.begin(), pid_end = pids.end();
  auto entry_it = m_entries.begin(), entry_end = m_entries.end();
  // to_adds = pids - m_entries.pid, to_removes = m_entries.pid - pids
  std::list<pid_t> to_adds, to_removes;
  while (pid_it != pid_end && entry_it != entry_end) {
    if ((*pid_it) < entry_it->pid) {
      to_adds.push_back(*pid_it);
      ++pid_it;
    } else if ((*pid_it) > entry_it->pid) {
      to_removes.push_back(entry_it->pid);
      ++entry_it;
    } else {
      ++pid_it;
      ++entry_it;
    }
  }
  to_adds.insert(to_adds.end(), pid_it, pid_end);
  for (; entry_it != entry_end; ++entry_it)
    to_removes.push_back(entry_it->pid);
  // remove pids that not in <pids>
  for (auto it = to_removes.begin(); it != to_removes.end(); ++it) {
    Entry fake;
    fake.pid = (*it);
    auto found = m_entries.find(fake);
    assert(found != m_entries.end());
    destroyChannels(found->channels);
    m_entries.erase(found);
  }
  // add new pids in <pids>
  for (auto it = to_adds.begin(); it != to_adds.end(); ++it) {
    Entry entry;
    entry.pid = (*it);
    int ret = createChannels(&(entry.channels), entry.pid);
    if (ret < 0)
      ERROR({}, ret, false, "createChannels(&(entry.channels), %d) failed",
            entry.pid);
    m_entries.insert(entry);
  }
  return 0;
}

int ChannelSet::setPeriod(unsigned long period) {
  if (m_epollfd < 0)
    ERROR({}, -EINVAL, false, "this ChannelSet has not been initialized yet");
  size_t count = m_types.size();
  for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
    Channel *channels = it->channels;
    for (size_t i = 0; i < count; i++) {
      int ret = channels[i].setPeriod(period);
      if (ret < 0)
        ERROR({}, ret, false, "channels[%lu].setPeriod(%lu) failed", i, period);
    }
  }
  m_period = period;
  return 0;
}

ssize_t ChannelSet::pollSamples(int timeout, void *privdata,
                                void (*on_sample)(void *privdata,
                                                  Channel::Sample *sample),
                                void (*on_exit)(void *privdata, pid_t pid)) {
  if (m_epollfd < 0)
    ERROR({}, -EINVAL, false, "this ChannelSet has not been initialized yet");
  // poll available channels
  struct epoll_event events[EPOLL_BATCH_SIZE];
  int ret = epoll_wait(m_epollfd, events, EPOLL_BATCH_SIZE, timeout);
  if (ret < 0) {
    ret = -errno;
    ERROR({}, ret, true, "epoll_wait(%d, events, %d, %d) failed: ", m_epollfd,
          EPOLL_BATCH_SIZE, timeout);
  }
  // count of active channel
  int channel_count = ret;
  // count of available samples
  ssize_t sample_count = 0;
  // exited processes
  std::set<pid_t> exit_pids;
  // for each active channel
  for (int i = 0; i < channel_count; i++) {
    auto reason = events[i].events;
    auto *channel = (Channel *)events[i].data.ptr;
    // process exits
    if (reason & EPOLLHUP) {
      exit_pids.insert(channel->getPid());
      break;
    }
    // process has new samples
    assert(reason == EPOLLIN);
    // read all available samples
    while (true) {
      Channel::Sample sample;
      ret = channel->readSample(&sample);
      if (ret == -EAGAIN)
        break;
      if (ret < 0)
        ERROR({}, ret, false, "channel->readSample(&sample) failed");
      if (on_sample)
        on_sample(privdata, &sample);
      sample_count++;
    }
  }
  for (auto it = exit_pids.begin(); it != exit_pids.end(); ++it) {
    Entry entry;
    entry.pid = (*it);
    auto found = m_entries.find(entry);
    assert(found != m_entries.end());
    destroyChannels(found->channels);
    m_entries.erase(found);
    if (on_exit)
      on_exit(privdata, entry.pid);
  }
  return sample_count;
}
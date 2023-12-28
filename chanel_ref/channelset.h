#ifndef CHANNELSET_H
#define CHANNELSET_H

#include "common.h"
#include "channel.h"

#include <set>
#include <vector>

class ChannelSet
{
public:

    ChannelSet();

    ~ChannelSet();

    /* Initialize the ChannelSet.
     *      types: set of Channel::Type to sample
     * RETURN: 0 if ok, or a negative error code
     */
    int init(std::set<Channel::Type>& types);
    
    /* Uninitialize the ChannelSet.
     */
    void deinit();

    /* Add a process into the ChannelSet.
     *      pid: the pid of the new process
     * RETURN: 0 if ok (either newly added or alreadly existed), or a negative error code
     * NOTE: Channel(s) will automatically be created for this process
     */
    int add(pid_t pid);

    /* Remove a process out from the ChannelSet.
     *      pid: the pid of the process
     * RETURN: 0 if ok (either actually removed or never existed), or a negative error code
     * NOTE: Channel(s) will automatically be destroyed for this process
     */
    int remove(pid_t pid);

    /* Update the processes in this ChannelSet.
     *      pids: the set of processes
     * RETURN: 0 if ok, or a negative error code
     * NOTE: For any process that is in this ChannelSet but not in <pids> will be removed,
     *      while for any process that is not in this ChannelSet but in <pids> will be added.
     */
    int update(std::set<pid_t>& pids);

    /* Set the period of all Channels.
     *      period: the new period to sample
     * RETURN: 0 if ok, or a negative error code
     */
    int setPeriod(unsigned long period);

    /* Poll samples from Channels.
     *      timeout: the number of milliseconds to block.
     *          -1 causes to block indefinitely until any sample is available,
     *          while 0 causes to return immediately, even if no samples are available.
     *      privdata: the user-defined argument passed to <func>, see below
     *      on_sample: the callback function to handle with each sample
     *      on_exit: the callback function to handle with exited process
     * RETURN: the count of samples handled, or a negative error code
     * NOTE: Designing in callback style is to reduce the overhead of data copy.
     */
    ssize_t pollSamples(int timeout, void* privdata,
        void (*on_sample)(void* privdata, Channel::Sample* sample),
        void (*on_exit)(void* privdata, pid_t pid));

private:

    struct Entry
    {
        pid_t pid;              // pid of this process
        Channel* channels;      // Channels for this process

        bool operator <(const Entry& entry) const
        {
            return pid < entry.pid;
        }
    };

    int createChannels(Channel** pchannels, pid_t pid);

    void destroyChannels(Channel* channels, ssize_t epoll_count = -1);

private:
    std::vector<Channel::Type> m_types; // types to sample (of Channels for each process)
    std::set<Entry> m_entries;          // set of processes and its Channels
    unsigned long m_period;             // the sample_period of all Channels
    int m_epollfd;                      // the file descriptor from epoll_create()
};

#endif

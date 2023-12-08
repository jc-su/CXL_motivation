#ifndef CHANNEL_H
#define CHANNEL_H

#include "common.h"

#include <unistd.h>

class Channel {
public:
  enum Type {
    CHANNEL_LOAD = 0x01D3,  // sample load instructions
    CHANNEL_STORE = 0x01D3, // sample store instructions
  };

  struct Sample {
    Type type;
    uint32_t cpu; // on which cpu(core) this sample happens
    uint32_t pid; // in which process(pid) and thread(tid) this sample happens
    uint32_t tid;
    uint64_t address; // the virtual address in this process to be accessed
  };

  Channel();

  ~Channel();

  /* Initialize the Channel.
   *      pid:    the process to be sampled
   *      type:   type of instructions to be sampled
   * RETURN: 0 if OK, or a negative error code
   * NOTE: after calling bind(), the Channel remains disabled until setPeriod()
   * is called.
   */
  int bind(pid_t pid, Type type);

  /* De-initialize the Channel.
   * NOTE: after calling unbind(), the Channel go back to uninitialized.
   */
  void unbind();

  /* Set the sample period.
   * Sample period means that a sample is triggered every how many instructions.
   * For example, if period is set to be 10000, then a sample happens every
   * 10000 instructions. period: the period RETURN: 0 if OK, or a negative error
   * code NOTE: a zero period disables this Channel. And there is a minimal
   * threshold on it, <period> is invalid if less than the threshold. The
   * threshold varies between different hardwares.
   */
  int setPeriod(unsigned long period);

  /* Read a sample from this Channel.
   *      sample: the buffer to receive the sample
   * RETURN: 0 if OK, -EAGAIN if not available, or a negative error code
   */
  int readSample(Sample *sample);

  /* Get the pid of target process.
   * RETURN: pid, or a meaningless value if uninitialized.
   */
  pid_t getPid();

  /* Get the type to sample.
   * RETURN: type, or a meaningless value if uninitialized.
   */
  Type getType();

  /* Get the file descriptor from perf_event_open().
   * RETURN: the file descriptor, or -1 if uninitialized.
   * NOTE: Be careful with the fd, a wrong use of it will disturb the logic of
   * this Channel.
   */
  int getPerfFd();

private:
  pid_t m_pid;            // pid of target process
  Type m_type;            // type
  int m_fd;               // file descriptor from perf_event_open()
  uint64_t m_id;          // sample id of each record
  void *m_buffer;         // ring buffer and its header
  unsigned long m_period; // sample_period
};

#endif
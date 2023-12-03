#include <linux/perf_event.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <sys/ioctl.h>
#include <errno.h>


#define BUFFER_PAGES 8

struct perf_event_attr setup_perf_event_attr(long config) {
    struct perf_event_attr pe;
    memset(&pe, 0, sizeof(struct perf_event_attr));
    pe.type = PERF_TYPE_HARDWARE;
    pe.size = sizeof(struct perf_event_attr);
    pe.config = config;
    pe.sample_type = PERF_SAMPLE_ADDR;
    pe.sample_period = 1;
    pe.precise_ip = 2;
    pe.disabled = 1;
    pe.exclude_kernel = 1; 
    pe.exclude_hv = 1;
    pe.wakeup_events = 1;

    return pe;
}

int open_perf_event(struct perf_event_attr *pe, pid_t pid) {
    return syscall(__NR_perf_event_open, pe, pid, -1, -1, 0);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <pid>\n", argv[0]);
        return -1;
    }

    pid_t target_pid = (pid_t)atoi(argv[1]);

    struct perf_event_attr pe = setup_perf_event_attr(PERF_COUNT_HW_CACHE_MISSES);

    int fd = open_perf_event(&pe, target_pid);
    if (fd == -1) {
        fprintf(stderr, "Error opening perf event for PID %d: %s\n", target_pid, strerror(errno));
        return -1;
    }

    // Map the buffer
    void *buffer = mmap(NULL, BUFFER_PAGES * getpagesize(), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (buffer == MAP_FAILED) {
        fprintf(stderr, "Error mapping buffer: %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);


    ioctl(fd, PERF_EVENT_IOC_DISABLE, 0);

    munmap(buffer, BUFFER_PAGES * getpagesize());

    close(fd);
    return 0;
}

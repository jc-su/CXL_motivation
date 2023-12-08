#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>


#define PAGEMAP_LENGTH 8
#define PAGE_SIZE 4096

int read_pagemap(char *path, unsigned long offset, uint64_t *value) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror("Can't open pagemap");
        return -1;
    }

    if (lseek(fd, offset, SEEK_SET) == -1) {
        perror("Can't seek in pagemap");
        close(fd);
        return -1;
    }

    ssize_t bytes_read = read(fd, value, PAGEMAP_LENGTH);
    if (bytes_read < 0) {
        perror("Can't read pagemap");
        close(fd);
        return -1;
    }

    if (bytes_read != PAGEMAP_LENGTH) {
        fprintf(stderr, "Short read from pagemap: %zd bytes\n", bytes_read);
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
}

int clear_refs(char *path) {
    int fd = open(path, O_WRONLY);
    if (fd < 0) {
        perror("Can't open clear_refs");
        return -1;
    }

    if (write(fd, "1", 1) != 1) {
        perror("Can't write to clear_refs");
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <pid>\n", argv[0]);
        return 1;
    }

    pid_t pid = (pid_t)atoi(argv[1]);
    char pagemap_path[256], clear_refs_path[256];
    snprintf(pagemap_path, sizeof(pagemap_path), "/proc/%d/pagemap", pid);
    snprintf(clear_refs_path, sizeof(clear_refs_path), "/proc/%d/clear_refs", pid);

    // just example: 0x400000 - 0x4FF000
    unsigned long start_addr = 0x400000;
    unsigned long end_addr = 0x4FF000;

    while (1) {
        int accessed_pages = 0;

        for (unsigned long addr = start_addr; addr < end_addr; addr += PAGE_SIZE) {
            unsigned long offset = (addr / PAGE_SIZE) * PAGEMAP_LENGTH;
            uint64_t entry;

            if (read_pagemap(pagemap_path, offset, &entry) == -1) {
                continue;
            }
            if ((entry >> 5) & 1) {
                accessed_pages++;
            }
        }

        printf("Accessed pages: %d\n", accessed_pages);

        if (clear_refs(clear_refs_path) == -1) {
            break;
        }

        sleep(10);
    }

    return 0;
}

#ifndef COMMON_H
#define COMMON_H

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ERROR(cleanup, ret, show_errstr, msgs...)                              \
  ({                                                                           \
    const char *_errstr = (show_errstr) ? strerror(errno) : "";                \
    (cleanup);                                                                 \
    fprintf(stderr, "[<%s> @ %s: %d]: ", __FUNCTION__, __FILE__, __LINE__);    \
    fprintf(stderr, ##msgs);                                                   \
    fprintf(stderr, "%s\n", _errstr);                                          \
    return (ret);                                                              \
  })

#ifndef likely
#define likely(x) __builtin_expect(!!(x), 1)
#endif

#ifndef unlikely
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif

#ifndef MIN2
#define MIN2(a, b)                                                             \
  ({                                                                           \
    typeof(a) x = (a);                                                         \
    typeof(b) y = (b);                                                         \
    x < y ? x : y;                                                             \
  })
#endif

#ifndef MAX2
#define MAX2(a, b)                                                             \
  ({                                                                           \
    typeof(a) x = (a);                                                         \
    typeof(b) y = (b);                                                         \
    x > y ? x : y;                                                             \
  })
#endif

#endif
#pragma once

#include "kernel/types.h"

#define RUSAGE_SELF     0
#define RUSAGE_CHILDREN 1

/*
 * Minimal struct rusage for sys_getrusage().
 * Mirrors libc/include/sys/resource.h — layouts must stay identical.
 */
struct timeval_k {
    uint64_t tv_sec;
    uint64_t tv_usec;
};

struct rusage {
    struct timeval_k ru_utime;  // user CPU time
    struct timeval_k ru_stime;  // system CPU time (stubbed as 0)
};

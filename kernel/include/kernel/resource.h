#pragma once

#include "kernel/types.h"

#define RUSAGE_SELF     0
#define RUSAGE_CHILDREN 1

/* POSIX clock IDs — shared by kernel and libc (values must stay in sync). */
#define CLOCK_REALTIME           0
#define CLOCK_MONOTONIC          1
#define CLOCK_PROCESS_CPUTIME_ID 2
#define CLOCK_THREAD_CPUTIME_ID  3

/* clock_nanosleep flags */
#define TIMER_ABSTIME 1

typedef int clockid_t;

struct timespec {
    long tv_sec;
    long tv_nsec;
};

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

/* POSIX interval timer types */
#define ITIMER_REAL    0
#define ITIMER_VIRTUAL 1
#define ITIMER_PROF    2

struct itimerval_k {
    struct timeval_k it_interval;  // reload value (0 = one-shot)
    struct timeval_k it_value;     // time until next expiry
};

/* Per-process resource limit (soft = current enforcement; hard = ceiling). */
struct rlimit {
    uint64_t rlim_cur;   /* soft limit */
    uint64_t rlim_max;   /* hard limit (ceiling) */
};
#define RLIM_INFINITY   ((uint64_t)~0ULL)

/* Resource identifiers for getrlimit/setrlimit */
#define RLIMIT_NOFILE   0    /* max open file descriptors */
#define RLIMIT_NPROC    1    /* max child processes */
#define RLIMIT_STACK    2    /* max stack size in bytes */
#define RLIMIT_AS       3    /* max virtual address space */
#define RLIMIT_DATA     4    /* max data segment (heap) size */
#define RLIMIT_CORE     5    /* max core dump file size (0 = no dump) */
#define RLIM_NLIMITS    6    /* number of resource types */
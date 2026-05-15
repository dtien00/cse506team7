#ifndef _SYS_RESOURCE_H
#define _SYS_RESOURCE_H

#include "sys/time.h"

#define RUSAGE_SELF     0
#define RUSAGE_CHILDREN 1

/*
 * Minimal struct rusage for getrusage(2).
 * ru_stime is always zero — kernel time is not tracked.
 */
struct rusage {
    struct timeval ru_utime;  // user CPU time consumed
    struct timeval ru_stime;  // system CPU time (always 0)
};

typedef uint64_t rlim_t;

/* Per-process resource limit (soft = current enforcement; hard = ceiling). */
struct rlimit {
    rlim_t rlim_cur;   /* soft limit */
    rlim_t rlim_max;   /* hard limit (ceiling) */
};
#define RLIM_INFINITY   ((rlim_t)~0ULL)
#define RLIM_SAVED_MAX  ((rlim_t)~0ULL)
#define RLIM_SAVED_CUR  ((rlim_t)~0ULL)


/* Resource identifiers for getrlimit/setrlimit */
#define RLIMIT_NOFILE   0    /* max open file descriptors */
#define RLIMIT_NPROC    1    /* max child processes */
#define RLIMIT_STACK    2    /* max stack size in bytes */
#define RLIMIT_AS       3    /* max virtual address space */
#define RLIMIT_DATA     4    /* max data segment (heap) size */
#define RLIMIT_CORE     5    /* max core dump file size (0 = no dump) */
#define RLIMIT_FSIZE    6    /* max file size in bytes */
#define RLIM_NLIMITS    7    /* number of resource types */

int getrusage(int who, struct rusage *usage);
int getrlimit(int resource, struct rlimit *rlim);
int setrlimit(int resource, const struct rlimit *rlim);


#endif /* _SYS_RESOURCE_H */

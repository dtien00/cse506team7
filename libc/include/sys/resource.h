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

int getrusage(int who, struct rusage *usage);

#endif /* _SYS_RESOURCE_H */

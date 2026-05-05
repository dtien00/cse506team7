#ifndef _SCHED_H
#define _SCHED_H

#include "sys/types.h"
#include "time.h"

/* Scheduling policy constants (POSIX.1-2024 §<sched.h>, [PS|TPS]) */
#define SCHED_OTHER  0   /* default MLFQ scheduler */
#define SCHED_FIFO   1   /* first-in, first-out (not implemented in SBUnix) */
#define SCHED_RR     2   /* round-robin with time quanta (not implemented) */

struct sched_param {
    int sched_priority;
};

int sched_get_priority_max(int policy);
int sched_get_priority_min(int policy);
int sched_getparam(pid_t pid, struct sched_param *param);
int sched_getscheduler(pid_t pid);
int sched_rr_get_interval(pid_t pid, struct timespec *interval);
int sched_setparam(pid_t pid, const struct sched_param *param);
int sched_setscheduler(pid_t pid, int policy, const struct sched_param *param);
int sched_yield(void);

#endif /* _SCHED_H */

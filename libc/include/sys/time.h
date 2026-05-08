#ifndef _SYS_TIME_H
#define _SYS_TIME_H

#include "sys/types.h"

struct timeval {
    time_t      tv_sec;
    suseconds_t tv_usec;
};

struct timezone {
    int tz_minuteswest;
    int tz_dsttime;
};

struct itimerval {
    struct timeval it_interval;
    struct timeval it_value;
};

#define ITIMER_REAL    0
#define ITIMER_VIRTUAL 1
#define ITIMER_PROF    2

int gettimeofday(struct timeval *restrict tv, void *restrict tz);
int settimeofday(const struct timeval *tv, const struct timezone *tz);  /* non-POSIX */
int getitimer(int which, struct itimerval* curr_value);
int setitimer(int which, const struct itimerval *restrict new_value, struct itimerval *restrict old_value);

#endif

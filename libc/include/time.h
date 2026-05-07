#ifndef _TIME_H
#define _TIME_H

#include "sys/time.h"

struct tm {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
    long tm_gmtoff;
    const char *tm_zone;
};

struct timespec {
    time_t tv_sec;
    long   tv_nsec;
};

struct itimerspec {
    struct timespec it_interval;
    struct timespec it_value;
};

#define CLOCKS_PER_SEC          ((clock_t)1000000)
#define TIME_UTC                1

#define CLOCK_REALTIME           0
#define CLOCK_MONOTONIC          1
#define CLOCK_PROCESS_CPUTIME_ID 2
#define CLOCK_THREAD_CPUTIME_ID  3

#define TIMER_ABSTIME           1

/* locale_t is opaque per POSIX; <locale.h> is its canonical home */
typedef int locale_t;

/* struct sigevent is defined in <signal.h>; forward-declared for timer_create */
struct sigevent;

clock_t    clock(void);
int        clock_getcpuclockid(pid_t, clockid_t *);
int        clock_getres(clockid_t, struct timespec *);
int        clock_gettime(clockid_t, struct timespec *);
int        clock_nanosleep(clockid_t, int, const struct timespec *,
               struct timespec *);
int        clock_settime(clockid_t, const struct timespec *);
double     difftime(time_t, time_t);
struct tm *gmtime(const time_t *);
struct tm *gmtime_r(const time_t *restrict, struct tm *restrict);
struct tm *localtime(const time_t *);
struct tm *localtime_r(const time_t *restrict, struct tm *restrict);
time_t     mktime(struct tm *);
int        nanosleep(const struct timespec *, struct timespec *);
size_t     strftime(char *restrict, size_t, const char *restrict,
               const struct tm *restrict);
size_t     strftime_l(char *restrict, size_t, const char *restrict,
               const struct tm *restrict, locale_t);
time_t     time(time_t *);
int        timer_create(clockid_t, struct sigevent *restrict,
               timer_t *restrict);
int        timer_delete(timer_t);
int        timer_getoverrun(timer_t);
int        timer_gettime(timer_t, struct itimerspec *);
int        timer_settime(timer_t, int, const struct itimerspec *restrict,
               struct itimerspec *restrict);
int        timespec_get(struct timespec *, int);
void       tzset(void);

extern char *tzname[];

/* OB (Obsolescent in POSIX.1-2024) — retained for build compatibility */
char *asctime(const struct tm *);
char *ctime(const time_t *);

#endif

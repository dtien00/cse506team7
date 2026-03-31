#ifndef _TIME_H
#define _TIME_H

#include "types.h"
#include "sys/time.h"

typedef long time_t;
typedef long clock_t;

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
};

struct timespec {
    time_t tv_sec;
    long   tv_nsec;
};

#define CLOCK_REALTIME  0
#define CLOCK_MONOTONIC 1

time_t time(time_t *tloc);
// int gettimeofday(struct timeval* tv, struct timezone* tz);
int clock_gettime(int clk_id, struct timespec *tp);
struct tm* localtime(const time_t *timep);
struct tm* gmtime(const time_t *timep);
time_t mktime(struct tm *tm);
char* asctime(const struct tm *tm);
char* ctime(const time_t *timep);
size_t strftime(char *s, size_t max, const char *format, const struct tm *tm);

int nanosleep(const struct timespec *req, struct timespec *rem);

#endif

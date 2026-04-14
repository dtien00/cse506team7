#include "time.h"
#include "stddef.h"
#include "unistd.h"

time_t time(time_t *tloc) {
    struct timeval tv;
    if (gettimeofday(&tv, NULL) != 0) {
        if (tloc) *tloc = (time_t)-1;
        return (time_t)-1;
    }
    if (tloc) *tloc = (time_t)tv.tv_sec;
    return (time_t)tv.tv_sec;
}

int clock_gettime(int clk_id, struct timespec *tp) {
    (void)clk_id;   /* treat MONOTONIC same as REALTIME — kernel has one clock */
    if (!tp) return -1;
    struct timeval tv;
    if (gettimeofday(&tv, NULL) != 0) return -1;
    tp->tv_sec  = tv.tv_sec;
    tp->tv_nsec = tv.tv_usec * 1000L;
    return 0;
}

static struct tm static_tm;

struct tm* localtime(const time_t *timep) {
    (void)timep;
    static_tm.tm_sec = 0;
    static_tm.tm_min = 0;
    static_tm.tm_hour = 0;
    static_tm.tm_mday = 1;
    static_tm.tm_mon = 0;
    static_tm.tm_year = 70;
    static_tm.tm_wday = 4;
    static_tm.tm_yday = 0;
    static_tm.tm_isdst = 0;
    return &static_tm;
}

struct tm* gmtime(const time_t *timep) {
    return localtime(timep);
}

time_t mktime(struct tm *tm) {
    (void)tm;
    return 0;
}

static char asctime_buf[26];

char* asctime(const struct tm *tm) {
    (void)tm;
    asctime_buf[0] = '\0';
    return asctime_buf;
}

char* ctime(const time_t *timep) {
    return asctime(localtime(timep));
}

size_t strftime(char *s, size_t max, const char *format, const struct tm *tm) {
    (void)format;
    (void)tm;
    if (s && max > 0) {
        s[0] = '\0';
    }
    return 0;
}

int nanosleep(const struct timespec *req, struct timespec *rem) {
    if (!req) return -1;
    if (req->tv_sec > 0) sleep((unsigned int)req->tv_sec);
    /* sub-second nsec not supported; treat as satisfied */
    if (rem) { rem->tv_sec = 0; rem->tv_nsec = 0; }
    return 0;
}

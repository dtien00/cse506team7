#include "sys/time.h"
#include "stddef.h"

int gettimeofday(struct timeval* tv, struct timezone* tz) {
    if (tv) {
        tv->tv_sec = 0;
        tv->tv_usec = 0;
    }
    if (tz) {
        tz->tz_minuteswest = 0;
        tz->tz_dsttime = 0;
    }
    return 0;
}

int settimeofday(const struct timeval* tv, const struct timezone* tz) {
    (void)tv;
    (void)tz;
    return -1;
}

int getitimer(int which, struct itimerval* curr_value) {
    (void)which;
    if (curr_value) {
        curr_value->it_interval.tv_sec = 0;
        curr_value->it_interval.tv_usec = 0;
        curr_value->it_value.tv_sec = 0;
        curr_value->it_value.tv_usec = 0;
    }
    return 0;
}

int setitimer(int which, const struct itimerval* new_value, struct itimerval* old_value) {
    (void)which;
    (void)new_value;
    if (old_value) {
        old_value->it_interval.tv_sec = 0;
        old_value->it_interval.tv_usec = 0;
        old_value->it_value.tv_sec = 0;
        old_value->it_value.tv_usec = 0;
    }
    return -1;
}

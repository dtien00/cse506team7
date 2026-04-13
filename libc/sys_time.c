#include "sys/time.h"
#include "stddef.h"

// gettimeofday() is implemented as a syscall wrapper in syscall.S (SYS_gettimeofday = 34)

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

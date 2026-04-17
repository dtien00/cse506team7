#include "sys/time.h"
#include "stddef.h"

// gettimeofday() is implemented as a syscall wrapper in syscall.S (SYS_gettimeofday = 34)

int settimeofday(const struct timeval* tv, const struct timezone* tz) {
    (void)tv;
    (void)tz;
    return -1;
}

// getitimer() and setitimer() are implemented as syscall wrappers in syscall.S
// (SYS_getitimer = 94, SYS_setitimer = 95)

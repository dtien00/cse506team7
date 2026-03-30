#include "unistd.h"

#define SYS_sleep 11

unsigned int sleep(unsigned int seconds) {
    register long a0 asm("a0") = seconds;
    register long a7 asm("a7") = SYS_sleep;
    register long ret asm("a0");
    asm volatile("ecall" : "=r"(ret) : "r"(a0), "r"(a7) : "memory");
    return (unsigned int)ret;
}

int usleep(unsigned int usec) {
    unsigned int seconds = usec / 1000000;
    if (seconds > 0) {
        sleep(seconds);
    }
    return 0;
}

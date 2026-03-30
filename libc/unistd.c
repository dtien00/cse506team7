#include "unistd.h"

#define SYS_exit  1
#define SYS_sleep 11

void _exit(int status) {
    register long a0 asm("a0") = status;
    register long a7 asm("a7") = SYS_exit;
    asm volatile("ecall" :: "r"(a0), "r"(a7) : "memory");
    while (1);
}

unsigned int sleep(unsigned int seconds) {
    register long a0 asm("a0") = seconds;
    register long a7 asm("a7") = SYS_sleep;
    asm volatile("ecall" :: "r"(a0), "r"(a7) : "memory");
    return 0;
}

int usleep(unsigned int usec) {
    /* No sub-second sleep support in kernel; treat as sleep(0) */
    (void)usec;
    return 0;
}

pid_t getppid(void) {
    return -1;
}

int isatty(int fd) {
    return (fd == 0 || fd == 1 || fd == 2) ? 1 : 0;
}

#include "unistd.h"
#include "sys/stat.h"
#include "fcntl.h"
#include "errno.h"
#include "string.h"
#include "time.h"
#include "termios.h"
#include "sys/ioctl.h"

#define SYS_exit  1
#define SYS_sleep 11
#define SYS_uptime 12
#define SYS_access 103

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

int uptime(void) {
    register long a7 asm("a7") = SYS_uptime;
    register long a0 asm("a0") = 0;
    asm volatile("ecall" : "+r"(a0) : "r"(a7) : "memory");
    return (int)a0;
}

int usleep(unsigned int usec) {
    struct timespec ts;
    ts.tv_sec  = (long)(usec / 1000000U);
    ts.tv_nsec = (long)(usec % 1000000U) * 1000L;
    return nanosleep(&ts, (struct timespec *)0);
}

/* getppid() is implemented as an assembly syscall wrapper in syscall.S */

int isatty(int fd) {
    struct termios t;
    if (tcgetattr(fd, &t) == 0)
        return 1;
    /* errno is already set by tcgetattr (ENOTTY or EBADF) */
    return 0;
}

int access(const char *pathname, int mode) {
    register long a0 asm("a0") = (long)pathname;
    register long a1 asm("a1") = (long)mode;
    register long a7 asm("a7") = SYS_access;
    asm volatile("ecall" : "+r"(a0) : "r"(a1), "r"(a7) : "memory");
    if (a0 < 0) { errno = (int)-a0; return -1; }
    return (int)a0;
}

/* getpgid / setpgid / getpgrp / setsid are real syscall wrappers in syscall.S.
 * setpgrp() is a BSD convenience equivalent to setpgid(0, 0). */
int setpgrp(void) {
    return setpgid(0, 0);
}



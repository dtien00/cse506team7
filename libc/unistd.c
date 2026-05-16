#include "unistd.h"
#include "sys/stat.h"
#include "fcntl.h"
#include "errno.h"
#include "string.h"
#include "stdlib.h"
#include "time.h"
#include "termios.h"
#include "sys/ioctl.h"
#include "limits.h"

#define SYS_exit  1
#define SYS_sleep 11
#define SYS_uptime 12

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

// Raw kernel wrapper — requires a non-NULL buffer.
char *__getcwd_raw(char *buf, size_t size);

// getcwd: POSIX + glibc extension (buf=NULL → allocate).
char *getcwd(char *buf, size_t size) {
    if (buf != NULL)
        return __getcwd_raw(buf, size);

    // buf=NULL: allocate a PATH_MAX buffer, fill it, shrink to fit.
    size_t alloc = (size != 0) ? size : (size_t)PATH_MAX;
    char *tmp = malloc(alloc);
    if (!tmp) { errno = ENOMEM; return NULL; }
    char *result = __getcwd_raw(tmp, alloc);
    if (!result) { free(tmp); return NULL; }
    size_t len = strlen(result) + 1;
    char *shrunk = realloc(tmp, len);
    return shrunk ? shrunk : tmp;
}

/* getpgid / setpgid / getpgrp / setsid are real syscall wrappers in syscall.S.
 * setpgrp() is a BSD convenience equivalent to setpgid(0, 0). */
int setpgrp(void) {
    return setpgid(0, 0);
}



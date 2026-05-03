#include "fcntl.h"
#include "stdarg.h"
#include "errno.h"
#include "sys/syscall.h"

int open(const char *pathname, int flags, ...);

int creat(const char *pathname, mode_t mode) {
    return open(pathname, O_CREAT | O_WRONLY | O_TRUNC, mode);
}

int fcntl(int fd, int cmd, ...) {
    long arg = 0;
    if (cmd == F_DUPFD || cmd == F_SETFD || cmd == F_SETFL ||
        cmd == F_SETOWN) {
        va_list ap;
        va_start(ap, cmd);
        arg = va_arg(ap, int);
        va_end(ap);
    }

    register long a0 asm("a0") = fd;
    register long a1 asm("a1") = cmd;
    register long a2 asm("a2") = arg;
    register long a7 asm("a7") = SYS_fcntl;
    asm volatile("ecall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a7) : "memory");

    if (a0 < 0) {
        errno = (int)-a0;
        return -1;
    }
    return (int)a0;
}

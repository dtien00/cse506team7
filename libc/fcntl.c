#include "fcntl.h"
#include "stdarg.h"
#include "errno.h"

int open(const char *pathname, int flags, ...);
long __syscall_fcntl(int fd, int cmd, long arg);

int creat(const char *pathname, mode_t mode) {
    return open(pathname, O_CREAT | O_WRONLY | O_TRUNC, mode);
}

int fcntl(int fd, int cmd, ...) {
    va_list ap;
    va_start(ap, cmd);
    long arg = va_arg(ap, long);
    va_end(ap);
    return (int)__syscall_fcntl(fd, cmd, arg);
}

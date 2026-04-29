#include "fcntl.h"
#include "stdarg.h"
#include "errno.h"

int open(const char *pathname, int flags, ...);

int creat(const char *pathname, mode_t mode) {
    return open(pathname, O_CREAT | O_WRONLY | O_TRUNC, mode);
}

int fcntl(int fd, int cmd, ...) {
    (void)fd;
    (void)cmd;
    errno = ENOSYS;
    return -1;
}

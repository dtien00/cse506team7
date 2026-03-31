#include "fcntl.h"
#include "stdarg.h"

int open(const char *pathname, int flags, ...);

int create(const char *pathname, mode_t mode) {
    return open(pathname, O_CREAT | O_WRONLY | O_TRUNC, mode);
}

int fcntl(int fd, int cmd, ...) {
    (void)fd;
    (void)cmd;
    return -1;
}

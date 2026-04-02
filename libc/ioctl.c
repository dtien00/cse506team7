#include "sys/ioctl.h"
#include "stdarg.h"

int ioctl(int fd, unsigned long request, ...) {
    (void)fd;
    (void)request;
    return -1;
}

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
    (void)pathname; (void)mode;
    errno = ENOSYS;
    return -1;
}

/* getpgid / setpgid / getpgrp / setsid are real syscall wrappers in syscall.S.
 * setpgrp() is a BSD convenience equivalent to setpgid(0, 0). */
int setpgrp(void) {
    return setpgid(0, 0);
}


int rename(const char *oldpath, const char *newpath) {
    if (!oldpath || !newpath) {
        errno = EINVAL;
        return -1;
    }

    if (strcmp(oldpath, newpath) == 0)
        return 0;

    int oldfd = open(oldpath, O_RDONLY);
    if (oldfd < 0) {
        return -1;
    }

    int newfd = open(newpath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (newfd < 0) {
        int err = errno;
        close(oldfd);
        errno = err;
        return -1;
    }

    char buf[4096];
    ssize_t nread;
    while ((nread = read(oldfd, buf, sizeof(buf))) > 0) {
        char *ptr = buf;
        ssize_t nwritten;
        while (nread > 0) {
            nwritten = write(newfd, ptr, (size_t)nread);
            if (nwritten <= 0) {
                int err = (nwritten < 0) ? errno : EIO;
                close(oldfd);
                close(newfd);
                unlink(newpath);
                errno = err;
                return -1;
            }
            ptr += nwritten;
            nread -= nwritten;
        }
    }

    if (nread < 0) {
        int err = errno;
        close(oldfd);
        close(newfd);
        unlink(newpath);
        errno = err;
        return -1;
    }

    int rc = close(oldfd);
    if (rc < 0) {
        int err = errno;
        close(newfd);
        unlink(newpath);
        errno = err;
        return -1;
    }

    rc = close(newfd);
    if (rc < 0) {
        int err = errno;
        unlink(newpath);
        errno = err;
        return -1;
    }

    rc = unlink(oldpath);
    if (rc < 0) {
        int err = errno;
        errno = err;
        return -1;
    }

    return 0;
}

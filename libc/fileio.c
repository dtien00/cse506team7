#include "unistd.h"
#include "fcntl.h"
#include "stdarg.h"

#define SYS_read     3
#define SYS_write    4
#define SYS_open     5
#define SYS_close    6
#define SYS_lseek    17
#define SYS_unlink   18
#define SYS_mkdir    19
#define SYS_sync     20

ssize_t read(int fd, void *buf, size_t count) {
    register long a0 asm("a0") = fd;
    register long a1 asm("a1") = (long)buf;
    register long a2 asm("a2") = count;
    register long a7 asm("a7") = SYS_read;
    register long ret asm("a0");
    asm volatile("ecall" : "=r"(ret) : "r"(a0), "r"(a1), "r"(a2), "r"(a7) : "memory");
    return (ssize_t)ret;
}

ssize_t write(int fd, const void *buf, size_t count) {
    register long a0 asm("a0") = fd;
    register long a1 asm("a1") = (long)buf;
    register long a2 asm("a2") = count;
    register long a7 asm("a7") = SYS_write;
    register long ret asm("a0");
    asm volatile("ecall" : "=r"(ret) : "r"(a0), "r"(a1), "r"(a2), "r"(a7) : "memory");
    return (ssize_t)ret;
}

int open(const char *pathname, int flags, ...) {
    register long a0 asm("a0") = (long)pathname;
    register long a1 asm("a1") = flags;
    register long a7 asm("a7") = SYS_open;
    register long ret asm("a0");
    asm volatile("ecall" : "=r"(ret) : "r"(a0), "r"(a1), "r"(a7) : "memory");
    return (int)ret;
}

int creat(const char *pathname, mode_t mode) {
    return open(pathname, O_CREAT | O_WRONLY | O_TRUNC, mode);
}

int close(int fd) {
    register long a0 asm("a0") = fd;
    register long a7 asm("a7") = SYS_close;
    register long ret asm("a0");
    asm volatile("ecall" : "=r"(ret) : "r"(a0), "r"(a7) : "memory");
    return (int)ret;
}

off_t lseek(int fd, off_t offset, int whence) {
    register long a0 asm("a0") = fd;
    register long a1 asm("a1") = offset;
    register long a2 asm("a2") = whence;
    register long a7 asm("a7") = SYS_lseek;
    register long ret asm("a0");
    asm volatile("ecall" : "=r"(ret) : "r"(a0), "r"(a1), "r"(a2), "r"(a7) : "memory");
    return (off_t)ret;
}

int pipe(int pipefd[2]) {
    (void)pipefd;
    return -1;
}

int dup(int oldfd) {
    (void)oldfd;
    return -1;
}

int dup2(int oldfd, int newfd) {
    (void)oldfd;
    (void)newfd;
    return -1;
}

int isatty(int fd) {
    (void)fd;
    return 1;
}

int fcntl(int fd, int cmd, ...) {
    (void)fd;
    (void)cmd;
    return -1;
}

int unlink(const char *pathname) {
    register long a0 asm("a0") = (long)pathname;
    register long a7 asm("a7") = SYS_unlink;
    register long ret asm("a0");
    asm volatile("ecall" : "=r"(ret) : "r"(a0), "r"(a7) : "memory");
    return (int)ret;
}

int mkdir(const char *pathname, mode_t mode) {
    register long a0 asm("a0") = (long)pathname;
    register long a1 asm("a1") = mode;
    register long a7 asm("a7") = SYS_mkdir;
    register long ret asm("a0");
    asm volatile("ecall" : "=r"(ret) : "r"(a0), "r"(a1), "r"(a7) : "memory");
    return (int)ret;
}

void sync(void) {
    register long a7 asm("a7") = SYS_sync;
    register long ret asm("a0");
    asm volatile("ecall" : "=r"(ret) : "r"(a7) : "memory");
}

#include "unistd.h"
#include "sys/stat.h"

#define SYS_exit   1
#define SYS_sleep  11
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
    /* No sub-second sleep support in kernel; treat as sleep(0) */
    (void)usec;
    return 0;
}

/* getppid() is implemented as an assembly syscall wrapper in syscall.S */

int isatty(int fd) {
    return (fd == 0 || fd == 1 || fd == 2) ? 1 : 0;
}

int access(const char *pathname, int mode) {
    (void)pathname; (void)mode;
    return -1;
}

pid_t getpgid(pid_t pid) {
    (void)pid;
    return -1;
}

int setpgid(pid_t pid, pid_t pgid) {
    (void)pid; (void)pgid;
    return -1;
}

pid_t getpgrp(void) {
    return -1;
}

int setpgrp(void) {
    return -1;
}

int lstat(const char *pathname, struct stat *statbuf) {
    (void)pathname; (void)statbuf;
    return -1;
}

#include "unistd.h"
#include "sys/wait.h"

#define SYS_exit     1
#define SYS_fork     2
#define SYS_read     3
#define SYS_write    4
#define SYS_open     5
#define SYS_close    6
#define SYS_wait     7
#define SYS_exec     8
#define SYS_getpid   9
#define SYS_sbrk     10
#define SYS_sleep    11
#define SYS_uptime   12
#define SYS_kill     13

pid_t fork(void) {
    register long a7 asm("a7") = SYS_fork;
    register long ret asm("a0");
    asm volatile("ecall" : "=r"(ret) : "r"(a7) : "memory");
    return (pid_t)ret;
}

int execve(const char *pathname, char *const argv[], char *const envp[]) {
    (void)envp;
    register long a0 asm("a0") = (long)pathname;
    register long a1 asm("a1") = (long)argv;
    register long a7 asm("a7") = SYS_exec;
    register long ret asm("a0");
    asm volatile("ecall" : "=r"(ret) : "r"(a0), "r"(a1), "r"(a7) : "memory");
    return (int)ret;
}

pid_t getpid(void) {
    register long a7 asm("a7") = SYS_getpid;
    register long ret asm("a0");
    asm volatile("ecall" : "=r"(ret) : "r"(a7) : "memory");
    return (pid_t)ret;
}

pid_t getppid(void) {
    return 0;
}

uid_t getuid(void) {
    return 0;
}

gid_t getgid(void) {
    return 0;
}

pid_t wait(int *wstatus) {
    register long a0 asm("a0") = (long)wstatus;
    register long a7 asm("a7") = SYS_wait;
    register long ret asm("a0");
    asm volatile("ecall" : "=r"(ret) : "r"(a0), "r"(a7) : "memory");
    return (pid_t)ret;
}

pid_t waitpid(pid_t pid, int *wstatus, int options) {
    (void)pid;
    (void)options;
    return wait(wstatus);
}

void _exit(int status) {
    register long a0 asm("a0") = status;
    register long a7 asm("a7") = SYS_exit;
    asm volatile("ecall" :: "r"(a0), "r"(a7) : "memory");
    while (1);
}

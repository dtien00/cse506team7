#pragma once

#include "kernel/types.h"
#include "kernel/resource.h"
#include "trap/trap.h"
#include "process/process.h"

// System call numbers
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
#define SYS_mmap     14
#define SYS_munmap   15
#define SYS_brk      16
#define SYS_lseek    17
#define SYS_unlink   18
#define SYS_mkdir    19
#define SYS_sync     20
#define SYS_getppid  21
#define SYS_pipe     22
#define SYS_dup      23
#define SYS_getuid   24
#define SYS_waitpid  25
#define SYS_sigprocmask 26
#define SYS_getcwd   27
#define SYS_getrusage 28
#define SYS_nice      29
#define SYS_procinfo  30
#define SYS_yield     31
#define SYS_pause     32
#define SYS_dup2     33
#define SYS_getgid   47
#define SYS_chdir    49
#define SYS_fs_getdents  61
#define SYS_stat     79
#define SYS_fstat    80
#define SYS_link     86
#define SYS_symlink  88
#define SYS_readlink 89
#define SYS_chmod    90
#define SYS_fchmod   91

// Initialize syscall subsystem
void syscall_init(void);

// Handle system call (called from trap handler)
void syscall_handler(trap_frame *tf);

// Individual system call implementations
void sys_exit(int status);
pid_t sys_fork(void);
ssize_t sys_read(int fd, void *buf, size_t count);
ssize_t sys_write(int fd, const void *buf, size_t count);
int sys_open(const char *path, int flags);
int sys_close(int fd);
pid_t sys_wait(int *status);
int sys_exec(const char *path, char **argv);
pid_t sys_getpid(void);
pid_t sys_getppid(void);
void *sys_sbrk(intptr_t increment);
int sys_sleep(int seconds);
int sys_uptime(void);
void *sys_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
int sys_munmap(void *addr, size_t length);
int sys_brk(void *addr);
off_t sys_lseek(int fd, off_t offset, int whence);
int sys_unlink(const char *path);
int sys_mkdir(const char *path, mode_t mode);
int sys_sync(void);
int sys_pipe(int pipefd[2]);
int sys_dup(int oldfd);
int sys_dup2(int oldfd, int newfd);
int sys_kill(pid_t pid, int sig);
pid_t sys_waitpid(pid_t pid, int *status, int options);
int sys_sigprocmask(int how, const uint32_t *set, uint32_t *oldset);
int    sys_chdir(const char *path);
char  *sys_getcwd(char *buf, size_t size);
int sys_fs_getdents(int fd, void *dirp, uint32_t count);
int    sys_getrusage(int who, struct rusage *usage);
int    sys_nice(int increment);
int    sys_procinfo(struct proc_info *infos, int max_count);
int    sys_yield(void);
int    sys_pause(void);

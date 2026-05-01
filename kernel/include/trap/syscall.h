#pragma once

#include "kernel/types.h"
#include "kernel/resource.h"
#include "trap/trap.h"
#include "fs/file.h"
#include "process/process.h"
#include "signal.h"
#include "tty/term.h"

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
#define SYS_getuid   174  /* Linux RISC-V ABI; matches libc/syscall.S "li a7, 174" */
#define SYS_waitpid  25
#define SYS_sigprocmask 26
#define SYS_getcwd   27
#define SYS_getrusage 28
#define SYS_nice      29
#define SYS_procinfo  30
#define SYS_yield     31
#define SYS_pause     32
#define SYS_dup2          33
#define SYS_gettimeofday  34
#define SYS_tcgetattr 35
#define SYS_tcsetattr 36
#define SYS_alarm    37
#define SYS_sigaction 38
#define SYS_sigpending 39
#define SYS_sigsuspend 40
#define SYS_tcsetpgrp  41
#define SYS_tcgetpgrp  42
#define SYS_getgid   47
#define SYS_chdir    49
#define SYS_getpgrp  50
#define SYS_setpgid  51
#define SYS_setsid   52
#define SYS_getpgid  53
#define SYS_rmdir    54    /* was 52; moved to avoid conflict with SYS_setsid */
#define SYS_fs_getdents  61
#define SYS_stat     79
#define SYS_fstat    80
#define SYS_fsync    82    /* Linux RISC-V ABI */
#define SYS_fdatasync 83   /* Linux RISC-V ABI */
#define SYS_lstat    107
#define SYS_link     86
#define SYS_symlink  88
#define SYS_readlink 89
#define SYS_chmod    90
#define SYS_fchmod   91
#define SYS_nanosleep       92
#define SYS_clock_nanosleep 93
#define SYS_getitimer       94
#define SYS_setitimer       95
#define SYS_sysconf   96   /* long sysconf(int name) */
#define SYS_getrlimit   97   /* int getrlimit(int resource, struct rlimit *rlim) */
#define SYS_setrlimit   98   /* int setrlimit(int resource, const struct rlimit *rlim) */

// Initialize syscall subsystem
void syscall_init(void);

// Handle system call (called from trap handler)
void syscall_handler(trap_frame *tf);

// Individual system call implementations
void sys_exit(int status);
pid_t sys_fork(void);
ssize_t sys_read(int fd, void *buf, size_t count);
ssize_t sys_write(int fd, const void *buf, size_t count);
int sys_open(const char *path, int flags, mode_t mode);
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
int sys_tcgetattr(int fd, struct termios *termios_p);
int sys_tcsetattr(int fd, int action, const struct termios *termios_p);
int sys_kill(pid_t pid, int sig);
pid_t sys_waitpid(pid_t pid, int *status, int options);
int sys_sigprocmask(int how, const uint32_t *set, uint32_t *oldset);
int    sys_chdir(const char *path);
char  *sys_getcwd(char *buf, size_t size);
int sys_fs_getdents(int fd, void *dirp, uint32_t count);
int sys_stat(const char *upath, struct stat *ustat);
int sys_fstat(int fd, struct stat *ustat);
int sys_lstat(const char *upath, struct stat *ustat);
int sys_chmod(const char *upath, mode_t mode);
int sys_fchmod(int fd, mode_t mode);
int sys_rmdir(const char *path);
int sys_link(const char *oldpath, const char *newpath);
int sys_symlink(const char *target, const char *linkpath);
ssize_t sys_readlink(const char *path, char *buf, size_t bufsiz);
int    sys_getrusage(int who, struct rusage *usage);
int    sys_gettimeofday(struct timeval_k *tv, void *tz);
int    sys_nice(int increment);
int    sys_procinfo(struct proc_info *infos, int max_count);
int    sys_yield(void);
int    sys_pause(void);
unsigned int sys_alarm(unsigned int seconds);
int sys_sigaction(int sig, const struct sigaction *act, struct sigaction *oldact);
int sys_sigpending(uint32_t *set);
int sys_sigsuspend(const uint32_t *mask);
pid_t sys_getpgrp(void);
int   sys_setpgid(pid_t pid, pid_t pgid);
pid_t sys_setsid(void);
pid_t sys_getpgid(pid_t pid);
int   sys_tcsetpgrp(pid_t pgid);
pid_t sys_tcgetpgrp(void);
int sys_nanosleep(const struct timespec *req, struct timespec *rem);
int sys_clock_nanosleep(clockid_t clockid, int flags,
                        const struct timespec *req, struct timespec *rem);
int sys_getitimer(int which, struct itimerval_k *curr_value);
int sys_setitimer(int which, const struct itimerval_k *new_value,
                  struct itimerval_k *old_value);
int sys_fsync(int fildes);
int sys_fdatasync(int fildes);
long sys_sysconf(int name);
int sys_getrlimit(int resource, struct rlimit *user_rlim);
int sys_setrlimit(int resource, const struct rlimit *user_rlim);
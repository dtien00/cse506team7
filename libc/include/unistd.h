#ifndef _UNISTD_H
#define _UNISTD_H

#include "sys/types.h"

/* POSIX: the process environment, initialised from envp by crt.S */
extern char **environ;

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif

/* access() mode flags */
#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4

/* sysconf() name constants (_SC_*) — values must match SBUNIX_SC_* in kernel/limits.h */
#define _SC_ARG_MAX             0
#define _SC_CHILD_MAX           1
#define _SC_CLK_TCK             2
#define _SC_NGROUPS_MAX         3
#define _SC_OPEN_MAX            4
#define _SC_STREAM_MAX          5
#define _SC_PAGESIZE            6
#define _SC_PAGE_SIZE           6   /* XSI alias: must equal _SC_PAGESIZE */
#define _SC_SIGQUEUE_MAX        7
#define _SC_NPROCESSORS_CONF    8
#define _SC_NPROCESSORS_ONLN    9
#define _SC_PHYS_PAGES          10
#define _SC_LOGIN_NAME_MAX      11
#define _SC_HOST_NAME_MAX       12
#define _SC_SYMLOOP_MAX         13
#define _SC_VERSION             14
#define _POSIX_VERSION          200809L

ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
int close(int fd);
off_t lseek(int fd, off_t offset, int whence);

pid_t fork(void);
int execve(const char *pathname, char *const argv[], char *const envp[]);
pid_t getpid(void);
pid_t getppid(void);
uid_t getuid(void);
gid_t getgid(void);

unsigned int sleep(unsigned int seconds);
int usleep(unsigned int usec);
int uptime(void);   /* returns kernel timer_ticks since boot (10 ticks/s on QEMU) */
int nice(int increment); /* adjust scheduling priority; clamps to [0,19]; returns new priority */
int sched_yield(void);  /* voluntarily relinquish the CPU; always returns 0 */

int chdir(const char *path);
char* getcwd(char *buf, size_t size);
ssize_t fs_getdents(int fd, void *buf, size_t count, int flags);
int rename(const char *old, const char *new);  // mv applet
int rmdir(const char *path);                    // rmdir applet
int truncate(const char *path, off_t length);   // truncate applet

int pipe(int pipefd[2]);
int dup(int oldfd);
int dup2(int oldfd, int newfd);

int isatty(int fd);

void _exit(int status);

void* sbrk(intptr_t increment);
int brk(void* addr);

int unlink(const char *pathname);

int access(const char *pathname, int mode);

void sync(void);
int fsync(int fd);
int fdatasync(int fd);

pid_t getpgid(pid_t pid);
int   setpgid(pid_t pid, pid_t pgid);
pid_t getpgrp(void);
int   setsid(void);

int   tcsetpgrp(int fd, pid_t pgid);
pid_t tcgetpgrp(int fd);

int link(const char *oldpath, const char *newpath);
int symlink(const char *target, const char *linkpath);
ssize_t readlink(const char *pathname, char *buf, size_t bufsiz);

int pause(void);   /* suspend until signal; always returns -1 (EINTR) */

unsigned int alarm(unsigned int seconds);

long sysconf(int name);

/* pathconf()/fpathconf() name constants (_PC_*).
 * Values match Linux POSIX ABI for cross-platform compatibility. */
#define _PC_LINK_MAX            0
#define _PC_MAX_CANON           1
#define _PC_MAX_INPUT           2
#define _PC_NAME_MAX            3
#define _PC_PATH_MAX            4
#define _PC_PIPE_BUF            5
#define _PC_CHOWN_RESTRICTED    6
#define _PC_NO_TRUNC            7
#define _PC_VDISABLE            8
#define _PC_SYNC_IO             9
#define _PC_ASYNC_IO            10
#define _PC_PRIO_IO             11
#define _PC_FILESIZEBITS        13
#define _PC_SYMLINK_MAX         19
#define _PC_2_SYMLINKS          20

long pathconf(const char *path, int name);
long fpathconf(int fd, int name);

#endif

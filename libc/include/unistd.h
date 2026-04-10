#ifndef _UNISTD_H
#define _UNISTD_H

#include "sys/types.h"

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/* access() mode flags */
#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4

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

pid_t getpgid(pid_t pid);
int   setpgid(pid_t pid, pid_t pgid);
pid_t getpgrp(void);
int   setpgrp(void);

int link(const char *oldpath, const char *newpath);
int symlink(const char *target, const char *linkpath);
ssize_t readlink(const char *pathname, char *buf, size_t bufsiz);

int pause(void);   /* suspend until signal; always returns -1 (EINTR) */

#endif

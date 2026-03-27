#ifndef _UNISTD_H
#define _UNISTD_H

#include "types.h"

typedef int pid_t;
typedef int uid_t;
typedef int gid_t;
typedef long off_t;

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

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

int chdir(const char *path);
char* getcwd(char *buf, size_t size);

int pipe(int pipefd[2]);
int dup(int oldfd);
int dup2(int oldfd, int newfd);

int isatty(int fd);

void _exit(int status);

void* sbrk(intptr_t increment);
int brk(void* addr);

#endif

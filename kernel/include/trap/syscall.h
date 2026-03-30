#pragma once

#include "kernel/types.h"
#include "trap/trap.h"

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
void *sys_sbrk(intptr_t increment);
int sys_sleep(int seconds);
int sys_uptime(void);
int sys_kill(pid_t pid);
void *sys_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
int sys_munmap(void *addr, size_t length);
int sys_brk(void *addr);
off_t sys_lseek(int fd, off_t offset, int whence);
int sys_unlink(const char *path);
int sys_mkdir(const char *path, mode_t mode);
int sys_sync(void);

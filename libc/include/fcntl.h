#ifndef _FCNTL_H
#define _FCNTL_H

#include "sys/types.h"

/* File access modes */
#define O_RDONLY    0x0000
#define O_WRONLY    0x0001
#define O_RDWR      0x0002
#define O_EXEC      0x200000
#define O_SEARCH    O_EXEC

/* Access mode mask */
#define O_ACCMODE   0x0003

/* File creation flags */
#define O_CREAT     0x0040
#define O_EXCL      0x0080
#define O_NOCTTY    0x0100
#define O_TRUNC     0x0200
#define O_DIRECTORY 0x10000
#define O_NOFOLLOW  0x20000
#define O_CLOEXEC   0x80000
#define O_CLOFORK   0x100000
#define O_TTY_INIT  0

/* File status flags */
#define O_APPEND    0x0400
#define O_DSYNC     0x1000
#define O_RSYNC     0x2000
#define O_SYNC      0x4000
#define O_NONBLOCK  0x0800

#define F_DUPFD         0
#define F_GETFD         1
#define F_SETFD         2
#define F_GETFL         3
#define F_SETFL         4
#define F_GETOWN        5
#define F_SETOWN        6
#define F_GETLK         7
#define F_SETLK         8
#define F_SETLKW        9
#define F_SETOWN_EX     15
#define F_GETOWN_EX     16
#define F_OFD_GETLK     36
#define F_OFD_SETLK     37
#define F_OFD_SETLKW    38
#define F_DUPFD_CLOEXEC 1030
#define F_DUPFD_CLOFORK 1031

#define F_RDLCK 0
#define F_WRLCK 1
#define F_UNLCK 2

#define FD_CLOEXEC  1
#define FD_CLOFORK  2

#define F_OWNER_TID  0
#define F_OWNER_PID  1
#define F_OWNER_PGRP 2

struct f_owner_ex {
    int type;
    pid_t pid;
};

struct flock {
    short l_type;
    short l_whence;
    off_t l_start;
    off_t l_len;
    pid_t l_pid;
};

#define AT_FDCWD            (-100)
#define AT_EACCESS          0x800
#define AT_SYMLINK_NOFOLLOW 0x100
#define AT_SYMLINK_FOLLOW   0x200
#define AT_REMOVEDIR        0x400

#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif

int creat(const char *, mode_t);
int fcntl(int, int, ...);
int open(const char *, int, ...);
int openat(int, const char *, int, ...);

#endif

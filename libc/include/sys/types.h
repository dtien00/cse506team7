#ifndef _SYS_TYPES_H
#define _SYS_TYPES_H

#include "../types.h"

typedef int pid_t;
typedef int uid_t;
typedef int gid_t;
typedef int64_t off_t;
typedef unsigned int mode_t;
typedef unsigned long dev_t;
typedef unsigned long ino_t;  // defined in types.h
typedef unsigned int nlink_t;
typedef long blksize_t;
typedef long blkcnt_t;
typedef long time_t;
typedef long clock_t;

#endif

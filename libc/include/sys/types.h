#ifndef _SYS_TYPES_H
#define _SYS_TYPES_H

#include "../types.h"

/* types.h already defines (do NOT redefine here):
 *   size_t   — long unsigned int  (64-bit; matches SSIZE_MAX=LLONG_MAX in limits.h)
 *   ssize_t  — int64_t            (64-bit signed)
 */

/* Signed integer types */
typedef int             pid_t;        /* process/process-group IDs */
typedef int             uid_t;        /* user IDs */
typedef int             gid_t;        /* group IDs */
typedef int             id_t;         /* general identifier: holds pid_t, uid_t, or gid_t */
typedef int64_t         off_t;        /* file sizes and offsets (signed, 64-bit) */
typedef long            blksize_t;    /* block sizes (signed, fits in long) */
typedef long         blkcnt_t;     /* file block counts (signed) */
typedef long         time_t;       /* seconds since epoch; CX: width >= 64 bits */
typedef long            suseconds_t;  /* microseconds: signed, range [-1, 1000000] */

/* Unsigned integer types */
typedef unsigned int    mode_t;       /* file attribute bits */
typedef unsigned long   dev_t;        /* device IDs */
typedef unsigned long   ino_t;        /* file serial numbers */
typedef unsigned int    nlink_t;      /* link counts */
typedef unsigned long   fsblkcnt_t;   /* filesystem block counts */
typedef unsigned long   fsfilcnt_t;   /* filesystem file counts */
typedef uint16_t    reclen_t;     /* directory entry record lengths */

/* Clock / timer types */
typedef long            clock_t;      /* CPU-time clock ticks */
typedef int             clockid_t;    /* clock ID for clock_gettime() */
typedef int             timer_t;      /* timer ID returned by timer_create() */

/* pthread stub types — defined as opaque scalars; no comparison operators defined */
typedef int             pthread_t;
typedef int             pthread_attr_t;
typedef int             pthread_barrier_t;
typedef int             pthread_barrierattr_t;
typedef int             pthread_cond_t;
typedef int             pthread_condattr_t;
typedef int             pthread_key_t;
typedef int             pthread_mutex_t;
typedef int             pthread_mutexattr_t;
typedef int             pthread_once_t;
typedef int             pthread_rwlock_t;
typedef int             pthread_rwlockattr_t;
typedef int             pthread_spinlock_t;

#endif /* _SYS_TYPES_H */

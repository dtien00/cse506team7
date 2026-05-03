/* libc/include/limits.h — derived from kernel/include/kernel/limits.h values.
 * Keep in sync manually: kernel/include/kernel/limits.h is the canonical source. */
#ifndef _LIMITS_H
#define _LIMITS_H

/* ---- Runtime invariant limits (enforcement) -------------------------------- */
#define OPEN_MAX        64      /* SBUNIX_OPEN_MAX  — fd table size in kernel */
#define CHILD_MAX       64      /* SBUNIX_CHILD_MAX — system-wide (not per-UID) */

/* ---- Pathname variable limits (enforcement) -------------------------------- */
/* NOTE: POSIX allows these to vary per filesystem; pathconf() is authoritative.
 * These fixed values reflect the single enforcement limit in SBUnix.
 * PATH_MAX meets both _POSIX_PATH_MAX (256) and _XOPEN_PATH_MAX (1024). */
#define PATH_MAX        1024    /* SBUNIX_PATH_MAX  — kernel copy-in buffer size */
#define NAME_MAX        255     /* SBUNIX_NAME_MAX  — VFS dentry name field */
#define PIPE_BUF        4096    /* SBUNIX_PIPE_BUF  — pipe ring buffer capacity */
#define LINK_MAX        8       /* SBUNIX_LINK_MAX  — informational */
#define SYMLINK_MAX     255     /* SBUNIX_SYMLINK_MAX */
#define SYMLOOP_MAX     8       /* SBUNIX_SYMLOOP_MAX — VFS loop limit (not yet enforced) */

/* ---- Other runtime invariant limits (informational) ------------------------ */
#define ARG_MAX         4096    /* SBUNIX_ARG_MAX   */
#define NGROUPS_MAX     8       /* SBUNIX_NGROUPS_MAX — groups not yet implemented */
#define LOGIN_NAME_MAX  16      /* SBUNIX_LOGIN_NAME_MAX */
#define HOST_NAME_MAX   255     /* SBUNIX_HOST_NAME_MAX — gethostname not implemented */
#define TTY_NAME_MAX    9       /* SBUNIX_TTY_NAME_MAX */
#define NSIG            32      /* SBUNIX_NSIG */
#define MAX_CANON       255     /* SBUNIX_MAX_CANON */
#define MAX_INPUT       255     /* SBUNIX_MAX_INPUT */

/* ---- Memory ---------------------------------------------------------------- */
#define PAGESIZE        4096    /* SBUNIX_PAGESIZE */
#define PAGE_SIZE       4096    /* XSI: must equal PAGESIZE when PAGESIZE is defined */

/* ---- Numerical limits (IEEE Std 1003.1 §Numerical Limits) ------------------ */
#define CHAR_BIT        8
#define SCHAR_MIN       (-128)
#define SCHAR_MAX       127
#define UCHAR_MAX       255
#define CHAR_MIN        SCHAR_MIN   /* char is signed on RISC-V GCC default */
#define CHAR_MAX        SCHAR_MAX
#define SHRT_MIN        (-32768)
#define SHRT_MAX        32767
#define USHRT_MAX       65535
#define INT_MIN         (-2147483648)
#define INT_MAX         2147483647
#define UINT_MAX        4294967295U
#define LONG_MIN        (-9223372036854775808L)
#define LONG_MAX        9223372036854775807L
#define ULONG_MAX       18446744073709551615UL
#define LLONG_MIN       (-9223372036854775808LL)
#define LLONG_MAX       9223372036854775807LL
#define ULLONG_MAX      18446744073709551615ULL

/* ssize_t is int64_t on RISC-V 64-bit; SSIZE_MAX = LLONG_MAX */
#define SSIZE_MAX       LLONG_MAX

/* ---- POSIX minimum guaranteed values (_POSIX_* constants) ------------------ */
#define _POSIX_OPEN_MAX         20
#define _POSIX_PATH_MAX         256
#define _POSIX_NAME_MAX         14
#define _POSIX_ARG_MAX          4096
#define _POSIX_LINK_MAX         8
#define _POSIX_SYMLINK_MAX      255
#define _POSIX_SYMLOOP_MAX      8
#define _POSIX_CHILD_MAX        25
#define _POSIX_NGROUPS_MAX      8
#define _POSIX_PIPE_BUF         512
#define _POSIX_SSIZE_MAX        32767
#define _POSIX_MAX_CANON        255
#define _POSIX_MAX_INPUT        255
#define _POSIX_LOGIN_NAME_MAX   9
#define _POSIX_HOST_NAME_MAX    255
#define _POSIX_TTY_NAME_MAX     9

/* ---- XSI minimum values (Single UNIX Specification extensions) ------------- */
/* _XOPEN_PATH_MAX = 1024: our PATH_MAX (1024) now meets this requirement. */
#define _XOPEN_PATH_MAX     1024    /* met: PATH_MAX = 1024 */
#define _XOPEN_NAME_MAX     255     /* met: NAME_MAX = 255 */

#endif /* _LIMITS_H */
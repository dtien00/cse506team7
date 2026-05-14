/* libc/include/limits.h — POSIX.1-2024 §<limits.h> compliance.
 * Keep in sync manually: kernel/include/kernel/limits.h is the canonical source
 * for enforcement limits. The _POSIX_* and _POSIX2_* floor constants are
 * POSIX-specified minimums, not SBUnix-specific values. */
#ifndef _LIMITS_H
#define _LIMITS_H

/* ---- Runtime invariant limits (SBUnix enforcement values) ------------------- */
#define OPEN_MAX        64      /* fd table size in kernel */
#define CHILD_MAX       64      /* system-wide process limit (not per-UID) */
#define ARG_MAX         4096    /* max bytes for exec args + environ */
#define NGROUPS_MAX     8       /* supplementary group limit (not yet implemented) */
#define LOGIN_NAME_MAX  16      /* max login name length including NUL */
#define HOST_NAME_MAX   255     /* max hostname length excluding NUL */
#define TTY_NAME_MAX    9       /* max tty device name length including NUL */
#define ATEXIT_MAX      32      /* max functions registered with atexit() */
#define STREAM_MAX      8       /* max open stdio streams per process */
#define TIMER_MAX       32      /* max per-process POSIX timers */
#define TZNAME_MAX      6       /* max bytes in a timezone name */
#define RE_DUP_MAX      255     /* max occurrences in \{m,n\} interval expression */
#define LINE_MAX        2048    /* max bytes in a utility input line (= _POSIX2_LINE_MAX) */
#define RTSIG_MAX       8       /* stub: RT signals unimplemented (= _POSIX_RTSIG_MAX) */
#define SIGQUEUE_MAX    32      /* stub: queued signals unimplemented (= _POSIX_SIGQUEUE_MAX) */
#define PTHREAD_DESTRUCTOR_ITERATIONS 4    /* stub: pthreads unimplemented */
#define PTHREAD_KEYS_MAX              128  /* stub: pthreads unimplemented */
#define PTHREAD_STACK_MIN             0    /* stub: pthreads unimplemented */
#define PTHREAD_THREADS_MAX           64   /* stub: pthreads unimplemented */
#define NSIG_MAX        31      /* maximum signal number (SIGSYS = 31) */
#define GETENTROPY_MAX  256     /* max bytes returned by getentropy() */

/* ---- Pathname variable limits (SBUnix enforcement values) ------------------- */
/* NOTE: POSIX allows these to vary per filesystem; pathconf() is authoritative.
 * These fixed values reflect SBUnix's single enforcement limit.
 * PATH_MAX meets both _POSIX_PATH_MAX (256) and _XOPEN_PATH_MAX (1024). */
#define PATH_MAX        1024    /* kernel copy-in buffer size */
#define NAME_MAX        255     /* VFS dentry name field */
#define PIPE_BUF        4096    /* pipe ring buffer capacity */
#define LINK_MAX        8       /* informational */
#define SYMLINK_MAX     255     /* max symlink target length */
#define SYMLOOP_MAX     8       /* VFS symlink loop limit (not yet enforced) */
#define MAX_CANON       255     /* max bytes in a terminal canonical line */
#define MAX_INPUT       255     /* max bytes in a terminal input queue */
#define FILESIZEBITS    64      /* bits in off_t (int64_t on LP64) */
#define TEXTDOMAIN_MAX  252     /* max text domain name length (NAME_MAX - 3) */

/* ---- Memory ---------------------------------------------------------------- */
#define PAGESIZE        4096    /* RISC-V Sv39 base page size */
#define PAGE_SIZE       4096    /* XSI: must equal PAGESIZE when PAGESIZE is defined */

/* ---- Local extension ------------------------------------------------------- */
#define NSIG            32      /* SBUnix signal count (not POSIX; see NSIG_MAX above) */

/* ---- Numerical limits (POSIX.1-2024 §Numerical Limits) --------------------- */
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
#define LONG_MIN        (-9223372036854775807L - 1)
#define LONG_MAX        9223372036854775807L
#define ULONG_MAX       18446744073709551615UL
#define LLONG_MIN       (-9223372036854775807LL - 1)
#define LLONG_MAX       9223372036854775807LL
#define ULLONG_MAX      18446744073709551615ULL
#define LONG_BIT        64      /* CX: bits in long (RISC-V LP64: long = 64-bit) */
#define WORD_BIT        32      /* CX: bits in int (RISC-V: int = 32-bit) */
#define MB_LEN_MAX      1       /* max bytes per multibyte char (single-byte locale only) */

/* ssize_t is int64_t on RISC-V 64-bit; SSIZE_MAX = LLONG_MAX */
#define SSIZE_MAX       LLONG_MAX

/* ---- Other invariant values (POSIX.1-2024 §Other Invariant Values) ---------- */
#define NL_ARGMAX       9       /* max value of digit in printf %n$ conversion */
#define NL_MSGMAX       32767   /* max message number in a message catalog */
#define NL_SETMAX       255     /* max set number in a message catalog */
#define NL_TEXTMAX      2048    /* max bytes in a message string (= _POSIX2_LINE_MAX) */

/* ---- _POSIX_ minimum floor constants (base POSIX.1-2024 §Runtime Invariants) */
/* These are POSIX-guaranteed minimums; implementations shall not go below them. */
#define _POSIX_AIO_LISTIO_MAX               2
#define _POSIX_AIO_MAX                      1
#define _POSIX_ARG_MAX                      4096
#define _POSIX_CHILD_MAX                    25
#define _POSIX_CLOCKRES_MIN                 20000000    /* nanoseconds; max resolution */
#define _POSIX_DELAYTIMER_MAX               32
#define _POSIX_HOST_NAME_MAX                255
#define _POSIX_LINK_MAX                     8
#define _POSIX_LOGIN_NAME_MAX               9
#define _POSIX_MAX_CANON                    255
#define _POSIX_MAX_INPUT                    255
#define _POSIX_NAME_MAX                     14
#define _POSIX_NGROUPS_MAX                  8
#define _POSIX_OPEN_MAX                     20
#define _POSIX_PATH_MAX                     256
#define _POSIX_PIPE_BUF                     512
#define _POSIX_RE_DUP_MAX                   255
#define _POSIX_MQ_OPEN_MAX                  8
#define _POSIX_MQ_PRIO_MAX                  32
#define _POSIX_RTSIG_MAX                    8
#define _POSIX_SEM_NSEMS_MAX                256
#define _POSIX_SEM_VALUE_MAX                32767
#define _POSIX_SIGQUEUE_MAX                 32
#define _POSIX_SSIZE_MAX                    32767
#define _POSIX_STREAM_MAX                   8
#define _POSIX_SYMLINK_MAX                  255
#define _POSIX_SYMLOOP_MAX                  8
#define _POSIX_THREAD_DESTRUCTOR_ITERATIONS 4
#define _POSIX_THREAD_KEYS_MAX              128
#define _POSIX_THREAD_THREADS_MAX           64
#define _POSIX_TIMER_MAX                    32
#define _POSIX_TTY_NAME_MAX                 9
#define _POSIX_TZNAME_MAX                   6

/* ---- _POSIX2_ minimum floor constants (POSIX.2 utility limits) ------------- */
#define _POSIX2_BC_BASE_MAX                 99
#define _POSIX2_BC_DIM_MAX                  2048
#define _POSIX2_BC_SCALE_MAX                99
#define _POSIX2_BC_STRING_MAX               1000
#define _POSIX2_CHARCLASS_NAME_MAX          14
#define _POSIX2_COLL_WEIGHTS_MAX            2
#define _POSIX2_EXPR_NEST_MAX               32
#define _POSIX2_LINE_MAX                    2048
#define _POSIX2_RE_DUP_MAX                  255

/* ---- XSI minimum values (Single UNIX Specification extensions) ------------- */
/* _XOPEN_PATH_MAX = 1024: our PATH_MAX (1024) meets this requirement. */
#define _XOPEN_PATH_MAX     1024    /* met: PATH_MAX = 1024 */
#define _XOPEN_NAME_MAX     255     /* met: NAME_MAX = 255 */

#endif /* _LIMITS_H */

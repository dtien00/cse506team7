#ifndef _UNISTD_H
#define _UNISTD_H

#include "sys/types.h"

/* POSIX: the process environment, initialised from envp by crt.S */
extern char **environ;

/* --- Standard file descriptor numbers --- */
#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

/* --- lseek() whence constants --- */
#ifndef SEEK_SET
#define SEEK_SET  0
#define SEEK_CUR  1
#define SEEK_END  2
#define SEEK_HOLE 3
#define SEEK_DATA 4
#endif

/* --- access() mode flags --- */
#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4

/* --- lockf() function constants [XSI] --- */
#define F_ULOCK 0
#define F_LOCK  1
#define F_TLOCK 2
#define F_TEST  3

/* --- Special character disable value (terminal discipline) --- */
#define _POSIX_VDISABLE '\0'

/* --- posix_close() restart flag --- */
#define POSIX_CLOSE_RESTART 1

/* -----------------------------------------------------------------------
 * POSIX version constants
 * ----------------------------------------------------------------------- */
#define _POSIX_VERSION          202405L
#define _POSIX2_VERSION         202405L

/* -----------------------------------------------------------------------
 * POSIX option macros — "always supported" (base POSIX, value 202405L)
 * ----------------------------------------------------------------------- */
#define _POSIX_ASYNCHRONOUS_IO      202405L
#define _POSIX_BARRIERS             202405L
#define _POSIX_CLOCK_SELECTION      202405L
#define _POSIX_MAPPED_FILES         202405L
#define _POSIX_MEMORY_PROTECTION    202405L
#define _POSIX_MONOTONIC_CLOCK      202405L
#define _POSIX_READER_WRITER_LOCKS  202405L
#define _POSIX_REALTIME_SIGNALS     202405L
#define _POSIX_SEMAPHORES           202405L
#define _POSIX_SPIN_LOCKS           202405L
#define _POSIX_THREAD_SAFE_FUNCTIONS 202405L
#define _POSIX_THREADS              202405L
#define _POSIX_TIMEOUTS             202405L
#define _POSIX_TIMERS               202405L
#define _POSIX2_C_BIND              202405L
#define _POSIX2_CHAR_TERM           202405L

/* --- "always supported > 0" options --- */
#define _POSIX_JOB_CONTROL          202405L
#define _POSIX_REGEXP               1
#define _POSIX_SAVED_IDS            1
#define _POSIX_SHELL                1

/* --- File characteristic options (value != -1 means always set) --- */
#define _POSIX_CHOWN_RESTRICTED     1
#define _POSIX_NO_TRUNC             1
#define _POSIX2_SYMLINKS            1

/* --- Optional POSIX options (not implemented; sysconf returns -1) --- */
#define _POSIX_ADVISORY_INFO            -1
#define _POSIX_CPUTIME                  -1
#define _POSIX_DEVICE_CONTROL           -1
#define _POSIX_FSYNC                    -1
#define _POSIX_IPV6                     -1
#define _POSIX_MEMLOCK                  -1
#define _POSIX_MEMLOCK_RANGE            -1
#define _POSIX_MESSAGE_PASSING          -1
#define _POSIX_PRIORITIZED_IO           -1
#define _POSIX_PRIORITY_SCHEDULING      -1
#define _POSIX_RAW_SOCKETS              -1
#define _POSIX_SHARED_MEMORY_OBJECTS    -1
#define _POSIX_SPAWN                    -1
#define _POSIX_SPORADIC_SERVER          -1
#define _POSIX_SYNCHRONIZED_IO          -1
#define _POSIX_THREAD_ATTR_STACKADDR    -1
#define _POSIX_THREAD_ATTR_STACKSIZE    -1
#define _POSIX_THREAD_CPUTIME           -1
#define _POSIX_THREAD_PRIO_INHERIT      -1
#define _POSIX_THREAD_PRIO_PROTECT      -1
#define _POSIX_THREAD_PRIORITY_SCHEDULING -1
#define _POSIX_THREAD_PROCESS_SHARED    -1
#define _POSIX_THREAD_ROBUST_PRIO_INHERIT -1
#define _POSIX_THREAD_ROBUST_PRIO_PROTECT -1
#define _POSIX_THREAD_SPORADIC_SERVER   -1
#define _POSIX_TYPED_MEMORY_OBJECTS     -1
#define _POSIX2_C_DEV                   -1
#define _POSIX2_FORT_RUN                -1
#define _POSIX2_LOCALEDEF               -1
#define _POSIX2_SW_DEV                  -1
#define _POSIX2_UPE                     -1

/* --- Per-file async/sync/timestamp options (queried via pathconf) --- */
#define _POSIX_ASYNC_IO             -1
#define _POSIX_FALLOC               -1
#define _POSIX_PRIO_IO              -1
#define _POSIX_SYNC_IO              -1
#define _POSIX_TIMESTAMP_RESOLUTION 202405L

/* --- V8 programming environment models (LP64 platform: ILP32 not supported) --- */
#define _POSIX_V8_ILP32_OFF32   -1
#define _POSIX_V8_ILP32_OFFBIG  -1
#define _POSIX_V8_LP64_OFF64    1
#define _POSIX_V8_LPBIG_OFFBIG  1

/* --- XSI options (defined for compilation compatibility) --- */
#define _XOPEN_ENH_I18N             1
#define _XOPEN_SHM                  1
#define _XOPEN_UNIX                 1
#define _XOPEN_CRYPT                -1
#define _XOPEN_REALTIME             -1
#define _XOPEN_REALTIME_THREADS     -1
#define _XOPEN_UUCP                 -1

/* -----------------------------------------------------------------------
 * sysconf() name constants (_SC_*)
 * Values 0-14 must match SBUNIX_SC_* in kernel/include/kernel/limits.h.
 * Values 15+ are POSIX-required names; kernel sysconf() returns -1 for them
 * (feature not implemented).
 * ----------------------------------------------------------------------- */
#define _SC_ARG_MAX             0
#define _SC_CHILD_MAX           1
#define _SC_CLK_TCK             2
#define _SC_NGROUPS_MAX         3
#define _SC_OPEN_MAX            4
#define _SC_STREAM_MAX          5
#define _SC_PAGESIZE            6
#define _SC_PAGE_SIZE           6   /* alias — must equal _SC_PAGESIZE */
#define _SC_SIGQUEUE_MAX        7
#define _SC_NPROCESSORS_CONF    8
#define _SC_NPROCESSORS_ONLN    9
#define _SC_PHYS_PAGES          10
#define _SC_LOGIN_NAME_MAX      11
#define _SC_HOST_NAME_MAX       12
#define _SC_SYMLOOP_MAX         13
#define _SC_VERSION             14

#define _SC_2_C_BIND                    15
#define _SC_2_C_DEV                     16
#define _SC_2_CHAR_TERM                 17
#define _SC_2_FORT_RUN                  18
#define _SC_2_LOCALEDEF                 19
#define _SC_2_SW_DEV                    20
#define _SC_2_UPE                       21
#define _SC_2_VERSION                   22
#define _SC_ADVISORY_INFO               23
#define _SC_AIO_LISTIO_MAX              24
#define _SC_AIO_MAX                     25
#define _SC_AIO_PRIO_DELTA_MAX          26
#define _SC_ASYNCHRONOUS_IO             27
#define _SC_ATEXIT_MAX                  28
#define _SC_BARRIERS                    29
#define _SC_BC_BASE_MAX                 30
#define _SC_BC_DIM_MAX                  31
#define _SC_BC_SCALE_MAX                32
#define _SC_BC_STRING_MAX               33
#define _SC_CLOCK_SELECTION             34
#define _SC_COLL_WEIGHTS_MAX            35
#define _SC_CPUTIME                     36
#define _SC_DELAYTIMER_MAX              37
#define _SC_DEVICE_CONTROL              38
#define _SC_EXPR_NEST_MAX               39
#define _SC_FSYNC                       40
#define _SC_GETGR_R_SIZE_MAX            41
#define _SC_GETPW_R_SIZE_MAX            42
#define _SC_IOV_MAX                     43
#define _SC_IPV6                        44
#define _SC_JOB_CONTROL                 45
#define _SC_LINE_MAX                    46
#define _SC_MAPPED_FILES                47
#define _SC_MEMLOCK                     48
#define _SC_MEMLOCK_RANGE               49
#define _SC_MEMORY_PROTECTION           50
#define _SC_MESSAGE_PASSING             51
#define _SC_MONOTONIC_CLOCK             52
#define _SC_MQ_OPEN_MAX                 53
#define _SC_MQ_PRIO_MAX                 54
#define _SC_NSIG                        55
#define _SC_PRIORITIZED_IO              56
#define _SC_PRIORITY_SCHEDULING         57
#define _SC_RAW_SOCKETS                 58
#define _SC_RE_DUP_MAX                  59
#define _SC_READER_WRITER_LOCKS         60
#define _SC_REALTIME_SIGNALS            61
#define _SC_REGEXP                      62
#define _SC_RTSIG_MAX                   63
#define _SC_SAVED_IDS                   64
#define _SC_SEM_NSEMS_MAX               65
#define _SC_SEM_VALUE_MAX               66
#define _SC_SEMAPHORES                  67
#define _SC_SHARED_MEMORY_OBJECTS       68
#define _SC_SHELL                       69
#define _SC_SPAWN                       70
#define _SC_SPIN_LOCKS                  71
#define _SC_SPORADIC_SERVER             72
#define _SC_SS_REPL_MAX                 73
#define _SC_SYNCHRONIZED_IO             74
#define _SC_THREAD_ATTR_STACKADDR       75
#define _SC_THREAD_ATTR_STACKSIZE       76
#define _SC_THREAD_CPUTIME              77
#define _SC_THREAD_DESTRUCTOR_ITERATIONS 78
#define _SC_THREAD_KEYS_MAX             79
#define _SC_THREAD_PRIO_INHERIT         80
#define _SC_THREAD_PRIO_PROTECT         81
#define _SC_THREAD_PRIORITY_SCHEDULING  82
#define _SC_THREAD_PROCESS_SHARED       83
#define _SC_THREAD_ROBUST_PRIO_INHERIT  84
#define _SC_THREAD_ROBUST_PRIO_PROTECT  85
#define _SC_THREAD_SAFE_FUNCTIONS       86
#define _SC_THREAD_SPORADIC_SERVER      87
#define _SC_THREAD_STACK_MIN            88
#define _SC_THREAD_THREADS_MAX          89
#define _SC_THREADS                     90
#define _SC_TIMEOUTS                    91
#define _SC_TIMER_MAX                   92
#define _SC_TIMERS                      93
#define _SC_TTY_NAME_MAX                94
#define _SC_TYPED_MEMORY_OBJECTS        95
#define _SC_TZNAME_MAX                  96
#define _SC_V8_ILP32_OFF32              97
#define _SC_V8_ILP32_OFFBIG             98
#define _SC_V8_LP64_OFF64               99
#define _SC_V8_LPBIG_OFFBIG             100
#define _SC_XOPEN_CRYPT                 101
#define _SC_XOPEN_ENH_I18N              102
#define _SC_XOPEN_REALTIME              103
#define _SC_XOPEN_REALTIME_THREADS      104
#define _SC_XOPEN_SHM                   105
#define _SC_XOPEN_UNIX                  106
#define _SC_XOPEN_UUCP                  107
#define _SC_XOPEN_VERSION               108

/* -----------------------------------------------------------------------
 * pathconf()/fpathconf() name constants (_PC_*)
 * Values 0-20 follow the Linux POSIX ABI for cross-platform compatibility.
 * Values 21-23 are SBUnix-assigned for POSIX.1-2024 additions.
 * ----------------------------------------------------------------------- */
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
#define _PC_REC_INCR_XFER_SIZE  14
#define _PC_REC_MAX_XFER_SIZE   15
#define _PC_REC_MIN_XFER_SIZE   16
#define _PC_REC_XFER_ALIGN      17
#define _PC_ALLOC_SIZE_MIN      18
#define _PC_SYMLINK_MAX         19
#define _PC_2_SYMLINKS          20
#define _PC_FALLOC              21
#define _PC_TEXTDOMAIN_MAX      22
#define _PC_TIMESTAMP_RESOLUTION 23

/* -----------------------------------------------------------------------
 * confstr() name constants (_CS_*)
 * ----------------------------------------------------------------------- */
#define _CS_PATH                              0
#define _CS_POSIX_V8_ILP32_OFF32_CFLAGS       1
#define _CS_POSIX_V8_ILP32_OFF32_LDFLAGS      2
#define _CS_POSIX_V8_ILP32_OFF32_LIBS         3
#define _CS_POSIX_V8_ILP32_OFFBIG_CFLAGS      4
#define _CS_POSIX_V8_ILP32_OFFBIG_LDFLAGS     5
#define _CS_POSIX_V8_ILP32_OFFBIG_LIBS        6
#define _CS_POSIX_V8_LP64_OFF64_CFLAGS        7
#define _CS_POSIX_V8_LP64_OFF64_LDFLAGS       8
#define _CS_POSIX_V8_LP64_OFF64_LIBS          9
#define _CS_POSIX_V8_LPBIG_OFFBIG_CFLAGS      10
#define _CS_POSIX_V8_LPBIG_OFFBIG_LDFLAGS     11
#define _CS_POSIX_V8_LPBIG_OFFBIG_LIBS        12
#define _CS_POSIX_V8_THREADS_CFLAGS           13
#define _CS_POSIX_V8_THREADS_LDFLAGS          14
#define _CS_POSIX_V8_WIDTH_RESTRICTED_ENVS    15
#define _CS_V8_ENV                            16

/* -----------------------------------------------------------------------
 * Function declarations (POSIX.1-2024 base + CX, alphabetical order)
 * ----------------------------------------------------------------------- */

int          access(const char *, int);
unsigned     alarm(unsigned);
int          chdir(const char *);
int          chown(const char *, uid_t, gid_t);
int          close(int);
size_t       confstr(int, char *, size_t);
int          dup(int);
int          dup2(int, int);
int          dup3(int, int, int);
_Noreturn void _exit(int);
int          faccessat(int, const char *, int, int);
int          fchdir(int);
int          fchown(int, uid_t, gid_t);
int          fchownat(int, const char *, uid_t, gid_t, int);
int          fdatasync(int);
int          fexecve(int, char *const [], char *const []);
pid_t        _Fork(void);
pid_t        fork(void);
long         fpathconf(int, int);
int          fsync(int);
int          ftruncate(int, off_t);
char        *getcwd(char *, size_t);
gid_t        getegid(void);
int          getentropy(void *, size_t);
uid_t        geteuid(void);
gid_t        getgid(void);
int          getgroups(int, gid_t []);
int          gethostname(char *, size_t);
char        *getlogin(void);
int          getlogin_r(char *, size_t);
int          getopt(int, char * const [], const char *);
pid_t        getpgid(pid_t);
pid_t        getpgrp(void);
pid_t        getpid(void);
pid_t        getppid(void);
pid_t        getsid(pid_t);
uid_t        getuid(void);
int          isatty(int);
int          lchown(const char *, uid_t, gid_t);
int          link(const char *, const char *);
int          linkat(int, const char *, int, const char *, int);
off_t        lseek(int, off_t, int);
int          nice(int);
long         pathconf(const char *, int);
int          pause(void);
int          pipe(int [2]);
int          pipe2(int [2], int);
int          posix_close(int, int);
ssize_t      pread(int, void *, size_t, off_t);
ssize_t      pwrite(int, const void *, size_t, off_t);
ssize_t      read(int, void *, size_t);
ssize_t      readlink(const char *restrict, char *restrict, size_t);
ssize_t      readlinkat(int, const char *restrict, char *restrict, size_t);
int          rmdir(const char *);
int          setegid(gid_t);
int          seteuid(uid_t);
int          setgid(gid_t);
int          setpgid(pid_t, pid_t);
pid_t        setsid(void);
int          setuid(uid_t);
unsigned     sleep(unsigned);
int          symlink(const char *, const char *);
int          symlinkat(const char *, int, const char *);
void         sync(void);
long         sysconf(int);
pid_t        tcgetpgrp(int);
int          tcsetpgrp(int, pid_t);
int          truncate(const char *, off_t);
char        *ttyname(int);
int          ttyname_r(int, char *, size_t);
int          unlink(const char *);
int          unlinkat(int, const char *, int);
int          execl(const char *, const char *, ...);
int          execle(const char *, const char *, ...);
int          execlp(const char *, const char *, ...);
int          execv(const char *, char *const []);
int          execve(const char *, char *const [], char *const []);
int          execvp(const char *, char *const []);
ssize_t      write(int, const void *, size_t);

extern char  *optarg;
extern int    opterr, optind, optopt;

/* -----------------------------------------------------------------------
 * SBUnix extensions (non-POSIX)
 * ----------------------------------------------------------------------- */
int          usleep(unsigned int usec);
int          uptime(void);
void        *sbrk(intptr_t increment);
int          brk(void *addr);
ssize_t      fs_getdents(int fd, void *buf, size_t count, int flags);
int          rename(const char *oldpath, const char *newpath);

/* sched_yield() canonical home is <sched.h>; retained here for compatibility */
#ifndef _SCHED_H
int          sched_yield(void);
#endif

#endif /* _UNISTD_H */

#ifndef _KERNEL_LIMITS_H
#define _KERNEL_LIMITS_H

/*
 * SBUnix compile-time resource limits.
 * All kernel subsystems and libc/include/limits.h derive their constants from here.
 * POSIX minimum guaranteed values are documented inline.
 */

/* --- File descriptor limits -------------------------------------------- */

/* Maximum open file descriptors per process.
 * POSIX minimum (_POSIX_OPEN_MAX) = 20. We use 20 to meet the minimum.
 * To increase: also update fd_table_t in kernel/include/fs/file.h. */
#define SBUNIX_OPEN_MAX         20

/* Maximum length of a pathname (bytes, including NUL terminator).
 * POSIX minimum (_POSIX_PATH_MAX) = 256. Kernel currently enforces 1024.
 * libc/include/limits.h must match this value exactly (enforcement limit). */
#define SBUNIX_PATH_MAX         1024

/* Maximum length of a single filename component (bytes, excluding NUL).
 * POSIX minimum (_POSIX_NAME_MAX) = 14. */
#define SBUNIX_NAME_MAX         255

/* Maximum number of bytes in exec argument list (argv + envp combined).
 * POSIX minimum (_POSIX_ARG_MAX) = 4096. */
#define SBUNIX_ARG_MAX          4096

/* Maximum number of hard links to a single file.
 * POSIX minimum (_POSIX_LINK_MAX) = 8. */
#define SBUNIX_LINK_MAX         8

/* --- Process limits ----------------------------------------------------- */

/* Maximum number of simultaneous processes system-wide.
 * POSIX minimum (_POSIX_CHILD_MAX) = 25. */
#define SBUNIX_CHILD_MAX        64

/* Maximum number of real-time or POSIX signals (1-based; signals 1..NSIG-1). */
#define SBUNIX_NSIG             32

/* Maximum length of supplementary group list per process.
 * POSIX minimum (_POSIX_NGROUPS_MAX) = 8. Not yet implemented — placeholder. */
#define SBUNIX_NGROUPS_MAX      8

/* Maximum length of a login name string (including NUL).
 * POSIX minimum (_POSIX_LOGIN_NAME_MAX) = 9. */
#define SBUNIX_LOGIN_NAME_MAX   16

/* --- I/O limits --------------------------------------------------------- */

/* Minimum guaranteed atomic write size to a pipe.
 * POSIX minimum (_POSIX_PIPE_BUF) = 512. */
#define SBUNIX_PIPE_BUF         4096

/* Maximum bytes returned by a single read()/write() call.
 * ssize_t is int64_t on RISC-V 64-bit; SSIZE_MAX = INT64_MAX = 2^63 - 1.
 * _POSIX_SSIZE_MAX minimum = 32767; we expose the full 64-bit range. */
#define SBUNIX_SSIZE_MAX        (9223372036854775807LL)

/* Maximum bytes for a canonical terminal input line (including NUL). */
#define SBUNIX_MAX_CANON        255

/* Maximum bytes in a terminal type-ahead buffer. */
#define SBUNIX_MAX_INPUT        255

/* Maximum length of a terminal device name (bytes, including NUL).
 * POSIX minimum (_POSIX_TTY_NAME_MAX) = 9. */
#define SBUNIX_TTY_NAME_MAX     9

/* Maximum length of a hostname string (bytes, not including NUL).
 * POSIX minimum (_POSIX_HOST_NAME_MAX) = 255. */
#define SBUNIX_HOST_NAME_MAX    255

/* --- Filesystem limits -------------------------------------------------- */

/* Maximum bytes in a symbolic link target (not including NUL).
 * POSIX minimum (_POSIX_SYMLINK_MAX) = 255. */
#define SBUNIX_SYMLINK_MAX      255

/* Maximum symbolic links traversed in a single pathname resolution.
 * POSIX minimum (_POSIX_SYMLOOP_MAX) = 8.
 * NOTE: the VFS resolver does not currently count symlink depth.
 * This constant is informational until loop detection is enforced. */
#define SBUNIX_SYMLOOP_MAX      8

/* Maximum files in tmpfs volume. */
#define SBUNIX_TMPFS_MAX_FILES      128

/* Maximum size of a single tmpfs file in bytes. */
#define SBUNIX_TMPFS_MAX_FILESIZE   (1024 * 1024)

/* --- Memory limits ------------------------------------------------------ */

/* System page size in bytes. Must match PGSIZE in arch/riscv.h.
 * Per POSIX XSI: if PAGESIZE is defined, PAGE_SIZE must equal it. */
#define SBUNIX_PAGESIZE         4096

/* Default user stack size in bytes (256 pages = 1 MB).
 * Must match USTACK_SIZE in arch/riscv.h. */
#define SBUNIX_STACK_MAX        (256 * SBUNIX_PAGESIZE)

/* --- Clock / timing ----------------------------------------------------- */

/* Clock ticks per second exposed to user space via sysconf(_SC_CLK_TCK). */
#define SBUNIX_CLK_TCK          100

/* --- sysconf() name constants (_SC_*) ----------------------------------- */
/* These numeric values must match the switch cases in sys_sysconf()
 * and the _SC_* defines in libc/include/unistd.h. */
#define SBUNIX_SC_ARG_MAX             0
#define SBUNIX_SC_CHILD_MAX           1
#define SBUNIX_SC_CLK_TCK             2
#define SBUNIX_SC_NGROUPS_MAX         3
#define SBUNIX_SC_OPEN_MAX            4
#define SBUNIX_SC_STREAM_MAX          5
#define SBUNIX_SC_PAGESIZE            6
#define SBUNIX_SC_SIGQUEUE_MAX        7
#define SBUNIX_SC_NPROCESSORS_CONF    8
#define SBUNIX_SC_NPROCESSORS_ONLN    9
#define SBUNIX_SC_PHYS_PAGES          10
#define SBUNIX_SC_LOGIN_NAME_MAX      11
#define SBUNIX_SC_HOST_NAME_MAX       12
#define SBUNIX_SC_SYMLOOP_MAX         13
#define SBUNIX_SC_VERSION             14

#endif /* _KERNEL_LIMITS_H */
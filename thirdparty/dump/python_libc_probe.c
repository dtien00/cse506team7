/*
 * python_libc_probe.c — compile-time probe for all libc symbols that
 * MicroPython (minimal variant, RISC-V64 sbunix) actually depends on.
 *
 * Build with the same flags the Makefile uses:
 *
 *   riscv64-unknown-elf-gcc \
 *     -ffreestanding -fno-builtin -nostdlib -nostdinc -mcmodel=medany \
 *     -march=rv64imac_zicsr_zifencei -mabi=lp64 \
 *     -isystem <sbunix>/thirdparty/include \
 *     -isystem <sbunix>/libc/include \
 *     -isystem $(riscv64-unknown-elf-gcc -print-file-name=include) \
 *     -D__sbunix__ \
 *     -c python_libc_probe.c -o /dev/null
 *
 * A clean compile means every header is present and every type/macro
 * referenced here is defined.  Linker errors (unresolved symbols) when
 * linking micropython reveal which *functions* are missing from libc.a.
 *
 * Modules covered (minimal variant — socket/thread/termios/ffi OFF):
 *   main.c      stdio stdlib string unistd signal errno sys/stat sys/types ctype
 *   modos.c     errno stdlib string
 *   modtime.c   unistd errno string time sys/time math
 *   gccollect.c stdio (GC not native — sys/mman not needed for minimal)
 *   alloc.c     sys/mman (MICROPY_ENABLE_NATIVE_CODE — off in minimal, but probe anyway)
 *   input.c     unistd
 *   compat.c    stdlib stdio string signal unistd fcntl sys/stat sys/wait
 *               fnmatch poll time dirent glob termios errno
 *   dummy.c     sys/types sys/socket sys/time sys/times time stdio signal
 *               string errno grp
 *   setjmp.S    (asm — no header probe needed)
 */

/* ── headers from main.c ── */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <signal.h>

/* ── headers from modtime.c ── */
#include <time.h>
#include <sys/time.h>
#include <math.h>

/* ── headers from alloc.c (mmap path) ── */
#include <sys/mman.h>

/* ── headers from compat.c ── */
#include <fcntl.h>
#include <sys/wait.h>
#include <fnmatch.h>
#include <poll.h>
#include <dirent.h>
#include <glob.h>
#include <termios.h>

/* ── headers from dummy.c ── */
#include <sys/socket.h>
#include <sys/times.h>
#include <grp.h>

/* ================================================================
 * Types / macros — if any of these fail the header is broken or missing
 * ================================================================ */
static void probe_types(void)
{
    /* sys/types.h */
    pid_t   _pid  = 0; (void)_pid;
    uid_t   _uid  = 0; (void)_uid;
    gid_t   _gid  = 0; (void)_gid;
    off_t   _off  = 0; (void)_off;
    ssize_t _ss   = 0; (void)_ss;
    size_t  _sz   = 0; (void)_sz;
    mode_t  _mode = 0; (void)_mode;
    ino_t   _ino  = 0; (void)_ino;
    dev_t   _dev  = 0; (void)_dev;
    nlink_t _nl   = 0; (void)_nl;

    /* time.h */
    struct timespec  _ts; (void)_ts;
    struct timeval   _tv; (void)_tv;
    clock_t          _cl = 0; (void)_cl;
    time_t           _t  = 0; (void)_t;

    /* signal.h */
    struct sigaction _sa; (void)_sa;
    sigset_t         _ss2; (void)_ss2;

    /* sys/stat.h */
    struct stat _st; (void)_st;

    /* dirent.h */
    struct dirent _de; (void)_de;
    DIR          *_dp = NULL; (void)_dp;

    /* poll.h */
    struct pollfd _pfd; (void)_pfd;

    /* termios.h */
    struct termios _tio; (void)_tio;
    tcflag_t       _fl = 0; (void)_fl;

    /* glob.h */
    glob_t _gl; (void)_gl;

    /* grp.h */
    struct group *_gr = NULL; (void)_gr;

    /* sys/times.h */
    struct tms _tms; (void)_tms;

    /* sys/wait.h */
    int _wstatus = 0;
    (void)WIFEXITED(_wstatus);
    (void)WEXITSTATUS(_wstatus);

    /* sys/mman.h macros */
    int _prot  = PROT_READ | PROT_WRITE | PROT_EXEC; (void)_prot;
    int _flags = MAP_PRIVATE | MAP_ANONYMOUS;         (void)_flags;

    /* fcntl.h macros */
    int _ofl = O_RDONLY | O_WRONLY | O_RDWR | O_CREAT | O_TRUNC | O_APPEND; (void)_ofl;

    /* math.h */
    double _d = (double)M_PI; (void)_d;

    /* errno macros */
    (void)ENOENT; (void)EINTR; (void)EAGAIN; (void)EBADF;
    (void)ENOMEM; (void)EACCES; (void)EEXIST; (void)ENOTDIR;
    (void)EISDIR; (void)ENOTEMPTY; (void)EOVERFLOW; (void)ERANGE;

    /* signal macros */
    (void)SIGINT; (void)SIGTERM; (void)SIGCHLD; (void)SIGPIPE;
    (void)SIGUSR1; (void)SIGUSR2; (void)SIGKILL; (void)SIGSTOP;
    (void)SIG_DFL; (void)SIG_IGN;

    /* stdint types */
    uint8_t  _u8  = 0; (void)_u8;
    uint16_t _u16 = 0; (void)_u16;
    uint32_t _u32 = 0; (void)_u32;
    uint64_t _u64 = 0; (void)_u64;
    int8_t   _i8  = 0; (void)_i8;
    int32_t  _i32 = 0; (void)_i32;
    int64_t  _i64 = 0; (void)_i64;
    uintptr_t _up = 0; (void)_up;
}

/* ================================================================
 * Functions — reference every libc fn MicroPython calls so the
 * linker will fail fast if any are absent from libc.a
 * ================================================================ */
static void probe_functions(void)
{
    /* --- stdio --- */
    (void)printf;
    (void)fprintf;
    (void)sprintf;
    (void)snprintf;
    (void)vprintf;
    (void)vfprintf;
    (void)vsnprintf;
    (void)puts;
    (void)putchar;
    (void)fputc;
    (void)fgetc;
    (void)fputs;
    (void)fgets;
    (void)fflush;
    (void)fopen;
    (void)fclose;
    (void)fread;
    (void)fwrite;
    (void)fseek;
    (void)ftell;
    (void)rewind;
    (void)feof;
    (void)ferror;
    (void)fileno;
    (void)stdin;
    (void)stdout;
    (void)stderr;
    (void)perror;
    (void)sscanf;

    /* --- stdlib --- */
    (void)malloc;
    (void)free;
    (void)realloc;
    (void)calloc;
    (void)exit;
    (void)abort;
    (void)getenv;
    (void)atoi;
    (void)atol;
    (void)strtol;
    (void)strtoul;
    (void)strtod;
    (void)qsort;
    (void)bsearch;
    (void)abs;

    /* --- string --- */
    (void)memcpy;
    (void)memmove;
    (void)memset;
    (void)memcmp;
    (void)memchr;
    (void)strlen;
    (void)strcmp;
    (void)strncmp;
    (void)strcpy;
    (void)strncpy;
    (void)strcat;
    (void)strncat;
    (void)strchr;
    (void)strrchr;
    (void)strstr;
    (void)strtok;
    (void)strdup;
    (void)strerror;
    (void)strspn;
    (void)strcspn;

    /* --- unistd / POSIX --- */
    (void)read;
    (void)write;
    (void)close;
    (void)open;
    (void)lseek;
    (void)unlink;
    (void)rmdir;
    (void)mkdir;
    (void)getcwd;
    (void)chdir;
    (void)getpid;
    (void)getuid;
    (void)getgid;
    (void)isatty;
    (void)dup;
    (void)dup2;
    (void)pipe;
    (void)fork;
    (void)execv;
    (void)execve;
    (void)waitpid;
    (void)access;
    (void)rename;
    (void)fsync;

    /* --- sys/stat --- */
    (void)stat;
    (void)fstat;
    (void)lstat;
    (void)chmod;
    (void)umask;

    /* --- sys/mman --- */
    (void)mmap;
    (void)munmap;

    /* --- time --- */
    (void)gettimeofday;
    (void)clock_gettime;
    (void)time;
    (void)gmtime;
    (void)localtime;
    (void)mktime;
    (void)strftime;
    (void)difftime;
    (void)clock;

    /* --- signal --- */
    (void)sigaction;
    (void)sigemptyset;
    (void)sigfillset;
    (void)sigaddset;
    (void)sigdelset;
    (void)sigprocmask;
    (void)kill;
    (void)raise;

    /* --- dirent --- */
    (void)opendir;
    (void)readdir;
    (void)closedir;

    /* --- poll --- */
    (void)poll;

    /* --- fcntl --- */
    (void)fcntl;

    /* --- fnmatch / glob --- */
    (void)fnmatch;
    (void)glob;
    (void)globfree;

    /* --- math (softfloat-backed on sbunix) --- */
    (void)floor;
    (void)ceil;
    (void)fmod;
    (void)pow;
    (void)sqrt;
    (void)sin;
    (void)cos;
    (void)exp;
    (void)log;
    (void)log10;
    (void)fabs;
    (void)round;
    (void)trunc;
    (void)modf;
    (void)frexp;
    (void)ldexp;
    (void)isnan;
    (void)isinf;
    (void)isfinite;

    /* --- ctype --- */
    (void)isdigit;
    (void)isalpha;
    (void)isalnum;
    (void)isspace;
    (void)isupper;
    (void)islower;
    (void)toupper;
    (void)tolower;
    (void)isprint;
    (void)ispunct;
}

/* dummy main so it links as a standalone object if needed */
int main(void) {
    probe_types();
    probe_functions();
    return 0;
}

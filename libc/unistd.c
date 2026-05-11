#include "unistd.h"
#include "sys/stat.h"
#include "fcntl.h"
#include "errno.h"
#include "string.h"
#include "time.h"
#include "termios.h"
#include "sys/ioctl.h"

#define SYS_exit  1
#define SYS_sleep 11
#define SYS_uptime 12

void _exit(int status) {
    register long a0 asm("a0") = status;
    register long a7 asm("a7") = SYS_exit;
    asm volatile("ecall" :: "r"(a0), "r"(a7) : "memory");
    while (1);
}

unsigned int sleep(unsigned int seconds) {
    register long a0 asm("a0") = seconds;
    register long a7 asm("a7") = SYS_sleep;
    asm volatile("ecall" :: "r"(a0), "r"(a7) : "memory");
    return 0;
}

int uptime(void) {
    register long a7 asm("a7") = SYS_uptime;
    register long a0 asm("a0") = 0;
    asm volatile("ecall" : "+r"(a0) : "r"(a7) : "memory");
    return (int)a0;
}

int usleep(unsigned int usec) {
    struct timespec ts;
    ts.tv_sec  = (long)(usec / 1000000U);
    ts.tv_nsec = (long)(usec % 1000000U) * 1000L;
    return nanosleep(&ts, (struct timespec *)0);
}

/* getppid() is implemented as an assembly syscall wrapper in syscall.S */

int isatty(int fd) {
    struct termios t;
    if (tcgetattr(fd, &t) == 0)
        return 1;
    /* errno is already set by tcgetattr (ENOTTY or EBADF) */
    return 0;
}

/* getpgid / setpgid / getpgrp / setsid are real syscall wrappers in syscall.S.
 * setpgrp() is a BSD convenience equivalent to setpgid(0, 0). */
int setpgrp(void) {
    return setpgid(0, 0);
}

/* -----------------------------------------------------------------------
 * getopt — POSIX.1 option parser
 * ----------------------------------------------------------------------- */
char *optarg = NULL;
int   opterr  = 1;
int   optind  = 1;
int   optopt  = 0;

static int _optpos = 0;

int getopt(int argc, char * const argv[], const char *optstring) {
    if (optind == 0) {
        optind  = 1;
        _optpos = 0;
    }

    optarg = NULL;

    while (optind < argc) {
        char *arg = argv[optind];

        if (_optpos == 0) {
            if (!arg || arg[0] != '-' || arg[1] == '\0') return -1;
            if (arg[1] == '-' && arg[2] == '\0') { optind++; return -1; }
            _optpos = 1;
        }

        int c = (unsigned char)argv[optind][_optpos];
        _optpos++;

        if (argv[optind][_optpos] == '\0') {
            optind++;
            _optpos = 0;
        }

        const char *p = optstring;
        if (*p == ':') p++;
        while (*p && *p != c) p++;

        if (!*p) {
            optopt = c;
            if (opterr && optstring[0] != ':')
                write(2, "Unknown option\n", 15);
            return '?';
        }

        if (p[1] == ':') {
            if (_optpos == 0) {
                if (optind >= argc) {
                    optopt = c;
                    if (optstring[0] == ':') return ':';
                    if (opterr) write(2, "Option requires argument\n", 25);
                    return '?';
                }
                optarg = argv[optind++];
            } else {
                optarg = argv[optind - 1] + _optpos;
                _optpos = 0;
            }
        }

        return c;
    }

    return -1;
}



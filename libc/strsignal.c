/*
 * strsignal.c — POSIX signal-number to short human-readable description.
 *
 * Companion to FEEDBACK_SIG_TAB.md Issue S.  Pure libc: no syscall, no
 * kernel dependency.  Strings match conventional glibc/musl wording so
 * test harnesses that grep for "Segmentation fault" / "Terminated" /
 * "Broken pipe" keep working unchanged.
 *
 * Unknown signal numbers (including 0 and negatives) fall back to the
 * single static buffer "_unk_buf" holding "Unknown signal N".  POSIX
 * permits this; SBUnix has no threads so reentrancy is not a concern.
 */

#include "string.h"
#include "signal.h"
#include "stdio.h"

static const char *const _sig_names[] = {
    [SIGHUP]    = "Hangup",
    [SIGINT]    = "Interrupt",
    [SIGQUIT]   = "Quit",
    [SIGILL]    = "Illegal instruction",
    [SIGTRAP]   = "Trace/breakpoint trap",
    [SIGABRT]   = "Aborted",
    [SIGBUS]    = "Bus error",
    [SIGFPE]    = "Floating point exception",
    [SIGKILL]   = "Killed",
    [SIGUSR1]   = "User defined signal 1",
    [SIGSEGV]   = "Segmentation fault",
    [SIGUSR2]   = "User defined signal 2",
    [SIGPIPE]   = "Broken pipe",
    [SIGALRM]   = "Alarm clock",
    [SIGTERM]   = "Terminated",
    [SIGSTKFLT] = "Stack fault",
    [SIGCHLD]   = "Child exited",
    [SIGCONT]   = "Continued",
    [SIGSTOP]   = "Stopped (signal)",
    [SIGTSTP]   = "Stopped",
    [SIGTTIN]   = "Stopped (tty input)",
    [SIGTTOU]   = "Stopped (tty output)",
    [SIGURG]    = "Urgent I/O condition",
    [SIGXCPU]   = "CPU time limit exceeded",
    [SIGXFSZ]   = "File size limit exceeded",
    [SIGVTALRM] = "Virtual timer expired",
    [SIGPROF]   = "Profiling timer expired",
    [SIGWINCH]  = "Window changed",
    [SIGIO]     = "I/O possible",
    [SIGPWR]    = "Power failure",
    [SIGSYS]    = "Bad system call",
};

#define _SIG_NAMES_LEN (sizeof(_sig_names) / sizeof(_sig_names[0]))

static char _unk_buf[32];

char *strsignal(int signum) {
    if (signum >= 1 && (size_t)signum < _SIG_NAMES_LEN && _sig_names[signum])
        return (char *)_sig_names[signum];
    snprintf(_unk_buf, sizeof(_unk_buf), "Unknown signal %d", signum);
    return _unk_buf;
}

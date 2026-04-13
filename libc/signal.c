#include "signal.h"
#include "unistd.h"

/* kill(), getpid(), and sigprocmask() are provided by the assembly wrappers in syscall.S */

int raise(int sig) {
    return kill(getpid(), sig);
}

sighandler_t signal(int signum, sighandler_t handler) {
    struct sigaction act, oldact;
    act.sa_handler  = handler;
    act.sa_mask     = 0;
    act.sa_flags    = 0;
    act.sa_restorer = 0;
    if (sigaction(signum, &act, &oldact) < 0)
        return SIG_ERR;
    return oldact.sa_handler;
}

/* sigaction() itself is provided by the ecall stub in syscall.S (SYS_sigaction = 36).
 * Only SIG_DFL and SIG_IGN are accepted; custom function pointers return -EINVAL
 * because the kernel has no signal-return trampoline (BLOCKED-8 in TRAILBLAZE.md). */

/* sigset stubs — no kernel signal infrastructure */

int sigemptyset(sigset_t *set) {
    if (!set) return -1;
    *set = 0;
    return 0;
}

int sigfillset(sigset_t *set) {
    if (!set) return -1;
    *set = ~0UL;
    return 0;
}

int sigaddset(sigset_t *set, int signum) {
    if (!set || signum < 1 || signum > 31) return -1;
    *set |= (1UL << signum);
    return 0;
}

int sigdelset(sigset_t *set, int signum) {
    if (!set || signum < 1 || signum > 31) return -1;
    *set &= ~(1UL << signum);
    return 0;
}

int sigismember(const sigset_t *set, int signum) {
    if (!set || signum < 1 || signum > 31) return -1;
    return (*set & (1UL << signum)) ? 1 : 0;
}

/* sigpending() itself is provided by the ecall stub in syscall.S (SYS_sigpending = 37). */

int sigsuspend(const sigset_t *mask) {
    (void)mask;
    return -1;
}

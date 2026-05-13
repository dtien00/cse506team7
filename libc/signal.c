#include "signal.h"
#include "unistd.h"

/* kill(), getpid(), and sigprocmask() are provided by the assembly wrappers in syscall.S */

int raise(int sig) {
    return kill(getpid(), sig);
}

/*
 * sigaction() is provided directly by the ecall stub in libc/syscall.S
 * (SYS_sigaction = 38). The earlier libc-side wrapper that injected
 * sa_restorer / SA_RESTORER was removed — struct sigaction carries no
 * sa_restorer field. If custom handlers are wired through later, the
 * trampoline must be delivered via proc->sa_restorer (kernel-side write
 * in sys_sigaction), not a userspace sigaction field. See TCC_BB.md §13.
 */

sighandler_t signal(int signum, sighandler_t handler) {
    struct sigaction act, oldact;
    act.sa_handler   = handler;
    act.sa_mask      = 0;
    act.sa_flags     = 0;
    act.sa_sigaction = 0;
    if (sigaction(signum, &act, &oldact) < 0)
        return SIG_ERR;
    return oldact.sa_handler;
}

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

/* sigsuspend() itself is provided by the ecall stub in syscall.S (SYS_sigsuspend = 38). */

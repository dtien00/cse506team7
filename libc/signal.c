#include "signal.h"
#include "string.h"
#include "unistd.h"

/* kill(), getpid(), and sigprocmask() are provided by the assembly wrappers in syscall.S */

int raise(int sig) {
    return kill(getpid(), sig);
}

/* Raw ecall stub — passes arguments straight to the kernel without modification. */
int __sigaction_raw(int sig, const struct sigaction *act, struct sigaction *oldact);

/*
 * Sigreturn trampoline: called after a user-space signal handler returns.
 * Sets a7 = SYS_sigreturn (103) and issues ecall.  The kernel restores GP
 * registers, sepc, and signal_mask from the sigframe on the user stack.
 */
static void __sig_restorer(void) {
    register long a7 asm("a7") = 103;   /* SYS_sigreturn */
    asm volatile("ecall" : : "r"(a7) : "memory");
}

/*
 * sigaction() wrapper: injects __sig_restorer and SA_RESTORER for custom
 * handlers so the kernel can set frame->ra = restorer at dispatch time.
 * SIG_DFL and SIG_IGN pass through unmodified (no restorer needed).
 */
int sigaction(int sig, const struct sigaction *act, struct sigaction *oldact) {
    if (act != NULL && act->sa_handler != SIG_DFL && act->sa_handler != SIG_IGN) {
        struct sigaction kact  = *act;
        kact.sa_restorer       = __sig_restorer;
        kact.sa_flags         |= SA_RESTORER;
        return __sigaction_raw(sig, &kact, oldact);
    }
    return __sigaction_raw(sig, act, oldact);
}

sighandler_t signal(int signum, sighandler_t handler) {
    struct sigaction act, oldact;
    act.sa_handler   = handler;
    act.sa_mask      = 0;
    act.sa_flags     = 0;
    act.sa_sigaction = 0;
    act.sa_restorer  = NULL;
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

void psignal(int sig, const char *s) {
    const char *msg = strsignal(sig);
    if (s && *s) {
        write(2, s, strlen(s));
        write(2, ": ", 2);
    }
    write(2, msg, strlen(msg));
    write(2, "\n", 1);
}

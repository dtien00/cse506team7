#include "signal.h"
#include "unistd.h"

/* kill() and getpid() are provided by the assembly wrappers in syscall.S */

int raise(int sig) {
    return kill(getpid(), sig);
}

sighandler_t signal(int signum, sighandler_t handler) {
    (void)signum;
    (void)handler;
    return SIG_DFL;
}

int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact) {
    (void)signum;
    (void)act;
    (void)oldact;
    return -1;
}

/* sigset stubs — no kernel signal infrastructure */

int sigemptyset(sigset_t *set) {
    if (!set) return -1;
    set->sig[0] = 0;
    return 0;
}

int sigfillset(sigset_t *set) {
    if (!set) return -1;
    set->sig[0] = ~0UL;
    return 0;
}

int sigaddset(sigset_t *set, int signum) {
    if (!set || signum < 1 || signum > 31) return -1;
    set->sig[0] |= (1UL << signum);
    return 0;
}

int sigdelset(sigset_t *set, int signum) {
    if (!set || signum < 1 || signum > 31) return -1;
    set->sig[0] &= ~(1UL << signum);
    return 0;
}

int sigismember(const sigset_t *set, int signum) {
    if (!set || signum < 1 || signum > 31) return -1;
    return (set->sig[0] & (1UL << signum)) ? 1 : 0;
}

int sigprocmask(int how, const sigset_t *set, sigset_t *oldset) {
    (void)how;
    (void)set;
    (void)oldset;
    return 0;
}

int sigpending(sigset_t *set) {
    if (set) sigemptyset(set);
    return 0;
}

int sigsuspend(const sigset_t *mask) {
    (void)mask;
    return -1;
}

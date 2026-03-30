#include "signal.h"
#include "unistd.h"

#define SYS_kill 13

sighandler_t signal(int signum, sighandler_t handler) {
    struct sigaction new_act, old_act;
    new_act.sa_handler = handler;
    sigemptyset(&new_act.sa_mask);
    new_act.sa_flags = 0;
    new_act.sa_restorer = NULL;
    
    if (sigaction(signum, &new_act, &old_act) < 0) {
        return SIG_ERR;
    }
    return old_act.sa_handler;
}

int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact) {
    (void)signum;
    (void)act;
    (void)oldact;
    return -1;
}

int kill(pid_t pid, int sig) {
    register long a0 asm("a0") = pid;
    register long a1 asm("a1") = sig;
    register long a7 asm("a7") = SYS_kill;
    register long ret asm("a0");
    asm volatile("ecall" : "=r"(ret) : "r"(a0), "r"(a1), "r"(a7) : "memory");
    return (int)ret;
}

int raise(int sig) {
    return kill(getpid(), sig);
}

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
    if (!set || signum < 1 || signum > 64) return -1;
    set->sig[0] |= (1UL << (signum - 1));
    return 0;
}

int sigdelset(sigset_t *set, int signum) {
    if (!set || signum < 1 || signum > 64) return -1;
    set->sig[0] &= ~(1UL << (signum - 1));
    return 0;
}

int sigismember(const sigset_t *set, int signum) {
    if (!set || signum < 1 || signum > 64) return -1;
    return (set->sig[0] & (1UL << (signum - 1))) ? 1 : 0;
}

int sigprocmask(int how, const sigset_t *set, sigset_t *oldset) {
    (void)how;
    (void)set;
    (void)oldset;
    return -1;
}

int sigpending(sigset_t *set) {
    (void)set;
    return -1;
}

int sigsuspend(const sigset_t *mask) {
    (void)mask;
    return -1;
}

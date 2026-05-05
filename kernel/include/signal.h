#ifndef _SIGNAL_H
#define _SIGNAL_H

#include "sys/types.h"

struct timespec;  /* forward declaration for sigtimedwait */

typedef int           sig_atomic_t;
typedef void        (*sighandler_t)(int);

#define SIGHUP    1
#define SIGINT    2
#define SIGQUIT   3
#define SIGILL    4
#define SIGTRAP   5
#define SIGABRT   6
#define SIGIOT    SIGABRT
#define SIGBUS    7
#define SIGFPE    8
#define SIGKILL   9
#define SIGUSR1   10
#define SIGSEGV   11
#define SIGUSR2   12
#define SIGPIPE   13
#define SIGALRM   14
#define SIGTERM   15
#define SIGSTKFLT 16
#define SIGCHLD   17
#define SIGCONT   18
#define SIGSTOP   19
#define SIGTSTP   20
#define SIGTTIN   21
#define SIGTTOU   22
#define SIGURG    23
#define SIGXCPU   24
#define SIGXFSZ   25
#define SIGVTALRM 26
#define SIGPROF   27
#define SIGWINCH  28
#define SIGIO     29
#define SIGPOLL   SIGIO
#define SIGPWR    30
#define SIGSYS    31

#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)
#define SIG_ERR ((sighandler_t)-1)

#define SIG_BLOCK   0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

#define SA_NOCLDSTOP  0x00000001  /* don't send SIGCHLD when child stops */
#define SA_NOCLDWAIT  0x00000002  /* don't create zombie on child exit */
#define SA_SIGINFO    0x00000004  /* sa_sigaction takes (int,siginfo_t*,void*) -- BLOCKED */
#define SA_ONSTACK    0x08000000  /* use alternate signal stack -- BLOCKED */
#define SA_RESTART    0x10000000  /* restart syscall on signal return -- BLOCKED */
#define SA_NODEFER    0x40000000  /* don't mask this signal while handling it */
#define SA_NOMASK     SA_NODEFER
#define SA_RESETHAND  0x80000000  /* reset disposition to SIG_DFL after delivery */
#define SA_ONESHOT    SA_RESETHAND

/* Mask of flags this kernel actually honors; all others are accepted but silently ignored. */
#define SA_KNOWN_FLAGS  (SA_NOCLDSTOP | SA_NOCLDWAIT | SA_RESETHAND | SA_NODEFER)

typedef unsigned long sigset_t;

union sigval {
    int   sival_int;
    void *sival_ptr;
};

typedef struct {
    int          si_signo;
    int          si_code;
    pid_t        si_pid;
    uid_t        si_uid;
    void        *si_addr;
    int          si_status;
    union sigval si_value;
} siginfo_t;

struct sigaction {
    sighandler_t  sa_handler;
    sigset_t      sa_mask;
    int           sa_flags;
    void        (*sa_sigaction)(void);  /* kernel-internal; matches libc sa_sigaction slot */
};

/* Field order matches libc stack_t (ss_sp, ss_size, ss_flags) for ABI consistency. */
typedef struct {
    void  *ss_sp;
    size_t ss_size;
    int    ss_flags;
} stack_t;

int    kill(pid_t pid, int sig);
int    raise(int sig);
void (*signal(int, void (*)(int)))(int);
int    sigaction(int signum, const struct sigaction *act, struct sigaction *oldact);

int    sigemptyset(sigset_t *set);
int    sigfillset(sigset_t *set);
int    sigaddset(sigset_t *set, int signum);
int    sigdelset(sigset_t *set, int signum);
int    sigismember(const sigset_t *set, int signum);

int    sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
int    sigpending(sigset_t *set);
int    sigsuspend(const sigset_t *mask);

void   psiginfo(const siginfo_t *, const char *);
void   psignal(int, const char *);
int    pthread_sigmask(int, const sigset_t *restrict, sigset_t *restrict);
int    sig2str(int, char *);

int    sigqueue(pid_t, int, union sigval);
int    sigtimedwait(const sigset_t *restrict, siginfo_t *restrict,
                    const struct timespec *restrict);
int    sigwait(const sigset_t *restrict, int *restrict);
int    sigwaitinfo(const sigset_t *restrict, siginfo_t *restrict);
int    str2sig(const char *restrict, int *restrict);

#endif /* _SIGNAL_H */

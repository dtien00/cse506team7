#ifndef _SIGNAL_H
#define _SIGNAL_H

#include "sys/types.h"

struct timespec;  /* forward declaration for sigtimedwait */

/* ---------- primitive scalar typedefs ---------- */
typedef int           sig_atomic_t;
typedef void        (*sighandler_t)(int);
typedef unsigned long sigset_t;

/* ---------- signal numbers ---------- */
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

/* ---------- sa_flags values for struct sigaction ---------- */
#define SA_NOCLDSTOP  0x00000001  /* don't send SIGCHLD when child stops */
#define SA_NOCLDWAIT  0x00000002  /* don't create zombie on child exit */
#define SA_SIGINFO    0x00000004  /* sa_sigaction(int,siginfo_t*,void*) -- BLOCKED */
#define SA_ONSTACK    0x08000000  /* use alternate signal stack -- BLOCKED */
#define SA_RESTART    0x10000000  /* restart syscall on signal return -- BLOCKED */
#define SA_NODEFER    0x40000000  /* don't mask this signal while handling it */
#define SA_NOMASK     SA_NODEFER
#define SA_RESTORER   0x04000000  /* sa_restorer field is valid */
#define SA_RESETHAND  0x80000000  /* reset disposition to SIG_DFL after delivery */
#define SA_ONESHOT    SA_RESETHAND

/* ---------- signal disposition constants ---------- */
#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)
#define SIG_ERR ((sighandler_t)-1)

/* ---------- sigprocmask how values ---------- */
#define SIG_BLOCK   0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

/* ---------- sigevent notification types ---------- */
#define SIGEV_NONE   0
#define SIGEV_SIGNAL 1
#define SIGEV_THREAD 2

/* ---------- realtime signal range (stub — SBUnix has no RT signals) ---------- */
#define SIGRTMIN 32
#define SIGRTMAX 32

/* ---------- sig2str buffer size ---------- */
#define SIG2STR_MAX 32

/* ---------- composite types (dependency order) ---------- */

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

typedef struct {
    unsigned long __regs[32];  /* RISC-V GP registers + pc — opaque stub */
} mcontext_t;

typedef struct {
    void  *ss_sp;
    size_t ss_size;
    int    ss_flags;
} stack_t;

typedef struct ucontext {
    struct ucontext *uc_link;
    sigset_t         uc_sigmask;
    stack_t          uc_stack;
    mcontext_t       uc_mcontext;
} ucontext_t;

struct sigaction {
    void        (*sa_handler)(int);
    sigset_t     sa_mask;
    int          sa_flags;
    void        (*sa_sigaction)(int, siginfo_t *, void *);
    void        (*sa_restorer)(void);   /* set by sigaction() wrapper when SA_RESTORER */
};

struct sigevent {
    int          sigev_notify;
    int          sigev_signo;
    union sigval sigev_value;
    void       (*sigev_notify_function)(union sigval);
    pthread_attr_t *sigev_notify_attributes;
};

/* ---------- function declarations ---------- */

/* base */
int    raise(int);
void (*signal(int, void (*)(int)))(int);

/* CX */
int    kill(pid_t, int);
void   psiginfo(const siginfo_t *, const char *);
void   psignal(int, const char *);
int    pthread_kill(pthread_t, int);
int    pthread_sigmask(int, const sigset_t *restrict, sigset_t *restrict);
int    sig2str(int, char *);
int    sigaction(int, const struct sigaction *restrict, struct sigaction *restrict);
int    sigaddset(sigset_t *, int);
int    sigdelset(sigset_t *, int);
int    sigemptyset(sigset_t *);
int    sigfillset(sigset_t *);
int    sigismember(const sigset_t *, int);
int    sigpending(sigset_t *);
int    sigprocmask(int, const sigset_t *restrict, sigset_t *restrict);
int    sigqueue(pid_t, int, union sigval);
int    sigsuspend(const sigset_t *);
int    sigtimedwait(const sigset_t *restrict, siginfo_t *restrict,
                    const struct timespec *restrict);
int    sigwait(const sigset_t *restrict, int *restrict);
int    sigwaitinfo(const sigset_t *restrict, siginfo_t *restrict);
int    str2sig(const char *restrict, int *restrict);

#endif /* _SIGNAL_H */

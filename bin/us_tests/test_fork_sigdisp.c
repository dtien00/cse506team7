/*
 * test_fork_sigdisp.c — Fork inherits complete sigaction disposition
 *
 * POSIX XSH fork: "signal dispositions … shall be shared" — the child must
 * inherit not only the handler pointer and sa_flags but also sa_mask and the
 * libc-injected sa_restorer trampoline address.
 *
 * Sections:
 *   A. Full handler round-trip in child — tests sa_restorer inheritance:
 *      parent installs SIGUSR1 handler, forks, child raises SIGUSR1, verifies
 *      the handler ran and the child returned cleanly (not killed by SIGSEGV).
 *   B. sa_mask inherited across fork — tests sa_masks[] inheritance:
 *      SIGUSR1 handler has sa_mask={SIGUSR2}; inside the handler the process
 *      raises SIGUSR2; with correct mask inheritance SIGUSR2 is deferred until
 *      after the handler returns (SIGUSR2 handler then sets g_flag2).
 */

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include "us_test_common.h"

static volatile int g_flag;
static volatile int g_flag2;

static void handler_A(int sig) { (void)sig; g_flag = 1; }

static void handler_B1(int sig) {
    (void)sig;
    kill(getpid(), SIGUSR2);  /* SIGUSR2 in sa_mask — must not preempt us */
    g_flag = (g_flag2 == 0) ? 1 : -1;  /* 1=good (deferred), -1=reentered (bad) */
}

static void handler_B2(int sig) { (void)sig; g_flag2 = 1; }

/* ── child helpers ─────────────────────────────────────────────────────── */

static void child_A(void) {
    g_flag = 0;
    raise(SIGUSR1);
    _exit(g_flag == 1 ? 0 : 1);
}

static void child_B(void) {
    g_flag  = 0;
    g_flag2 = 0;
    raise(SIGUSR1);
    /* After raise() returns:
     *   handler_B1 ran (g_flag=1), raised SIGUSR2 which was deferred by sa_mask.
     *   At sigreturn the mask is restored; SIGUSR2 fires at the next ecall. */
    getpid();  /* delivery point for deferred SIGUSR2 */
    _exit((g_flag == 1 && g_flag2 == 1) ? 0 : 2);
}

/* ── helper: fork, run child fn, collect status ─────────────────────────── */

static int fork_and_collect(void (*child_fn)(void)) {
    pid_t pid = fork();
    if (pid == 0) {
        child_fn();
        __builtin_unreachable();
    }
    if (pid < 0) return -1;
    int status = 0;
    waitpid(pid, &status, 0);
    return status;
}

/* ── test entry point ───────────────────────────────────────────────────── */

void run_test_fork_sigdisp(us_test_group_t *group) {
    ut_group_begin(group, "fork-sigdisp");

    /* ── §A: full handler round-trip (sa_restorer inherited) ──────────────── */
    {
        struct sigaction sa = { .sa_handler = handler_A, .sa_flags = SA_RESTART };
        sigemptyset(&sa.sa_mask);
        sigaddset(&sa.sa_mask, SIGUSR2);
        sigaction(SIGUSR1, &sa, NULL);

        int status = fork_and_collect(child_A);
        if (status == -1) {
            SKIP(group, "fork-sigdisp §A: child exits cleanly (sa_restorer inherited)", "fork failed");
            SKIP(group, "fork-sigdisp §A: handler ran in child", "fork failed");
        } else {
            TEST(group, "fork-sigdisp §A: child exits cleanly (sa_restorer inherited)",
                 WIFEXITED(status));
            TEST(group, "fork-sigdisp §A: handler ran in child",
                 WIFEXITED(status) && WEXITSTATUS(status) == 0);
        }

        struct sigaction dfl = { .sa_handler = SIG_DFL };
        sigaction(SIGUSR1, &dfl, NULL);
    }

    /* ── §B: sa_mask defers SIGUSR2 during SIGUSR1 handler (sa_masks inherited) */
    {
        struct sigaction sa1 = { .sa_handler = handler_B1, .sa_flags = 0 };
        sigemptyset(&sa1.sa_mask);
        sigaddset(&sa1.sa_mask, SIGUSR2);
        sigaction(SIGUSR1, &sa1, NULL);

        struct sigaction sa2 = { .sa_handler = handler_B2, .sa_flags = 0 };
        sigemptyset(&sa2.sa_mask);
        sigaction(SIGUSR2, &sa2, NULL);

        int status = fork_and_collect(child_B);
        if (status == -1) {
            SKIP(group, "fork-sigdisp §B: sa_mask inherited — SIGUSR2 deferred during handler", "fork failed");
        } else {
            TEST(group, "fork-sigdisp §B: sa_mask inherited — SIGUSR2 deferred during handler",
                 WIFEXITED(status) && WEXITSTATUS(status) == 0);
        }

        struct sigaction dfl = { .sa_handler = SIG_DFL };
        sigaction(SIGUSR1, &dfl, NULL);
        sigaction(SIGUSR2, &dfl, NULL);
    }

    ut_group_end(group);
}

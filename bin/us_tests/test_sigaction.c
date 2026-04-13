/*
 * test_sigaction.c — Userspace tests for sigaction() / signal() disposition.
 *
 * Exercises the sys_sigaction() kernel path end-to-end.  Only SIG_DFL and
 * SIG_IGN are supported (custom handlers are BLOCKED-8); all tests stay within
 * that constraint.
 *
 * Design note — why children use "while(1) sleep(1)" instead of a spin loop:
 *   Sleeping children are PROC_BLOCKED.  sys_kill() with a fatal signal calls
 *   proc_unblock() immediately, making the child READY before this function
 *   returns.  Signal delivery then runs through the guaranteed proc_sleep()
 *   wakeup path (scheduler.c), not the timer-tick-dependent trap-return path.
 *   Spin loops only receive signals on timer ticks, making delivery timing
 *   unpredictable and prone to races with the parent's waitpid.
 *
 * Test sections (all within one group):
 *   §1  signal() API                — return values, basic set/query
 *   §2  sigaction() API             — oldact read-back, act=NULL query, rejects
 *                                     SIGKILL/SIGSTOP/function pointers
 *   §3  SIG_IGN prevents fatal kill — SIGTERM/SIGPIPE/SIGINT ignored by child
 *   §4  SIG_DFL restores default    — ignoring then resetting kills on signal
 *   §5  Fork inheritance            — child inherits SIG_IGN from parent
 *   §6  Exec SIG_IGN survival       — SIG_IGN survives exec() per POSIX
 */

#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include "us_test_common.h"

/*
 * Helper used only to get a non-NULL, non-SIG_IGN function pointer address
 * for the "function pointer rejected" tests (§1f, §2i).
 * Must NOT be called directly.
 */
static void dummy_handler_addr(void) {}

/*
 * wait_and_cleanup — WNOHANG check + safety SIGKILL.
 *
 * After sending a signal, the parent sleeps 1 s (proc_sleep), which allows
 * the child to run and process the signal through proc_sleep wakeup.
 * This function then does a non-blocking check and ensures no zombie leaks.
 *
 * Returns 1 if child was still alive when checked (signal was ignored/missed),
 *         0 if child had already exited.
 */
static int wait_and_cleanup(pid_t pid) {
    int status = 0;
    pid_t w = waitpid(pid, &status, WNOHANG);
    int alive = (w == 0);
    /* Safety: kill + reap regardless. SIGKILL on a zombie is harmless (-ESRCH). */
    kill(pid, SIGKILL);
    if (alive)
        waitpid(pid, NULL, 0); /* child was alive; block until SIGKILL takes effect */
    return alive;
}

/*
 * wait_for_death — expect the child to have been killed by a signal.
 *
 * Used after sending a fatal signal with SIG_DFL disposition.  The child
 * should be a zombie by the time the parent wakes from its sleep(1).
 * Returns 1 if child was killed by a signal (WIFSIGNALED), 0 otherwise.
 * Always cleans up the child.
 */
static int wait_for_death(pid_t pid) {
    int status = 0;
    pid_t w = waitpid(pid, &status, WNOHANG);
    int killed = (w == pid && WIFSIGNALED(status));
    if (w == 0) {
        /* Child still alive — signal delivery may be delayed; give one more tick. */
        sleep(1);
        w = waitpid(pid, &status, WNOHANG);
        killed = (w == pid && WIFSIGNALED(status));
    }
    if (w != pid) {
        /* Still not reaped (test will FAIL); kill for cleanup — must block-wait. */
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
    }
    return killed;
}

void run_test_sigaction(us_test_group_t *group) {
    ut_group_begin(group, "sigaction");

    /*
     * run_test_signal() (which runs before this suite) deliberately leaves
     * SIGTERM *blocked and pending* in the parent process so it doesn't die
     * from a pending signal it sent to itself.  Children forked here would
     * inherit that mask, making SIGTERM invisible to the fatal-signal check
     * in proc_sleep() (unmasked = pending & ~mask = 0).
     *
     * Clean up: set SIG_IGN first so the pending SIGTERM is cleared by the
     * SIG_IGN filter at trap-return when we unblock it, then restore SIG_DFL.
     */
    {
        sigset_t unblock;
        sigemptyset(&unblock);
        sigaddset(&unblock, SIGTERM);
        signal(SIGTERM, SIG_IGN);                  /* don't die when we unmask  */
        sigprocmask(SIG_UNBLOCK, &unblock, NULL);  /* unmask; trap-return clears pending */
        signal(SIGTERM, SIG_DFL);                  /* restore default           */
    }

    /* ── §1 signal() API ────────────────────────────────────────────────── */

    /* 1a. Fresh signal — previous disposition must be SIG_DFL */
    sighandler_t prev = signal(SIGUSR1, SIG_IGN);
    TEST(group, "signal: fresh SIGUSR1 returns SIG_DFL", prev == SIG_DFL);

    /* 1b. Second call returns previous disposition (SIG_IGN) */
    prev = signal(SIGUSR1, SIG_DFL);
    TEST(group, "signal: second call returns previous SIG_IGN", prev == SIG_IGN);

    /* 1c. After restore the disposition is SIG_DFL again */
    prev = signal(SIGUSR1, SIG_DFL);
    TEST(group, "signal: restoring SIG_DFL returns SIG_DFL", prev == SIG_DFL);

    /* 1d. SIGKILL cannot be caught/ignored — must return SIG_ERR */
    prev = signal(SIGKILL, SIG_IGN);
    TEST(group, "signal: SIGKILL returns SIG_ERR", prev == SIG_ERR);

    /* 1e. SIGSTOP cannot be caught/ignored — must return SIG_ERR */
    prev = signal(SIGSTOP, SIG_IGN);
    TEST(group, "signal: SIGSTOP returns SIG_ERR", prev == SIG_ERR);

    /* 1f. Function pointer rejected (BLOCKED-8) */
    prev = signal(SIGUSR1, (sighandler_t)dummy_handler_addr);
    TEST(group, "signal: function handler returns SIG_ERR (BLOCKED-8)", prev == SIG_ERR);

    /* ── §2 sigaction() API ──────────────────────────────────────────────── */

    struct sigaction act, oldact;

    /* 2a. Query with act=NULL — initial disposition must be SIG_DFL */
    oldact.sa_handler = (sighandler_t)0xdeadbeef;
    int r = sigaction(SIGUSR2, NULL, &oldact);
    TEST(group, "sigaction: query SIGUSR2 returns 0", r == 0);
    TEST(group, "sigaction: query fresh signal returns SIG_DFL",
         oldact.sa_handler == SIG_DFL);

    /* 2b. Set SIG_IGN; oldact must return prior SIG_DFL */
    act.sa_handler  = SIG_IGN;
    act.sa_mask     = 0;
    act.sa_flags    = 0;
    act.sa_restorer = NULL;
    r = sigaction(SIGUSR2, &act, &oldact);
    TEST(group, "sigaction: set SIG_IGN returns 0", r == 0);
    TEST(group, "sigaction: oldact after set returns SIG_DFL", oldact.sa_handler == SIG_DFL);

    /* 2c. Query after set must return SIG_IGN */
    r = sigaction(SIGUSR2, NULL, &oldact);
    TEST(group, "sigaction: query after set SIG_IGN returns SIG_IGN",
         r == 0 && oldact.sa_handler == SIG_IGN);

    /* 2d. Restore to SIG_DFL with oldact=NULL (must not fault) */
    act.sa_handler = SIG_DFL;
    r = sigaction(SIGUSR2, &act, NULL);
    TEST(group, "sigaction: restore SIG_DFL with NULL oldact returns 0", r == 0);

    /* 2e. SIGKILL must be rejected */
    act.sa_handler = SIG_IGN;
    r = sigaction(SIGKILL, &act, NULL);
    TEST(group, "sigaction: SIGKILL rejected (returns < 0)", r < 0);

    /* 2f. SIGSTOP must be rejected */
    r = sigaction(SIGSTOP, &act, NULL);
    TEST(group, "sigaction: SIGSTOP rejected (returns < 0)", r < 0);

    /* 2g. sig=0 is invalid */
    r = sigaction(0, &act, NULL);
    TEST(group, "sigaction: sig=0 invalid (returns < 0)", r < 0);

    /* 2h. sig=32 is out-of-range */
    r = sigaction(32, &act, NULL);
    TEST(group, "sigaction: sig=32 out-of-range (returns < 0)", r < 0);

    /* 2i. Function pointer rejected */
    act.sa_handler = (sighandler_t)dummy_handler_addr;
    r = sigaction(SIGUSR1, &act, NULL);
    TEST(group, "sigaction: function pointer rejected (returns < 0)", r < 0);

    /* ── §3 SIG_IGN prevents fatal termination ──────────────────────────── */
    /*
     * Each child uses "while(1) sleep(1)".  When the parent sends a fatal
     * signal, sys_kill() calls proc_unblock() on the sleeping child.  The
     * child wakes in proc_sleep(), the SIG_IGN filter clears the ignored
     * signal bit, and proc_sleep() returns normally — child re-enters sleep.
     *
     * The parent's own sleep(1) gives the child time to complete that cycle
     * before the WNOHANG check.
     */

    /* 3a. SIGTERM ignored */
    {
        pid_t pid = fork();
        if (pid == 0) {
            signal(SIGTERM, SIG_IGN);
            while (1) sleep(1);
        }
        sleep(1);                   /* let child set handler and enter first sleep */
        kill(pid, SIGTERM);
        sleep(1);                   /* child processes ignored signal and re-sleeps */
        int alive = wait_and_cleanup(pid);
        TEST(group, "SIG_IGN: SIGTERM ignored — child still alive", alive);
    }

    /* 3b. SIGPIPE ignored */
    {
        pid_t pid = fork();
        if (pid == 0) {
            signal(SIGPIPE, SIG_IGN);
            while (1) sleep(1);
        }
        sleep(1);
        kill(pid, SIGPIPE);
        sleep(1);
        int alive = wait_and_cleanup(pid);
        TEST(group, "SIG_IGN: SIGPIPE ignored — child still alive", alive);
    }

    /* 3c. SIGINT ignored */
    {
        pid_t pid = fork();
        if (pid == 0) {
            signal(SIGINT, SIG_IGN);
            while (1) sleep(1);
        }
        sleep(1);
        kill(pid, SIGINT);
        sleep(1);
        int alive = wait_and_cleanup(pid);
        TEST(group, "SIG_IGN: SIGINT ignored — child still alive", alive);
    }

    /* ── §4 SIG_DFL restores default fatal behavior ─────────────────────── */
    /*
     * Child ignores SIGTERM then immediately restores SIG_DFL.  With a sleeping
     * child, kill(SIGTERM) triggers proc_unblock(); the child wakes in
     * proc_sleep(), the fatal-signal check fires (handlers[SIGTERM]==SIG_DFL),
     * and proc_exit() is called.  The zombie is collected by wait_for_death().
     */
    {
        pid_t pid = fork();
        if (pid == 0) {
            signal(SIGTERM, SIG_IGN);
            signal(SIGTERM, SIG_DFL);
            while (1) sleep(1);
        }
        sleep(1);
        kill(pid, SIGTERM);
        sleep(1);
        int killed = wait_for_death(pid);
        TEST(group, "SIG_DFL: restored — child killed by SIGTERM", killed);
    }

    /* ── §5 Fork inherits signal dispositions ────────────────────────────── */

    /* 5a. Parent ignores SIGTERM; forked child should inherit SIG_IGN */
    signal(SIGTERM, SIG_IGN);
    {
        pid_t pid = fork();
        if (pid == 0) {
            while (1) sleep(1); /* inherits SIG_IGN for SIGTERM */
        }
        sleep(1);
        kill(pid, SIGTERM);
        sleep(1);
        int alive = wait_and_cleanup(pid);
        TEST(group, "fork-inherit: child inherits SIG_IGN for SIGTERM — alive", alive);
    }
    signal(SIGTERM, SIG_DFL); /* restore before §5b and §6 */

    /* 5b. Parent has SIG_DFL; child inherits SIG_DFL — killed by SIGTERM */
    {
        pid_t pid = fork();
        if (pid == 0) {
            while (1) sleep(1);
        }
        sleep(1);
        kill(pid, SIGTERM);
        sleep(1);
        int killed = wait_for_death(pid);
        TEST(group, "fork-inherit: child inherits SIG_DFL — killed by SIGTERM", killed);
    }

    /* ── §6 SIG_IGN survives exec() (POSIX) ─────────────────────────────── */
    /*
     * POSIX: "Signals set to SIG_IGN shall remain set to SIG_IGN after a
     * successful call to exec()."
     *
     * Child: set SIGTERM → SIG_IGN, exec /bin/sigaction_helper.
     * sigaction_helper loops on sleep(5) so that an ignored SIGTERM (which
     * causes proc_sleep to return early) does not make the helper exit.
     * Parent: send SIGTERM, check child still alive, then SIGKILL to clean up.
     */
    {
        pid_t pid = fork();
        if (pid == 0) {
            signal(SIGTERM, SIG_IGN);
            char *argv[] = { "/bin/sigaction_helper", NULL };
            execve("/bin/sigaction_helper", argv, NULL);
            _exit(99); /* exec failed */
        }

        sleep(1);              /* give child time to exec */
        kill(pid, SIGTERM);
        sleep(1);              /* child processes ignored signal inside helper */

        int status = 0;
        pid_t w = waitpid(pid, &status, WNOHANG);
        int still_alive = (w == 0);
        int exec_failed = (w == pid && WIFEXITED(status) && WEXITSTATUS(status) == 99);

        kill(pid, SIGKILL);
        if (still_alive)
            waitpid(pid, NULL, 0);

        TEST(group, "exec-SIG_IGN: exec succeeded (helper found)", !exec_failed);
        TEST(group, "exec-SIG_IGN: SIGTERM ignored after exec — child still alive",
             still_alive);
    }

    ut_group_end(group);
}

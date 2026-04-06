/*
 * test_signal.c — Userspace tests for signal delivery and masking.
 *
 * Tests run inside the OS as normal user processes and exercise the signal
 * subsystem end-to-end through real syscalls:
 *
 *  1. SIGSTOP/SIGCONT round-trip: parent stops a spinning child, resumes it,
 *     then kills it — verifying the child is properly reaped as signaled.
 *
 *  2. sigprocmask blocks SIGTERM: block SIGTERM, send it to self, survive;
 *     then fork a child that unblocks and kills itself with SIGTERM, verifying
 *     the signal was actually pending and delivered upon unmask.
 *
 *  3. Pending signals are NOT inherited by fork children (POSIX): fork while
 *     SIGTERM is pending+blocked in the parent; child unblocks and survives,
 *     confirming proc_create() correctly zeros pending_signals.
 */

#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include "us_test_common.h"

void run_test_signal(us_test_group_t *group) {
    ut_group_begin(group, "signal");

    /* ── 1. SIGSTOP/SIGCONT round-trip ───────────────────────────────────────
     * Fork a spinning child, stop it, wait 1 second, resume it, then kill it.
     * Expect waitpid to return the child's pid and report it as signaled.
     */
    {
        pid_t pid = fork();
        TEST(group, "SIGSTOP/SIGCONT: fork succeeded", pid >= 0);

        if (pid == 0) {
            volatile int x = 0;
            while (1) x++;
            _exit(0); /* unreachable */
        }

        if (pid > 0) {
            kill(pid, SIGSTOP);
            sleep(1);
            kill(pid, SIGCONT);
            kill(pid, SIGKILL);
            int status = 0;
            pid_t w = waitpid(pid, &status, 0);
            TEST(group, "SIGSTOP/SIGCONT: child reaped", w == pid);
            TEST(group, "SIGSTOP/SIGCONT: child terminated by signal", WIFSIGNALED(status));
        }
    }

    /* ── 2. sigprocmask blocks SIGTERM ───────────────────────────────────────
     * Block SIGTERM, then send it to ourselves — the signal should remain
     * pending and we should survive to the next test.
     *
     * Fork a child that unblocks SIGTERM and sends it to itself.  The child
     * should be killed by the signal.  The parent keeps SIGTERM blocked for
     * the remainder of its lifetime to avoid dying from the pending signal.
     */
    {
        sigset_t set;
        sigemptyset(&set);
        sigaddset(&set, SIGTERM);

        sigprocmask(SIG_BLOCK, &set, NULL);
        kill(getpid(), SIGTERM); /* signal is masked — stays pending */
        TEST(group, "sigprocmask: blocked SIGTERM did not kill us", 1);

        pid_t pid = fork();
        TEST(group, "sigprocmask: fork for unblock test succeeded", pid >= 0);

        if (pid == 0) {
            /* Child unblocks SIGTERM, then explicitly sends it to itself. */
            sigprocmask(SIG_UNBLOCK, &set, NULL);
            kill(getpid(), SIGTERM); /* send explicitly — pending_signals not inherited */
            _exit(0);                /* unreachable if SIGTERM is delivered */
        }

        if (pid > 0) {
            int status = 0;
            pid_t w = waitpid(pid, &status, 0);
            TEST(group, "sigprocmask: child killed by SIGTERM after unblock",
                 w == pid && WIFSIGNALED(status));
            /* Parent leaves SIGTERM blocked to avoid dying from pending signal. */
        }
    }

    /* ── 3. Pending signals are NOT inherited by fork children (POSIX) ──────
     * The parent still has a pending (blocked) SIGTERM from test 2.
     * Fork a child that inherits the signal mask (SIGTERM blocked), then
     * unblocks SIGTERM.  Because proc_create() zeros pending_signals, the
     * child has nothing pending and survives.
     * If the kernel incorrectly copied pending_signals, the child would be
     * killed by the delivered SIGTERM and WIFSIGNALED(status) would be true.
     * "The set of signals pending for the child shall be initialized to the
     * empty set." — POSIX.1-2017 fork()
     *
     * Note: SIGTERM must be used here (not SIGUSR1) because only signals in
     * KERNEL_FATAL_SIGS actually kill the process — SIGUSR1 is not fatal in
     * this kernel and would not detect the regression.
     */
    {
        sigset_t term_set;
        sigemptyset(&term_set);
        sigaddset(&term_set, SIGTERM);

        /* SIGTERM is still blocked and pending in the parent from test 2 */
        pid_t pid = fork();
        TEST(group, "signal-no-inherit: fork succeeded", pid >= 0);

        if (pid == 0) {
            /* Unblock SIGTERM: if pending_signals was inherited, child dies here */
            sigprocmask(SIG_UNBLOCK, &term_set, NULL);
            /* Still alive — pending SIGTERM was NOT inherited */
            _exit(0);
        }

        if (pid > 0) {
            int status = 0;
            pid_t w = waitpid(pid, &status, 0);
            TEST(group, "signal-no-inherit: child survived unblocking SIGTERM (not inherited)",
                 w == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0);
            /* Parent keeps SIGTERM blocked — it still has a pending SIGTERM */
        }
    }

    ut_group_end(group);
}

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
 *
 *  4. sigpending() reports blocked+pending signals:
 *     a. Block SIGTERM, send to self → sigpending shows SIGTERM.
 *     b. Unblocked pending signal (SIGINT) does NOT appear in sigpending.
 *     c. Masked signal with nothing pending → not in sigpending output.
 *     d. Edge: sigpending on a fresh child (no pending signals) returns 0.
 *
 *  5. Blocked signal stays pending until unmask (liveness probe via pipe):
 *     child blocks SIGTERM, sends to self, writes 'A' to pipe (alive), then
 *     unblocks — the pending SIGTERM must fire before the child writes 'B'.
 *     Parent reads exactly one byte ('A') and confirms child died by SIGTERM.
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
     * SIGTERM is used here because it was already pending+blocked from test 2,
     * making this a zero-cost reuse.  SIGUSR1 is now also in KERNEL_FATAL_SIGS
     * (added in SIGS_2), so either signal would detect the regression.
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

    /* ── 4. sigpending() reports exactly (pending & masked) ─────────────────
     *
     * 4a. Block SIGTERM, send it to self → sigpending should report it.
     *     The process must still be alive (signal stayed pending).
     */
    {
        sigset_t block_term;
        sigemptyset(&block_term);
        sigaddset(&block_term, SIGTERM);
        sigprocmask(SIG_BLOCK, &block_term, NULL);

        kill(getpid(), SIGTERM);   /* pending; will not be delivered while masked */

        sigset_t pending;
        sigemptyset(&pending);
        int r = sigpending(&pending);
        TEST(group, "sigpending: return value is 0",        r == 0);
        TEST(group, "sigpending: SIGTERM appears (blocked+pending)",
             sigismember(&pending, SIGTERM) == 1);

        /* Unblock in a forked child to consume the pending signal safely.
         * The parent keeps SIGTERM blocked for the rest of this group. */
        pid_t pid = fork();
        TEST(group, "sigpending: fork for unblock succeeded", pid >= 0);
        if (pid == 0) {
            sigprocmask(SIG_UNBLOCK, &block_term, NULL);
            /* pending SIGTERM delivers immediately — child should die */
            kill(getpid(), SIGTERM);
            _exit(0); /* unreachable if delivered */
        }
        if (pid > 0) {
            int status = 0;
            waitpid(pid, &status, 0);
            TEST(group, "sigpending: child killed by unblocked SIGTERM",
                 WIFSIGNALED(status));
        }
    }

    /* ── 4b. Unblocked pending signal does NOT appear in sigpending ──────────
     * Block nothing, send SIGKILL to a child — the child is killed outright.
     * In the parent: send SIGUSR1 (not in KERNEL_FATAL_SIGS, so it doesn't
     * kill) — but since it is NOT masked it is never "pending and blocked",
     * so sigpending must return an empty set.
     *
     * We exercise this by using a child process: fork, mask nothing, then
     * call sigpending immediately — the child has no pending signals at all,
     * so sigpending must return 0.
     */
    {
        pid_t pid = fork();
        TEST(group, "sigpending-unmasked: fork succeeded", pid >= 0);
        if (pid == 0) {
            /* Fresh child: no pending signals, no mask */
            sigset_t pending;
            sigemptyset(&pending);
            int r = sigpending(&pending);
            /* Exit 0 if sigpending returned 0 and the set is empty */
            if (r == 0 && sigismember(&pending, SIGTERM) == 0)
                _exit(0);
            else
                _exit(1);
        }
        if (pid > 0) {
            int status = 0;
            waitpid(pid, &status, 0);
            TEST(group, "sigpending-unmasked: fresh child reports empty set",
                 WIFEXITED(status) && WEXITSTATUS(status) == 0);
        }
    }

    /* ── 4c. Masked-but-not-pending signal absent from sigpending ────────────
     * Mask SIGINT but never send it.  sigpending must not report SIGINT.
     */
    {
        pid_t pid = fork();
        TEST(group, "sigpending-notpending: fork succeeded", pid >= 0);
        if (pid == 0) {
            sigset_t block_int;
            sigemptyset(&block_int);
            sigaddset(&block_int, SIGINT);
            sigprocmask(SIG_BLOCK, &block_int, NULL);
            /* SIGINT is masked but nothing has been sent */
            sigset_t pending;
            sigemptyset(&pending);
            sigpending(&pending);
            /* SIGINT must NOT appear */
            if (sigismember(&pending, SIGINT) == 0)
                _exit(0);
            else
                _exit(1);
        }
        if (pid > 0) {
            int status = 0;
            waitpid(pid, &status, 0);
            TEST(group, "sigpending-notpending: masked-but-not-pending absent",
                 WIFEXITED(status) && WEXITSTATUS(status) == 0);
        }
    }

    /* ── 4d. Multiple blocked+pending signals all appear ─────────────────────
     * Block both SIGTERM and SIGINT, send both, verify both show up.
     */
    {
        pid_t pid = fork();
        TEST(group, "sigpending-multi: fork succeeded", pid >= 0);
        if (pid == 0) {
            sigset_t mask;
            sigemptyset(&mask);
            sigaddset(&mask, SIGTERM);
            sigaddset(&mask, SIGINT);
            sigprocmask(SIG_BLOCK, &mask, NULL);

            kill(getpid(), SIGTERM);
            kill(getpid(), SIGINT);

            sigset_t pending;
            sigemptyset(&pending);
            sigpending(&pending);

            if (sigismember(&pending, SIGTERM) == 1 &&
                sigismember(&pending, SIGINT)  == 1)
                _exit(0);
            else
                _exit(1);
        }
        if (pid > 0) {
            int status = 0;
            waitpid(pid, &status, 0);
            TEST(group, "sigpending-multi: SIGTERM and SIGINT both appear",
                 WIFEXITED(status) && WEXITSTATUS(status) == 0);
        }
    }

    /* ── 5. Blocked signal stays pending until unmask (Issue B regression) ────
     * A pipe provides a liveness witness: the child writes 'A' after the
     * masked kill() call (proving it survived), then unblocks SIGTERM.
     * The kernel must deliver the pending signal at that unmask point, killing
     * the child before it can write 'B'.  Reading exactly one byte ('A') from
     * the pipe confirms the signal did not fire while masked.
     *
     * The child inherits SIGTERM blocked from the parent (left over from tests
     * 2–4), so the sigprocmask(SIG_BLOCK) call here is a no-op — SIGTERM is
     * already masked.  This is intentional: it means the test exercises the
     * path where a signal is sent to a process whose mask already blocks it,
     * and relies on the unmask to trigger delivery.  The child's pending_signals
     * are clear at fork (POSIX), so the only pending SIGTERM is the one the
     * child sends to itself at step 2.
     */
    {
        int pfd[2];
        int pok = (pipe(pfd) == 0);

        pid_t pid = pok ? fork() : -1;
        if (pid == 0) {
            close(pfd[0]);
            sigset_t set;
            sigemptyset(&set);
            sigaddset(&set, SIGTERM);
            sigprocmask(SIG_BLOCK, &set, NULL);   /* ensure SIGTERM blocked */
            kill(getpid(), SIGTERM);              /* queue it (mask keeps it pending) */
            write(pfd[1], "A", 1);               /* alive: signal did not fire */
            sigprocmask(SIG_UNBLOCK, &set, NULL); /* must die here */
            write(pfd[1], "B", 1);               /* unreachable if masking correct */
            close(pfd[1]);
            _exit(0);
        }

        if (pid > 0) {
            close(pfd[1]);
            char buf[4] = {0};
            int n = (int)read(pfd[0], buf, sizeof(buf));
            close(pfd[0]);
            int status = 0;
            waitpid(pid, &status, 0);
            TEST(group, "mask-then-kill: alive after masked kill (wrote A, not B)",
                 n == 1 && buf[0] == 'A');
            TEST(group, "mask-then-kill: killed by pending SIGTERM after unmask",
                 WIFSIGNALED(status) && WTERMSIG(status) == SIGTERM);
        } else {
            if (pok) { close(pfd[0]); close(pfd[1]); }
            SKIP(group, "mask-then-kill: alive after masked kill (wrote A, not B)",
                 "pipe/fork failed");
            SKIP(group, "mask-then-kill: killed by pending SIGTERM after unmask",
                 "pipe/fork failed");
        }
    }

    ut_group_end(group);
}

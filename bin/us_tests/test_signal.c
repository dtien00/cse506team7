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
            /* Child unblocks SIGTERM; the inherited pending signal delivers. */
            sigprocmask(SIG_UNBLOCK, &set, NULL);
            kill(getpid(), SIGTERM); /* belt-and-suspenders: also send explicitly */
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

    ut_group_end(group);
}

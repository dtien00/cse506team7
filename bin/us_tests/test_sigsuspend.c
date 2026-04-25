/*
 * test_sigsuspend.c — userspace tests for sigsuspend()
 *
 * Tests are ordered from simple to complex:
 *   T1  Basic blocking: sigsuspend blocks until a fatal signal terminates the child.
 *   T2  Mask filtering: sigsuspend masks SIGTERM so it does not wake the child;
 *       SIGKILL (unmaskable) terminates it.
 *   T3  Already-pending fatal signal: SIGTERM is pending (blocked by sigprocmask)
 *       before sigsuspend is called; sigsuspend must not block forever — it must
 *       deliver the signal immediately and terminate the child.
 *   T4  Already-pending non-fatal signal: SIGURG is pending before sigsuspend;
 *       sigsuspend returns EINTR without hanging, and the child survives.
 *   T5  Mask restoration: the original signal mask is reinstated after sigsuspend
 *       returns so that a previously-unblocked signal can still kill the process.
 *
 * Signal numbers used (kernel/include/process/process.h):
 *   SIGURG  = 23 — not in KERNEL_FATAL_SIGS, KERNEL_STOP_SIGS, or
 *                  KERNEL_DFL_IGNORE_SIGS; wakes sigsuspend without killing.
 *   SIGUSR1 = 10, SIGUSR2 = 12 — IN KERNEL_FATAL_SIGS (added in SIGS_2); fatal.
 *   SIGTERM = 15, SIGKILL = 9  — in KERNEL_FATAL_SIGS; terminate the process.
 *
 * Timer resolution is 0.1 s (TICKS_PER_SEC = 10).  sleep(1) is the minimum
 * reliable inter-process delay; tests use sleep(1) between send and check.
 */

#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include "us_test_common.h"

void run_test_sigsuspend(us_test_group_t *group) {
    ut_group_begin(group, "sigsuspend");

    /* ------------------------------------------------------------------
     * T1: sigsuspend blocks until a fatal signal is delivered.
     *
     * Child:  block all signals (empty new_mask = 0 means nothing blocked —
     *         wait, we want to accept everything), call sigsuspend(empty).
     * Parent: wait 1 s, send SIGTERM.
     * Expect: child is killed by SIGTERM (WIFSIGNALED, WTERMSIG == SIGTERM).
     * ------------------------------------------------------------------ */
    {
        pid_t pid = fork();
        if (pid < 0) {
            SKIP(group, "sigsuspend/basic: fork", "fork failed (resource exhaustion)");
        } else {
            TEST(group, "sigsuspend/basic: fork", 1);
        }
        if (pid == 0) {
            sigset_t empty;
            sigemptyset(&empty);
            /* sigsuspend with empty mask: accept any signal */
            sigsuspend(&empty);
            /* If we reach here the signal was non-fatal — not expected in T1 */
            _exit(99);
        } else if (pid > 0) {
            sleep(1);
            kill(pid, SIGTERM);
            int status = 0;
            waitpid(pid, &status, 0);
            TEST(group, "sigsuspend/basic: child killed by SIGTERM",
                 WIFSIGNALED(status) && WTERMSIG(status) == SIGTERM);
        } else {
            SKIP(group, "sigsuspend/basic: child killed by SIGTERM", "fork failed");
        }
    }

    /* ------------------------------------------------------------------
     * T2: sigsuspend's mask blocks SIGTERM; child stays alive.
     *     SIGKILL cannot be masked — it terminates the child anyway.
     *
     * Child:  call sigsuspend with mask that blocks SIGTERM.
     * Parent: send SIGTERM; child must still be alive after 1 s.
     *         Then send SIGKILL.
     * Expect: child survives SIGTERM; dies from SIGKILL.
     * ------------------------------------------------------------------ */
    {
        pid_t pid = fork();
        if (pid < 0) {
            SKIP(group, "sigsuspend/mask: fork", "fork failed (resource exhaustion)");
        } else {
            TEST(group, "sigsuspend/mask: fork", 1);
        }
        if (pid == 0) {
            sigset_t block_term;
            sigemptyset(&block_term);
            sigaddset(&block_term, SIGTERM);
            /* sigsuspend blocks SIGTERM; only SIGKILL (or other unblocked signals)
             * will wake us */
            sigsuspend(&block_term);
            _exit(0);
        } else if (pid > 0) {
            sleep(1);
            /* Send SIGTERM — must be ignored by the mask */
            kill(pid, SIGTERM);
            sleep(1);
            /* Child should still be alive (WNOHANG returns 0) */
            int st = 0;
            pid_t r = waitpid(pid, &st, WNOHANG);
            TEST(group, "sigsuspend/mask: child alive after masked SIGTERM", r == 0);
            /* Now kill it for real */
            kill(pid, SIGKILL);
            waitpid(pid, &st, 0);
            TEST(group, "sigsuspend/mask: child killed by SIGKILL",
                 WIFSIGNALED(st) && WTERMSIG(st) == SIGKILL);
        } else {
            SKIP(group, "sigsuspend/mask: child alive after masked SIGTERM", "fork failed");
            SKIP(group, "sigsuspend/mask: child killed by SIGKILL", "fork failed");
        }
    }

    /* ------------------------------------------------------------------
     * T3: Already-pending fatal signal.
     *
     * Child:  (a) block SIGTERM via sigprocmask so it queues without firing,
     *         (b) send SIGTERM to self (now pending but blocked),
     *         (c) call sigsuspend with empty mask (unblocks SIGTERM).
     * Expect: sigsuspend must NOT block forever.  The pre-pending check in
     *         sys_sigsuspend detects the fatal signal and calls proc_exit;
     *         the child is killed by SIGTERM.
     * ------------------------------------------------------------------ */
    {
        pid_t pid = fork();
        if (pid < 0) {
            SKIP(group, "sigsuspend/pending_fatal: fork", "fork failed (resource exhaustion)");
        } else {
            TEST(group, "sigsuspend/pending_fatal: fork", 1);
        }
        if (pid == 0) {
            /* Step (a): block SIGTERM */
            sigset_t block_term;
            sigemptyset(&block_term);
            sigaddset(&block_term, SIGTERM);
            sigprocmask(SIG_BLOCK, &block_term, NULL);

            /* Step (b): send SIGTERM to self — queued, not delivered */
            kill(getpid(), SIGTERM);

            /* Verify it is pending */
            sigset_t pending;
            sigpending(&pending);
            if (!sigismember(&pending, SIGTERM)) {
                /* SIGTERM not pending — test setup broken */
                _exit(77);
            }

            /* Step (c): sigsuspend with empty mask — unblocks SIGTERM */
            sigset_t empty;
            sigemptyset(&empty);
            sigsuspend(&empty); /* must not return; proc_exit is called inside */

            /* If we reach here the implementation is broken */
            _exit(88);
        } else if (pid > 0) {
            int status = 0;
            /* No explicit kill from parent: child must self-terminate */
            waitpid(pid, &status, 0);
            TEST(group, "sigsuspend/pending_fatal: child killed by pre-pending SIGTERM",
                 WIFSIGNALED(status) && WTERMSIG(status) == SIGTERM);
        } else {
            SKIP(group, "sigsuspend/pending_fatal: child killed by pre-pending SIGTERM", "fork failed");
        }
    }

    /* ------------------------------------------------------------------
     * T4: Already-pending non-fatal signal (SIGURG = 23).
     *
     * SIGURG is not in KERNEL_FATAL_SIGS, KERNEL_STOP_SIGS, or
     * KERNEL_DFL_IGNORE_SIGS, so delivering it does not terminate or stop
     * the process.  sigsuspend must return (EINTR) instead of blocking.
     *
     * SIGUSR1/SIGUSR2 were moved to KERNEL_FATAL_SIGS in SIGS_2 and are
     * therefore unsuitable as "non-fatal" wake signals here.
     *
     * Child:  (a) block SIGURG,
     *         (b) send SIGURG to self (pending, blocked),
     *         (c) call sigsuspend with empty mask.
     * Expect: sigsuspend returns (child survives), exits 0 → WIFEXITED.
     * ------------------------------------------------------------------ */
    {
        pid_t pid = fork();
        if (pid < 0) {
            SKIP(group, "sigsuspend/pending_nonfatal: fork", "fork failed (resource exhaustion)");
        } else {
            TEST(group, "sigsuspend/pending_nonfatal: fork", 1);
        }
        if (pid == 0) {
            /* (a) Block SIGURG */
            sigset_t block_urg;
            sigemptyset(&block_urg);
            sigaddset(&block_urg, SIGURG);
            sigprocmask(SIG_BLOCK, &block_urg, NULL);

            /* (b) Send SIGURG to self */
            kill(getpid(), SIGURG);

            /* (c) sigsuspend with empty mask — unblocks SIGURG */
            sigset_t empty;
            sigemptyset(&empty);
            sigsuspend(&empty);
            /* Must return here because SIGURG is not fatal */

            _exit(0); /* success */
        } else if (pid > 0) {
            int status = 0;
            /* Give the child a generous deadline; if it blocks forever the
             * parent will hang here — detectable as a test timeout */
            waitpid(pid, &status, 0);
            TEST(group, "sigsuspend/pending_nonfatal: child returned from sigsuspend",
                 WIFEXITED(status) && WEXITSTATUS(status) == 0);
        } else {
            SKIP(group, "sigsuspend/pending_nonfatal: child returned from sigsuspend", "fork failed");
        }
    }

    /* ------------------------------------------------------------------
     * T5: Original signal mask is restored after sigsuspend returns.
     *
     * Child's original mask: empty (nothing blocked).
     * sigsuspend mask:        blocks SIGTERM (so SIGTERM won't wake us).
     * Wakeup signal:          SIGURG (non-fatal, not blocked by sigsuspend mask).
     *                         SIGUSR2 was moved to KERNEL_FATAL_SIGS in SIGS_2
     *                         so it would kill rather than wake.
     * After sigsuspend returns, the original mask (empty) is restored.
     * We then verify that SIGTERM is unblocked again by sending it:
     *   the child must die from SIGTERM, not survive.
     *
     * Timeline:
     *   t=0  child enters sigsuspend(block_term)
     *   t=1  parent sends SIGURG  →  sigsuspend wakes, restores empty mask, returns
     *   t=2  parent sends SIGTERM →  child's restored mask allows it; child dies
     * ------------------------------------------------------------------ */
    {
        pid_t pid = fork();
        if (pid < 0) {
            SKIP(group, "sigsuspend/mask_restore: fork", "fork failed (resource exhaustion)");
        } else {
            TEST(group, "sigsuspend/mask_restore: fork", 1);
        }
        if (pid == 0) {
            /* Ensure we start with an empty mask (no inherited blocks) */
            sigset_t empty;
            sigemptyset(&empty);
            sigprocmask(SIG_SETMASK, &empty, NULL);

            /* sigsuspend: block SIGTERM, accept SIGURG and everything else */
            sigset_t block_term;
            sigemptyset(&block_term);
            sigaddset(&block_term, SIGTERM);
            sigsuspend(&block_term);
            /* Returns here after SIGURG wakes us; original mask (empty) restored */

            /* Stay alive long enough for the parent to send SIGTERM */
            sleep(5);
            _exit(0); /* should not reach here */
        } else if (pid > 0) {
            sleep(1);
            /* Wake sigsuspend with a non-fatal, unblocked signal */
            kill(pid, SIGURG);

            sleep(1);
            /* Now send SIGTERM — must kill the child because the original
             * empty mask was restored (SIGTERM unblocked) */
            kill(pid, SIGTERM);

            int status = 0;
            waitpid(pid, &status, 0);
            TEST(group, "sigsuspend/mask_restore: SIGTERM kills after mask restored",
                 WIFSIGNALED(status) && WTERMSIG(status) == SIGTERM);
        } else {
            SKIP(group, "sigsuspend/mask_restore: SIGTERM kills after mask restored", "fork failed");
        }
    }

    ut_group_end(group);
}

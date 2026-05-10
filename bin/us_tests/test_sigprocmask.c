/*
 * test_sigprocmask.c — Userspace tests for sigprocmask mask-deferral semantics
 *
 * Corresponds to PREVAL_SIGNAL_MASK.md §9 (Suggested Test Cases).
 * Exercises the pending-then-deliver path using process termination as the
 * observable side-effect (no custom handler required).
 *
 *  A. Masked SIGTERM stays pending; process survives the block window and is
 *     killed only after SIG_UNBLOCK.
 *
 *  B. SIGUSR1 masked, then SIG_IGN installed while still masked; after unblock
 *     the SIG_IGN sweep discards the pending signal — process survives.
 *
 *  C. sigprocmask(SIG_BLOCK, …, oldset) captures the mask that was in effect
 *     *before* the call, not after.
 */

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include "us_test_common.h"

void run_test_sigprocmask(us_test_group_t *group) {
    ut_group_begin(group, "sigprocmask");

    /* ── A. Masked SIGTERM deferred until SIG_UNBLOCK ────────────────────────
     *
     * Child:  block SIGTERM → kill(self, SIGTERM) → sleep(2) [survives] →
     *         SIG_UNBLOCK → sleep(1) → _exit(0) [unreachable]
     * Parent: wait; expect WIFSIGNALED && WTERMSIG == SIGTERM.
     *
     * Tests pending_signals & ~signal_mask gating in syscall_handler (lines
     * 84, 101) and trap_handler (lines 324, 337).
     */
    printf("--- A. Masked SIGTERM stays pending; delivered only after unblock ---\n");
    {
        pid_t pid = fork();
        TEST(group, "sigprocmask/block-SIGTERM: fork succeeded", pid >= 0);

        if (pid == 0) {
            sigset_t set;
            sigemptyset(&set);
            sigaddset(&set, SIGTERM);
            sigprocmask(SIG_BLOCK, &set, NULL);   /* block SIGTERM */
            kill(getpid(), SIGTERM);              /* masked — stays pending */
            sleep(2);                             /* must survive this window */
            sigprocmask(SIG_UNBLOCK, &set, NULL); /* unblock → pending SIGTERM fires */
            sleep(1);                             /* delivery happens here */
            _exit(0);                             /* unreachable */
        }

        if (pid > 0) {
            int status = 0;
            pid_t w = waitpid(pid, &status, 0);
            TEST(group, "sigprocmask/block-SIGTERM: child reaped",                     w == pid);
            TEST(group, "sigprocmask/block-SIGTERM: terminated by signal after unblock", WIFSIGNALED(status));
            TEST(group, "sigprocmask/block-SIGTERM: WTERMSIG == SIGTERM",              WTERMSIG(status) == SIGTERM);
        }
    }

    /* ── B. SIG_IGN installed while signal is masked discards pending signal ─
     *
     * Child:  block SIGUSR1 → kill(self, SIGUSR1) → sigaction(SIG_IGN) →
     *         SIG_UNBLOCK → _exit(77)  [must be reached]
     * Parent: wait; expect WIFEXITED(77) — SIG_IGN swept pending bit away.
     *
     * Tests the SIG_IGN filter loop (syscall.c:84 / trap.c:324): masked signals
     * are excluded from the sweep while blocked, but after unblock the SIG_IGN
     * disposition is evaluated and the pending bit is cleared before any fatal
     * check fires.
     */
    printf("--- B. SIG_IGN installed while masked discards pending SIGUSR1 ---\n");
    {
        pid_t pid = fork();
        TEST(group, "sigprocmask/sigign-while-masked: fork succeeded", pid >= 0);

        if (pid == 0) {
            sigset_t set;
            sigemptyset(&set);
            sigaddset(&set, SIGUSR1);
            sigprocmask(SIG_BLOCK, &set, NULL);     /* block SIGUSR1 */
            kill(getpid(), SIGUSR1);                /* masked — stays pending */

            struct sigaction sa;
            sa.sa_handler = SIG_IGN;
            sigemptyset(&sa.sa_mask);
            sa.sa_flags = 0;
            sigaction(SIGUSR1, &sa, NULL);          /* install SIG_IGN while masked */

            sigprocmask(SIG_UNBLOCK, &set, NULL);   /* SIG_IGN sweep drops pending bit */
            _exit(77);                              /* must be reached */
        }

        if (pid > 0) {
            int status = 0;
            pid_t w = waitpid(pid, &status, 0);
            TEST(group, "sigprocmask/sigign-while-masked: child reaped",              w == pid);
            TEST(group, "sigprocmask/sigign-while-masked: exited normally (SIG_IGN discarded pending)",
                 WIFEXITED(status) && WEXITSTATUS(status) == 77);
        }
    }

    /* ── C. oldset captures the mask in effect *before* the call ────────────
     *
     * No fork required.  Tests sys_sigprocmask lines 1598–1599: oldset is
     * written before the new mask is computed, so it reflects the pre-call state.
     *
     * Steps:
     *   1. SIG_BLOCK SIGTERM with oldset capture → old must NOT have SIGTERM.
     *   2. SIG_SETMASK NULL (read-only) with oldset capture → must HAVE SIGTERM.
     *   3. SIG_SETMASK restore.
     */
    printf("--- C. sigprocmask oldset reflects mask before the call ---\n");
    {
        sigset_t block_set, old_set, cur_set;
        sigemptyset(&block_set);
        sigaddset(&block_set, SIGTERM);

        /* Step 1: block SIGTERM; capture old mask */
        sigprocmask(SIG_BLOCK, &block_set, &old_set);
        int old_has_sigterm = sigismember(&old_set, SIGTERM);

        /* Step 2: read current mask without changing it */
        sigprocmask(SIG_SETMASK, NULL, &cur_set);
        int cur_has_sigterm = sigismember(&cur_set, SIGTERM);

        /* Restore before any assertions so SIGTERM cannot fire on failure */
        sigprocmask(SIG_SETMASK, &old_set, NULL);

        TEST(group, "sigprocmask/oldset: old mask did not contain SIGTERM", !old_has_sigterm);
        TEST(group, "sigprocmask/oldset: current mask contains SIGTERM after SIG_BLOCK", cur_has_sigterm);
    }

    ut_group_end(group);
}

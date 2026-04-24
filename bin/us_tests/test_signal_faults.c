/*
 * test_signal_faults.c — Userspace integration tests for SIGSEGV, SIGILL, SIGQUIT.
 *
 * Tests exercise the three signals whose kernel-side dispatch was wired up in
 * SIGS.md: SIGQUIT (3), SIGILL (4), SIGSEGV (11).
 *
 *  A. NULL dereference → page fault → SIGSEGV → child terminated by signal
 *  B. kill(pid, SIGQUIT) terminates a spinning child
 *  C. sigprocmask blocks SIGQUIT: child survives kill-to-self, dies after unblock
 *  D. kill(pid, SIGSEGV) from parent terminates child
 *  E. kill(pid, SIGILL) from parent terminates child
 *  F. signal_mask inherits across fork: child inherits SIGQUIT block, exits normally
 */

#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include "us_test_common.h"

void run_test_signal_faults(us_test_group_t *group) {
    ut_group_begin(group, "signal_faults");

    /* ── A. NULL dereference → SIGSEGV ──────────────────────────────────────
     * Child writes through a NULL pointer, triggering a page fault that is
     * converted to SIGSEGV by kill_current().  Parent verifies the child was
     * reaped as terminated by a signal (WIFSIGNALED).
     */
    {
        pid_t pid = fork();
        TEST(group, "SIGSEGV/null-deref: fork succeeded", pid >= 0);

        if (pid == 0) {
            volatile int *p = (int *)0;
            *p = 42;        /* load/store page fault → kill_current → SIGSEGV */
            _exit(0);       /* unreachable */
        }

        if (pid > 0) {
            int status = 0;
            pid_t w = waitpid(pid, &status, 0);
            TEST(group, "SIGSEGV/null-deref: child reaped", w == pid);
            TEST(group, "SIGSEGV/null-deref: child terminated by signal",
                 WIFSIGNALED(status));
        }
    }

    /* ── B. kill(pid, SIGQUIT) terminates a spinning child ──────────────────
     * SIGQUIT is now in KERNEL_FATAL_SIGS so the pending bit set by sys_kill
     * is delivered at the next trap return.
     */
    {
        pid_t pid = fork();
        TEST(group, "SIGQUIT/kill: fork succeeded", pid >= 0);

        if (pid == 0) {
            volatile int x = 0;
            while (1) x++;  /* spin until killed */
            _exit(0);
        }

        if (pid > 0) {
            kill(pid, SIGQUIT);
            int status = 0;
            pid_t w = waitpid(pid, &status, 0);
            TEST(group, "SIGQUIT/kill: child reaped", w == pid);
            TEST(group, "SIGQUIT/kill: child terminated by signal",
                 WIFSIGNALED(status));
        }
    }

    /* ── C. sigprocmask blocks SIGQUIT: deferred termination ────────────────
     * Child blocks SIGQUIT, sends it to itself (stays pending), then unblocks.
     * The pending signal must be delivered at the next syscall entry after
     * unblock, killing the child before it reaches _exit(0).
     */
    {
        pid_t pid = fork();
        TEST(group, "SIGQUIT/mask: fork succeeded", pid >= 0);

        if (pid == 0) {
            sigset_t set;
            sigemptyset(&set);
            sigaddset(&set, SIGQUIT);
            sigprocmask(SIG_BLOCK, &set, NULL);
            kill(getpid(), SIGQUIT);    /* masked — stays pending */
            sleep(2);                   /* survive: mask is still active */
            sigprocmask(SIG_UNBLOCK, &set, NULL);
            sleep(1);                   /* this syscall triggers delivery */
            _exit(0);                   /* unreachable if delivery worked */
        }

        if (pid > 0) {
            int status = 0;
            pid_t w = waitpid(pid, &status, 0);
            TEST(group, "SIGQUIT/mask: child reaped", w == pid);
            TEST(group, "SIGQUIT/mask: child killed by deferred SIGQUIT",
                 WIFSIGNALED(status));
        }
    }

    /* ── D. kill(pid, SIGSEGV) from parent terminates child ─────────────────
     * SIGSEGV delivered via sys_kill (not a hardware fault) goes through the
     * pending_signals + KERNEL_FATAL_SIGS delivery path.
     */
    {
        pid_t pid = fork();
        TEST(group, "SIGSEGV/kill: fork succeeded", pid >= 0);

        if (pid == 0) {
            volatile int x = 0;
            while (1) x++;
            _exit(0);
        }

        if (pid > 0) {
            sleep(1);
            kill(pid, SIGSEGV);
            int status = 0;
            pid_t w = waitpid(pid, &status, 0);
            TEST(group, "SIGSEGV/kill: child reaped", w == pid);
            TEST(group, "SIGSEGV/kill: child terminated by signal",
                 WIFSIGNALED(status));
        }
    }

    /* ── E. kill(pid, SIGILL) from parent terminates child ──────────────────
     * SIGILL delivered via sys_kill exercises the same fatal-signal path.
     */
    {
        pid_t pid = fork();
        TEST(group, "SIGILL/kill: fork succeeded", pid >= 0);

        if (pid == 0) {
            volatile int x = 0;
            while (1) x++;
            _exit(0);
        }

        if (pid > 0) {
            sleep(1);
            kill(pid, SIGILL);
            int status = 0;
            pid_t w = waitpid(pid, &status, 0);
            TEST(group, "SIGILL/kill: child reaped", w == pid);
            TEST(group, "SIGILL/kill: child terminated by signal",
                 WIFSIGNALED(status));
        }
    }

    /* ── F. signal_mask inherited across fork: child exits normally ──────────
     * Parent blocks SIGQUIT then forks.  Child inherits the mask, kills itself
     * with SIGQUIT (stays pending/masked), sleeps, and exits with code 42.
     * WIFEXITED + WEXITSTATUS==42 confirms the mask was inherited and SIGQUIT
     * was never delivered inside the child.
     * Parent unblocks its own SIGQUIT after fork to avoid carrying pending signal.
     */
    {
        sigset_t set;
        sigemptyset(&set);
        sigaddset(&set, SIGQUIT);
        sigprocmask(SIG_BLOCK, &set, NULL);  /* parent blocks SIGQUIT */

        pid_t pid = fork();
        TEST(group, "SIGQUIT/inherit-mask: fork succeeded", pid >= 0);

        if (pid == 0) {
            kill(getpid(), SIGQUIT);    /* masked in child (inherited) */
            sleep(1);
            _exit(42);                  /* reached only if mask was inherited */
        }

        if (pid > 0) {
            sigprocmask(SIG_UNBLOCK, &set, NULL);  /* restore parent mask */
            int status = 0;
            pid_t w = waitpid(pid, &status, 0);
            TEST(group, "SIGQUIT/inherit-mask: child reaped", w == pid);
            TEST(group, "SIGQUIT/inherit-mask: child exited normally (mask inherited)",
                 WIFEXITED(status) && WEXITSTATUS(status) == 42);
        }
    }

    // Extend Test A to verify WTERMSIG returns the correct signal number
    {
        pid_t pid = fork();
        if (pid == 0) {
            volatile int *p = (int *)0;
            *p = 42;    // null deref → SIGSEGV
            _exit(0);   // unreachable
        }
        int status;
        waitpid(pid, &status, 0);
        if (!WIFSIGNALED(status)) {
            printf("FAIL: child exited normally, expected signal death\n");
            return;
        }
        int sig = WTERMSIG(status);
        if (sig == SIGSEGV)
            printf("PASS: WTERMSIG == SIGSEGV (%d)\n", sig);
        else
            printf("FAIL: WTERMSIG == %d (expected SIGSEGV=%d)\n", sig, SIGSEGV);
    }

    ut_group_end(group);
}

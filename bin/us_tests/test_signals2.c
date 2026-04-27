/*
 * test_signals2.c — Userspace integration tests for SIGHUP, SIGTRAP, SIGABRT,
 *                   SIGUSR1, SIGUSR2, and SIGSYS.
 *
 * Tests exercise the six signals wired up in SIGS_2.md:
 *
 *  1. kill(pid, SIGUSR1) terminates child; WTERMSIG == SIGUSR1
 *  2. kill(pid, SIGUSR2) terminates child; WTERMSIG == SIGUSR2
 *  3. kill(pid, SIGHUP)  terminates child; WTERMSIG == SIGHUP
 *  4. abort() in child delivers SIGABRT; WTERMSIG == SIGABRT
 *  5. kill(pid, SIGABRT) from parent terminates child
 *  6. EBREAK in child delivers SIGTRAP; WTERMSIG == SIGTRAP
 *  7. kill(pid, SIGTRAP) from parent terminates child
 *  8. Invalid syscall number delivers SIGSYS and terminates child
 *  9. sigprocmask blocks SIGUSR1: child survives kill-to-self, dies after unblock
 * 10. SIGABRT is unmaskable: sigprocmask(SIG_BLOCK, SIGABRT) is silently stripped
 * 11. SIGTRAP is unmaskable: sigprocmask(SIG_BLOCK, SIGTRAP) is silently stripped
 * 12. SIGUSR1 mask inherited across fork: child with inherited block exits normally
 * 13. WTERMSIG encoding: proc_waitpid returns correct signal for each new signal
 */

#include <sys/wait.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include "us_test_common.h"

void run_test_signals2(us_test_group_t *group) {
    ut_group_begin(group, "signals2");

    /* ── 1. kill(pid, SIGUSR1) terminates child ──────────────────────────────
     * SIGUSR1 is in KERNEL_FATAL_SIGS: the pending bit set by sys_kill is
     * delivered at the next trap return (woken from sleep via proc_unblock).
     */
    printf("--- 1. kill(SIGUSR1) terminates child ---\n");
    {
        pid_t pid = fork();
        TEST(group, "SIGUSR1/kill: fork succeeded", pid >= 0);

        if (pid == 0) {
            while (1) sleep(1);
        }

        if (pid > 0) {
            kill(pid, SIGUSR1);
            int status = 0;
            pid_t w = waitpid(pid, &status, 0);
            TEST(group, "SIGUSR1/kill: child reaped",            w == pid);
            TEST(group, "SIGUSR1/kill: terminated by signal",    WIFSIGNALED(status));
            TEST(group, "SIGUSR1/kill: WTERMSIG == SIGUSR1",     WTERMSIG(status) == SIGUSR1);
        }
    }

    /* ── 2. kill(pid, SIGUSR2) terminates child ──────────────────────────────
     */
    printf("--- 2. kill(SIGUSR2) terminates child ---\n");
    {
        pid_t pid = fork();
        TEST(group, "SIGUSR2/kill: fork succeeded", pid >= 0);

        if (pid == 0) {
            while (1) sleep(1);
        }

        if (pid > 0) {
            kill(pid, SIGUSR2);
            int status = 0;
            pid_t w = waitpid(pid, &status, 0);
            TEST(group, "SIGUSR2/kill: child reaped",            w == pid);
            TEST(group, "SIGUSR2/kill: terminated by signal",    WIFSIGNALED(status));
            TEST(group, "SIGUSR2/kill: WTERMSIG == SIGUSR2",     WTERMSIG(status) == SIGUSR2);
        }
    }

    /* ── 3. kill(pid, SIGHUP) terminates child ───────────────────────────────
     * SIGHUP is in KERNEL_FATAL_SIGS.  This tests the kill()-delivery path;
     * automatic generation on session-leader exit is a separate mechanism.
     */
    printf("--- 3. kill(SIGHUP) terminates child ---\n");
    {
        pid_t pid = fork();
        TEST(group, "SIGHUP/kill: fork succeeded", pid >= 0);

        if (pid == 0) {
            while (1) sleep(1);
        }

        if (pid > 0) {
            kill(pid, SIGHUP);
            int status = 0;
            pid_t w = waitpid(pid, &status, 0);
            TEST(group, "SIGHUP/kill: child reaped",             w == pid);
            TEST(group, "SIGHUP/kill: terminated by signal",     WIFSIGNALED(status));
            TEST(group, "SIGHUP/kill: WTERMSIG == SIGHUP",       WTERMSIG(status) == SIGHUP);
        }
    }

    /* ── 4. abort() in child delivers SIGABRT ────────────────────────────────
     * abort() calls raise(SIGABRT) = kill(getpid(), SIGABRT).  After returning
     * from the kill ecall, trap_handler's KERNEL_FATAL_SIGS check fires
     * proc_exit(-SIGABRT).  The _Exit(134) fallback in libc/stdlib.c is
     * unreachable because SIGABRT is in KERNEL_FATAL_SIGS.
     */
    printf("--- 4. abort() in child delivers SIGABRT ---\n");
    {
        pid_t pid = fork();
        TEST(group, "SIGABRT/abort: fork succeeded", pid >= 0);

        if (pid == 0) {
            abort();    /* raise(SIGABRT) → KERNEL_FATAL_SIGS → proc_exit(-6) */
            _exit(0);   /* unreachable */
        }

        if (pid > 0) {
            int status = 0;
            pid_t w = waitpid(pid, &status, 0);
            TEST(group, "SIGABRT/abort: child reaped",           w == pid);
            TEST(group, "SIGABRT/abort: terminated by signal",   WIFSIGNALED(status));
            TEST(group, "SIGABRT/abort: WTERMSIG == SIGABRT",    WTERMSIG(status) == SIGABRT);
        }
    }

    /* ── 5. kill(pid, SIGABRT) from parent terminates child ─────────────────
     */
    printf("--- 5. kill(SIGABRT) from parent terminates child ---\n");
    {
        pid_t pid = fork();
        TEST(group, "SIGABRT/kill: fork succeeded", pid >= 0);

        if (pid == 0) {
            while (1) sleep(1);
        }

        if (pid > 0) {
            kill(pid, SIGABRT);
            int status = 0;
            pid_t w = waitpid(pid, &status, 0);
            TEST(group, "SIGABRT/kill: child reaped",            w == pid);
            TEST(group, "SIGABRT/kill: terminated by signal",    WIFSIGNALED(status));
            TEST(group, "SIGABRT/kill: WTERMSIG == SIGABRT",     WTERMSIG(status) == SIGABRT);
        }
    }

    /* ── 6. EBREAK in child delivers SIGTRAP ─────────────────────────────────
     * EBREAK (0x00100073) generates SCAUSE_BREAKPOINT = 3.  handle_exception()
     * routes this to SIGTRAP via pending_signals.  SIGTRAP is in KERNEL_FATAL_SIGS
     * so the process is terminated at the next trap return.
     * Standard RISC-V EBREAK is 4 bytes; C.EBREAK (0x9002) is 2 bytes; both
     * generate SCAUSE_BREAKPOINT = 3.
     */
    printf("--- 6. EBREAK in child delivers SIGTRAP ---\n");
    {
        pid_t pid = fork();
        TEST(group, "SIGTRAP/ebreak: fork succeeded", pid >= 0);

        if (pid == 0) {
            asm volatile(".word 0x00100073"); /* EBREAK → SCAUSE_BREAKPOINT → SIGTRAP */
            _exit(0);   /* unreachable if SIGTRAP is in KERNEL_FATAL_SIGS */
        }

        if (pid > 0) {
            int status = 0;
            pid_t w = waitpid(pid, &status, 0);
            TEST(group, "SIGTRAP/ebreak: child reaped",          w == pid);
            TEST(group, "SIGTRAP/ebreak: terminated by signal",  WIFSIGNALED(status));
            TEST(group, "SIGTRAP/ebreak: WTERMSIG == SIGTRAP",   WTERMSIG(status) == SIGTRAP);
        }
    }

    /* ── 7. kill(pid, SIGTRAP) from parent terminates child ─────────────────
     */
    printf("--- 7. kill(SIGTRAP) from parent terminates child ---\n");
    {
        pid_t pid = fork();
        TEST(group, "SIGTRAP/kill: fork succeeded", pid >= 0);

        if (pid == 0) {
            while (1) sleep(1);
        }

        if (pid > 0) {
            kill(pid, SIGTRAP);
            int status = 0;
            pid_t w = waitpid(pid, &status, 0);
            TEST(group, "SIGTRAP/kill: child reaped",            w == pid);
            TEST(group, "SIGTRAP/kill: terminated by signal",    WIFSIGNALED(status));
            TEST(group, "SIGTRAP/kill: WTERMSIG == SIGTRAP",     WTERMSIG(status) == SIGTRAP);
        }
    }

    /* ── 8. Invalid syscall number delivers SIGSYS ───────────────────────────
     * The syscall dispatcher default case sets pending SIGSYS and returns -ENOSYS.
     * SIGSYS (bit 31) is in KERNEL_FATAL_SIGS, so the process is killed at the
     * next trap return.  Uses inline assembly to issue ecall with a7 = 999 (out of
     * range for all implemented syscalls).
     */
    printf("--- 8. Invalid syscall number delivers SIGSYS ---\n");
    {
        pid_t pid = fork();
        TEST(group, "SIGSYS/bad-syscall: fork succeeded", pid >= 0);

        if (pid == 0) {
            register long a7 asm("a7") = 999;  /* invalid syscall number */
            register long a0 asm("a0") = 0;
            asm volatile("ecall" : "+r"(a0) : "r"(a7) : "memory");
            /* If SIGSYS is unmasked: proc_exit(-31) fires before this line. */
            /* If SIGSYS is masked: a0 == -ENOSYS; process continues.       */
            _exit(0);   /* unreachable when SIGSYS is unmasked */
        }

        if (pid > 0) {
            int status = 0;
            pid_t w = waitpid(pid, &status, 0);
            TEST(group, "SIGSYS/bad-syscall: child reaped",         w == pid);
            TEST(group, "SIGSYS/bad-syscall: terminated by signal", WIFSIGNALED(status));
            TEST(group, "SIGSYS/bad-syscall: WTERMSIG == SIGSYS",   WTERMSIG(status) == SIGSYS);
        }
    }

    /* ── 9. sigprocmask blocks SIGUSR1 — deferred delivery on unblock ───────
     * Child blocks SIGUSR1, sends it to itself (stays pending), sleeps through
     * the block window, then unblocks.  The still-pending signal is delivered
     * at the next signal check point (entry to the sleep(1) ecall), killing
     * the child.  This tests the pending-then-deliver path.
     */
    printf("--- 9. sigprocmask blocks SIGUSR1: deferred delivery on unblock ---\n");
    {
        pid_t pid = fork();
        TEST(group, "SIGUSR1/mask: fork succeeded", pid >= 0);

        if (pid == 0) {
            sigset_t set;
            sigemptyset(&set);
            sigaddset(&set, SIGUSR1);
            sigprocmask(SIG_BLOCK, &set, NULL);
            kill(getpid(), SIGUSR1);   /* masked — stays pending */
            sleep(3);                  /* survives because SIGUSR1 is blocked */
            sigprocmask(SIG_UNBLOCK, &set, NULL);
            sleep(1);                  /* pending SIGUSR1 delivered → proc_exit(-10) */
            _exit(0);                  /* unreachable */
        }

        if (pid > 0) {
            int status = 0;
            pid_t w = waitpid(pid, &status, 0);
            TEST(group, "SIGUSR1/mask: child reaped",                    w == pid);
            TEST(group, "SIGUSR1/mask: terminated by signal after unblock",
                 WIFSIGNALED(status));
            TEST(group, "SIGUSR1/mask: WTERMSIG == SIGUSR1",             WTERMSIG(status) == SIGUSR1);
        }
    }

    /* ── 10. SIGABRT is unmaskable ───────────────────────────────────────────
     * sys_sigprocmask strips SIGABRT from new_mask unconditionally.  The child
     * attempts to block SIGABRT and then sends it to itself.  Since the block
     * had no effect, the signal fires immediately and the child is killed.
     * If the child reaches _exit(42), the kernel incorrectly allowed blocking.
     */
    printf("--- 10. SIGABRT is unmaskable: SIG_BLOCK attempt is stripped ---\n");
    {
        pid_t pid = fork();
        TEST(group, "SIGABRT/unmask: fork succeeded", pid >= 0);

        if (pid == 0) {
            sigset_t set;
            sigemptyset(&set);
            sigaddset(&set, SIGABRT);
            sigprocmask(SIG_BLOCK, &set, NULL); /* bit 6 stripped by kernel */
            kill(getpid(), SIGABRT);            /* unmasked → fires at next trap return */
            sleep(3);                           /* unreachable if SIGABRT is unmaskable */
            _exit(42);                          /* sentinel: reached only if block worked */
        }

        if (pid > 0) {
            int status = 0;
            pid_t w = waitpid(pid, &status, 0);
            TEST(group, "SIGABRT/unmask: child reaped",                    w == pid);
            TEST(group, "SIGABRT/unmask: killed by SIGABRT (not blocked)", WIFSIGNALED(status) &&
                 WTERMSIG(status) == SIGABRT);
        }
    }

    /* ── 11. SIGTRAP is unmaskable ───────────────────────────────────────────
     * Same test as SIGABRT above.  If SIGTRAP were maskable and the process
     * then executed EBREAK, the masked SIGTRAP would pend but sepc would not
     * advance, causing an infinite re-fault loop (identical to SIGILL Bug 1
     * in SIGS.md).  The unmaskable strip prevents this.
     */
    printf("--- 11. SIGTRAP is unmaskable: SIG_BLOCK attempt is stripped ---\n");
    {
        pid_t pid = fork();
        TEST(group, "SIGTRAP/unmask: fork succeeded", pid >= 0);

        if (pid == 0) {
            sigset_t set;
            sigemptyset(&set);
            sigaddset(&set, SIGTRAP);
            sigprocmask(SIG_BLOCK, &set, NULL); /* bit 5 stripped by kernel */
            kill(getpid(), SIGTRAP);            /* unmasked → fires immediately */
            sleep(3);                           /* unreachable */
            _exit(42);                          /* sentinel */
        }

        if (pid > 0) {
            int status = 0;
            pid_t w = waitpid(pid, &status, 0);
            TEST(group, "SIGTRAP/unmask: child reaped",                    w == pid);
            TEST(group, "SIGTRAP/unmask: killed by SIGTRAP (not blocked)", WIFSIGNALED(status) &&
                 WTERMSIG(status) == SIGTRAP);
        }
    }

    /* ── 12. SIGUSR1 mask inherited across fork ──────────────────────────────
     * Parent blocks SIGUSR1 before fork.  Child inherits signal_mask (POSIX:
     * "The child shall inherit … the signal mask" — fork(2)).  Child sends
     * SIGUSR1 to itself; it stays pending.  Child sleeps and exits normally
     * with code 77.  If the mask were NOT inherited, SIGUSR1 would kill the
     * child immediately, producing WIFSIGNALED instead of WIFEXITED.
     */
    printf("--- 12. SIGUSR1 signal mask inherited across fork ---\n");
    {
        sigset_t set;
        sigemptyset(&set);
        sigaddset(&set, SIGUSR1);
        sigprocmask(SIG_BLOCK, &set, NULL);   /* parent blocks SIGUSR1 */

        pid_t pid = fork();
        TEST(group, "SIGUSR1/inherit: fork succeeded", pid >= 0);

        if (pid == 0) {
            kill(getpid(), SIGUSR1);   /* masked in child — stays pending */
            sleep(2);
            _exit(77);   /* reached because SIGUSR1 is still masked */
        }

        sigprocmask(SIG_UNBLOCK, &set, NULL);   /* restore parent's mask */

        if (pid > 0) {
            int status = 0;
            pid_t w = waitpid(pid, &status, 0);
            TEST(group, "SIGUSR1/inherit: child reaped",                   w == pid);
            TEST(group, "SIGUSR1/inherit: exited normally (mask inherited)",
                 WIFEXITED(status) && WEXITSTATUS(status) == 77);
        }
    }

    /* ── 13. WTERMSIG encoding for all new signals ───────────────────────────
     * Verifies that proc_waitpid at process.c:307-310 encodes the negative
     * exit code correctly: (-exit_code) & 0x7f == signal number.
     * Each child is a separate fork so failures are isolated.
     */
    printf("--- 13. WTERMSIG encoding for SIGHUP/SIGTRAP/SIGABRT/SIGUSR1/SIGUSR2 ---\n");
    {
        const int   sigs[]  = { SIGHUP, SIGTRAP, SIGABRT, SIGUSR1, SIGUSR2 };
        const char *names[] = { "SIGHUP", "SIGTRAP", "SIGABRT", "SIGUSR1", "SIGUSR2" };
        const char *descs[] = {
            "WTERMSIG/enc: SIGHUP(1) encoded correctly",
            "WTERMSIG/enc: SIGTRAP(5) encoded correctly",
            "WTERMSIG/enc: SIGABRT(6) encoded correctly",
            "WTERMSIG/enc: SIGUSR1(10) encoded correctly",
            "WTERMSIG/enc: SIGUSR2(12) encoded correctly",
        };
        (void)names;   /* suppress unused-variable warning if TEST is elided */

        for (int i = 0; i < 5; i++) {
            pid_t pid = fork();
            if (pid == 0) {
                while (1) sleep(1);
            }
            if (pid > 0) {
                kill(pid, sigs[i]);
                int status = 0;
                waitpid(pid, &status, 0);
                TEST(group, descs[i],
                     WIFSIGNALED(status) && WTERMSIG(status) == sigs[i]);
            }
        }
    }

    ut_group_end(group);
}
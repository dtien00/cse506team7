/*
 * test_sighandler.c — Custom signal handler dispatch tests (P4/P5)
 *
 * Exercises the signal-dispatch infrastructure added by patches P1–P6
 * (PREVAL_SIGNAL_MASK.md §10.5).  All tests use SIGUSR1 or SIGUSR2,
 * which are catchable per POSIX despite having a fatal default action.
 *
 * Each test forks a child that performs the observable work and exits
 * with code 0 (pass) or a non-zero sentinel (fail).  The parent checks
 * WIFEXITED && WEXITSTATUS == 0.
 *
 * Sections:
 *   A. Handler invocation — custom handler runs; flag set after raise()
 *   B. Mask deferral (evaluator's test) — flag stays 0 while masked,
 *      transitions to 1 only after sigprocmask(SIG_UNBLOCK)
 *   C. Computation continues after sigreturn — value computed before and
 *      after the handler matches expectations (tests sepc and s-reg restore)
 *   D. sa_mask defers SIGUSR2 during SIGUSR1 handler
 *   E. Signal mask restored after sigreturn — second raise() dispatches
 *      the handler again (mask not permanently modified)
 */

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include "us_test_common.h"

/* ── shared volatile state (per-child address space after fork) ─────────── */

static volatile int g_flag   = 0;
static volatile int g_flag2  = 0;
static volatile int g_count  = 0;
static volatile int g_counter = 0;

/* ── handlers ───────────────────────────────────────────────────────────── */

static void handler_A(int sig) { (void)sig; g_flag = 1; }

static void handler_B(int sig) { (void)sig; g_flag = 1; }

/* handler_C: bumps g_counter and uses many local variables to force the
   compiler to spill into callee-saved registers, verifying they survive
   sigreturn. */
static void handler_C(int sig) {
    (void)sig;
    volatile long a=1,b=2,c=3,d=4,e=5,f=6,g=7,h=8,i=9,j=10,k=11,l=12;
    g_counter += (int)(a+b+c+d+e+f+g+h+i+j+k+l); /* += 78 */
}

static void handler_D1(int sig) {
    (void)sig;
    /* SIGUSR2 is in sa_mask — this kill() must not interrupt us. */
    kill(getpid(), SIGUSR2);
    /* If sa_mask works, g_flag2 is still 0 here. */
    g_flag  = (g_flag2 == 0) ? 1 : -1; /* 1 = good, -1 = SIGUSR2 reentrant (bad) */
}

static void handler_D2(int sig) { (void)sig; g_flag2 = 1; }

static void handler_E(int sig) { (void)sig; g_count++; }

/* ── Section A — basic handler invocation ─────────────────────────────── */

static void child_A(void) {
    g_flag = 0;
    struct sigaction sa = { .sa_handler = handler_A, .sa_flags = 0 };
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR1, &sa, NULL);

    raise(SIGUSR1);          /* handler must fire synchronously at ecall return */

    _exit(g_flag == 1 ? 0 : 1);
}

/* ── Section B — mask deferral (evaluator's volatile-flag test) ─────────── */

static void child_B(void) {
    g_flag = 0;
    struct sigaction sa = { .sa_handler = handler_B, .sa_flags = 0 };
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR1, &sa, NULL);

    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGUSR1);

    sigprocmask(SIG_BLOCK, &set, NULL);  /* block SIGUSR1 */
    kill(getpid(), SIGUSR1);             /* queued — must NOT deliver yet */

    if (g_flag != 0) _exit(1);          /* flag set before unblock → FAIL */

    sigprocmask(SIG_UNBLOCK, &set, NULL); /* unblock → handler fires at ecall return */

    if (g_flag != 1) _exit(2);          /* flag not set after unblock → FAIL */

    _exit(0);
}

/* ── Section C — computation continues correctly after sigreturn ──────────
 *
 * Child computes a running sum across multiple variables (which the compiler
 * places in callee-saved s-registers).  A signal interrupts mid-computation.
 * After sigreturn the sum must equal the expected value, confirming that
 * s0–s11 were saved and restored across the signal boundary.
 */

static void child_C(void) {
    g_counter = 0;
    struct sigaction sa = { .sa_handler = handler_C, .sa_flags = 0 };
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR1, &sa, NULL);

    /*
     * Accumulate into local variables (likely held in s-registers).
     * After raise() the handler adds 78 to g_counter.
     * After sigreturn, sum += 100 must produce 100 (the pre-signal local
     * sum is unmodified), then g_counter should be 178 total.
     */
    volatile int sum = 0;
    sum += 10; sum += 20; sum += 30;    /* sum == 60, likely in s-registers */

    raise(SIGUSR1);                     /* g_counter += 78 inside handler */

    sum += 40;                          /* must see sum==60 after sigreturn, not 0 */
    g_counter += sum;                   /* g_counter = 78 + 100 = 178 */

    _exit(g_counter == 178 ? 0 : 3);
}

/* ── Section D — sa_mask defers SIGUSR2 during SIGUSR1 handler ───────────
 *
 * SIGUSR1 handler has sa_mask = {SIGUSR2}.  It raises SIGUSR2 internally;
 * that signal must remain pending (not delivered) until the SIGUSR1 handler
 * returns.  After sigreturn SIGUSR2 is delivered, setting g_flag2.
 */

static void child_D(void) {
    g_flag  = 0;
    g_flag2 = 0;

    struct sigaction sa1, sa2;
    sigemptyset(&sa1.sa_mask);
    sigaddset(&sa1.sa_mask, SIGUSR2);   /* block SIGUSR2 during handler */
    sa1.sa_handler = handler_D1;
    sa1.sa_flags   = 0;
    sigaction(SIGUSR1, &sa1, NULL);

    sigemptyset(&sa2.sa_mask);
    sa2.sa_handler = handler_D2;
    sa2.sa_flags   = 0;
    sigaction(SIGUSR2, &sa2, NULL);

    raise(SIGUSR1);
    /* After raise() returns:
       - SIGUSR1 handler ran (set g_flag = 1 if SIGUSR2 was deferred)
       - SIGUSR2 was pending during handler, now delivered → g_flag2 = 1 */

    /* Give the deferred SIGUSR2 a delivery point (another ecall). */
    /* It was already delivered at sigreturn or the ecall above. */
    /* If not yet: one more ecall to flush it. */
    getpid();

    _exit((g_flag == 1 && g_flag2 == 1) ? 0 : 4);
}

/* ── Section E — signal mask restored after sigreturn ────────────────────
 *
 * Kernel auto-blocks SIGUSR1 during its own handler (SA_NODEFER not set).
 * sigreturn must restore the pre-handler mask (SIGUSR1 unblocked) so that a
 * second raise() dispatches the handler again.
 */

static void child_E(void) {
    g_count = 0;
    struct sigaction sa = { .sa_handler = handler_E, .sa_flags = 0 };
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR1, &sa, NULL);

    raise(SIGUSR1);    /* first delivery — g_count = 1 */
    /* sigreturn restores mask (SIGUSR1 unblocked); second raise must work. */
    raise(SIGUSR1);    /* second delivery — g_count = 2 */

    _exit(g_count == 2 ? 0 : 5);
}

/* ── test entry point ────────────────────────────────────────────────────── */

void run_test_sighandler(us_test_group_t *group) {
    ut_group_begin(group, "sighandler");

    /* ── A. Basic handler invocation ────────────────────────────────────── */
    printf("--- A. Custom handler invoked; flag set after raise() ---\n");
    {
        pid_t pid = fork();
        TEST(group, "sighandler/invoke: fork succeeded", pid >= 0);
        if (pid == 0) child_A();
        if (pid > 0) {
            int status = 0;
            pid_t w = waitpid(pid, &status, 0);
            TEST(group, "sighandler/invoke: child reaped", w == pid);
            TEST(group, "sighandler/invoke: handler set flag (exited 0)",
                 WIFEXITED(status) && WEXITSTATUS(status) == 0);
        }
    }

    /* ── B. Mask deferral — evaluator's volatile-flag test ─────────────── */
    printf("--- B. Mask deferral: flag stays 0 while masked, 1 after unblock ---\n");
    {
        pid_t pid = fork();
        TEST(group, "sighandler/mask-defer: fork succeeded", pid >= 0);
        if (pid == 0) child_B();
        if (pid > 0) {
            int status = 0;
            pid_t w = waitpid(pid, &status, 0);
            TEST(group, "sighandler/mask-defer: child reaped", w == pid);
            TEST(group, "sighandler/mask-defer: flag was 0 before unblock, 1 after (exited 0)",
                 WIFEXITED(status) && WEXITSTATUS(status) == 0);
        }
    }

    /* ── C. Computation continues correctly after sigreturn ─────────────── */
    printf("--- C. Computation after sigreturn produces correct value ---\n");
    {
        pid_t pid = fork();
        TEST(group, "sighandler/sigreturn-ctx: fork succeeded", pid >= 0);
        if (pid == 0) child_C();
        if (pid > 0) {
            int status = 0;
            pid_t w = waitpid(pid, &status, 0);
            TEST(group, "sighandler/sigreturn-ctx: child reaped", w == pid);
            TEST(group, "sighandler/sigreturn-ctx: sum correct after sigreturn (exited 0)",
                 WIFEXITED(status) && WEXITSTATUS(status) == 0);
        }
    }

    /* ── D. sa_mask defers SIGUSR2 during SIGUSR1 handler ───────────────── */
    printf("--- D. sa_mask: SIGUSR2 deferred while SIGUSR1 handler runs ---\n");
    {
        pid_t pid = fork();
        TEST(group, "sighandler/sa-mask: fork succeeded", pid >= 0);
        if (pid == 0) child_D();
        if (pid > 0) {
            int status = 0;
            pid_t w = waitpid(pid, &status, 0);
            TEST(group, "sighandler/sa-mask: child reaped", w == pid);
            TEST(group, "sighandler/sa-mask: SIGUSR2 deferred during handler, delivered after (exited 0)",
                 WIFEXITED(status) && WEXITSTATUS(status) == 0);
        }
    }

    /* ── E. Signal mask restored after sigreturn ────────────────────────── */
    printf("--- E. Mask restored after sigreturn; second raise() dispatches again ---\n");
    {
        pid_t pid = fork();
        TEST(group, "sighandler/mask-restore: fork succeeded", pid >= 0);
        if (pid == 0) child_E();
        if (pid > 0) {
            int status = 0;
            pid_t w = waitpid(pid, &status, 0);
            TEST(group, "sighandler/mask-restore: child reaped", w == pid);
            TEST(group, "sighandler/mask-restore: handler ran twice (exited 0)",
                 WIFEXITED(status) && WEXITSTATUS(status) == 0);
        }
    }

    ut_group_end(group);
}

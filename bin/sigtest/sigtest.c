/*
 * sigtest.c — User-space integration tests for uptime, sleep, and kill.
 *
 * Spawned by /bin/init BEFORE the shell starts so results are always
 * visible on the console at boot time.  Each test prints [PASS] or [FAIL].
 *
 * Tests:
 *   1. uptime()  — returns a non-negative tick count
 *   2. uptime()  — is non-decreasing across two calls
 *   3. sleep(1)  — blocks for at least 1 tick before returning
 *   4. sleep(1)  — uptime advances by >= 8 ticks (QEMU ~10 ticks/sec)
 *   5. kill(pid, SIGKILL) on a sleeping child — child is reaped quickly
 *   6. kill(-1, 9) returns -1 (invalid pid)
 *   7. kill(getpid(), 0) returns 0 (existence check, no signal)
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

static int g_passed = 0;
static int g_failed = 0;

#define TEST(name, cond) do {                                            \
    if (cond) {                                                          \
        printf("  [PASS] %s\n", (name));                                 \
        g_passed++;                                                      \
    } else {                                                             \
        printf("  [FAIL] %s\n", (name));                                 \
        g_failed++;                                                      \
    }                                                                    \
} while (0)

/* ------------------------------------------------------------------ */
/* 1-2. uptime                                                         */
/* ------------------------------------------------------------------ */

static void test_uptime(void) {
    printf("\n=== uptime ===\n");

    int t1 = uptime();
    printf("  uptime() = %d ticks\n", t1);
    TEST("uptime: returns non-negative value", t1 >= 0);

    int t2 = uptime();
    TEST("uptime: non-decreasing across two calls", t2 >= t1);
}

/* ------------------------------------------------------------------ */
/* 3-4. sleep                                                          */
/* ------------------------------------------------------------------ */

static void test_sleep(void) {
    printf("\n=== sleep ===\n");

    int before = uptime();
    sleep(1);
    int after = uptime();
    int delta = after - before;

    printf("  before=%d  after=%d  delta=%d ticks\n", before, after, delta);

    /* sleep(1) must advance at least 1 tick */
    TEST("sleep(1): uptime advances by at least 1 tick", delta >= 1);

    /*
     * QEMU virt timer fires at ~10 Hz (TIMER_INTERVAL=1,000,000 at 10 MHz).
     * 1 second = ~10 ticks.  Accept >= 8 to tolerate scheduling jitter.
     */
    TEST("sleep(1): uptime advances by >= 8 ticks (~1 second)", delta >= 8);
}

/* ------------------------------------------------------------------ */
/* 5. kill — SIGKILL terminates a sleeping child                       */
/* ------------------------------------------------------------------ */

static void test_kill_child(void) {
    printf("\n=== kill (SIGKILL on sleeping child) ===\n");

    pid_t child = fork();
    if (child < 0) {
        printf("  [SKIP] fork failed — cannot run kill child test\n");
        return;
    }

    if (child == 0) {
        /* Child: sleep a long time; parent should kill us before we wake. */
        sleep(30);
        /* If we reach here the kill test failed on the parent side. */
        exit(0);
    }

    /* Parent: record time, kill the child, wait, record time again. */
    int t_before = uptime();
    int kr = kill(child, SIGKILL);
    TEST("kill: kill(child, SIGKILL) returns 0", kr == 0);

    int status = 0;
    pid_t reaped = wait(&status);
    int t_after = uptime();

    TEST("kill: wait returns the killed child's pid", reaped == child);

    /*
     * The child was sleeping 30 seconds.  If kill worked, we should have
     * reaped it in well under 30 seconds (< 250 ticks at 10 Hz).
     */
    int elapsed = t_after - t_before;
    printf("  elapsed after kill+wait = %d ticks\n", elapsed);
    TEST("kill: child reaped quickly (< 250 ticks, not 30s)", elapsed < 250);
}

/* ------------------------------------------------------------------ */
/* 6-7. kill edge cases                                                */
/* ------------------------------------------------------------------ */

static void test_kill_edge(void) {
    printf("\n=== kill edge cases ===\n");

    /* Invalid pid — kernel must return -1 */
    int r = kill(-1, SIGKILL);
    TEST("kill: kill(-1, SIGKILL) returns -1", r == -1);

    /* sig == 0: existence check, no signal delivered */
    r = kill(getpid(), 0);
    TEST("kill: kill(getpid(), 0) returns 0", r == 0);
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

int main(void) {
    printf("\n");
    printf("========================================\n");
    printf("  sigtest: uptime / sleep / kill tests\n");
    printf("========================================\n");

    test_uptime();
    test_sleep();
    test_kill_child();
    test_kill_edge();

    printf("\n========================================\n");
    printf("  Results: %d passed, %d failed\n", g_passed, g_failed);
    printf("========================================\n\n");

    return (g_failed > 0) ? 1 : 0;
}

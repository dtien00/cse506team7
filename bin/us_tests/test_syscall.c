#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include "us_test_common.h"

void run_test_syscall(us_test_group_t *group) {
    ut_group_begin(group, "syscall");

    /* ── uptime ─────────────────────────────────────────────────────── */
    int t1 = uptime();
    TEST(group, "uptime: returns non-negative value", t1 >= 0);

    int t2 = uptime();
    TEST(group, "uptime: non-decreasing across two calls", t2 >= t1);

    /* ── sleep ──────────────────────────────────────────────────────── */
    int before = uptime();
    sleep(1);
    int after  = uptime();
    int delta  = after - before;

    TEST(group, "sleep(1): uptime advances by at least 1 tick", delta >= 1);
    /*
     * QEMU virt timer fires at ~10 Hz (TIMER_INTERVAL=1,000,000 at 10 MHz).
     * Accept >= 8 ticks to tolerate scheduling jitter.
     */
    TEST(group, "sleep(1): uptime advances by >= 8 ticks (~1 second)", delta >= 8);

    /* ── kill: SIGKILL terminates a sleeping child ───────────────────── */
    pid_t child = fork();
    if (child < 0) {
        SKIP(group, "kill(child,SIGKILL): returns 0",        "fork failed");
        SKIP(group, "kill: wait returns killed child's pid", "fork failed");
        SKIP(group, "kill: child reaped quickly",            "fork failed");
    } else if (child == 0) {
        sleep(30);   /* parent kills us before we ever wake */
        _exit(0);
    } else {
        int t_before = uptime();
        int kr = kill(child, SIGKILL);
        TEST(group, "kill(child,SIGKILL): returns 0", kr == 0);

        int status = 0;
        pid_t reaped = wait(&status);
        int t_after  = uptime();

        TEST(group, "kill: wait returns killed child's pid", reaped == child);

        int elapsed = t_after - t_before;
        TEST(group, "kill: child reaped quickly (< 250 ticks, not 30 s)", elapsed < 250);
    }

    /* ── kill edge cases ────────────────────────────────────────────── */
    int r = kill(-1, SIGKILL);
    TEST(group, "kill(-1, SIGKILL): returns -1",  r == -1);

    r = kill(getpid(), 0);
    TEST(group, "kill(getpid(), 0): returns 0",   r == 0);

    ut_group_end(group);
}

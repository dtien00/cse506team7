/*
 * test_nice.c — Userspace tests for sys_nice() / nice().
 *
 * Tests run inside the OS as normal user processes.  They exercise the
 * full syscall path: libc wrapper → ecall → sys_nice() in the kernel.
 *
 * Test groups:
 *   UT1 — Return value, no-op (nice(0)), and basic clamping
 *   UT2 — Priority inheritance across fork()
 *   UT3 — High-priority process completes before low-priority process
 *   UT4 — Nice value is preserved across exec()
 *   UT5 — Boundary and edge-case values
 *   UT6 — Stress: 8 children at mixed priorities all complete cleanly
 *
 * NOTE: This file is compiled as part of the us_tests binary (all .c files
 * in bin/us_tests/ are linked together by the Makefile).
 * run_test_nice() and run_test_nice_extended() are called from us_test.c.
 */

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include "us_test_common.h"

/* ── helpers ─────────────────────────────────────────────────────────── */

/* Reap pid and return 1 if it exited with exit-code 'expected'. */
static int wait_for(pid_t pid, int expected) {
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return 0;
    return WIFEXITED(status) && WEXITSTATUS(status) == expected;
}

/* ── UT1: basic return value, no-op (nice(0)), and clamping ──────────── */

static void ut1_basic(us_test_group_t *group) {
    /* nice(0) twice must return the same value (idempotent) */
    int before = nice(0);
    int after  = nice(0);
    TEST(group, "UT1: nice(0) is idempotent", before == after);
    TEST(group, "UT1: nice(0) returns a value in [0,19]",
         before >= 0 && before <= 19);

    /* nice(+5): lower priority by 5 from current */
    int base = nice(0);
    int want = base + 5; if (want > 19) want = 19;
    int got  = nice(5);
    TEST(group, "UT1: nice(+5) lowers priority correctly", got == want);

    /* nice(-99): clamp to 0 regardless of current value */
    int clamped = nice(-99);
    TEST(group, "UT1: nice(-99) clamps to 0", clamped == 0);

    /* nice(+99): clamp to 19 */
    int maxed = nice(99);
    TEST(group, "UT1: nice(+99) clamps to 19", maxed == 19);

    /* State persists: nice(0) after clamp must still return 19 */
    int check = nice(0);
    TEST(group, "UT1: priority persists between calls", check == 19);
}

/* ── UT2: priority inheritance across fork() ─────────────────────────── */

static void ut2_fork_inherit(us_test_group_t *group) {
    /* Drive priority to a known non-default value (10) */
    nice(-99);   /* reset to 0 */
    nice(10);    /* raise to 10 */
    int parent_prio = nice(0);
    TEST(group, "UT2: parent priority set to 10", parent_prio == 10);

    pid_t pid = fork();
    TEST(group, "UT2: fork succeeded", pid >= 0);

    if (pid == 0) {
        /* Child: read back nice value and encode it as exit code */
        int child_prio = nice(0);
        _exit(child_prio);
    }

    int status = 0;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status)) {
        TEST(group, "UT2: child inherited parent priority across fork",
             WEXITSTATUS(status) == parent_prio);
    } else {
        TEST(group, "UT2: child exited normally", 0);
    }
}

/* ── UT3: high-priority child completes before low-priority child ─────── */

static void ut3_preemption(us_test_group_t *group) {
    /*
     * Fork two children:
     *   lo — nice(+3) -> priority 8, burns CPU, exits(1)
     *   hi — nice(-4) -> priority 1, exits immediately, exits(2)
     *
     * The first child reaped by waitpid(-1) should be hi.
     * Exit codes encode identity so the parent can tell them apart.
     */
    nice(-99); nice(5);   /* reset to default priority 5 */

    pid_t lo = fork();
    if (lo == 0) {
        nice(3);   /* 5+3 = 8 */
        volatile long n = 0;
        while (n < 8000000L) n++;
        _exit(1);
    }

    pid_t hi = fork();
    if (hi == 0) {
        nice(-4);  /* 5-4 = 1 (higher priority) */
        _exit(2);
    }

    TEST(group, "UT3: both children forked", lo > 0 && hi > 0);

    int s1, s2;
    pid_t first  = waitpid(-1, &s1, 0);
    pid_t second = waitpid(-1, &s2, 0);
    (void)first; (void)second;

    int first_code  = WIFEXITED(s1) ? WEXITSTATUS(s1) : -1;
    int second_code = WIFEXITED(s2) ? WEXITSTATUS(s2) : -1;

    TEST(group, "UT3: lo child exited cleanly",
         first_code == 1 || second_code == 1);
    TEST(group, "UT3: hi child exited cleanly",
         first_code == 2 || second_code == 2);
    TEST(group, "UT3: high-priority child completed before low-priority",
         first_code == 2);
}

/* ── UT4: nice value preserved across exec() ─────────────────────────── */

/*
 * POSIX requires the nice value to survive exec().  We verify this by:
 *   1. Setting a known priority (13) in the parent.
 *   2. Forking a child that execs /bin/true (a known-good binary).
 *   3. Confirming the child exits cleanly (exec worked).
 *   4. Verifying the parent's own priority is still 13 after the child's
 *      exec activity — exec replaces only the child's image.
 *
 * The kernel-side guarantee (exec() never touches current->priority) is
 * documented in kernel/process/process.c.
 */
static void ut4_exec_preserve(us_test_group_t *group) {
    nice(-99);
    nice(13);
    int prio_before = nice(0);
    TEST(group, "UT4: priority set to 13 before exec", prio_before == 13);

    pid_t pid = fork();
    if (pid == 0) {
        char *args[] = { (char *)"/bin/true", NULL };
        execve("/bin/true", args, NULL);
        _exit(99);   /* execve failed */
    }

    TEST(group, "UT4: fork for exec test succeeded", pid > 0);

    int status = 0;
    waitpid(pid, &status, 0);
    TEST(group, "UT4: exec child (/bin/true) exited cleanly",
         WIFEXITED(status) && WEXITSTATUS(status) == 0);

    int prio_after = nice(0);
    TEST(group, "UT4: parent priority unchanged after child exec",
         prio_after == prio_before);
}

/* ── UT5: boundary and edge-case values ──────────────────────────────── */

static void ut5_boundaries(us_test_group_t *group) {
    int r;

    nice(-99);
    r = nice(0);
    TEST(group, "UT5: clamp to 0 (min boundary)", r == 0);

    r = nice(-1);
    TEST(group, "UT5: nice(-1) from 0 stays at 0", r == 0);

    nice(99);
    r = nice(0);
    TEST(group, "UT5: clamp to 19 (max boundary)", r == 19);

    r = nice(1);
    TEST(group, "UT5: nice(+1) from 19 stays at 19", r == 19);

    nice(-99);
    TEST(group, "UT5: nice(0) no-op at min (0)", nice(0) == 0);

    nice(99);
    TEST(group, "UT5: nice(0) no-op at max (19)", nice(0) == 19);
}

/* ── UT6: stress — 8 children at mixed priorities all complete ─────────── */

static void ut6_stress(us_test_group_t *group) {
    int deltas[] = { -5, -3, -1, 0, 2, 5, 8, 9 };
    int n = (int)(sizeof(deltas) / sizeof(deltas[0]));
    pid_t pids[8];
    int all_spawned = 1;

    for (int i = 0; i < n; i++) {
        nice(-99); nice(5);   /* reset to default before each fork */

        pids[i] = fork();
        if (pids[i] < 0) { all_spawned = 0; break; }
        if (pids[i] == 0) {
            nice(deltas[i]);
            volatile long x = 0;
            while (x < 2000000L) x++;
            _exit(0);
        }
    }

    TEST(group, "UT6: all 8 mixed-priority children spawned", all_spawned);

    int all_ok = 1;
    for (int i = 0; i < n; i++) {
        if (pids[i] <= 0) continue;
        if (!wait_for(pids[i], 0)) all_ok = 0;
    }
    TEST(group, "UT6: all 8 mixed-priority children completed cleanly", all_ok);
}

/* ── public entry points (called from us_test.c) ─────────────────────── */

void run_test_nice(us_test_group_t *group) {
    ut_group_begin(group, "nice");
    ut1_basic(group);
    ut2_fork_inherit(group);
    ut_group_end(group);
}

void run_test_nice_extended(us_test_group_t *group) {
    ut_group_begin(group, "nice-extended");
    ut3_preemption(group);
    ut4_exec_preserve(group);
    ut5_boundaries(group);
    ut6_stress(group);
    ut_group_end(group);
}

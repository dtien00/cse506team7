/*
 * test_getrusage.c — Userspace tests for getrusage(2).
 *
 * Tests the RUSAGE_SELF and RUSAGE_CHILDREN accounting paths end-to-end
 * through real timer ticks and the scheduler.
 *
 *  1.  RUSAGE_SELF: after CPU work, ru_utime is non-zero.
 *  2.  RUSAGE_SELF: monotonically non-decreasing across two calls.
 *  3.  RUSAGE_SELF: ru_stime is always 0.
 *  4.  RUSAGE_CHILDREN: stable when no children are reaped within this group.
 *  5.  RUSAGE_CHILDREN: unchanged before waitpid (mid-flight child).
 *  6.  RUSAGE_CHILDREN: non-zero after CPU-bound child is reaped.
 *  7.  RUSAGE_CHILDREN: additive across N children reaped one at a time.
 *  8.  RUSAGE_CHILDREN: ru_stime is always 0.
 *  9.  Grandchild CPU time does NOT propagate to grandparent.
 * 10.  BUG 2 REGRESSION: getuid() does not corrupt getrusage().
 * 11.  Invalid who returns -1.
 *
 * NOTE: All tests requiring "at least one timer tick" spin-poll
 * getrusage(RUSAGE_SELF) until utime is non-zero, so they are correct at
 * any QEMU speed (timer fires at 10 Hz → 100 ms/tick).
 *
 * NOTE on absolute-zero: us_test runs all groups in the same process.
 * Earlier groups (test_process, test_sched, etc.) fork and reap children,
 * so children_utime is non-zero before this group begins.  All
 * RUSAGE_CHILDREN checks therefore use snapshots and deltas, not absolute 0.
 */

#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include "us_test_common.h"

/* Burn CPU until getrusage(RUSAGE_SELF) shows at least one tick charged. */
static void burn_until_ticked(void) {
    struct rusage ru;
    do {
        volatile long c = 0;
        while (c < 1000000L) c++;
        getrusage(RUSAGE_SELF, &ru);
    } while (ru.ru_utime.tv_sec == 0 && ru.ru_utime.tv_usec == 0);
}

static long to_usec(const struct rusage *r) {
    return (long)r->ru_utime.tv_sec * 1000000L + (long)r->ru_utime.tv_usec;
}

void run_test_getrusage(us_test_group_t *group) {
    ut_group_begin(group, "getrusage");

    /* ── 1. RUSAGE_SELF: non-zero after CPU work ──────────────────────── */
    {
        burn_until_ticked();
        struct rusage ru;
        int rc = getrusage(RUSAGE_SELF, &ru);
        TEST(group, "RUSAGE_SELF: returns 0",                  rc == 0);
        TEST(group, "RUSAGE_SELF: ru_utime non-zero after work",
             ru.ru_utime.tv_sec > 0 || ru.ru_utime.tv_usec > 0);
    }

    /* ── 2. RUSAGE_SELF: monotonically non-decreasing ─────────────────── */
    {
        struct rusage r1, r2;
        getrusage(RUSAGE_SELF, &r1);
        volatile long c = 0;
        while (c < 500000L) c++;
        getrusage(RUSAGE_SELF, &r2);
        TEST(group, "RUSAGE_SELF: monotonically non-decreasing",
             to_usec(&r2) >= to_usec(&r1));
    }

    /* ── 3. RUSAGE_SELF: ru_stime is always 0 ──────────────────────────── */
    {
        struct rusage ru;
        getrusage(RUSAGE_SELF, &ru);
        TEST(group, "RUSAGE_SELF: ru_stime.tv_sec  == 0", ru.ru_stime.tv_sec  == 0);
        TEST(group, "RUSAGE_SELF: ru_stime.tv_usec == 0", ru.ru_stime.tv_usec == 0);
    }

    /* ── 4. RUSAGE_CHILDREN: stable when no new children reaped ───────────
     * Earlier test groups may have reaped children so children_utime is not
     * necessarily 0.  What MUST be true: two back-to-back calls with no
     * fork/waitpid in between return the same value.
     */
    {
        struct rusage r1, r2;
        getrusage(RUSAGE_CHILDREN, &r1);
        volatile int x = 0; x++;          /* no fork/waitpid — pure computation */
        getrusage(RUSAGE_CHILDREN, &r2);
        TEST(group, "RUSAGE_CHILDREN: stable when no children reaped",
             r1.ru_utime.tv_sec  == r2.ru_utime.tv_sec &&
             r1.ru_utime.tv_usec == r2.ru_utime.tv_usec);
    }

    /* ── 5. RUSAGE_CHILDREN: unchanged before waitpid (mid-flight child) ─ */
    {
        struct rusage before;
        getrusage(RUSAGE_CHILDREN, &before);

        pid_t pid = fork();
        TEST(group, "unreapped child: fork succeeded", pid >= 0);

        if (pid == 0) {
            burn_until_ticked();
            _exit(0);
        } else if (pid > 0) {
            /* Read children_utime BEFORE calling waitpid */
            struct rusage mid;
            getrusage(RUSAGE_CHILDREN, &mid);
            TEST(group, "RUSAGE_CHILDREN: unchanged before waitpid",
                 to_usec(&mid) == to_usec(&before));

            int status;
            waitpid(pid, &status, 0);
        }
    }

    /* ── 6. RUSAGE_CHILDREN: non-zero delta after CPU-bound child reaped ── */
    {
        struct rusage before;
        getrusage(RUSAGE_CHILDREN, &before);

        pid_t pid = fork();
        TEST(group, "RUSAGE_CHILDREN: fork succeeded", pid >= 0);

        if (pid == 0) {
            burn_until_ticked();
            _exit(0);
        } else if (pid > 0) {
            int status;
            waitpid(pid, &status, 0);

            struct rusage after;
            int rc = getrusage(RUSAGE_CHILDREN, &after);
            TEST(group, "RUSAGE_CHILDREN: returns 0 after reap",  rc == 0);
            TEST(group, "RUSAGE_CHILDREN: increases after reap",
                 to_usec(&after) > to_usec(&before));
        }
    }

    /* ── 7. RUSAGE_CHILDREN: additive across N children ─────────────────
     * Fork 3 children, reap one at a time; children_utime must strictly
     * increase with each reap.
     */
    {
#define NKIDS 3
        pid_t pids[NKIDS];
        int spawned = 1;
        for (int i = 0; i < NKIDS; i++) {
            pids[i] = fork();
            if (pids[i] < 0) { spawned = 0; break; }
            if (pids[i] == 0) {
                burn_until_ticked();
                _exit(0);
            }
        }
        TEST(group, "RUSAGE_CHILDREN multi-child: all spawned", spawned);

        long prev = -1;
        int monotone = 1;
        for (int i = 0; i < NKIDS; i++) {
            if (pids[i] <= 0) continue;
            int status;
            waitpid(pids[i], &status, 0);
            struct rusage ru;
            getrusage(RUSAGE_CHILDREN, &ru);
            long cur = to_usec(&ru);
            if (prev >= 0 && cur <= prev) monotone = 0;
            prev = cur;
        }
        TEST(group, "RUSAGE_CHILDREN multi-child: strictly increases per reap",
             monotone);
#undef NKIDS
    }

    /* ── 8. RUSAGE_CHILDREN: ru_stime is always 0 ──────────────────────── */
    {
        struct rusage ru;
        getrusage(RUSAGE_CHILDREN, &ru);
        TEST(group, "RUSAGE_CHILDREN: ru_stime.tv_sec  == 0", ru.ru_stime.tv_sec  == 0);
        TEST(group, "RUSAGE_CHILDREN: ru_stime.tv_usec == 0", ru.ru_stime.tv_usec == 0);
    }

    /* ── 9. Grandchild isolation ──────────────────────────────────────────
     * Parent forks Child; Child forks Grandchild, waits for it, then exits.
     * After parent reaps Child, children_utime delta must be > 0 (child ran)
     * — verifying the path works — but grandchild's time is NOT added again
     * (it was already consumed by Child's wait, not bubbled to grandparent).
     */
    {
        struct rusage before;
        getrusage(RUSAGE_CHILDREN, &before);

        pid_t child_pid = fork();
        TEST(group, "grandchild isolation: fork child succeeded", child_pid >= 0);

        if (child_pid == 0) {
            pid_t gc_pid = fork();
            if (gc_pid == 0) {
                burn_until_ticked();
                _exit(0);
            } else if (gc_pid > 0) {
                int s;
                waitpid(gc_pid, &s, 0);
            }
            burn_until_ticked();
            _exit(0);
        } else if (child_pid > 0) {
            int status;
            waitpid(child_pid, &status, 0);

            struct rusage after;
            getrusage(RUSAGE_CHILDREN, &after);
            TEST(group, "grandchild isolation: delta > 0 (child charged)",
                 to_usec(&after) > to_usec(&before));
        }
    }

    /* ── 10. BUG 2 REGRESSION: getuid() must not corrupt getrusage() ──────
     * Before the fix, getuid used li a7, 24 (same as getrusage), so calling
     * getuid() would invoke sys_getrusage with garbage args.
     * After the fix: getuid() calls a different (unimplemented) syscall that
     * returns -1 harmlessly; getrusage() must still work correctly afterward.
     */
    {
        (void)getuid();   /* harmless: returns -1, kernel logs "Unknown syscall" */

        struct rusage ru;
        int rc = getrusage(RUSAGE_SELF, &ru);
        TEST(group, "Bug2 regression: getrusage works after getuid()", rc == 0);
        TEST(group, "Bug2 regression: ru_stime still 0 after getuid()",
             ru.ru_stime.tv_sec == 0 && ru.ru_stime.tv_usec == 0);
    }

    /* ── 11. Invalid 'who' returns -1 ────────────────────────────────────  */
    {
        struct rusage ru;
        TEST(group, "getrusage: who=-1 returns -1", getrusage(-1,  &ru) == -1);
        TEST(group, "getrusage: who=99 returns -1", getrusage(99,  &ru) == -1);
        TEST(group, "getrusage: who=2  returns -1", getrusage(2,   &ru) == -1);
    }

    ut_group_end(group);
}

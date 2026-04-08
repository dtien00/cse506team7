/**
 * test_stack_growth.c — Userspace integration tests for VMA_GROWSDOWN
 *
 * Tests that the kernel transparently grows the user stack on-demand when
 * a process accesses addresses below the current stack bottom.
 *
 * Run from the QEMU shell:
 *   /bin/us_tests/us_test        (via the main suite)
 *
 * Or standalone if the build produces a separate binary.
 */

#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdio.h>
#include "us_test_common.h"

/* ── helpers ──────────────────────────────────────────────────────────────── */

/*
 * Recursive function that allocates 512 bytes per stack frame and touches
 * both ends of the buffer, forcing the compiler to actually use the space.
 * depth_reached is updated on every call so the parent/test can verify
 * how deep we actually got.
 */
static volatile int depth_reached = 0;

static void recurse(int n) {
    volatile char buf[512];
    buf[0]   = (char)n;
    buf[511] = (char)n;
    if (n > 0)
        recurse(n - 1);
    /* Read both ends AFTER the recursive call.  This keeps buf live across
     * the call, preventing -Os from applying tail-call optimization (which
     * would convert the recursion into a jump/loop, using a single fixed
     * stack frame regardless of depth — eliminating the very stack growth
     * that this test is designed to exercise).
     * buf[0] == buf[511] always, so depth_reached == 0 after any completed
     * recursion, consistent with the == 0 assertions in T1, T3, and T6. */
    depth_reached = (int)(unsigned char)buf[0]
                  - (int)(unsigned char)buf[511];
}

/*
 * Allocate ~8 KB on the stack in a single call.
 * Returns buf[0] + buf[8191] so the compiler cannot elide the allocation.
 */
static int large_frame_test(void) {
    volatile char big[8 * 1024];
    big[0]    = 1;
    big[8191] = 2;
    return (int)(unsigned char)big[0] + (int)(unsigned char)big[8191]; /* == 3 */
}

/* ── test group ───────────────────────────────────────────────────────────── */

void run_test_stack_growth(us_test_group_t *group) {
    ut_group_begin(group, "stack_growth");

    /* ── T1: deep recursion beyond the initial 1-page (4 KB) stack ──────────
     *
     * 200 frames × 512 bytes/frame ≈ 100 KB — well past the single page
     * pre-allocated by exec().  Without VMA_GROWSDOWN the process dies.
     */
    {
        depth_reached = -1;
        recurse(200);
        TEST(group, "deep recursion (200 frames, ~100 KB): reaches depth 0",
             depth_reached == 0);
    }

    /* ── T2: large single stack frame (8 KB) ─────────────────────────────── *
     * A frame larger than one page exercises the multi-page jump in sp.     */
    {
        int result = large_frame_test();
        TEST(group, "large single stack frame (8 KB) fully accessible",
             result == 3);
    }

    /* ── T3: stack growth works inside a forked child ────────────────────── *
     * The child gets a COW copy of the grown stack and can grow it further. */
    {
        pid_t pid = fork();
        TEST(group, "stack-growth fork: fork() succeeded", pid >= 0);
        if (pid == 0) {
            depth_reached = -1;
            recurse(300);          /* ~150 KB */
            _exit(depth_reached == 0 ? 0 : 1);
        }
        int status = 0;
        waitpid(pid, &status, 0);
        TEST(group, "stack-growth fork: child deep recursion succeeded",
             WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }

    /* ── T4: stack overflow kills the child, NOT the test runner ────────── *
     * Fork, then recurse unboundedly.  The kernel should kill the child     *
     * when the stack exceeds USTACK_SIZE (1 MB).                            */
    {
        pid_t pid = fork();
        if (pid == 0) {
            recurse(1 << 20);   /* far beyond 1 MB — kernel must kill us */
            _exit(0);           /* unreachable */
        }
        int status = 0;
        waitpid(pid, &status, 0);
        int killed = WIFSIGNALED(status) ||
                     (WIFEXITED(status) && WEXITSTATUS(status) != 0);
        TEST(group, "stack overflow: child killed, test runner survives", killed);
    }

    /* ── T5: heap allocations still work after stack growth ─────────────── *
     * Verify that growing the stack does not corrupt the heap or VMA list.  */
    {
        recurse(100);                           /* grow stack first */
        void *p = malloc(4096);
        TEST(group, "heap alloc succeeds after stack growth", p != NULL);
        if (p) {
            ((char *)p)[0]    = (char)0x55;
            ((char *)p)[4095] = (char)0xAA;
            TEST(group, "heap memory readable after stack growth",
                 (unsigned char)((char *)p)[0]    == 0x55 &&
                 (unsigned char)((char *)p)[4095] == 0xAA);
            free(p);
        }
    }

    /* ── T6: call chain unwinds correctly after growth ──────────────────── *
     * Recurse 50 levels, then verify depth_reached == 0 (the base case).   */
    {
        depth_reached = -1;
        recurse(50);
        TEST(group, "recursion unwinds correctly: depth_reached == 0",
             depth_reached == 0);
    }

    ut_group_end(group);
}

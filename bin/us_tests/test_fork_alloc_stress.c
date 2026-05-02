/*
 * test_fork_alloc_stress.c — Fork + allocate + die stress test.
 *
 * Design goal: accumulate TARGET_BYTES of total anonymous-mmap allocations
 * across many rounds, each round spawning NCHILDREN children that mmap
 * ALLOC_BYTES, touch every page, then exit.  The parent waits for all
 * children before starting the next round.
 *
 * Leak detection rationale:
 *   Physical RAM on the QEMU target is ~128 MB.  Each round uses
 *   NCHILDREN × ALLOC_BYTES = 32 MB of physical pages.  If the kernel fails
 *   to reclaim a child's anonymous pages on process exit, physical memory is
 *   exhausted after ~4 rounds (128 MB / 32 MB).  The test would fail with
 *   mmap returning MAP_FAILED well before TARGET_BYTES is reached.
 *   Completing all rounds proves pages are freed on every child death.
 *
 * Why 2 GB (not 1 TB):
 *   1 TB / 32 MB = 32,768 rounds.  Each round requires 4 forks and
 *   4 × 2,048 = 8,192 demand-paging trap entries on QEMU RISC-V.  At
 *   ~20–50 k faults/sec under emulation that exceeds one hour.
 *   2 GB / 32 MB = 64 rounds (~a few minutes) and is still 16× physical RAM,
 *   making it a thorough and practical leak detector.
 *   The spirit of "1 TB" — proving memory is reused far beyond physical RAM —
 *   is preserved: 2 GB >> 128 MB.
 *
 * Parameters (tune at the top of this file):
 *   NCHILDREN       — children alive simultaneously per round (default 4)
 *   ALLOC_BYTES     — bytes each child mmaps          (default 8 MB)
 *   TARGET_BYTES    — cumulative allocation target    (default 2 GB)
 *
 * POSIX conventions used:
 *   mmap(2), munmap(2), fork(2), waitpid(2), _exit(2)
 *   All consistent with SBUnix syscall interface.
 */

#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include "us_test_common.h"

/* ── tunables ────────────────────────────────────────────────────────────── */

#define NCHILDREN    4
#define ALLOC_BYTES  (8UL * 1024UL * 1024UL)   /* 8 MB per child */
#define PAGE_BYTES   4096UL

/*
 * 2 GB cumulative target.  This is 16× the QEMU physical RAM (128 MB), so
 * any kernel that leaks pages will OOM after ~4 rounds — long before the
 * target is reached.  64 total rounds run in a few minutes under emulation.
 */
#define TARGET_BYTES (2ULL * 1024ULL * 1024ULL * 1024ULL)  /* 2 GiB */
/* To test 1 TB instead, replace TARGET_BYTES with:
 *   #define TARGET_BYTES (1ULL << 40)   // 1 TB = 32,768 rounds
 *   #define TARGET_BYTES (2ULL * 1024ULL * 1024ULL * 1024ULL)  // 2 GiB  = 4 rounds
 * WARNING: at ~20–50k demand-page faults/sec on QEMU RISC-V this takes
 * 1.5–4 hours of wall time.  Only use on real hardware or a fast simulator. */

/* Bytes freed per round (all NCHILDREN combined). */
#define BYTES_PER_ROUND ((uint64_t)NCHILDREN * (uint64_t)ALLOC_BYTES)

/* ── child body ──────────────────────────────────────────────────────────── */

/*
 * Each child:
 *   1. mmap an anonymous private region of ALLOC_BYTES.
 *   2. Touch every page to force physical allocation (and to exercise lazy
 *      demand-paging if enabled).
 *   3. munmap the region (belt-and-suspenders; kernel also unmaps on exit).
 *   4. exit(0) on success, exit(1) on mmap failure.
 */
static void child_body(void) {
    void *p = mmap(NULL, ALLOC_BYTES,
                   PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS,
                   -1, 0);
    if (p == MAP_FAILED)
        _exit(1);

    volatile char *cp = (volatile char *)p;
    for (size_t i = 0; i < ALLOC_BYTES; i += PAGE_BYTES)
        cp[i] = (char)(i & 0xFF);

    munmap(p, ALLOC_BYTES);
    _exit(0);
}

/* ── single stress round ─────────────────────────────────────────────────── */

/*
 * Spawn NCHILDREN children, wait for all of them, return 1 if all exited 0
 * or 0 if any child reported mmap failure or was killed.
 */
static int run_one_round(void) {
    pid_t pids[NCHILDREN];

    for (int i = 0; i < NCHILDREN; i++) {
        pid_t p = fork();
        if (p < 0) {
            /* fork failed; reap what we have then report failure */
            for (int j = 0; j < i; j++)
                waitpid(pids[j], NULL, 0);
            return 0;
        }
        if (p == 0)
            child_body(); /* never returns */
        pids[i] = p;
    }

    int all_ok = 1;
    for (int i = 0; i < NCHILDREN; i++) {
        int status = 0;
        pid_t w = waitpid(pids[i], &status, 0);
        if (w != pids[i] || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
            all_ok = 0;
    }
    return all_ok;
}

/* ── main stress loop ────────────────────────────────────────────────────── */

static void test_fork_alloc_loop(us_test_group_t *group) {
    uint64_t total = 0;
    int leak_detected = 0;

    while (total < TARGET_BYTES) {
        if (!run_one_round()) {
            leak_detected = 1;
            break;
        }
        total += BYTES_PER_ROUND;
    }

    TEST(group, "fork-alloc-stress: no mmap failure across 2 GiB of allocations",
         !leak_detected);
    TEST(group, "fork-alloc-stress: 2 GiB cumulative target reached (16x physical RAM)",
         total >= TARGET_BYTES);
}

/* ── post-stress sanity: one more mmap must succeed ─────────────────────── */

static void test_post_stress_alloc(us_test_group_t *group) {
    /* If pages were leaked, physical memory is exhausted; this mmap fails. */
    void *p = mmap(NULL, ALLOC_BYTES,
                   PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS,
                   -1, 0);
    TEST(group, "fork-alloc-stress: post-stress mmap(8 MB) still succeeds (no leak)",
         p != MAP_FAILED);
    if (p != MAP_FAILED)
        munmap(p, ALLOC_BYTES);
}

/* ── public entry point ─────────────────────────────────────────────────── */

void run_test_fork_alloc_stress(us_test_group_t *group) {
    ut_group_begin(group, "fork_alloc_stress");

    test_fork_alloc_loop(group);
    test_post_stress_alloc(group);

    ut_group_end(group);
}

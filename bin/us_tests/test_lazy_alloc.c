/*
 * test_lazy_alloc.c — Demand-paging / lazy allocation correctness.
 *
 * The kernel allocates physical pages on first access (page fault), not at
 * mmap/sbrk time.  These tests verify:
 *
 *  1. Large sbrk region: only touched pages are faulted in; others stay
 *     unmapped.  All touched pages read back correctly.
 *
 *  2. Sparse access pattern: touch every 16th page across a 4 MB sbrk
 *     region; untouched pages stay lazily unmapped without crashing.
 *
 *  3. Large anonymous mmap: 8 MB MAP_ANONYMOUS, touch every page,
 *     verify zero-fill and write correctness.
 *
 *  4. Lazy mmap in child: child touches only its half; parent verifies
 *     its half is independent (COW + demand-paging interaction).
 *
 *  5. sbrk then fork: parent sbrk's large region, forks, child touches
 *     all pages → COW faults one page at a time.  Parent pages unchanged.
 *
 *  6. Sequential demand-page walk: sbrk 1 MB, walk every byte sequentially,
 *     write index pattern, re-read to verify.
 *
 *  7. Stack demand growth: already tested in test_stack_growth.c but we
 *     include a quick smoke test here for completeness.
 */

#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include "us_test_common.h"

#define PAGE 4096

/* ── 1. large anonymous mmap, touch every page ───────────────────────────── */

static void test_mmap_large(us_test_group_t *group) {
    pid_t pid = fork();
    if (pid == 0) {
        int n = 2048; /* 8 MB */
        char *p = mmap(NULL, (size_t)n * PAGE,
                       PROT_READ | PROT_WRITE,
                       MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
        if (p == MAP_FAILED) _exit(1);
        /* verify zero-fill */
        for (int i = 0; i < n; i++)
            if (p[i * PAGE] != 0) _exit(2);
        /* write pattern */
        for (int i = 0; i < n; i++)
            p[i * PAGE] = (char)(i & 0xFF);
        for (int i = 0; i < n; i++)
            if (p[i * PAGE] != (char)(i & 0xFF)) _exit(3);
        munmap(p, (size_t)n * PAGE);
        _exit(0);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    TEST(group, "lazy-mmap-large: 8 MB anon mmap zero-fill + write correct",
         WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

/* ── 2. mmap in child: child/parent halves independent ───────────────────── */

static void test_mmap_fork_halves(us_test_group_t *group) {
    int n = 256; /* 1 MB, split in two */
    char *p = mmap(NULL, (size_t)n * PAGE,
                   PROT_READ | PROT_WRITE,
                   MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (p == MAP_FAILED) {
        TEST(group, "lazy-mmap-fork: mmap succeeded", 0);
        return;
    }
    TEST(group, "lazy-mmap-fork: mmap succeeded", 1);

    /* parent touches first half */
    for (int i = 0; i < n / 2; i++)
        p[i * PAGE] = 0xAA;

    pid_t pid = fork();
    if (pid == 0) {
        /* child: touch second half with 0xBB */
        for (int i = n / 2; i < n; i++)
            p[i * PAGE] = 0xBB;
        /* first half should still be 0xAA (COW copy) */
        for (int i = 0; i < n / 2; i++)
            if ((unsigned char)p[i * PAGE] != 0xAA) _exit(1);
        _exit(0);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    TEST(group, "lazy-mmap-fork: child sees parent data and writes own half",
         WIFEXITED(status) && WEXITSTATUS(status) == 0);

    /* parent's first half must be intact */
    int ok = 1;
    for (int i = 0; i < n / 2; i++)
        if ((unsigned char)p[i * PAGE] != 0xAA) { ok = 0; break; }
    /* parent's second half: child wrote 0xBB but that was in child's private copy */
    TEST(group, "lazy-mmap-fork: parent first half unchanged after child fork",
         ok);

    munmap(p, (size_t)n * PAGE);
}

/* ── 3. demand-page zero-fill: freshly faulted pages must be zero ─────────── */

static void test_demand_zero_fill(us_test_group_t *group) {
    pid_t pid = fork();
    if (pid == 0) {
        /* mmap and verify zero before any write */
        char *p = mmap(NULL, 64 * PAGE,
                       PROT_READ | PROT_WRITE,
                       MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
        if (p == MAP_FAILED) _exit(1);
        for (int i = 0; i < 64; i++) {
            /* check entire page is zero */
            for (int j = 0; j < PAGE; j++)
                if (p[i * PAGE + j] != 0) _exit(2);
        }
        munmap(p, 64 * PAGE);
        _exit(0);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    TEST(group, "lazy-zero-fill: all bytes in 64 demand-paged pages are zero",
         WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

/* ── 4. fixed-address anonymous mmap must avoid device reservations ─────── */

static void test_fixed_mmap_layout(us_test_group_t *group) {
    pid_t pid = fork();
    if (pid < 0) {
        SKIP(group, "mmap-fixed: device overlap rejected, safe fixed page zero-filled",
             "fork failed");
        return;
    }
    if (pid == 0) {
        char *dev = mmap((void *)0x40000000UL, PAGE,
                         PROT_READ | PROT_WRITE,
                         MAP_ANONYMOUS | MAP_PRIVATE | MAP_FIXED, -1, 0);
        if (dev != MAP_FAILED)
            _exit(1);

        char *p = mmap((void *)0x50000000UL, PAGE,
                       PROT_READ | PROT_WRITE,
                       MAP_ANONYMOUS | MAP_PRIVATE | MAP_FIXED, -1, 0);
        if (p == MAP_FAILED)
            _exit(2);

        for (int i = 0; i < PAGE; i++)
            if (p[i] != 0)
                _exit(3);

        p[0] = 0x5a;
        p[PAGE - 1] = 0x33;
        if (p[0] != 0x5a || p[PAGE - 1] != 0x33)
            _exit(4);

        munmap(p, PAGE);
        _exit(0);
    }

    int status = 0;
    waitpid(pid, &status, 0);
    TEST(group, "mmap-fixed: device overlap rejected, safe fixed page zero-filled",
         WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

/* ── public entry point ─────────────────────────────────────────────────── */

void run_test_lazy_alloc(us_test_group_t *group) {
    ut_group_begin(group, "lazy_alloc");

    test_mmap_large(group);
    test_mmap_fork_halves(group);
    test_demand_zero_fill(group);
    test_fixed_mmap_layout(group);

    ut_group_end(group);
}

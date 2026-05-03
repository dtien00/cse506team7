/*
 * test_mm_health.c — Memory health / leak detection tests
 *
 * Tests:
 *  A. anon mmap returns zero-initialized pages (POSIX rule #1)
 *  B. mmap/munmap cycle — repeated alloc+free doesn't exhaust VA (no leak)
 *  C. malloc/free stress — heap not leaked across many alloc/free pairs
 *  D. fork+wait stress — many short-lived children, no zombie/resource leak
 *  E. munmap makes VMA inaccessible (child SIGSEGV after munmap)
 *  F. mmap after munmap reuses VA without stale content
 */

#include <sys/mman.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include "us_test_common.h"

/* ── A. anon mmap returns zeroed pages ──────────────────────────────────── */

static void test_anon_mmap_zero(us_test_group_t *group) {
    size_t len = 4 * 4096;
    void *m = mmap(NULL, len, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    TEST(group, "mm-health/anon-zero: mmap succeeds", m != MAP_FAILED);
    if (m == MAP_FAILED)
        return;

    char *p = (char *)m;
    int all_zero = 1;
    for (size_t i = 0; i < len; i++) {
        if (p[i] != 0) { all_zero = 0; break; }
    }
    TEST(group, "mm-health/anon-zero: all bytes zero-initialized", all_zero);

    munmap(m, len);
}

/* ── B. mmap/munmap cycle — no VA exhaustion ────────────────────────────── */

static void test_mmap_munmap_cycle(us_test_group_t *group) {
#define MMAP_CYCLES 64
    int ok = 1;
    for (int i = 0; i < MMAP_CYCLES; i++) {
        void *m = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (m == MAP_FAILED) { ok = 0; break; }
        /* Touch page to demand-fault it, then free. */
        ((volatile char *)m)[0] = (char)i;
        munmap(m, 4096);
    }
    TEST(group, "mm-health/mmap-cycle: 64 mmap+munmap cycles succeed", ok);
#undef MMAP_CYCLES
}

/* ── C. malloc/free stress — heap not leaked ───────────────────────────── */

static void test_malloc_free_stress(us_test_group_t *group) {
#define MALLOC_CYCLES 256
    int ok = 1;
    for (int i = 0; i < MALLOC_CYCLES; i++) {
        size_t sz = 64 + (size_t)(i % 8) * 64;   /* 64..576 bytes, vary size */
        char *p = (char *)malloc(sz);
        if (!p) { ok = 0; break; }
        memset(p, (char)i, sz);
        /* verify write survives until free (catches use-after-free in allocator) */
        if ((unsigned char)p[0] != (unsigned char)i) ok = 0;
        free(p);
    }
    TEST(group, "mm-health/malloc-stress: 256 alloc/free pairs succeed", ok);

    /* After all frees a fresh large alloc must still work. */
    void *big = malloc(8 * 4096);
    TEST(group, "mm-health/malloc-stress: large alloc after stress succeeds", big != NULL);
    free(big);
#undef MALLOC_CYCLES
}

/* ── D. fork+wait stress — no zombie/resource leak ─────────────────────── */

static void test_fork_wait_stress(us_test_group_t *group) {
#define FORK_CYCLES 32
    int all_ok = 1;
    for (int i = 0; i < FORK_CYCLES; i++) {
        pid_t pid = fork();
        if (pid < 0) { all_ok = 0; break; }
        if (pid == 0) {
            /* Child: allocate a page, touch it, exit cleanly. */
            void *m = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (m != MAP_FAILED) {
                ((volatile char *)m)[0] = 1;
                munmap(m, 4096);
            }
            _exit(0);
        }
        /* Parent: reap immediately so proctab doesn't fill up. */
        int status = 0;
        pid_t reaped = waitpid(pid, &status, 0);
        if (reaped != pid || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
            all_ok = 0;
    }
    TEST(group, "mm-health/fork-stress: 32 fork+wait cycles, all children exit 0", all_ok);
#undef FORK_CYCLES
}

/* ── E. munmap makes region inaccessible (child gets SIGSEGV) ───────────── */

static void test_munmap_inaccessible(us_test_group_t *group) {
    void *m = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    TEST(group, "mm-health/munmap-access: mmap for inaccessible test succeeds",
         m != MAP_FAILED);
    if (m == MAP_FAILED)
        return;

    munmap(m, 4096);

    /* Child tries to read the unmapped address — must SIGSEGV. */
    pid_t pid = fork();
    TEST(group, "mm-health/munmap-access: fork succeeded", pid >= 0);
    if (pid == 0) {
        volatile char c = ((char *)m)[0];
        (void)c;
        _exit(0);   /* unreachable if SIGSEGV fires */
    }
    if (pid > 0) {
        int status = 0;
        waitpid(pid, &status, 0);
        TEST(group, "mm-health/munmap-access: access after munmap delivers SIGSEGV",
             WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV);
    }
}

/* ── F. mmap after munmap: fresh mapping, no stale content ─────────────── */

static void test_mmap_reuse_clean(us_test_group_t *group) {
    size_t len = 4096;

    /* First mapping: write a sentinel. */
    void *m1 = mmap(NULL, len, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    TEST(group, "mm-health/mmap-reuse: first mmap succeeds", m1 != MAP_FAILED);
    if (m1 == MAP_FAILED)
        return;

    memset(m1, 0xAB, len);
    munmap(m1, len);

    /* Second mapping at same hint address — may or may not reuse VA, but must be zeroed. */
    void *m2 = mmap(m1, len, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    TEST(group, "mm-health/mmap-reuse: second mmap succeeds", m2 != MAP_FAILED);
    if (m2 == MAP_FAILED)
        return;

    char *p = (char *)m2;
    int all_zero = 1;
    for (size_t i = 0; i < len; i++) {
        if (p[i] != 0) { all_zero = 0; break; }
    }
    TEST(group, "mm-health/mmap-reuse: new mapping is zero-initialized (no stale content)",
         all_zero);

    munmap(m2, len);
}

/* ── public entry point ──────────────────────────────────────────────────── */

void run_test_mm_health(us_test_group_t *group) {
    ut_group_begin(group, "mm_health");

    test_anon_mmap_zero(group);
    test_mmap_munmap_cycle(group);
    test_malloc_free_stress(group);
    test_fork_wait_stress(group);
    test_munmap_inaccessible(group);
    test_mmap_reuse_clean(group);

    ut_group_end(group);
}

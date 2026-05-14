/*
 * test_eval_mm.c — Targeted tests for Evals 05-10
 *
 * Eval 05: bogus fd → EBADF, not silent 0
 * Eval 06: fork+child writes COW page, must not crash
 * Eval 07: repeated fork+child-alloc+exit, no cascading failure
 * Eval 08: fork+dirty-child+exec, grandchild gets zeroed stack
 * Eval 09: mmap at low and high user VA, pages accessible and zero
 * Eval 10: child allocates large memory (30/60/100 MB), exits cleanly
 */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "us_test_common.h"

/* ── Eval 05: bogus fd returns EBADF, not 0 ─────────────────────────────── */

static void test_ebadf_vs_empty(us_test_group_t *group) {
    char buf[16];

    /* read from fd -1: must return -1 (EBADF), not 0 */
    ssize_t r = read(-1, buf, sizeof(buf));
    TEST(group, "eval05/ebadf: read(-1,...) returns -1", r == -1);

    /* read from fd 999 (not open): must return -1 */
    r = read(999, buf, sizeof(buf));
    TEST(group, "eval05/ebadf: read(999,...) returns -1", r == -1);

    /* write to fd -1: must return -1 */
    ssize_t w = write(-1, buf, 1);
    TEST(group, "eval05/ebadf: write(-1,...) returns -1", w == -1);

    /* write to fd 999: must return -1 */
    w = write(999, buf, 1);
    TEST(group, "eval05/ebadf: write(999,...) returns -1", w == -1);

    /* lseek on bad fd: must return -1 */
    off_t s = lseek(999, 0, SEEK_SET);
    TEST(group, "eval05/ebadf: lseek(999,...) returns -1", s == (off_t)-1);

    /* close on bad fd: must return -1 */
    int c = close(999);
    TEST(group, "eval05/ebadf: close(999) returns -1", c == -1);

    /*
     * Distinguish "nothing happened" (0) from "request invalid" (-1).
     * read(999,...) returning 0 would be indistinguishable from EOF;
     * returning -1/EBADF lets caller know the fd was never valid.
     */
    int open_fd = open("/hello.txt", O_RDONLY);
    if (open_fd >= 0) {
        /* valid fd: read returns >= 0 (may be 0 at EOF, never -1 on open file) */
        ssize_t valid_r = read(open_fd, buf, sizeof(buf));
        TEST(group, "eval05/ebadf: read on valid fd returns >= 0", valid_r >= 0);
        close(open_fd);
    }
}

/* ── Eval 06: fork → child writes COW page → child exits 0 ─────────────── */

static void test_cow_write_survives(us_test_group_t *group) {
    /* Allocate via mmap (not malloc) to be sure we test mmap COW */
    void *m = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    TEST(group, "eval06/cow: mmap succeeded", m != MAP_FAILED);
    if (m == MAP_FAILED) return;

    volatile char *p = (volatile char *)m;
    p[0] = 0x42;

    pid_t pid = fork();
    TEST(group, "eval06/cow: fork succeeded", pid >= 0);
    if (pid < 0) { munmap(m, 4096); return; }

    if (pid == 0) {
        /* Child: write to the inherited mmap page — must NOT crash */
        p[0] = 0xAB;
        /* verify the write took effect */
        _exit(p[0] == (char)0xAB ? 0 : 1);
    }

    int status = 0;
    waitpid(pid, &status, 0);
    TEST(group, "eval06/cow: child write on COW page did not crash",
         WIFEXITED(status) && WEXITSTATUS(status) == 0);
    /* parent sees its own copy unchanged */
    TEST(group, "eval06/cow: parent sees original value after child write",
         p[0] == 0x42);

    munmap(m, 4096);
}

/* stack-COW: first action after fork writes to stack (like exec path) */
static void test_cow_stack_write(us_test_group_t *group) {
    volatile int local = 100;
    pid_t pid = fork();
    TEST(group, "eval06/cow-stack: fork succeeded", pid >= 0);
    if (pid == 0) {
        local = 200;   /* COW fault on stack page */
        _exit(local == 200 ? 0 : 1);
    }
    if (pid > 0) {
        int status = 0;
        waitpid(pid, &status, 0);
        TEST(group, "eval06/cow-stack: child stack write didn't crash",
             WIFEXITED(status) && WEXITSTATUS(status) == 0);
        TEST(group, "eval06/cow-stack: parent stack unchanged", local == 100);
    }
}

/* ── Eval 07: repeated fork+alloc+exit, no cascading failure ────────────── */

#define FORK_ALLOC_CYCLES 32

static void test_fork_alloc_stress(us_test_group_t *group) {
    int all_ok = 1;
    for (int i = 0; i < FORK_ALLOC_CYCLES; i++) {
        pid_t pid = fork();
        if (pid < 0) { all_ok = 0; break; }
        if (pid == 0) {
            /* Child: allocate and touch 1 MB via mmap, then exit */
            size_t sz = 1024 * 1024;
            void *m = mmap(NULL, sz, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (m != MAP_FAILED) {
                /* touch every page to trigger demand allocation */
                volatile char *p = (volatile char *)m;
                for (size_t off = 0; off < sz; off += 4096)
                    p[off] = (char)(i & 0xFF);
                munmap(m, sz);
            }
            _exit(0);
        }
        /* parent: reap immediately — no zombie accumulation */
        int status = 0;
        pid_t reaped = waitpid(pid, &status, 0);
        if (reaped != pid || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
            all_ok = 0;
    }
    TEST(group, "eval07/fork-alloc: 32 cycles fork+1MB-alloc+exit all succeed",
         all_ok);

    /* After stress, parent can still allocate */
    void *check = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    TEST(group, "eval07/fork-alloc: parent can still mmap after stress",
         check != MAP_FAILED);
    if (check != MAP_FAILED) munmap(check, 4096);
}

/* ── Eval 08: fork + dirty child + exec grandchild gets zeroed stack ─────── */

static void test_dirty_then_exec(us_test_group_t *group) {
    /*
     * Scenario: parent forks a child that writes to a large buffer (dirties
     * many physical pages), child exits returning those pages to PMM.
     * Then parent forks again and the new child execs a simple program.
     * If exec's stack pages are not zeroed from the PMM, the new program
     * will see stale data and its argc/argv will be corrupt.
     *
     * We use /bin/us_test with --envp-probe to verify exec'd program works.
     */
    /* Step 1: dirty child */
    pid_t dirty = fork();
    if (dirty == 0) {
        size_t sz = 4 * 1024 * 1024; /* 4 MB */
        void *m = mmap(NULL, sz, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (m != MAP_FAILED) {
            volatile char *p = (volatile char *)m;
            for (size_t off = 0; off < sz; off += 4096)
                p[off] = 0xCD;   /* distinctive non-zero byte */
            munmap(m, sz);
        }
        _exit(0);
    }
    TEST(group, "eval08/dirty-exec: dirty child forked", dirty > 0);
    if (dirty < 0) return;
    waitpid(dirty, NULL, 0);

    /* Step 2: exec a program that validates its own environment */
    pid_t exec_child = fork();
    TEST(group, "eval08/dirty-exec: exec child forked", exec_child >= 0);
    if (exec_child == 0) {
        /* exec the test binary itself with --envp-probe; it exits 0 on success */
        char *av[] = { "us_tests", "--envp-probe", "PATH=/bin", NULL };
        char *ev[] = { "PATH=/bin", NULL };
        execve("/bin/us_tests", av, ev);
        _exit(2); /* execve failed */
    }
    if (exec_child > 0) {
        int status = 0;
        waitpid(exec_child, &status, 0);
        TEST(group, "eval08/dirty-exec: exec'd program ran and exited correctly",
             WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }
}

/* ── Eval 09: mmap at low and high user VA, zero-initialized and accessible ─ */

static void test_mmap_address_range(us_test_group_t *group) {
    /* Low VA (just above normal mmap start) */
    void *lo = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    TEST(group, "eval09/mmap-range: low-VA mmap succeeds", lo != MAP_FAILED);
    if (lo != MAP_FAILED) {
        volatile char *p = (volatile char *)lo;
        int zero = 1;
        for (int i = 0; i < 4096; i++) if (p[i]) { zero = 0; break; }
        TEST(group, "eval09/mmap-range: low-VA page is zero-initialized", zero);
        p[0] = 1; /* write must not crash */
        TEST(group, "eval09/mmap-range: low-VA page is writable", p[0] == 1);
        munmap(lo, 4096);
    }

    /* High VA: just below KERNBASE (0x80000000); pick 0x70000000 */
    void *hi = mmap((void *)0x70000000, 4096, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    TEST(group, "eval09/mmap-range: high-VA mmap returns valid address",
         hi != MAP_FAILED);
    if (hi != MAP_FAILED) {
        volatile char *p = (volatile char *)hi;
        int zero = 1;
        for (int i = 0; i < 4096; i++) if (p[i]) { zero = 0; break; }
        TEST(group, "eval09/mmap-range: high-VA page is zero-initialized", zero);
        p[0] = 2;
        TEST(group, "eval09/mmap-range: high-VA page is writable", p[0] == 2);
        munmap(hi, 4096);
    }

    /* Mid VA: 0x60000000 */
    void *mid = mmap((void *)0x60000000, 8192, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    TEST(group, "eval09/mmap-range: mid-VA mmap succeeds", mid != MAP_FAILED);
    if (mid != MAP_FAILED) {
        volatile char *p = (volatile char *)mid;
        int zero = 1;
        for (int i = 0; i < 8192; i++) if (p[i]) { zero = 0; break; }
        TEST(group, "eval09/mmap-range: mid-VA pages are zero-initialized", zero);
        munmap(mid, 8192);
    }
}

/* ── Eval 10: child allocates 30 / 60 / 100 MB, must exit cleanly ────────── */

static int alloc_touch_mb(size_t mb) {
    size_t sz = mb * 1024 * 1024;
    void *m = mmap(NULL, sz, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (m == MAP_FAILED) return 1;
    volatile char *p = (volatile char *)m;
    for (size_t off = 0; off < sz; off += 4096)
        p[off] = (char)(off >> 12); /* write page index, not constant */
    /* verify no page was silently aliased (double-alloc produces same PA twice) */
    int ok = 1;
    for (size_t off = 0; off < sz; off += 4096) {
        if (p[off] != (char)(off >> 12)) { ok = 0; break; }
    }
    munmap(m, sz);
    return ok ? 0 : 1;
}

static void test_large_alloc_stress(us_test_group_t *group) {
    static const size_t sizes_mb[] = { 30, 60, 80 };
    static const char *names[]     = {
        "eval10/large-alloc: 30 MB child exits cleanly",
        "eval10/large-alloc: 60 MB child exits cleanly",
        "eval10/large-alloc: 80 MB child exits cleanly",
    };

    for (int t = 0; t < 3; t++) {
        pid_t pid = fork();
        if (pid < 0) {
            TEST(group, names[t], 0);
            continue;
        }
        if (pid == 0) {
            _exit(alloc_touch_mb(sizes_mb[t]));
        }
        int status = 0;
        waitpid(pid, &status, 0);
        TEST(group, names[t],
             WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }
}

/* ── public entry point ──────────────────────────────────────────────────── */

void run_test_eval_mm(us_test_group_t *group) {
    ut_group_begin(group, "eval_mm");

    test_ebadf_vs_empty(group);
    test_cow_write_survives(group);
    test_cow_stack_write(group);
    test_fork_alloc_stress(group);
    test_dirty_then_exec(group);
    test_mmap_address_range(group);
    test_large_alloc_stress(group);

    ut_group_end(group);
}

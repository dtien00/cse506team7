/*
 * test_sbrk_brk.c — Isolated lazy-allocation tests for brk/sbrk.
 *
 * Keep brk/sbrk checks in their own group so mmap-heavy tests cannot affect
 * heap-break behavior. Each check runs in a child and restores brk baseline
 * before exit for a clean parent state.
 */

#include <sys/wait.h>
#include <unistd.h>
#include "us_test_common.h"

#define PAGE 4096

static void restore_brk_or_die(void *saved_break, int code) {
    /* sys_brk returns the new program break on success (not 0).
     * Failure is indicated by a return value != saved_break. */
    void *result = (void *)brk(saved_break);
    if (result != saved_break) {
        _exit(code);
    }
}

static void test_sbrk_large(us_test_group_t *group) {
    pid_t pid = fork();
    if (pid == 0) {
        void *saved = sbrk(0);
        int n = 512; /* 2 MB */
        char *base = sbrk(n * PAGE);
        if (base == (void *)-1) _exit(1);
        for (int i = 0; i < n; i++)
            base[i * PAGE] = (char)(i & 0xFF);
        for (int i = 0; i < n; i++)
            if (base[i * PAGE] != (char)(i & 0xFF)) _exit(2);
        restore_brk_or_die(saved, 3);
        _exit(0);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    TEST(group, "lazy-sbrk-large: 2 MB touch all pages, read back correct",
         WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

static void test_sbrk_sparse(us_test_group_t *group) {
    pid_t pid = fork();
    if (pid == 0) {
        void *saved = sbrk(0);
        int n = 1024; /* 4 MB */
        char *base = sbrk(n * PAGE);
        if (base == (void *)-1) _exit(1);
        for (int i = 0; i < n; i += 16)
            base[i * PAGE] = (char)(i & 0xFF);
        for (int i = 0; i < n; i += 16)
            if (base[i * PAGE] != (char)(i & 0xFF)) _exit(2);
        restore_brk_or_die(saved, 3);
        _exit(0);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    TEST(group, "lazy-sbrk-sparse: 4 MB sparse (every 16th page), correct",
         WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

static void test_sbrk_fork_cow(us_test_group_t *group) {
    pid_t outer = fork();
    if (outer == 0) {
        void *saved = sbrk(0);
        int n = 128; /* 512 KB */
        char *base = sbrk(n * PAGE);
        if (base == (void *)-1) _exit(10);

        base[0] = 0x55;

        pid_t inner = fork();
        if (inner == 0) {
            for (int i = 0; i < n; i++)
                base[i * PAGE] = (char)(i & 0xFF);
            if (base[0] != 0) _exit(1);
            _exit(0);
        }
        int st = 0;
        waitpid(inner, &st, 0);
        if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) _exit(1);
        if ((unsigned char)base[0] != 0x55) _exit(2);
        restore_brk_or_die(saved, 3);
        _exit(0);
    }
    int status = 0;
    waitpid(outer, &status, 0);
    int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    TEST(group, "lazy-sbrk-fork-cow: child demand-paged all 128 pages via COW",
         code == 0 || code == 2);
    TEST(group, "lazy-sbrk-fork-cow: parent sbrk page unchanged after child COW",
         code == 0 || code == 1);
}

static void test_sbrk_sequential(us_test_group_t *group) {
    pid_t pid = fork();
    if (pid == 0) {
        void *saved = sbrk(0);
        int sz = 256 * PAGE; /* 1 MB */
        char *base = sbrk(sz);
        if (base == (void *)-1) _exit(1);
        for (int i = 0; i < sz; i++)
            base[i] = (char)(i & 0xFF);
        for (int i = 0; i < sz; i++)
            if (base[i] != (char)(i & 0xFF)) _exit(2);
        restore_brk_or_die(saved, 3);
        _exit(0);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    TEST(group, "lazy-sbrk-sequential: 1 MB every-byte write+verify correct",
         WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

/* DEFECT-A verification: sbrk(N) must return the OLD break (POSIX).
 * The existing tests exercise allocation size and COW but never check the
 * return value contract.  Runs in a child so the parent's break is clean. */
static void test_sbrk_retval(us_test_group_t *group) {
    pid_t pid = fork();
    if (pid == 0) {
        void *before = sbrk(0);
        void *ret    = sbrk(PAGE);
        void *after  = sbrk(0);
        /* ret must equal before (old break); break must have advanced by PAGE */
        int ok = (ret == before) &&
                 ((char *)after - (char *)before == PAGE);
        if (ok) *(volatile char *)ret = 0xAB; /* touch: confirm page is mapped */
        restore_brk_or_die(before, 3);
        _exit(ok ? 0 : 1);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    TEST(group, "sbrk-retval: sbrk(N) returns old break; break advances by N",
         WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

/* P3 verification: brk(new_addr) must return new_addr on success.
 * Before the fix sys_brk returned 0 on success; after the fix it returns
 * the new program break so that musl-style allocators can distinguish
 * success from failure by comparing the return value to the requested addr. */
static void test_brk_retval(us_test_group_t *group) {
    pid_t pid = fork();
    if (pid == 0) {
        void *before = (void *)brk(NULL);  /* query: kernel returns current break */
        void *target = (char *)before + PAGE;
        void *result = (void *)brk(target);
        /* On success kernel must return target; on failure it returns old break. */
        int ok = (result == target);
        if (ok) {
            *(volatile char *)before = 0xBB; /* touch the newly mapped page */
            brk(before);                     /* restore — ignore return */
        }
        _exit(ok ? 0 : 1);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    TEST(group, "brk-retval: brk(new_addr) returns new_addr on success",
         WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

void run_test_sbrk_brk(us_test_group_t *group) {
    ut_group_begin(group, "sbrk_brk");

    test_sbrk_large(group);
    test_sbrk_sparse(group);
    test_sbrk_fork_cow(group);
    test_sbrk_sequential(group);
    test_sbrk_retval(group);
    test_brk_retval(group);

    ut_group_end(group);
}


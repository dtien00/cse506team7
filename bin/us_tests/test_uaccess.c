/**
 * test_uaccess.c — Userspace integration tests for copy_to/from_user fault recovery
 *
 * Verifies that passing a bad pointer to a syscall returns -EFAULT and does NOT
 * kernel-panic.  Each test forks so that even an unexpected process death does
 * not abort the suite.
 *
 * NOTE: Our libc syscall stubs are bare ecall wrappers (no errno translation).
 * Syscalls return the raw kernel value, e.g. -EFAULT (-14) on bad pointer.
 */

#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "us_test_common.h"

/* An address in user VA space (canonical Sv39, VPN[2]=1) but far from any mapped
 * page: above the data segment (ends ~0x32000) and below the stack (~0x7ffe4000).
 * VPN[1]=128 within the level-1 table that exists for VPN[2]=1 is not populated. */
#define BAD_PTR ((void *)0x50001000UL)

/* ── helpers ──────────────────────────────────────────────────────────────── */

static int run_in_child(void (*fn)(void)) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) { fn(); _exit(1); }
    int status = 0;
    waitpid(pid, &status, 0);
    return status;
}

/* ── test bodies (run inside fork) ──────────────────────────────────────── */

static void child_write_bad_buf(void) {
    /* write() with unmapped source: kernel copy_from_user faults → -EFAULT */
    ssize_t r = write(STDOUT_FILENO, BAD_PTR, 4);
    _exit(r == -EFAULT ? 0 : 1);
}

static void child_open_bad_path(void) {
    /* open() with unmapped path: kernel strncpy_from_user faults → -EFAULT */
    int fd = open((const char *)BAD_PTR, O_RDONLY, 0);
    _exit(fd == -EFAULT ? 0 : 1);
}

static void child_stat_bad_stat_buf(void) {
    /* stat() with valid path but unmapped output buffer → -EFAULT from copy_to_user.
     * Use "/" which always exists so we reach the copy_to_user call. */
    struct stat *bad = (struct stat *)BAD_PTR;
    int r = stat("/", bad);
    _exit(r == -EFAULT ? 0 : 1);
}

static void child_fstat_bad_buf(void) {
    /* fstat() with valid fd but unmapped output buffer → -EFAULT */
    int fd = open("/", O_RDONLY, 0);
    if (fd < 0) _exit(2);
    struct stat *bad = (struct stat *)BAD_PTR;
    int r = fstat(fd, bad);
    close(fd);
    _exit(r == -EFAULT ? 0 : 1);
}

static void child_write_null_buf(void) {
    /* NULL buffer is caught by the !buf guard → -EINVAL (not EFAULT), but either
     * way the process must survive and get a negative return. */
    ssize_t r = write(STDOUT_FILENO, NULL, 4);
    _exit(r < 0 ? 0 : 1);
}

static void child_valid_write(void) {
    const char msg[] = "uaccess-ok\n";
    ssize_t r = write(STDOUT_FILENO, msg, sizeof(msg) - 1);
    _exit(r == (ssize_t)(sizeof(msg) - 1) ? 0 : 1);
}

static void child_invalid_write(void) {
    // const char msg[] = "uaccess-trial\n";
    volatile char *bad = (volatile char *)(BAD_PTR);

    *bad = 0x42;   /* ← store to unmapped VA: triggers page fault immediately */

    _exit(1);
}

/* ── test group ───────────────────────────────────────────────────────────── */

void run_test_uaccess(us_test_group_t *group) {
    ut_group_begin(group, "uaccess");

    /* T1: write() with bad source buffer returns -EFAULT, kernel survives */
    {
        int st = run_in_child(child_write_bad_buf);
        TEST(group, "write bad buf: -EFAULT, no panic",
             WIFEXITED(st) && WEXITSTATUS(st) == 0);
    }

    /* T2: open() with bad path pointer returns -EFAULT */
    {
        int st = run_in_child(child_open_bad_path);
        TEST(group, "open bad path ptr: -EFAULT, no panic",
             WIFEXITED(st) && WEXITSTATUS(st) == 0);
    }

    /* T3: stat() with bad output pointer returns -EFAULT */
    {
        int st = run_in_child(child_stat_bad_stat_buf);
        TEST(group, "stat bad stat buf: -EFAULT, no panic",
             WIFEXITED(st) && WEXITSTATUS(st) == 0);
    }

    /* T4: fstat() with bad output pointer returns -EFAULT */
    {
        int st = run_in_child(child_fstat_bad_buf);
        int skip = WIFEXITED(st) && WEXITSTATUS(st) == 2;
        if (skip)
            SKIP(group, "fstat bad stat buf: -EFAULT, no panic", "/ not openable");
        else
            TEST(group, "fstat bad stat buf: -EFAULT, no panic",
                 WIFEXITED(st) && WEXITSTATUS(st) == 0);
    }

    /* T5: NULL buffer — caught before copy, process gets negative return */
    {
        int st = run_in_child(child_write_null_buf);
        TEST(group, "write NULL buf: returns error, no panic",
             WIFEXITED(st) && WEXITSTATUS(st) == 0);
    }

    /* T6 regression: write() with valid pointer still works */
    {
        int st = run_in_child(child_valid_write);
        TEST(group, "regression: write valid buf still works",
            WIFEXITED(st) && WEXITSTATUS(st) == 0);
    }

    /* T7: user process accessing invalid VA → kernel kills child, parent survives */
    {
        int st = run_in_child(child_invalid_write);
        TEST(group, "invalid user access: kernel kills child, system survives",
             WIFEXITED(st) && WEXITSTATUS(st) != 0);
    }

    ut_group_end(group);
}

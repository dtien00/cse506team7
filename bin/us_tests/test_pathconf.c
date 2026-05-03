/*
 * test_pathconf.c — Tests for pathconf(2) and fpathconf(2).
 *
 * SBUnix uses fixed, filesystem-independent limits, so every valid path and
 * open fd returns the same value for a given _PC_* name.  Tests verify:
 *   - known _PC_* names return the expected POSIX-minimum values
 *   - an invalid path returns -1 (ENOENT)
 *   - an invalid fd returns -1 (EBADF)
 *   - fpathconf works on an fd opened from a real file
 *
 * The file "/hello.txt" is present in the SBUnix tarfs root.
 */

#include "us_test_common.h"
#include <unistd.h>
#include <limits.h>
#include <fcntl.h>
#include <errno.h>

/* ── pathconf() ────────────────────────────────────────────────────────────── */

static void test_pathconf_name_max(us_test_group_t *tg) {
    long v = pathconf("/", _PC_NAME_MAX);
    TEST(tg, "pathconf(\"/\", _PC_NAME_MAX) > 0", v > 0);
    TEST(tg, "pathconf(\"/\", _PC_NAME_MAX) == NAME_MAX (255)", v == NAME_MAX);
}

static void test_pathconf_path_max(us_test_group_t *tg) {
    long v = pathconf("/", _PC_PATH_MAX);
    TEST(tg, "pathconf(\"/\", _PC_PATH_MAX) > 0", v > 0);
    TEST(tg, "pathconf(\"/\", _PC_PATH_MAX) == PATH_MAX (1024)", v == PATH_MAX);
}

static void test_pathconf_pipe_buf(us_test_group_t *tg) {
    long v = pathconf("/", _PC_PIPE_BUF);
    TEST(tg, "pathconf(\"/\", _PC_PIPE_BUF) >= 512", v >= 512);
    TEST(tg, "pathconf(\"/\", _PC_PIPE_BUF) == PIPE_BUF (4096)", v == PIPE_BUF);
}

static void test_pathconf_no_trunc(us_test_group_t *tg) {
    /* _PC_NO_TRUNC = 1 means long filenames return ENAMETOOLONG (not silently
     * truncated) — required for POSIX conformance. */
    long v = pathconf("/", _PC_NO_TRUNC);
    TEST(tg, "pathconf(\"/\", _PC_NO_TRUNC) == 1 (ENAMETOOLONG on long names)",
         v == 1);
}

static void test_pathconf_enoent(us_test_group_t *tg) {
    /* A path that does not exist must return -1. */
    long v = pathconf("/this/path/does/not/exist", _PC_NAME_MAX);
    TEST(tg, "pathconf(nonexistent path) returns -1", v == -1L);
}

static void test_pathconf_on_file(us_test_group_t *tg) {
    /* pathconf works on a regular file path too, not just directories. */
    long v = pathconf("/hello.txt", _PC_NAME_MAX);
    TEST(tg, "pathconf(\"/hello.txt\", _PC_NAME_MAX) == NAME_MAX", v == NAME_MAX);
}

/* ── fpathconf() ───────────────────────────────────────────────────────────── */

static void test_fpathconf_stdin(us_test_group_t *tg) {
    /* fd 0 (stdin) is always open; fpathconf must succeed on it. */
    long v = fpathconf(0, _PC_NAME_MAX);
    TEST(tg, "fpathconf(0 /*stdin*/, _PC_NAME_MAX) == NAME_MAX", v == NAME_MAX);
}

static void test_fpathconf_open_file(us_test_group_t *tg) {
    int fd = open("/hello.txt", O_RDONLY);
    if (fd < 0) {
        SKIP(tg, "fpathconf(file fd, _PC_PATH_MAX): open failed", "/hello.txt not found");
        SKIP(tg, "fpathconf(file fd, _PC_NAME_MAX): open failed", "/hello.txt not found");
        return;
    }
    TEST(tg, "fpathconf(file fd, _PC_PATH_MAX) == PATH_MAX",
         fpathconf(fd, _PC_PATH_MAX) == PATH_MAX);
    TEST(tg, "fpathconf(file fd, _PC_NAME_MAX) == NAME_MAX",
         fpathconf(fd, _PC_NAME_MAX) == NAME_MAX);
    close(fd);
}

static void test_fpathconf_ebadf(us_test_group_t *tg) {
    /* An fd that is not open must return -1. */
    long v = fpathconf(-1, _PC_NAME_MAX);
    TEST(tg, "fpathconf(-1, _PC_NAME_MAX) returns -1 (EBADF)", v == -1L);

    long v2 = fpathconf(100, _PC_NAME_MAX);
    TEST(tg, "fpathconf(100 /*closed*/, _PC_NAME_MAX) returns -1 (EBADF)", v2 == -1L);
}

/* ── entry point ───────────────────────────────────────────────────────────── */

void run_test_pathconf(us_test_group_t *group) {
    ut_group_begin(group, "pathconf");
    test_pathconf_name_max(group);
    test_pathconf_path_max(group);
    test_pathconf_pipe_buf(group);
    test_pathconf_no_trunc(group);
    test_pathconf_enoent(group);
    test_pathconf_on_file(group);
    test_fpathconf_stdin(group);
    test_fpathconf_open_file(group);
    test_fpathconf_ebadf(group);
    ut_group_end(group);
}

#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include "us_test_common.h"

#define TESTFILE "/tmp/fsync_test.txt"

static void test_fsync_basic(us_test_group_t *group) {
    int fd = open(TESTFILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    TEST(group, "fsync-basic: open writable file", fd >= 0);
    if (fd < 0) return;

    int n = write(fd, "fsync test data\n", 16);
    TEST(group, "fsync-basic: write 16 bytes", n == 16);

    int r = fsync(fd);
    TEST(group, "fsync-basic: fsync returns 0", r == 0);
    close(fd);
}

static void test_fsync_bad_fd(us_test_group_t *group) {
    int r = fsync(-1);
    TEST(group, "fsync-badfd: fsync(-1) returns error", r < 0);

    r = fsync(15);  /* MAX_FDS-1; slot is closed */
    TEST(group, "fsync-badfd: fsync on closed slot returns error", r < 0);
}

static void test_fsync_pipe(us_test_group_t *group) {
    int pfd[2];
    int r = pipe(pfd);
    TEST(group, "fsync-pipe: pipe() succeeds", r == 0);
    if (r < 0) return;

    r = fsync(pfd[0]);
    TEST(group, "fsync-pipe: fsync on read end returns error", r < 0);
    r = fsync(pfd[1]);
    TEST(group, "fsync-pipe: fsync on write end returns error", r < 0);

    close(pfd[0]);
    close(pfd[1]);
}

static void test_fdatasync_basic(us_test_group_t *group) {
    int fd = open(TESTFILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    TEST(group, "fdatasync-basic: open writable file", fd >= 0);
    if (fd < 0) return;

    int n = write(fd, "fdatasync\n", 10);
    TEST(group, "fdatasync-basic: write 10 bytes", n == 10);

    int r = fdatasync(fd);
    TEST(group, "fdatasync-basic: fdatasync returns 0", r == 0);
    close(fd);
}

void run_test_fsync(us_test_group_t *group) {
    ut_group_begin(group, "fsync/fdatasync");
    test_fsync_basic(group);
    test_fsync_bad_fd(group);
    test_fsync_pipe(group);
    test_fdatasync_basic(group);
    ut_group_end(group);
}
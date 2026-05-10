#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include "us_test_common.h"

void run_test_readlink_negative(us_test_group_t *group) {
    ut_group_begin(group, "readlink negative cases");

    char buf[256];
    ssize_t ret;

    // ── Test 1: readlink on regular file must fail with EINVAL ─────────────

    // Create a regular file in /tmp
    int fd = open("/tmp/readlink_test_file.txt", O_CREAT | O_WRONLY, 0644);
    if (fd >= 0) {
        write(fd, "test content", 12);
        close(fd);

        // Try to readlink on it
        errno = 0;
        ret = readlink("/tmp/readlink_test_file.txt", buf, sizeof(buf) - 1);
        TEST(group, "readlink on regular file returns -1", ret == -1);
        TEST(group, "readlink on regular file sets errno to EINVAL", errno == EINVAL);
        
        unlink("/tmp/readlink_test_file.txt");
    } else {
        SKIP(group, "readlink on regular file returns -1", "could not create test file");
        SKIP(group, "readlink on regular file sets errno to EINVAL", "could not create test file");
    }

    // ── Test 2: readlink on directory must fail with EINVAL ───────────────

    // Create a directory
    if (mkdir("/tmp/readlink_test_dir", 0755) == 0) {
        errno = 0;
        ret = readlink("/tmp/readlink_test_dir", buf, sizeof(buf) - 1);
        TEST(group, "readlink on directory returns -1", ret == -1);
        TEST(group, "readlink on directory sets errno to EINVAL", errno == EINVAL);
        
        rmdir("/tmp/readlink_test_dir");
    } else {
        SKIP(group, "readlink on directory returns -1", "could not create test dir");
        SKIP(group, "readlink on directory sets errno to EINVAL", "could not create test dir");
    }

    // ── Test 3: readlink on symlink succeeds ──────────────────────────────

    if (symlink("/etc/hostname", "/tmp/readlink_test_symlink") == 0) {
        errno = 0;
        ret = readlink("/tmp/readlink_test_symlink", buf, sizeof(buf) - 1);
        TEST(group, "readlink on symlink returns positive", ret > 0);
        if (ret > 0) {
            buf[ret] = '\0';
            TEST(group, "readlink on symlink returns correct target", strcmp(buf, "/etc/hostname") == 0);
        }
        
        unlink("/tmp/readlink_test_symlink");
    } else {
        SKIP(group, "readlink on symlink returns positive", "could not create test symlink");
        SKIP(group, "readlink on symlink returns correct target", "could not create test symlink");
    }

    // ── Test 4: readlink on procfs regular file (status) ───────────────────

    errno = 0;
    ret = readlink("/proc/self/status", buf, sizeof(buf) - 1);
    TEST(group, "readlink on /proc/self/status returns -1", ret == -1);
    TEST(group, "readlink on /proc/self/status sets errno to EINVAL", errno == EINVAL);

    // ── Test 5: readlink on procfs directory (self) with O_DIRECTORY ───────

    // Note: /proc/self is a symlink, so readlink should succeed
    errno = 0;
    ret = readlink("/proc/self", buf, sizeof(buf) - 1);
    TEST(group, "readlink on /proc/self (symlink) returns positive", ret > 0);
    if (ret > 0) {
        buf[ret] = '\0';
        // Should be a numeric string (pid)
        int valid = 1;
        for (int i = 0; i < (int)ret; i++) {
            if (buf[i] < '0' || buf[i] > '9') { valid = 0; break; }
        }
        TEST(group, "readlink on /proc/self returns numeric pid", valid);
    }

    ut_group_end(group);
}

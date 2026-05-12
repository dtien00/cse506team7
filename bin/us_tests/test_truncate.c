/*
 * test_truncate.c — userspace correctness tests for ftruncate() / truncate().
 *
 * Covers: truncate to zero, partial shrink, extension (sparse), read-back
 * verification, truncate-by-path, and error cases (EBADF, ENOENT,
 * EINVAL for negative length, EACCES for read-only fd).
 */

#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>
#include "us_test_common.h"

static const char *PATH = "/tmp/us_trunc_test.txt";
static const char *DATA = "Hello, ftruncate world!";  /* 23 bytes */
#define DLEN 23

static int setup_file(void) {
    int fd = open(PATH, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) return -1;
    int w = write(fd, DATA, DLEN);
    close(fd);
    return (w == DLEN) ? 0 : -1;
}

void run_test_truncate(us_test_group_t *group)
{
    ut_group_begin(group, "ftruncate/truncate");

    struct stat st;

    /* ── ftruncate to zero ───────────────────────────────────────────── */
    ut_assert(group, setup_file() == 0, "setup: write 23-byte file");
    int fd = open(PATH, O_WRONLY);
    ut_assert(group, fd >= 0, "ftruncate(0): open");
    if (fd >= 0) {
        ut_assert(group, ftruncate(fd, 0) == 0, "ftruncate(0): returns 0");
        close(fd);
        ut_assert(group, stat(PATH, &st) == 0 && st.st_size == 0,
                  "ftruncate(0): size == 0");
    }

    /* ── ftruncate partial shrink ────────────────────────────────────── */
    ut_assert(group, setup_file() == 0, "setup: rewrite 23 bytes");
    fd = open(PATH, O_WRONLY);
    ut_assert(group, fd >= 0, "ftruncate(10): open");
    if (fd >= 0) {
        ut_assert(group, ftruncate(fd, 10) == 0, "ftruncate(10): returns 0");
        close(fd);
        ut_assert(group, stat(PATH, &st) == 0 && st.st_size == 10,
                  "ftruncate(10): size == 10");

        char buf[32] = {0};
        fd = open(PATH, O_RDONLY);
        if (fd >= 0) {
            int n = read(fd, buf, sizeof(buf));
            close(fd);
            ut_assert(group, n == 10, "ftruncate(10): read 10 bytes");
            ut_assert(group, strncmp(buf, DATA, 10) == 0,
                      "ftruncate(10): content correct");
        }
    }

    /* ── ftruncate extend (sparse) ───────────────────────────────────── */
    fd = open(PATH, O_WRONLY);
    ut_assert(group, fd >= 0, "ftruncate(100): open");
    if (fd >= 0) {
        ut_assert(group, ftruncate(fd, 100) == 0, "ftruncate(100): returns 0");
        close(fd);
        ut_assert(group, stat(PATH, &st) == 0 && st.st_size == 100,
                  "ftruncate(100): size == 100");

        /* Bytes past old EOF must read as zero */
        char buf[16] = {0};
        fd = open(PATH, O_RDONLY);
        if (fd >= 0) {
            lseek(fd, 50, 0 /* SEEK_SET */);
            int n = read(fd, buf, 10);
            close(fd);
            ut_assert(group, n == 10, "ftruncate(extend): read past old EOF");
            int allzero = 1;
            for (int i = 0; i < n; i++) if (buf[i]) { allzero = 0; break; }
            ut_assert(group, allzero, "ftruncate(extend): new bytes are zero");
        }
    }

    /* ── truncate(path, …) by path ───────────────────────────────────── */
    ut_assert(group, setup_file() == 0, "setup: rewrite for truncate()");
    ut_assert(group, truncate(PATH, 7) == 0,   "truncate(path,7): returns 0");
    ut_assert(group, stat(PATH, &st) == 0 && st.st_size == 7,
              "truncate(path,7): size == 7");
    ut_assert(group, truncate(PATH, 0) == 0,   "truncate(path,0): returns 0");
    ut_assert(group, stat(PATH, &st) == 0 && st.st_size == 0,
              "truncate(path,0): size == 0");

    /* ── error: bad fd → EBADF ───────────────────────────────────────── */
    errno = 0;
    ut_assert(group, ftruncate(9999, 0) == -1 && errno == EBADF,
              "ftruncate(EBADF): errno==EBADF");

    /* ── error: negative length → EINVAL ────────────────────────────── */
    fd = open(PATH, O_WRONLY);
    if (fd >= 0) {
        errno = 0;
        ut_assert(group, ftruncate(fd, -1) == -1 && errno == EINVAL,
                  "ftruncate(-1): errno==EINVAL");
        close(fd);
    }

    /* ── error: read-only fd → EACCES ───────────────────────────────── */
    fd = open(PATH, O_RDONLY);
    if (fd >= 0) {
        errno = 0;
        int r = ftruncate(fd, 0);
        ut_assert(group, r == -1 && (errno == EACCES || errno == EINVAL),
                  "ftruncate(RDONLY): EACCES or EINVAL");
        close(fd);
    }

    /* ── error: missing path → ENOENT ───────────────────────────────── */
    errno = 0;
    ut_assert(group, truncate("/tmp/no_such_us_trunc_xyz.txt", 0) == -1
                     && errno == ENOENT,
              "truncate(missing): errno==ENOENT");

    /* ── error: negative length → EINVAL ────────────────────────────── */
    errno = 0;
    ut_assert(group, truncate(PATH, -1) == -1 && errno == EINVAL,
              "truncate(negative): errno==EINVAL");

    unlink(PATH);
}

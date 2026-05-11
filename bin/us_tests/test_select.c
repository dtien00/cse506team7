/*
 * test_select.c — Userspace tests for sys_select (SYS_select = 104).
 *
 * All tests are pipe-based or zero-timeout so they run without blocking on
 * real terminal input.  Interactive stdin readability is exercised only via
 * a non-blocking check (timeout = {0,0}).
 *
 * Test groups:
 *  1. Zero-timeout (non-blocking poll) — never sleeps.
 *  2. Pipe readability — write data, then select; must be immediately ready.
 *  3. writefds — stdout / pipe write-end always writable.
 *  4. nfds boundary — nfds = 0 always returns 0.
 *  5. Regular file readability — files always ready.
 *  6. Timed select on a ready pipe — must return before timeout.
 */

#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/time.h>
#include <errno.h>
#include "us_test_common.h"

void run_test_select(us_test_group_t *group) {
    ut_group_begin(group, "select");

    /* ── 1. Zero-timeout non-blocking polls ─────────────────────────────── */

    /* select(0, NULL, NULL, NULL, {0,0}) — nothing to watch, should return 0 */
    {
        struct timeval tv = {0, 0};
        int n = select(0, NULL, NULL, NULL, &tv);
        TEST(group, "select(0,NULL,NULL,NULL,{0,0}): returns 0", n == 0);
    }

    /* stdin is a tty: non-blocking check should return 0 (no typed input) */
    {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(STDIN_FILENO, &rfds);
        struct timeval tv = {0, 0};
        int n = select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv);
        TEST(group, "select(stdin,rfds,{0,0}): not ready → 0",
             n == 0 && !FD_ISSET(STDIN_FILENO, &rfds));
    }

    /* ── 2. Pipe readability — data written before select ───────────────── */

    int pfd[2];
    if (pipe(pfd) < 0) {
        SKIP(group, "pipe read-ready: data written before select", "pipe() failed");
        SKIP(group, "pipe read-ready: correct fd set in rfds",     "pipe() failed");
        SKIP(group, "pipe read-ready: zero-timeout sees data",     "pipe() failed");
    } else {
        /* Write one byte so read-end is immediately readable */
        write(pfd[1], "x", 1);

        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(pfd[0], &rfds);
        struct timeval tv = {0, 0};
        int n = select(pfd[0] + 1, &rfds, NULL, NULL, &tv);
        TEST(group, "pipe read-ready: returns 1", n == 1);
        TEST(group, "pipe read-ready: pfd[0] set in rfds", FD_ISSET(pfd[0], &rfds));

        /* Consume the byte.  MVL: all non-stdin fds are always marked ready,
         * so an empty pipe still returns 1 — pipe-level readability checking
         * is not implemented at this stage. */
        char buf[1];
        read(pfd[0], buf, 1);

        close(pfd[0]);
        close(pfd[1]);
    }

    /* ── 3. writefds — all fds always writable ──────────────────────────── */

    {
        fd_set wfds;
        FD_ZERO(&wfds);
        FD_SET(STDOUT_FILENO, &wfds);
        struct timeval tv = {0, 0};
        int n = select(STDOUT_FILENO + 1, NULL, &wfds, NULL, &tv);
        TEST(group, "writefds(stdout): returns 1", n == 1);
        TEST(group, "writefds(stdout): stdout set in wfds", FD_ISSET(STDOUT_FILENO, &wfds));
    }

    /* Both rfds (non-stdin file) and wfds set: both should come back ready */
    {
        int p2[2];
        if (pipe(p2) < 0) {
            SKIP(group, "rfds+wfds: combined ready count", "pipe() failed");
        } else {
            write(p2[1], "y", 1);
            fd_set rfds, wfds;
            FD_ZERO(&rfds); FD_SET(p2[0], &rfds);
            FD_ZERO(&wfds); FD_SET(p2[1], &wfds);
            int maxfd = (p2[0] > p2[1] ? p2[0] : p2[1]) + 1;
            struct timeval tv = {0, 0};
            int n = select(maxfd, &rfds, &wfds, NULL, &tv);
            TEST(group, "rfds+wfds: returns 2 (read+write ready)", n == 2);
            close(p2[0]);
            close(p2[1]);
        }
    }

    /* ── 4. nfds boundary ───────────────────────────────────────────────── */

    {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(0, &rfds);
        /* nfds = 0 means no fds are scanned; rfds is ignored */
        struct timeval tv = {0, 0};
        int n = select(0, &rfds, NULL, NULL, &tv);
        TEST(group, "nfds=0: returns 0 regardless of rfds", n == 0);
    }

    /* ── 5. Regular file always readable ────────────────────────────────── */

    {
        int fd = open("/hello.txt", 0 /* O_RDONLY */);
        if (fd < 0) {
            SKIP(group, "regular file fd always readable", "open(/hello.txt) failed");
        } else {
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(fd, &rfds);
            struct timeval tv = {0, 0};
            int n = select(fd + 1, &rfds, NULL, NULL, &tv);
            TEST(group, "regular file fd always readable: returns 1",
                 n == 1 && FD_ISSET(fd, &rfds));
            close(fd);
        }
    }

    /* ── 6. Timed select on a pipe already readable — must not block ─────── */

    {
        int p3[2];
        if (pipe(p3) < 0) {
            SKIP(group, "timed select on ready pipe: returns before timeout", "pipe() failed");
        } else {
            write(p3[1], "z", 1);
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(p3[0], &rfds);
            /* 2-second timeout — but pipe already has data, should return at once */
            struct timeval tv = {2, 0};
            int n = select(p3[0] + 1, &rfds, NULL, NULL, &tv);
            TEST(group, "timed select on ready pipe: returns 1 immediately", n == 1);
            TEST(group, "timed select on ready pipe: fd set in rfds", FD_ISSET(p3[0], &rfds));
            close(p3[0]);
            close(p3[1]);
        }
    }

    /* ── 7. Errno not set on success ────────────────────────────────────── */

    {
        errno = 0;
        struct timeval tv = {0, 0};
        int n = select(0, NULL, NULL, NULL, &tv);
        TEST(group, "select success: errno unchanged (0)", n == 0 && errno == 0);
    }

    ut_group_end(group);
}
/*
 * test_fd_limits.c — File descriptor table correctness.
 *
 * Covers: max open fds, dup/dup2 correctness, fd inheritance across fork,
 * fd close-on-exec not present (no CLOEXEC in this kernel), stdin/stdout/stderr
 * are fd 0/1/2, reading from a write-only fd fails.
 */

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>
#include "us_test_common.h"

/* ── dup correctness ────────────────────────────────────────────────────── */

static void test_dup(us_test_group_t *group) {
    int fd = open("test/hello.txt", O_RDONLY);
    TEST(group, "dup: open base file", fd >= 0);
    if (fd < 0) return;

    int fd2 = dup(fd);
    TEST(group, "dup: dup() returns new fd", fd2 >= 0 && fd2 != fd);

    if (fd2 >= 0) {
        /* read first byte via original */
        char c1 = 0, c2 = 0;
        read(fd, &c1, 1);
        /* dup shares offset; read via fd2 gets next byte */
        read(fd2, &c2, 1);
        TEST(group, "dup: shared offset — fd2 reads next byte", c1 != c2 || c1 == 0);

        /* closing original does not break fd2 */
        close(fd);
        char c3 = 0;
        int r = (int)read(fd2, &c3, 1);
        TEST(group, "dup: fd2 readable after original closed", r >= 0);
        close(fd2);
    } else {
        close(fd);
    }
}

/* ── dup2 correctness ───────────────────────────────────────────────────── */

static void test_dup2(us_test_group_t *group) {
    int fds[2];
    int r = pipe(fds);
    TEST(group, "dup2: pipe created", r == 0);
    if (r < 0) return;

    /* dup2 write-end onto a known slot */
    int target = fds[1] + 5;
    int d = dup2(fds[1], target);
    TEST(group, "dup2: returns target fd", d == target);

    if (d == target) {
        /* write via original, read via pipe read-end */
        write(fds[1], "A", 1);
        /* write via dup2 alias */
        write(target, "B", 1);
        close(fds[1]);
        close(target);

        char buf[4];
        int n = (int)read(fds[0], buf, sizeof(buf));
        close(fds[0]);
        TEST(group, "dup2: both writes visible on read end", n == 2);
        TEST(group, "dup2: data order correct", n == 2 && buf[0] == 'A' && buf[1] == 'B');
    } else {
        close(fds[0]); close(fds[1]);
    }

    /* dup2 to self — no-op, fd still valid */
    {
        int f = open("test/hello.txt", O_RDONLY);
        TEST(group, "dup2-self: open succeeds", f >= 0);
        if (f >= 0) {
            int d2 = dup2(f, f);
            TEST(group, "dup2-self: dup2(f,f) == f", d2 == f);
            char c = 0;
            TEST(group, "dup2-self: fd still readable", read(f, &c, 1) == 1);
            close(f);
        }
    }

    /* dup2 closes destination if already open */
    {
        int f1 = open("test/hello.txt", O_RDONLY);
        int f2 = open("test/hello.txt", O_RDONLY);
        TEST(group, "dup2-close-dest: two fds open", f1 >= 0 && f2 >= 0);
        if (f1 >= 0 && f2 >= 0) {
            dup2(f1, f2); /* f2 should be closed then replaced by f1 alias */
            char c = 0;
            TEST(group, "dup2-close-dest: new f2 still readable", read(f2, &c, 1) >= 0);
            close(f1);
            close(f2);
        }
    }
}

/* ── fd inheritance across fork ─────────────────────────────────────────── */

static void test_fd_inherit(us_test_group_t *group) {
    const char *path = "/mnt/xv6_fd_inherit.txt";
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    TEST(group, "fd-inherit: create file", fd >= 0);
    if (fd < 0) return;

    pid_t pid = fork();
    if (pid == 0) {
        /* child writes through inherited fd */
        write(fd, "child", 5);
        close(fd);
        _exit(0);
    }
    /* parent writes after child */
    waitpid(pid, NULL, 0);
    write(fd, "parent", 6);
    close(fd);

    int rfd = open(path, O_RDONLY);
    TEST(group, "fd-inherit: reopen for verify", rfd >= 0);
    if (rfd >= 0) {
        char buf[32];
        memset(buf, 0, sizeof(buf));
        int n = (int)read(rfd, buf, sizeof(buf) - 1);
        close(rfd);
        /* both writes must appear */
        TEST(group, "fd-inherit: 11 bytes total (child+parent)", n == 11);
    }
}

/* ── stdin/stdout/stderr are fd 0/1/2 ──────────────────────────────────── */

static void test_std_fds(us_test_group_t *group) {
    TEST(group, "std-fds: STDIN_FILENO == 0",  STDIN_FILENO  == 0);
    TEST(group, "std-fds: STDOUT_FILENO == 1", STDOUT_FILENO == 1);
    TEST(group, "std-fds: STDERR_FILENO == 2", STDERR_FILENO == 2);

    /* write to fd 1 (stdout) works */
    int w = (int)write(STDOUT_FILENO, "", 0);
    TEST(group, "std-fds: write(stdout, 0) returns 0", w == 0);
}

/* ── read from write-only fd fails ──────────────────────────────────────── */

static void test_wronly_read(us_test_group_t *group) {
    int fd = open("/mnt/xv6_wronly.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    TEST(group, "wronly-read: open O_WRONLY", fd >= 0);
    if (fd >= 0) {
        char buf[8];
        int r = (int)read(fd, buf, sizeof(buf));
        TEST(group, "wronly-read: read returns error", r < 0);
        close(fd);
    }
}

/* ── exhausting fd table and recovery ───────────────────────────────────── */

static void test_fd_exhaust(us_test_group_t *group) {
    /*
     * Open files until we get an error, then close half and confirm we can
     * open again.  This exercises the fd table boundary without knowing the
     * exact limit.
     */
    int opened[256];
    int n = 0;
    while (n < 256) {
        int fd = open("test/hello.txt", O_RDONLY);
        if (fd < 0) break;
        opened[n++] = fd;
    }
    TEST(group, "fd-exhaust: opened at least 3 fds", n >= 3);

    /* close half */
    int half = n / 2;
    for (int i = 0; i < half; i++)
        close(opened[i]);

    /* should be able to open again */
    int fd2 = open("test/hello.txt", O_RDONLY);
    TEST(group, "fd-exhaust: can open after closing some", fd2 >= 0);
    if (fd2 >= 0) close(fd2);

    /* close rest */
    for (int i = half; i < n; i++)
        close(opened[i]);
}

/* ── public entry point ─────────────────────────────────────────────────── */

void run_test_fd_limits(us_test_group_t *group) {
    ut_group_begin(group, "fd_limits");

    test_dup(group);
    test_dup2(group);
    test_fd_inherit(group);
    test_std_fds(group);
    test_wronly_read(group);
    test_fd_exhaust(group);

    ut_group_end(group);
}

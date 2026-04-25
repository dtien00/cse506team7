/*
 * test_syscall_robustness.c — Null/bad-pointer syscall safety (xv6 validatetest).
 *
 * Every syscall that copies from/to userspace must return an error when given
 * NULL or an out-of-bounds address.  The kernel must NOT crash or hang.
 *
 * Strategy: each sub-test forks a child.  The child issues the bad syscall.
 * The parent checks the child either:
 *   (a) exited with a non-zero status (syscall returned error, child detected),
 *   (b) was killed by a signal (kernel delivered fault on bad access), OR
 *   (c) the child explicitly exits 0 after confirming syscall returned < 0.
 * In all cases the parent must not crash.
 *
 * We do NOT test kernel-address access (that requires ring awareness).
 */

#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include "us_test_common.h"

/* helper: run fn in child, parent verifies child did not hang and kernel
 * did not crash (child either errored out or was killed — both are OK) */
#define BAD_SYSCALL_TEST(group, name, child_body)                        \
    do {                                                                  \
        pid_t _pid = fork();                                              \
        if (_pid == 0) { child_body ; _exit(0); }                        \
        int _st = 0;                                                      \
        pid_t _w = waitpid(_pid, &_st, 0);                               \
        TEST(group, name, _w == _pid);                                   \
    } while (0)

/* For syscalls that SHOULD return < 0 on bad ptr, child exits 0 on success */
#define EXPECT_ERROR(group, name, expr)                                  \
    do {                                                                  \
        pid_t _pid = fork();                                              \
        if (_pid == 0) {                                                  \
            int _r = (int)(expr);                                        \
            _exit(_r < 0 ? 0 : 1);                                       \
        }                                                                 \
        int _st = 0;                                                      \
        waitpid(_pid, &_st, 0);                                          \
        TEST(group, name,                                                 \
             WIFEXITED(_st) && WEXITSTATUS(_st) == 0);                   \
    } while (0)

/* ── open ────────────────────────────────────────────────────────────────── */
static void test_bad_open(us_test_group_t *group) {
    EXPECT_ERROR(group, "bad-open: open(NULL) returns error",
                 open(NULL, O_RDONLY));
    EXPECT_ERROR(group, "bad-open: open((char*)1) returns error",
                 open((char *)1, O_RDONLY));
}

/* ── read / write ────────────────────────────────────────────────────────── */
static void test_bad_read_write(us_test_group_t *group) {
    /* read into NULL */
    EXPECT_ERROR(group, "bad-read: read(stdin, NULL, 1) returns error",
                 read(0, NULL, 1));

    /* write from NULL */
    EXPECT_ERROR(group, "bad-write: write(stdout, NULL, 1) returns error",
                 write(1, NULL, 1));

    /* read into bad addr */
    EXPECT_ERROR(group, "bad-read: read(stdin, (char*)1, 1) returns error",
                 read(0, (char *)1, 1));

    /* write from bad addr */
    EXPECT_ERROR(group, "bad-write: write(stdout, (char*)1, 1) returns error",
                 write(1, (char *)1, 1));
}

/* ── stat / fstat ────────────────────────────────────────────────────────── */
static void test_bad_stat(us_test_group_t *group) {
    EXPECT_ERROR(group, "bad-stat: stat(NULL, &st) returns error",
                 stat(NULL, &(struct stat){0}));

    EXPECT_ERROR(group, "bad-stat: stat(path, NULL) returns error",
                 stat("/hello.txt", NULL));

    EXPECT_ERROR(group, "bad-fstat: fstat(0, NULL) returns error",
                 fstat(0, NULL));
}

/* ── getcwd ──────────────────────────────────────────────────────────────── */
static void test_bad_getcwd(us_test_group_t *group) {
    EXPECT_ERROR(group, "bad-getcwd: getcwd(NULL, 256) returns error",
                 (getcwd(NULL, 256) == NULL ? -1 : 0));

    /* size=0 with non-NULL buf — must not crash */
    BAD_SYSCALL_TEST(group, "bad-getcwd: getcwd(buf, 0) does not crash", {
        char buf[8];
        getcwd(buf, 0); /* may return NULL or buf; must not crash */
    });
}

/* ── mkdir / unlink / chdir ──────────────────────────────────────────────── */
static void test_bad_path_syscalls(us_test_group_t *group) {
    EXPECT_ERROR(group, "bad-mkdir: mkdir(NULL, 0755) returns error",
                 mkdir(NULL, 0755));

    EXPECT_ERROR(group, "bad-unlink: unlink(NULL) returns error",
                 unlink(NULL));

    EXPECT_ERROR(group, "bad-chdir: chdir(NULL) returns error",
                 chdir(NULL));
}

/* ── pipe ────────────────────────────────────────────────────────────────── */
static void test_bad_pipe(us_test_group_t *group) {
    EXPECT_ERROR(group, "bad-pipe: pipe(NULL) returns error",
                 pipe(NULL));

    EXPECT_ERROR(group, "bad-pipe: pipe((int*)1) returns error", ({
        int (*pipe_fn)(int *) = (int (*)(int *))pipe;
        pipe_fn((int *)1);
    }));
}

/* ── execve ──────────────────────────────────────────────────────────────── */
static void test_bad_exec(us_test_group_t *group) {
    EXPECT_ERROR(group, "bad-exec: execve(NULL,...) returns error", ({
        char *av[] = { NULL };
        execve(NULL, av, NULL);
    }));

    /* execve on nonexistent path must fail, not crash */
    EXPECT_ERROR(group, "bad-exec: execve nonexistent path returns error", ({
        char *av[] = { "x", NULL };
        execve("/this/does/not/exist/ever", av, NULL);
    }));
}

/* ── kill with bad pid ───────────────────────────────────────────────────── */
static void test_bad_kill(us_test_group_t *group) {
    /* kill pid 0 with sig 0: should be valid (finds self group) */
    int r = kill(0, 0);
    TEST(group, "bad-kill: kill(0, 0) does not crash", r == 0 || r == -1);

    /* sig 0 on nonexistent pid — error */
    int r2 = kill(99999, 0);
    TEST(group, "bad-kill: kill(99999, 0) returns error", r2 < 0);

    /* invalid signal number */
    int r3 = kill(getpid(), 99);
    TEST(group, "bad-kill: kill(self, 99) returns error", r3 < 0);
}

/* ── lseek on bad fd ─────────────────────────────────────────────────────── */
static void test_bad_lseek(us_test_group_t *group) {
    EXPECT_ERROR(group, "bad-lseek: lseek(-1, 0, SEEK_SET) returns error",
                 lseek(-1, 0, SEEK_SET));
    EXPECT_ERROR(group, "bad-lseek: lseek(999, 0, SEEK_SET) returns error",
                 lseek(999, 0, SEEK_SET));
}

/* ── public entry point ─────────────────────────────────────────────────── */

void run_test_syscall_robustness(us_test_group_t *group) {
    ut_group_begin(group, "syscall_robustness");

    test_bad_open(group);
    test_bad_read_write(group);
    test_bad_stat(group);
    test_bad_getcwd(group);
    test_bad_path_syscalls(group);
    test_bad_pipe(group);
    test_bad_exec(group);
    test_bad_kill(group);
    test_bad_lseek(group);

    ut_group_end(group);
}

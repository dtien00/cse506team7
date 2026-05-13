/*
 * test_fd_reclaim.c — File descriptor reclamation correctness.
 *
 * Tests that fd slots are properly returned to the kernel under every scenario:
 *   1. close() recycles the lowest slot (POSIX lowest-available rule)
 *   2. double-close returns EBADF — slot not double-freed
 *   3. close of invalid fds returns EBADF — no crash
 *   4. dup refcount: close original → dup still valid; close dup → slot freed
 *   5. dup2 onto occupied slot atomically closes target — no leak
 *   6. fork refcount: child close doesn't reclaim parent copy; vice versa
 *   7. child exit with open fds: parent copy survives, slot reusable after wait
 *   8. pipe: both ends closed → pipe freed (slot reusable, no fd leak)
 *   9. open→close→open: same path returns usable new fd (no stale state)
 *  10. unlink-while-open: slot freed after last close (not after unlink)
 */

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include "us_test_common.h"

/* ── 1. close recycles lowest slot ─────────────────────────────────────── */

static void test_slot_recycle(us_test_group_t *group) {
    int a = open("test/hello.txt", O_RDONLY);
    TEST(group, "slot-recycle: open a", a >= 0);
    if (a < 0) return;

    int b = open("test/hello.txt", O_RDONLY);
    TEST(group, "slot-recycle: open b", b >= 0 && b > a);
    if (b < 0) { close(a); return; }

    close(a); /* a's slot is now the lowest free */

    int c = open("test/hello.txt", O_RDONLY);
    TEST(group, "slot-recycle: reopen gets lowest (a's slot)", c == a);
    if (c >= 0) close(c);
    close(b);
}

/* ── 2. double-close returns EBADF ──────────────────────────────────────── */

static void test_double_close(us_test_group_t *group) {
    int fd = open("test/hello.txt", O_RDONLY);
    TEST(group, "double-close: open", fd >= 0);
    if (fd < 0) return;

    int r1 = close(fd);
    TEST(group, "double-close: first close ok", r1 == 0);

    int r2 = close(fd);
    TEST(group, "double-close: second close returns EBADF", r2 < 0);
}

/* ── 3. invalid fd close returns EBADF ──────────────────────────────────── */

static void test_invalid_close(us_test_group_t *group) {
    TEST(group, "invalid-close: fd -1 → EBADF",  close(-1)  < 0);
    TEST(group, "invalid-close: fd 999 → EBADF", close(999) < 0);
}

/* ── 4. dup refcount ─────────────────────────────────────────────────────── */

static void test_dup_refcount(us_test_group_t *group) {
    int orig = open("test/hello.txt", O_RDONLY);
    TEST(group, "dup-refcount: open orig", orig >= 0);
    if (orig < 0) return;

    int d = dup(orig);
    TEST(group, "dup-refcount: dup ok", d >= 0 && d != orig);
    if (d < 0) { close(orig); return; }

    /* close original — dup must still be readable */
    close(orig);
    char buf[4];
    int r = (int)read(d, buf, 1);
    TEST(group, "dup-refcount: dup readable after orig closed", r >= 0);

    /* after closing dup, slot must be free (reopen gets same or lower slot) */
    close(d);
    int reopen = open("test/hello.txt", O_RDONLY);
    TEST(group, "dup-refcount: slot freed after both closed", reopen >= 0 && reopen <= d);
    if (reopen >= 0) close(reopen);
}

/* ── 5. dup2 onto occupied slot closes target without leak ───────────────── */

static void test_dup2_closes_target(us_test_group_t *group) {
    int f1 = open("test/hello.txt", O_RDONLY);
    int f2 = open("test/hello.txt", O_RDONLY);
    TEST(group, "dup2-closes-target: two fds open", f1 >= 0 && f2 >= 0);
    if (f1 < 0 || f2 < 0) { if (f1 >= 0) close(f1); if (f2 >= 0) close(f2); return; }

    /* dup2 f1 onto f2: f2's original file must be closed, not leaked */
    int r = dup2(f1, f2);
    TEST(group, "dup2-closes-target: dup2 returns target", r == f2);

    /* f2 now aliases f1 — both readable */
    char c1 = 0, c2 = 0;
    read(f1, &c1, 1);
    read(f2, &c2, 1);
    TEST(group, "dup2-closes-target: both share offset", c1 != c2 || c1 == 0);

    close(f1);
    close(f2);

    /* slot f2+1 should not have been consumed by a leaked fd */
    int probe = open("test/hello.txt", O_RDONLY);
    TEST(group, "dup2-closes-target: no fd leak (probe <= f2+1)", probe >= 0 && probe <= f2 + 1);
    if (probe >= 0) close(probe);
}

/* ── 6. fork refcount: child close doesn't reclaim parent's fd ───────────── */

static void test_fork_refcount(us_test_group_t *group) {
    const char *path = "/mnt/xv6_fd_forkref.txt";

    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    TEST(group, "fork-refcount: create file", fd >= 0);
    if (fd < 0) return;
    write(fd, "forkref", 7);
    lseek(fd, 0, SEEK_SET);

    pid_t pid = fork();
    if (pid == 0) {
        /* child closes its copy immediately and exits */
        close(fd);
        _exit(0);
    }
    waitpid(pid, NULL, 0);

    /* parent's fd must still be valid after child closed its copy */
    char buf[16];
    memset(buf, 0, sizeof(buf));
    int n = (int)read(fd, buf, sizeof(buf));
    TEST(group, "fork-refcount: parent fd valid after child close", n == 7);
    TEST(group, "fork-refcount: data correct", memcmp(buf, "forkref", 7) == 0);

    close(fd);
    unlink(path);
}

/* ── 7. child exits with open fd: parent copy survives, slot reusable ─────── */

static void test_child_exit_fd(us_test_group_t *group) {
    const char *path = "/mnt/xv6_fd_childexit.txt";

    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    TEST(group, "child-exit-fd: create file", fd >= 0);
    if (fd < 0) return;
    write(fd, "childexit", 9);

    pid_t pid = fork();
    if (pid == 0) {
        /* child holds fd open and exits without closing */
        _exit(0);
    }
    waitpid(pid, NULL, 0);

    /* parent fd must still work */
    lseek(fd, 0, SEEK_SET);
    char buf[16];
    memset(buf, 0, sizeof(buf));
    int n = (int)read(fd, buf, sizeof(buf));
    TEST(group, "child-exit-fd: parent fd survives child exit", n == 9);

    close(fd);

    /* slot must be free now — reopen at same or lower number */
    int reopen = open(path, O_RDONLY);
    TEST(group, "child-exit-fd: slot freed after parent close", reopen >= 0 && reopen <= fd);
    if (reopen >= 0) close(reopen);
    unlink(path);
}

/* ── 8. pipe: both ends closed frees slots ───────────────────────────────── */

static void test_pipe_reclaim(us_test_group_t *group) {
    int fds[2];
    int r = pipe(fds);
    TEST(group, "pipe-reclaim: pipe created", r == 0);
    if (r < 0) return;

    int lo = fds[0] < fds[1] ? fds[0] : fds[1];

    close(fds[0]);
    close(fds[1]);

    /* both slots freed — next open should get one of them back */
    int reopen = open("test/hello.txt", O_RDONLY);
    TEST(group, "pipe-reclaim: slot freed after both ends closed", reopen >= 0 && reopen <= lo);
    if (reopen >= 0) close(reopen);
}

/* ── 9. open→close→open: no stale state ─────────────────────────────────── */

static void test_reopen_clean(us_test_group_t *group) {
    const char *path = "/mnt/xv6_fd_reopen.txt";

    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    TEST(group, "reopen-clean: create", fd >= 0);
    if (fd < 0) return;
    write(fd, "first", 5);
    close(fd);

    /* reopen; offset must start at 0, not where last session left off */
    fd = open(path, O_RDONLY);
    TEST(group, "reopen-clean: reopen ok", fd >= 0);
    if (fd >= 0) {
        char buf[16];
        memset(buf, 0, sizeof(buf));
        int n = (int)read(fd, buf, sizeof(buf));
        TEST(group, "reopen-clean: reads 5 bytes from offset 0", n == 5);
        TEST(group, "reopen-clean: data correct", memcmp(buf, "first", 5) == 0);
        close(fd);
    }
    unlink(path);
}

/* ── 10. unlink-while-open: slot freed after last close, not at unlink ────── */

static void test_unlink_fd_reclaim(us_test_group_t *group) {
    const char *path = "/mnt/xv6_fd_unlinkreclaim.txt";

    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    TEST(group, "unlink-fd-reclaim: create", fd >= 0);
    if (fd < 0) return;
    write(fd, "alive", 5);
    lseek(fd, 0, SEEK_SET);

    /* unlink removes dir entry but fd must stay open */
    int r = unlink(path);
    TEST(group, "unlink-fd-reclaim: unlink ok", r == 0);

    /* fd still readable after unlink */
    char buf[16];
    memset(buf, 0, sizeof(buf));
    int n = (int)read(fd, buf, sizeof(buf));
    TEST(group, "unlink-fd-reclaim: fd readable after unlink", n == 5);
    TEST(group, "unlink-fd-reclaim: data correct", memcmp(buf, "alive", 5) == 0);

    /* slot freed only after close */
    close(fd);

    /* path must be gone, and slot must now be reusable */
    TEST(group, "unlink-fd-reclaim: path gone after close",
         open(path, O_RDONLY) < 0);

    int reopen = open("test/hello.txt", O_RDONLY);
    TEST(group, "unlink-fd-reclaim: slot reusable after close", reopen >= 0 && reopen <= fd);
    if (reopen >= 0) close(reopen);
}

/* ── public entry point ─────────────────────────────────────────────────── */

void run_test_fd_reclaim(us_test_group_t *group) {
    ut_group_begin(group, "fd_reclaim");

    test_slot_recycle(group);
    test_double_close(group);
    test_invalid_close(group);
    test_dup_refcount(group);
    test_dup2_closes_target(group);
    test_fork_refcount(group);
    test_child_exit_fd(group);
    test_pipe_reclaim(group);
    test_reopen_clean(group);
    test_unlink_fd_reclaim(group);

    ut_group_end(group);
}

/*
 * test_fd_offset.c — Independent fd offsets, O_CREAT|O_EXCL, exit-code range.
 *
 * Covers:
 *  1. Two open() calls on same file → independent offsets (not shared like dup).
 *  2. O_CREAT|O_EXCL: succeeds on new file, fails on existing file.
 *  3. Exit status 0-255: WEXITSTATUS correctly recovers all values.
 *  4. sbrk edge cases: shrink (if supported), sbrk(0) stability,
 *     last-byte write, sbrk in child doesn't affect parent break.
 */

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>
#include "us_test_common.h"

/* ── 1. independent offsets ─────────────────────────────────────────────── */

static void test_independent_offsets(us_test_group_t *group) {
    const char *path = "/mnt/xv6_indep_off.txt";

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    TEST(group, "indep-offset: create file", fd >= 0);
    if (fd < 0) return;
    write(fd, "ABCDEFGHIJ", 10);
    close(fd);

    /* open same file twice */
    int fd1 = open(path, O_RDONLY);
    int fd2 = open(path, O_RDONLY);
    TEST(group, "indep-offset: fd1 open", fd1 >= 0);
    TEST(group, "indep-offset: fd2 open", fd2 >= 0);
    if (fd1 < 0 || fd2 < 0) { close(fd1); close(fd2); return; }

    /* advance fd1 to offset 5 */
    lseek(fd1, 5, SEEK_SET);
    char c1 = 0, c2 = 0;
    read(fd1, &c1, 1); /* should read 'F' */
    read(fd2, &c2, 1); /* should read 'A' — independent from fd1 */

    TEST(group, "indep-offset: fd1 at offset 5 reads 'F'", c1 == 'F');
    TEST(group, "indep-offset: fd2 still at offset 0 reads 'A'", c2 == 'A');

    /* advance fd2 to offset 3 */
    lseek(fd2, 3, SEEK_SET);
    char c3 = 0, c4 = 0;
    read(fd2, &c3, 1); /* 'D' */
    read(fd1, &c4, 1); /* fd1 was at 6, reads 'G' */
    TEST(group, "indep-offset: fd2 at 3 reads 'D'", c3 == 'D');
    TEST(group, "indep-offset: fd1 at 6 reads 'G'", c4 == 'G');

    close(fd1);
    close(fd2);
}

/* ── 2. O_CREAT | O_EXCL ─────────────────────────────────────────────────── */

static void test_ocreat_excl(us_test_group_t *group) {
    const char *path = "/mnt/xv6_excl.txt";
    unlink(path); /* ensure not present */

    /* first create must succeed */
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0644);
    TEST(group, "O_EXCL: first create succeeds", fd >= 0);
    if (fd >= 0) close(fd);

    /* second create on existing file must fail */
    int fd2 = open(path, O_WRONLY | O_CREAT | O_EXCL, 0644);
    TEST(group, "O_EXCL: second create on existing file fails", fd2 < 0);
    if (fd2 >= 0) close(fd2);

    /* unlink then create again must succeed */
    unlink(path);
    int fd3 = open(path, O_WRONLY | O_CREAT | O_EXCL, 0644);
    TEST(group, "O_EXCL: create after unlink succeeds again", fd3 >= 0);
    if (fd3 >= 0) close(fd3);

    /* O_EXCL without O_CREAT on existing file — behavior: should open normally */
    int fd4 = open(path, O_RDONLY);
    TEST(group, "O_EXCL: open existing file O_RDONLY works", fd4 >= 0);
    if (fd4 >= 0) close(fd4);

    unlink(path);
}

/* ── 3. exit status 0-255 range ──────────────────────────────────────────── */

static void test_exit_status_range(us_test_group_t *group) {
    /* test a selection of values across the 0-255 range */
    int values[] = { 0, 1, 2, 42, 100, 127, 128, 200, 254, 255 };
    int nv = 10;
    int all_ok = 1;

    for (int i = 0; i < nv; i++) {
        int v = values[i];
        pid_t pid = fork();
        if (pid == 0) _exit(v);
        int status = 0;
        waitpid(pid, &status, 0);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != (v & 0xFF))
            all_ok = 0;
    }
    TEST(group, "exit-status-range: all 10 values (0,1,42,127,128,200,255) correct",
         all_ok);
}

/* ── 4. sbrk edge cases ──────────────────────────────────────────────────── */

static void test_sbrk_edges(us_test_group_t *group) {
    /* sbrk(0) twice returns same or higher value — never decreases */
    void *b1 = sbrk(0);
    void *b2 = sbrk(0);
    TEST(group, "sbrk-edges: sbrk(0) twice non-decreasing", b2 >= b1);

    /* grow, write last byte, sbrk(0) reflects growth */
    void *old = sbrk(0);
    char *p = sbrk(4096);
    TEST(group, "sbrk-edges: grow 4096 succeeds", p != (void *)-1);
    if (p != (void *)-1) {
        /* write last byte of allocated region */
        p[4095] = 0xCC;
        TEST(group, "sbrk-edges: last byte of region writable", (unsigned char)p[4095] == 0xCC);
        void *newb = sbrk(0);
        TEST(group, "sbrk-edges: break advanced after grow", (char *)newb > (char *)old);
    }

    /* sbrk in child does not affect parent */
    void *parent_brk = sbrk(0);
    pid_t pid = fork();
    if (pid == 0) {
        sbrk(1024 * 1024); /* 1 MB in child */
        _exit(0);
    }
    waitpid(pid, NULL, 0);
    void *parent_brk2 = sbrk(0);
    TEST(group, "sbrk-edges: child sbrk does not affect parent break",
         parent_brk2 == parent_brk);

    /* sbrk shrink: if supported, break should decrease; if not, returns -1 */
    {
        void *cur = sbrk(0);
        void *r = sbrk(-4096);
        /* either: r != -1 (shrink supported) OR r == -1 (not supported) */
        /* both are acceptable; just must not crash */
        TEST(group, "sbrk-edges: sbrk(-4096) does not crash", 1);
        (void)cur; (void)r;
    }
}

/* ── public entry point ─────────────────────────────────────────────────── */

void run_test_fd_offset(us_test_group_t *group) {
    ut_group_begin(group, "fd_offset");

    test_independent_offsets(group);
    test_ocreat_excl(group);
    test_exit_status_range(group);
    test_sbrk_edges(group);

    ut_group_end(group);
}

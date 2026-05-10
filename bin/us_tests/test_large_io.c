/*
 * test_large_io.c — Large read/write correctness tests (P1/P2 coverage).
 *
 * Before P1/P2, sys_read and sys_write called kmalloc(count) to allocate a
 * contiguous kernel bounce buffer equal to the full request size.  For counts
 * larger than the kernel heap's initial 64 KB region, kmalloc could fail with
 * ENOMEM, causing the syscall to return an error instead of transferring data.
 *
 * After P1/P2 each syscall uses a stack-local 256-byte bounce buffer and loops
 * in chunks, so arbitrarily large requests succeed regardless of kernel-heap
 * fragmentation and without overflowing the 4 KB kernel stack.
 *
 * Tests:
 *   A. Write 128 KB to a file in a single write() call; read it back in a
 *      single read() call; verify every byte.
 *   B. Write 8 KB (many chunks) and read it back; verifies multi-chunk looping.
 *   C. Partial read: file contains N bytes; read(fd, buf, N+4096) must return
 *      exactly N (not error, not garbage beyond N).
 *   D. Large write to stdout (fd 1) — must not hang or return ENOMEM.
 */

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>
#include "us_test_common.h"
#include "stdlib.h"

#define PAGE        4096
#define LARGE_SIZE  (32 * PAGE)   /* 128 KB — well above 64-KB heap limit  */
#define TWO_PAGE    (2 * PAGE)    /*   8 KB — exercises multi-chunk looping */

static const char *TEST_PATH = "/mnt/large_io_test.bin";

/* Fill buf[0..len) with a deterministic byte pattern. */
static void fill_pattern(char *buf, int len) {
    for (int i = 0; i < len; i++)
        buf[i] = (char)((i * 37 + 13) & 0xFF);
}

/* Return 1 if buf[0..len) matches the deterministic pattern, 0 otherwise. */
static int check_pattern(const char *buf, int len) {
    for (int i = 0; i < len; i++)
        if (buf[i] != (char)((i * 37 + 13) & 0xFF)) return 0;
    return 1;
}

/* ── A. 128 KB write then read ──────────────────────────────────────────── */
static void test_write_large_read_large(us_test_group_t *group) {
    pid_t pid = fork();
    if (pid == 0) {
        char *wbuf = malloc(LARGE_SIZE);
        char *rbuf = malloc(LARGE_SIZE);
        if (!wbuf || !rbuf) _exit(10);

        fill_pattern(wbuf, LARGE_SIZE);

        int fd = open(TEST_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) _exit(1);
        int w = (int)write(fd, wbuf, LARGE_SIZE);
        close(fd);
        if (w != LARGE_SIZE) _exit(2);

        fd = open(TEST_PATH, O_RDONLY, 0);
        if (fd < 0) _exit(3);
        int r = (int)read(fd, rbuf, LARGE_SIZE);
        close(fd);
        if (r != LARGE_SIZE) _exit(4);

        if (!check_pattern(rbuf, LARGE_SIZE)) _exit(5);
        unlink(TEST_PATH);
        _exit(0);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    TEST(group, "large-io/A: 128 KB write+read round-trips correctly",
         WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

/* ── B. 8 KB write then read (two-chunk boundary) ───────────────────────── */
static void test_write_two_pages(us_test_group_t *group) {
    pid_t pid = fork();
    if (pid == 0) {
        char wbuf[TWO_PAGE], rbuf[TWO_PAGE];
        fill_pattern(wbuf, TWO_PAGE);

        int fd = open(TEST_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) _exit(1);
        int w = (int)write(fd, wbuf, TWO_PAGE);
        close(fd);
        if (w != TWO_PAGE) _exit(2);

        fd = open(TEST_PATH, O_RDONLY, 0);
        if (fd < 0) _exit(3);
        int r = (int)read(fd, rbuf, TWO_PAGE);
        close(fd);
        if (r != TWO_PAGE) _exit(4);

        if (!check_pattern(rbuf, TWO_PAGE)) _exit(5);
        unlink(TEST_PATH);
        _exit(0);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    TEST(group, "large-io/B: 8 KB write+read (multi-chunk) correct",
         WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

/* ── C. Partial read: request larger than file ──────────────────────────── */
static void test_read_past_eof(us_test_group_t *group) {
    pid_t pid = fork();
    if (pid == 0) {
        int file_sz = PAGE + 100;   /* slightly over one page */
        char wbuf[PAGE + 100];
        fill_pattern(wbuf, file_sz);

        int fd = open(TEST_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) _exit(1);
        int w = (int)write(fd, wbuf, file_sz);
        close(fd);
        if (w != file_sz) _exit(2);

        char rbuf[PAGE + 100 + PAGE];   /* larger than the file */
        fd = open(TEST_PATH, O_RDONLY, 0);
        if (fd < 0) _exit(3);
        int r = (int)read(fd, rbuf, file_sz + PAGE);
        close(fd);
        /* Must return exactly file_sz bytes (short-read at EOF), not an error. */
        if (r != file_sz) _exit(4);
        if (!check_pattern(rbuf, file_sz)) _exit(5);
        unlink(TEST_PATH);
        _exit(0);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    TEST(group, "large-io/C: read past EOF returns exact file size",
         WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

/* ── D. Large write to stdout — must not return ENOMEM ─────────────────── */
static void test_write_stdout_large(us_test_group_t *group) {
    pid_t pid = fork();
    if (pid == 0) {
        /* Write 16 KB to stdout in one call — stdout is term_write, not a pipe,
         * so it does not block.  The old code would kmalloc(16384) and fail if
         * the kernel heap was exhausted; P2 uses a PGSIZE stack buffer. */
        char buf[4 * PAGE];
        memset(buf, 'X', sizeof(buf));
        buf[sizeof(buf) - 1] = '\n';
        int w = (int)write(1, buf, sizeof(buf));
        _exit(w == (int)sizeof(buf) ? 0 : 1);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    TEST(group, "large-io/D: 16 KB write to stdout succeeds (no ENOMEM)",
         WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

void run_test_large_io(us_test_group_t *group) {
    ut_group_begin(group, "large_io");

    test_write_two_pages(group);
    test_write_large_read_large(group);
    test_read_past_eof(group);
    test_write_stdout_large(group);

    ut_group_end(group);
}

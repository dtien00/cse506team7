/*
 * test_mmap_file.c — File-backed mmap correctness tests.
 *
 * Tests the demand-paging path for VMA_TYPE_FILE mappings:
 *  A. File content is visible through the mapping (ops.read on fault)
 *  B. close(fd) after mmap does not invalidate the mapping (VMA holds ref)
 *  C. MAP_PRIVATE fork: child COW write does not affect parent's view
 *  D. Non-page-aligned offset returns MAP_FAILED
 *  E. Invalid fd returns MAP_FAILED
 *  F. Large file: mmap content matches read() reference byte-for-byte
 *  G. Page beyond EOF is zero-padded (no SIGBUS — intentional kernel gap)
 *
 * Tests B, C, F, G run in child processes to isolate faults and
 * memory pressure from the main test runner.
 */

#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "us_test_common.h"

#define MMAP_TEST_FILE "/hello.txt"

/* ── A. File content visible through mapping ─────────────────────────────── */

static void test_basic_read(us_test_group_t *group) {
    int fd = open(MMAP_TEST_FILE, O_RDONLY);
    if (fd < 0) {
        SKIP(group, "mmap-file/basic-read: file content visible", "no " MMAP_TEST_FILE);
        return;
    }

    char ref[16];
    memset(ref, 0, sizeof(ref));
    read(fd, ref, sizeof(ref));
    lseek(fd, 0, SEEK_SET);

    void *m = mmap(NULL, 4096, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);

    TEST(group, "mmap-file/basic-read: mmap succeeds", m != MAP_FAILED);
    if (m == MAP_FAILED)
        return;

    TEST(group, "mmap-file/basic-read: first 16 bytes match read()",
         memcmp(m, ref, sizeof(ref)) == 0);

    munmap(m, 4096);
}

/* ── B. close(fd) after mmap — mapping stays valid ──────────────────────── */

static void test_close_after_mmap(us_test_group_t *group) {
    int fd = open(MMAP_TEST_FILE, O_RDONLY);
    if (fd < 0) {
        SKIP(group, "mmap-file/close-after-mmap: mapping survives close(fd)",
             "no " MMAP_TEST_FILE);
        return;
    }

    void *m = mmap(NULL, 4096, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);

    if (m == MAP_FAILED) {
        TEST(group, "mmap-file/close-after-mmap: mapping survives close(fd)", 0);
        return;
    }

    /* Run in a child so a fault here does not kill the test runner. */
    pid_t pid = fork();
    TEST(group, "mmap-file/close-after-mmap: fork succeeded", pid >= 0);
    if (pid == 0) {
        volatile char c = ((char *)m)[0];
        (void)c;
        _exit(0);
    }
    if (pid > 0) {
        int status = 0;
        waitpid(pid, &status, 0);
        TEST(group, "mmap-file/close-after-mmap: mapping survives close(fd)",
             WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }

    munmap(m, 4096);
}

/* ── C. MAP_PRIVATE fork: child COW write isolated from parent ───────────── */

static void test_map_private_fork(us_test_group_t *group) {
    int fd = open(MMAP_TEST_FILE, O_RDONLY);
    if (fd < 0) {
        SKIP(group, "mmap-file/private-fork: child COW write isolated from parent",
             "no " MMAP_TEST_FILE);
        SKIP(group, "mmap-file/private-fork: parent mapping unchanged after child write",
             "no " MMAP_TEST_FILE);
        return;
    }

    void *m = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    close(fd);

    if (m == MAP_FAILED) {
        TEST(group, "mmap-file/private-fork: child COW write isolated from parent", 0);
        TEST(group, "mmap-file/private-fork: parent mapping unchanged after child write", 0);
        return;
    }

    char original = ((char *)m)[0];

    pid_t pid = fork();
    TEST(group, "mmap-file/private-fork: fork succeeded", pid >= 0);
    if (pid == 0) {
        /* Child writes to its private COW copy. */
        ((char *)m)[0] = (char)0xFF;
        _exit(((char *)m)[0] == (char)0xFF ? 0 : 1);
    }

    int status = 0;
    waitpid(pid, &status, 0);
    TEST(group, "mmap-file/private-fork: child COW write isolated from parent",
         WIFEXITED(status) && WEXITSTATUS(status) == 0);
    TEST(group, "mmap-file/private-fork: parent mapping unchanged after child write",
         ((char *)m)[0] == original);

    munmap(m, 4096);
}

/* ── D. Non-page-aligned offset → MAP_FAILED ─────────────────────────────── */

static void test_unaligned_offset(us_test_group_t *group) {
    int fd = open(MMAP_TEST_FILE, O_RDONLY);
    if (fd < 0) {
        SKIP(group, "mmap-file/unaligned-offset: unaligned offset returns MAP_FAILED",
             "no " MMAP_TEST_FILE);
        return;
    }

    void *m = mmap(NULL, 4096, PROT_READ, MAP_PRIVATE, fd, 1);
    close(fd);

    TEST(group, "mmap-file/unaligned-offset: unaligned offset returns MAP_FAILED",
         m == MAP_FAILED);
    if (m != MAP_FAILED)
        munmap(m, 4096);
}

/* ── E. Invalid fd → MAP_FAILED ──────────────────────────────────────────── */

static void test_invalid_fd(us_test_group_t *group) {
    void *m = mmap(NULL, 4096, PROT_READ, MAP_PRIVATE, 99, 0);
    TEST(group, "mmap-file/invalid-fd: invalid fd returns MAP_FAILED", m == MAP_FAILED);
    if (m != MAP_FAILED)
        munmap(m, 4096);
}

/* ── F. Large file: mmap content matches read() reference ────────────────── */

static void test_large_file_mmap(us_test_group_t *group) {
    int fd = open(MMAP_TEST_FILE, O_RDONLY);
    if (fd < 0) {
        SKIP(group, "mmap-file/large: content matches read() reference",
             "no " MMAP_TEST_FILE);
        return;
    }

    struct stat st;
    fstat(fd, &st);
    if (st.st_size < 8192) {
        close(fd);
        SKIP(group, "mmap-file/large: content matches read() reference",
             MMAP_TEST_FILE " < 8 KB");
        return;
    }

    size_t len = (size_t)st.st_size;

    pid_t pid = fork();
    TEST(group, "mmap-file/large: fork succeeded", pid >= 0);
    if (pid == 0) {
        void *m = mmap(NULL, len, PROT_READ, MAP_PRIVATE, fd, 0);
        close(fd);
        if (m == MAP_FAILED)
            _exit(1);

        int fd2 = open(MMAP_TEST_FILE, O_RDONLY);
        if (fd2 < 0) { munmap(m, len); _exit(2); }

        char *ref = malloc(len);
        if (!ref) { munmap(m, len); close(fd2); _exit(3); }

        if ((size_t)read(fd2, ref, len) != len) {
            free(ref); munmap(m, len); close(fd2); _exit(4);
        }
        close(fd2);

        int ok = (memcmp(m, ref, len) == 0);
        free(ref);
        munmap(m, len);
        _exit(ok ? 0 : 5);
    }

    close(fd);
    int status = 0;
    waitpid(pid, &status, 0);
    TEST(group, "mmap-file/large: content matches read() reference",
         WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

/* ── G. Page beyond EOF is zero-padded (no SIGBUS) ───────────────────────── */

static void test_past_eof_zero(us_test_group_t *group) {
    int fd = open(MMAP_TEST_FILE, O_RDONLY);
    if (fd < 0) {
        SKIP(group, "mmap-file/past-eof: access past EOF returns zero-padded page",
             "no " MMAP_TEST_FILE);
        return;
    }

    /* Map 2 pages regardless of file size; second page is past EOF for
     * any file smaller than 8 KB.  Access must not kill the process. */
    void *m = mmap(NULL, 2 * 4096, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);

    if (m == MAP_FAILED) {
        TEST(group, "mmap-file/past-eof: access past EOF returns zero-padded page", 0);
        return;
    }

    pid_t pid = fork();
    TEST(group, "mmap-file/past-eof: fork succeeded", pid >= 0);
    if (pid == 0) {
        volatile char c = ((char *)m)[4096];   /* second page — may be past EOF */
        _exit(c == 0 ? 0 : 1);
    }

    int status = 0;
    waitpid(pid, &status, 0);
    TEST(group, "mmap-file/past-eof: access past EOF returns zero-padded page",
         WIFEXITED(status) && WEXITSTATUS(status) == 0);

    munmap(m, 2 * 4096);
}

/* ── public entry point ──────────────────────────────────────────────────── */

void run_test_mmap_file(us_test_group_t *group) {
    ut_group_begin(group, "mmap_file");

    test_basic_read(group);
    test_close_after_mmap(group);
    test_map_private_fork(group);
    test_unaligned_offset(group);
    test_invalid_fd(group);
    test_large_file_mmap(group);
    test_past_eof_zero(group);

    ut_group_end(group);
}

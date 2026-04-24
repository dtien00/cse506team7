/*
 * test_bigfile.c — Large file I/O correctness (xv6 bigfile / bigwrite).
 *
 * Covers: writing > one block (512 B), reading back multi-block files,
 * lseek over large offsets, file size reported by fstat, write+read
 * at boundary of indirect blocks for ext2 (12 direct → single indirect).
 *
 * All files written to /mnt (ext2, persistent).
 */

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>
#include "us_test_common.h"

#define BLOCK    512
#define NBLOCKS  64        /* 32 KB — crosses direct+indirect boundary */
#define BIGSIZE  (BLOCK * NBLOCKS)

/* ── write pattern and verify ───────────────────────────────────────────── */

static void test_bigwrite(us_test_group_t *group) {
    const char *path = "/mnt/xv6_bigfile.dat";

    /* write BIGSIZE bytes, one block at a time, each block filled with its index */
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    TEST(group, "bigwrite: create file", fd >= 0);
    if (fd < 0) return;

    char buf[BLOCK];
    for (int i = 0; i < NBLOCKS; i++) {
        memset(buf, (char)(i & 0xFF), BLOCK);
        int w = (int)write(fd, buf, BLOCK);
        if (w != BLOCK) {
            close(fd);
            TEST(group, "bigwrite: all blocks written", 0);
            return;
        }
    }
    close(fd);
    TEST(group, "bigwrite: all blocks written", 1);

    /* verify via fstat */
    struct stat st;
    int sr = stat(path, &st);
    TEST(group, "bigwrite: stat succeeds", sr == 0);
    TEST(group, "bigwrite: file size matches written bytes",
         sr == 0 && st.st_size == BIGSIZE);

    /* read back and verify pattern */
    fd = open(path, O_RDONLY);
    TEST(group, "bigwrite: reopen for read", fd >= 0);
    if (fd < 0) return;

    int ok = 1;
    for (int i = 0; i < NBLOCKS && ok; i++) {
        memset(buf, 0, BLOCK);
        int total = 0;
        while (total < BLOCK) {
            int r = (int)read(fd, buf + total, (size_t)(BLOCK - total));
            if (r <= 0) { ok = 0; break; }
            total += r;
        }
        if (!ok) break;
        for (int j = 0; j < BLOCK; j++) {
            if ((unsigned char)buf[j] != (unsigned char)(i & 0xFF)) {
                ok = 0; break;
            }
        }
    }
    close(fd);
    TEST(group, "bigwrite: all blocks read back correctly", ok);
}

/* ── lseek over large file ──────────────────────────────────────────────── */

static void test_biglseek(us_test_group_t *group) {
    const char *path = "/mnt/xv6_bigfile.dat"; /* written by test_bigwrite */

    int fd = open(path, O_RDONLY);
    TEST(group, "biglseek: open large file", fd >= 0);
    if (fd < 0) return;

    /* seek to last block */
    off_t off = lseek(fd, BIGSIZE - BLOCK, SEEK_SET);
    TEST(group, "biglseek: SEEK_SET to last block", off == BIGSIZE - BLOCK);

    char buf[BLOCK];
    int n = 0;
    while (n < BLOCK) {
        int r = (int)read(fd, buf + n, (size_t)(BLOCK - n));
        if (r <= 0) break;
        n += r;
    }
    TEST(group, "biglseek: read last block (512 bytes)", n == BLOCK);

    int ok = 1;
    for (int i = 0; i < BLOCK; i++) {
        if ((unsigned char)buf[i] != (unsigned char)((NBLOCKS - 1) & 0xFF)) {
            ok = 0; break;
        }
    }
    TEST(group, "biglseek: last block pattern correct", ok);

    /* SEEK_END */
    off = lseek(fd, 0, SEEK_END);
    TEST(group, "biglseek: SEEK_END == file size", off == BIGSIZE);

    close(fd);
}

/* ── write to middle of file ─────────────────────────────────────────────── */

static void test_write_middle(us_test_group_t *group) {
    const char *path = "/mnt/xv6_middle.dat";
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    TEST(group, "write-middle: create", fd >= 0);
    if (fd < 0) return;

    /* write 2 KB of 'A' */
    char buf[2048];
    memset(buf, 'A', sizeof(buf));
    write(fd, buf, sizeof(buf));

    /* seek to middle and overwrite 512 bytes with 'B' */
    lseek(fd, 512, SEEK_SET);
    memset(buf, 'B', 512);
    write(fd, buf, 512);
    close(fd);

    /* verify */
    fd = open(path, O_RDONLY);
    TEST(group, "write-middle: reopen", fd >= 0);
    if (fd < 0) return;

    char rbuf[2048];
    memset(rbuf, 0, sizeof(rbuf));
    int n = 0;
    while (n < 2048) {
        int r = (int)read(fd, rbuf + n, (size_t)(2048 - n));
        if (r <= 0) break;
        n += r;
    }
    close(fd);
    TEST(group, "write-middle: total size 2048", n == 2048);

    int ok = 1;
    for (int i = 0; i < 512; i++) if (rbuf[i] != 'A') { ok = 0; break; }
    for (int i = 512; i < 1024 && ok; i++) if (rbuf[i] != 'B') { ok = 0; break; }
    for (int i = 1024; i < 2048 && ok; i++) if (rbuf[i] != 'A') { ok = 0; break; }
    TEST(group, "write-middle: correct A/B/A pattern", ok);
}

/* ── fstat returns correct metadata ─────────────────────────────────────── */

static void test_fstat(us_test_group_t *group) {
    int fd = open("/hello.txt", O_RDONLY);
    TEST(group, "fstat: open", fd >= 0);
    if (fd < 0) return;

    struct stat st;
    int r = fstat(fd, &st);
    TEST(group, "fstat: returns 0", r == 0);
    TEST(group, "fstat: is regular file", r == 0 && S_ISREG(st.st_mode));
    TEST(group, "fstat: size > 0", r == 0 && st.st_size > 0);
    close(fd);

    /* fstat on a directory */
    fd = open("/bin", O_RDONLY);
    TEST(group, "fstat-dir: open /bin", fd >= 0);
    if (fd >= 0) {
        struct stat dst;
        r = fstat(fd, &dst);
        TEST(group, "fstat-dir: returns 0", r == 0);
        TEST(group, "fstat-dir: is directory", r == 0 && S_ISDIR(dst.st_mode));
        close(fd);
    }

    /* fstat bad fd */
    r = fstat(-1, &st);
    TEST(group, "fstat: bad fd returns error", r < 0);
}

/* ── public entry point ─────────────────────────────────────────────────── */

void run_test_bigfile(us_test_group_t *group) {
    ut_group_begin(group, "bigfile");

    test_bigwrite(group);
    test_biglseek(group);
    test_write_middle(group);
    test_fstat(group);

    ut_group_end(group);
}

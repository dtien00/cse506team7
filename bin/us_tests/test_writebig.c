/*
 * test_writebig.c — Write until disk full; kernel must return error not crash.
 *
 * Strategy: write 4 KB blocks to /mnt until write() returns an error.
 * Verify:
 *  1. We could write at least some data (disk not already full).
 *  2. write() eventually returns <= 0 (disk full error, not infinite hang).
 *  3. After the error, close + open + read of a small file still works
 *     (FS not corrupted by the OOD condition).
 *  4. After unlinking the big file, new files can be created (space reclaimed).
 *
 * All done in a child so the test runner's state is clean if the child crashes.
 */

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>
#include "us_test_common.h"

#define BLOCK_SIZE 4096
#define MAX_BLOCKS 65536  /* 256 MB cap — stop even if disk never fills */

static void test_fill_disk(us_test_group_t *group) {
    const char *bigpath  = "/mnt/xv6_writebig.dat";
    const char *canary   = "/mnt/xv6_writebig_canary.txt";

    /* write canary file before filling disk */
    {
        int cfd = open(canary, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        TEST(group, "writebig: canary file created before fill", cfd >= 0);
        if (cfd >= 0) {
            write(cfd, "canary", 6);
            close(cfd);
        }
    }

    pid_t pid = fork();
    if (pid == 0) {
        /* child fills the disk */
        int fd = open(bigpath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) _exit(1); /* can't even create — already full? */

        char buf[BLOCK_SIZE];
        memset(buf, 0xAB, sizeof(buf));

        long blocks = 0;
        int got_error = 0;
        while (blocks < MAX_BLOCKS) {
            int w = (int)write(fd, buf, sizeof(buf));
            if (w <= 0) { got_error = 1; break; }
            blocks++;
        }
        close(fd);

        /* exit codes:
         *   0 = wrote ≥1 block AND got disk-full error
         *   1 = couldn't create file
         *   2 = hit MAX_BLOCKS cap without disk-full (disk very large; OK)
         *   3 = wrote 0 blocks before error (disk already full)
         */
        if (!got_error)        _exit(2); /* cap hit — treat as pass */
        if (blocks == 0)       _exit(3); /* disk was already full */
        _exit(0);
    }

    int status = 0;
    waitpid(pid, &status, 0);
    int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

    TEST(group, "writebig: child exited cleanly (no crash)", WIFEXITED(status));
    /* codes 0 (disk full after writing) and 2 (huge disk, hit cap) are passing */
    TEST(group, "writebig: write() returned error or hit cap (no hang)",
         code == 0 || code == 2);

    /* FS still sane: canary readable */
    {
        int cfd = open(canary, O_RDONLY);
        TEST(group, "writebig: canary readable after disk-full", cfd >= 0);
        if (cfd >= 0) {
            char buf[16];
            memset(buf, 0, sizeof(buf));
            int n = (int)read(cfd, buf, sizeof(buf));
            close(cfd);
            TEST(group, "writebig: canary data intact", n == 6 && memcmp(buf, "canary", 6) == 0);
        }
    }

    /* free space: unlink big file */
    unlink(bigpath);

    /* after unlink, new file must be creatable */
    {
        int nfd = open("/mnt/xv6_writebig_after.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
        TEST(group, "writebig: new file creatable after freeing big file", nfd >= 0);
        if (nfd >= 0) {
            write(nfd, "ok", 2);
            close(nfd);
        }
        unlink("/mnt/xv6_writebig_after.txt");
    }

    /* cleanup */
    unlink(canary);
}

/* ── public entry point ─────────────────────────────────────────────────── */

void run_test_writebig(us_test_group_t *group) {
    ut_group_begin(group, "writebig");
    test_fill_disk(group);
    ut_group_end(group);
}

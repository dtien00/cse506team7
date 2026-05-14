/*
 * test_tarfs_symlink.c — verify symlinks pre-installed in the tarfs root image
 *
 * rootfs/test/hello_link.txt -> hello.txt
 * These entries are baked into the USTAR archive at build time.  The tests
 * exercise lstat (S_IFLNK), readlink, and transparent open/stat through the
 * link so that the archive loader's symlink support can be validated end-to-end.
 */

// scp -r 506:~/jchlee/build ./build

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "us_test_common.h"

#define LINK_PATH   "/test/hello_link.txt"
#define TARGET_NAME "hello.txt"
#define TARGET_PATH "/test/hello.txt"

void run_test_tarfs_symlink(us_test_group_t *group) {
    ut_group_begin(group, "tarfs.symlink");

    /* lstat must report S_IFLNK — not follow the link */
    {
        struct stat lst;
        int r = lstat(LINK_PATH, &lst);
        TEST(group, "tarfs symlink: lstat returns 0", r == 0);
        TEST(group, "tarfs symlink: lstat reports S_IFLNK",
             r == 0 && S_ISLNK(lst.st_mode));
    }

    /* readlink must return the stored target string */
    {
        char buf[256];
        ssize_t n = readlink(LINK_PATH, buf, sizeof(buf) - 1);
        TEST(group, "tarfs symlink: readlink returns positive length", n > 0);
        if (n > 0) {
            buf[n] = '\0';
            TEST(group, "tarfs symlink: readlink returns correct target",
                 strcmp(buf, TARGET_NAME) == 0);
        }
    }

    /* stat must follow the link and report a regular file */
    {
        struct stat st;
        int r = stat(LINK_PATH, &st);
        TEST(group, "tarfs symlink: stat follows link, returns 0", r == 0);
        TEST(group, "tarfs symlink: stat reports regular file",
             r == 0 && S_ISREG(st.st_mode));
    }

    /* open through the symlink must succeed and read the target's content */
    {
        char buf[256];
        int fd = open(LINK_PATH, O_RDONLY);
        TEST(group, "tarfs symlink: open through symlink succeeds", fd >= 0);
        if (fd >= 0) {
            ssize_t n = read(fd, buf, sizeof(buf) - 1);
            close(fd);
            TEST(group, "tarfs symlink: read through symlink returns data", n > 0);
            /* content must match what reading the target directly returns */
            if (n > 0) {
                buf[n] = '\0';
                char tbuf[256];
                int tfd = open(TARGET_PATH, O_RDONLY);
                if (tfd >= 0) {
                    ssize_t tn = read(tfd, tbuf, sizeof(tbuf) - 1);
                    close(tfd);
                    if (tn > 0) tbuf[tn] = '\0';
                    TEST(group, "tarfs symlink: content matches target",
                         tn > 0 && strcmp(buf, tbuf) == 0);
                }
            }
        }
    }

    /* readlink on the target (regular file) must fail */
    {
        char buf[64];
        TEST(group, "tarfs symlink: readlink on regular file fails",
             readlink(TARGET_PATH, buf, sizeof(buf)) < 0);
    }

    ut_group_end(group);
}

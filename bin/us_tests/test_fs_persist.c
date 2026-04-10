#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string.h>
#include "us_test_common.h"

/* Helpers */
static int write_file(const char *path, const char *data) {
    int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC);
    if (fd < 0) return -1;
    ssize_t n = write(fd, data, strlen(data));
    close(fd);
    return (n == (ssize_t)strlen(data)) ? 0 : -1;
}

static int read_file(const char *path, char *buf, int bufsz) {
    int fd = open(path, O_CREAT | O_RDWR);
    if (fd < 0) return -1;
    int n = (int)read(fd, buf, bufsz - 1);
    close(fd);
    if (n < 0) return -1;
    buf[n] = '\0';
    return n;
}

/*
 * flush_and_read: sync all dirty blocks to disk, then re-open the file for
 * reading.  This is the closest we can get to a cold-cache read without an
 * actual reboot: the cache is clean after sync(), so any subsequent read
 * must go back through the block layer rather than returning stale in-memory
 * data.
 */
static int flush_and_read(const char *path, char *buf, int bufsz) {
    sync();
    return read_file(path, buf, bufsz);
}

/* ------------------------------------------------------------------ */
/* Group 1: basic create / write / read                                */
/* ------------------------------------------------------------------ */
static void test_basic_rw(us_test_group_t *group) {
    char buf[128];

    ut_group_begin(group, "fs_persist.basic_rw");

    /* create and write */
    int fd = open("/mnt/basic.txt", O_CREAT | O_WRONLY | O_TRUNC);
    TEST(group, "open for create returns valid fd", fd >= 0);
    if (fd < 0) { ut_group_end(group); return; }

    const char *msg = "hello persistent\n";
    ssize_t written = write(fd, msg, strlen(msg));
    TEST(group, "write returns byte count", written == (ssize_t)strlen(msg));
    TEST(group, "close after write succeeds", close(fd) == 0);

    /* flush to disk, then re-open — verifies data survives a cache flush */
    int n = flush_and_read("/mnt/basic.txt", buf, sizeof(buf));
    TEST(group, "read after flush returns positive bytes", n > 0);
    TEST(group, "read after flush content matches written", strcmp(buf, msg) == 0);

    ut_group_end(group);
}

/* ------------------------------------------------------------------ */
/* Group 2: open flags — O_TRUNC, O_APPEND                             */
/* ------------------------------------------------------------------ */
static void test_open_flags(us_test_group_t *group) {
    char buf[128];

    ut_group_begin(group, "fs_persist.open_flags");

    /* seed file */
    write_file("/mnt/flags.txt", "original\n");

    /* O_TRUNC should discard old content */
    int fd = open("/mnt/flags.txt", O_WRONLY | O_TRUNC, 0644);
    TEST(group, "O_TRUNC open succeeds", fd >= 0);
    if (fd >= 0) {
        write(fd, "new\n", 4);
        close(fd);
        int n = flush_and_read("/mnt/flags.txt", buf, sizeof(buf));
        TEST(group, "O_TRUNC discards old content after flush", strcmp(buf, "new\n") == 0);
        (void)n;
    }

    /* O_APPEND should add to end */
    fd = open("/mnt/flags.txt", O_WRONLY | O_APPEND, 0644);
    TEST(group, "O_APPEND open succeeds", fd >= 0);
    if (fd >= 0) {
        write(fd, "appended\n", 9);
        close(fd);
        int n = flush_and_read("/mnt/flags.txt", buf, sizeof(buf));
        TEST(group, "O_APPEND extends file after flush", n > 4);
        TEST(group, "O_APPEND content correct after flush", strcmp(buf, "new\nappended\n") == 0);
        (void)n;
    }

    ut_group_end(group);
}

/* ------------------------------------------------------------------ */
/* Group 3: lseek — SEEK_SET, SEEK_CUR, SEEK_END                       */
/* ------------------------------------------------------------------ */
static void test_lseek(us_test_group_t *group) {
    char buf[64];

    ut_group_begin(group, "fs_persist.lseek");

    write_file("/mnt/seek.txt", "0123456789");

    int fd = open("/mnt/seek.txt", O_RDWR);
    TEST(group, "open seek file succeeds", fd >= 0);
    if (fd < 0) { ut_group_end(group); return; }

    /* SEEK_SET */
    off_t pos = lseek(fd, 3, SEEK_SET);
    TEST(group, "lseek SEEK_SET returns new offset", pos == 3);
    int n = (int)read(fd, buf, 3);
    buf[n] = '\0';
    TEST(group, "read after SEEK_SET correct", strcmp(buf, "345") == 0);

    /* SEEK_CUR — already at offset 6 after reading 3 bytes */
    pos = lseek(fd, -2, SEEK_CUR);
    TEST(group, "lseek SEEK_CUR returns new offset", pos == 4);
    n = (int)read(fd, buf, 2);
    buf[n] = '\0';
    TEST(group, "read after SEEK_CUR correct", strcmp(buf, "45") == 0);

    /* SEEK_END */
    pos = lseek(fd, 0, SEEK_END);
    TEST(group, "lseek SEEK_END returns file size", pos == 10);

    /* write past old content */
    write(fd, "!", 1);
    pos = lseek(fd, 0, SEEK_END);
    TEST(group, "file grew after write at end", pos == 11);
    close(fd);

    /* flush and verify the appended byte survived */
    sync();
    fd = open("/mnt/seek.txt", O_RDONLY);
    TEST(group, "re-open after flush succeeds", fd >= 0);
    if (fd >= 0) {
        pos = lseek(fd, 0, SEEK_END);
        TEST(group, "file size correct after flush", pos == 11);
        lseek(fd, 10, SEEK_SET);
        int n = (int)read(fd, buf, 1);
        buf[n] = '\0';
        TEST(group, "appended byte correct after flush", strcmp(buf, "!") == 0);
        close(fd);
    }

    ut_group_end(group);
}

/* ------------------------------------------------------------------ */
/* Group 4: directories — mkdir, create in subdir                      */
/* ------------------------------------------------------------------ */
static void test_directories(us_test_group_t *group) {
    ut_group_begin(group, "fs_persist.directories");

    /* mkdir */
    int r = mkdir("/mnt/subdir", 0755);
    TEST(group, "mkdir returns 0", r == 0);

    /* open missing dir should fail */
    int fd = open("/mnt/no_such_dir/file.txt", O_CREAT | O_WRONLY, 0644);
    TEST(group, "open in missing dir fails", fd < 0);
    if (fd >= 0) close(fd);

    // /* create file in new subdir */
    // fd = open("/mnt/subdir/nested.txt", O_CREAT | O_WRONLY, 0644);
    // TEST(group, "create file in new subdir succeeds", fd >= 0);
    // if (fd >= 0) {
    //     write(fd, "nested\n", 7);
    //     close(fd);
    // }

    // /* flush and re-read nested file — checks inode + data blocks hit disk */
    // char buf[64];
    // int n = flush_and_read("/mnt/subdir/nested.txt", buf, sizeof(buf));
    // TEST(group, "nested file readable after flush", n > 0);
    // TEST(group, "nested file content correct after flush", strcmp(buf, "nested\n") == 0);

    // /* nested mkdir */
    // r = mkdir("/mnt/subdir/inner", 0755);
    // TEST(group, "nested mkdir succeeds", r == 0);

    // fd = open("/mnt/subdir/inner/deep.txt", O_CREAT | O_WRONLY, 0644);
    // TEST(group, "create file in nested subdir succeeds", fd >= 0);
    // if (fd >= 0) {
    //     write(fd, "deep\n", 5);
    //     close(fd);
    // }

    // /* flush and verify deeply nested file */
    // n = flush_and_read("/mnt/subdir/inner/deep.txt", buf, sizeof(buf));
    // TEST(group, "deep file readable after flush", n > 0);
    // TEST(group, "deep file content correct after flush", strcmp(buf, "deep\n") == 0);

    ut_group_end(group);
}

/* ------------------------------------------------------------------ */
/* Group 5: partial reads, multiple writes                             */
/* ------------------------------------------------------------------ */
static void test_partial_io(us_test_group_t *group) {
    char buf[9];

    ut_group_begin(group, "fs_persist.partial_io");

    /* write in two chunks */
    int fd = open("/mnt/partial.txt", O_CREAT | O_WRONLY | O_TRUNC, 0644);
    TEST(group, "open partial.txt for write succeeds", fd >= 0);
    if (fd >= 0) {
        ssize_t w1 = write(fd, "ABCD", 4);
        ssize_t w2 = write(fd, "EFGH", 4);
        TEST(group, "first partial write correct", w1 == 4);
        TEST(group, "second partial write correct", w2 == 4);
        close(fd);
    }

    /* read in pieces */
    fd = open("/mnt/partial.txt", O_RDONLY);
    TEST(group, "open partial.txt for read succeeds", fd >= 0);
    if (fd >= 0) {
        int n1 = (int)read(fd, buf, 4);
        buf[n1] = '\0';
        TEST(group, "first chunk read correct", strcmp(buf, "ABCD") == 0);

        int n2 = (int)read(fd, buf, 4);
        buf[n2] = '\0';
        TEST(group, "second chunk read correct", strcmp(buf, "EFGH") == 0);

        /* read at EOF */
        int n3 = (int)read(fd, buf, 4);
        TEST(group, "read at EOF returns 0", n3 == 0);

        close(fd);
    }

    /* flush and re-read the whole file in one shot */
    int fn = flush_and_read("/mnt/partial.txt", buf, sizeof(buf));
    TEST(group, "partial.txt readable after flush", fn == 8);
    TEST(group, "partial.txt full content correct after flush",
         fn == 8 && buf[0]=='A' && buf[3]=='D' && buf[4]=='E' && buf[7]=='H');

    ut_group_end(group);
}

/* ------------------------------------------------------------------ */
/* Group 6: sync — multiple sync calls, overwrite-then-sync            */
/* ------------------------------------------------------------------ */
static void test_sync(us_test_group_t *group) {
    char buf[64];

    ut_group_begin(group, "fs_persist.sync");

    /* basic: write → sync → read */
    write_file("/mnt/sync_me.txt", "first\n");
    sync();
    TEST(group, "sync does not crash", 1);
    int n = read_file("/mnt/sync_me.txt", buf, sizeof(buf));
    TEST(group, "file readable after sync", n > 0);
    TEST(group, "content correct after sync", strcmp(buf, "first\n") == 0);

    /* overwrite → sync → read: old content must be gone */
    write_file("/mnt/sync_me.txt", "second\n");
    sync();
    n = read_file("/mnt/sync_me.txt", buf, sizeof(buf));
    TEST(group, "overwritten content correct after sync", strcmp(buf, "second\n") == 0);

    /* multiple sync calls in a row must not corrupt anything */
    sync();
    sync();
    n = read_file("/mnt/sync_me.txt", buf, sizeof(buf));
    TEST(group, "content stable after repeated sync", strcmp(buf, "second\n") == 0);

    ut_group_end(group);
}

/* ------------------------------------------------------------------ */
/* Public entry point                                                   */
/* ------------------------------------------------------------------ */
void run_test_fs_persist(us_test_group_t groups[], int *count) {
    test_basic_rw    (&groups[(*count)++]);
    test_open_flags  (&groups[(*count)++]);
    test_lseek       (&groups[(*count)++]);
    test_directories (&groups[(*count)++]);
    test_partial_io  (&groups[(*count)++]);
    test_sync        (&groups[(*count)++]);
}

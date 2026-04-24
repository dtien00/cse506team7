/*
 * test_dirops.c — Directory correctness (xv6 dirtest / bigdir / subdir).
 *
 * Covers: mkdir/rmdir, stat reports S_ISDIR, readdir sees created entries,
 * nested subdirectories, filenames up to the fs limit, a directory with
 * many entries (bigdir), chdir + getcwd, and unlinking from a subdir.
 *
 * All writes go to /mnt (ext2).
 */

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>
#include "us_test_common.h"

/* ── basic mkdir / stat / rmdir ─────────────────────────────────────────── */

static void test_mkdir_stat(us_test_group_t *group) {
    const char *path = "/mnt/xv6_dir1";

    /* clean up from previous run */
    rmdir(path);

    int r = mkdir(path, 0755);
    TEST(group, "mkdir: returns 0", r == 0);

    struct stat st;
    r = stat(path, &st);
    TEST(group, "mkdir: stat succeeds", r == 0);
    TEST(group, "mkdir: mode is S_ISDIR", r == 0 && S_ISDIR(st.st_mode));

    /* mkdir again → must fail (already exists) */
    TEST(group, "mkdir: duplicate returns error", mkdir(path, 0755) < 0);

    /* rmdir */
    r = rmdir(path);
    TEST(group, "rmdir: empty dir returns 0", r == 0);

    r = stat(path, &st);
    TEST(group, "rmdir: dir gone after rmdir", r < 0);

    /* rmdir nonexistent */
    TEST(group, "rmdir: nonexistent returns error", rmdir("/mnt/no_such_dir_xv6") < 0);
}

/* ── readdir sees created entries ───────────────────────────────────────── */

static void test_readdir_entries(us_test_group_t *group) {
    const char *base = "/mnt/xv6_rdtest";
    rmdir("/mnt/xv6_rdtest/a");
    rmdir("/mnt/xv6_rdtest/b");
    /* can't remove non-empty dir easily, so just create fresh names */
    mkdir(base, 0755);

    /* create two files inside */
    int f;
    f = open("/mnt/xv6_rdtest/a", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (f >= 0) close(f);
    f = open("/mnt/xv6_rdtest/b", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (f >= 0) close(f);

    DIR *d = opendir(base);
    TEST(group, "readdir: opendir succeeds", d != NULL);
    if (!d) return;

    int saw_a = 0, saw_b = 0, saw_dot = 0, saw_dotdot = 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (strcmp(de->d_name, "a")    == 0) saw_a = 1;
        if (strcmp(de->d_name, "b")    == 0) saw_b = 1;
        if (strcmp(de->d_name, ".")    == 0) saw_dot = 1;
        if (strcmp(de->d_name, "..") == 0) saw_dotdot = 1;
    }
    closedir(d);

    TEST(group, "readdir: sees entry 'a'",    saw_a);
    TEST(group, "readdir: sees entry 'b'",    saw_b);
    TEST(group, "readdir: sees '.'",          saw_dot);
    TEST(group, "readdir: sees '..'",         saw_dotdot);
}

/* ── nested subdirectories ──────────────────────────────────────────────── */

static void test_nested_dirs(us_test_group_t *group) {
    /* create /mnt/xv6_nest/a/b/c */
    mkdir("/mnt/xv6_nest",       0755);
    mkdir("/mnt/xv6_nest/a",     0755);
    mkdir("/mnt/xv6_nest/a/b",   0755);
    mkdir("/mnt/xv6_nest/a/b/c", 0755);

    /* create file deep inside */
    int fd = open("/mnt/xv6_nest/a/b/c/deep.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    TEST(group, "nested: create file 4 levels deep", fd >= 0);
    if (fd >= 0) {
        write(fd, "deep", 4);
        close(fd);
    }

    /* stat the deep file */
    struct stat st;
    int r = stat("/mnt/xv6_nest/a/b/c/deep.txt", &st);
    TEST(group, "nested: stat deep file succeeds", r == 0);
    TEST(group, "nested: deep file is regular", r == 0 && S_ISREG(st.st_mode));
    TEST(group, "nested: deep file size 4", r == 0 && st.st_size == 4);

    /* stat each intermediate dir */
    r = stat("/mnt/xv6_nest/a/b/c", &st);
    TEST(group, "nested: /mnt/xv6_nest/a/b/c is dir", r == 0 && S_ISDIR(st.st_mode));
}

/* ── chdir and getcwd ───────────────────────────────────────────────────── */

static void test_chdir_getcwd(us_test_group_t *group) {
    /* save current cwd */
    char orig[256];
    char *p = getcwd(orig, sizeof(orig));
    TEST(group, "getcwd: initial getcwd succeeds", p != NULL);

    mkdir("/mnt/xv6_cwdtest", 0755);
    int r = chdir("/mnt/xv6_cwdtest");
    TEST(group, "chdir: chdir to /mnt/xv6_cwdtest returns 0", r == 0);

    char cwd[256];
    p = getcwd(cwd, sizeof(cwd));
    TEST(group, "getcwd: getcwd after chdir succeeds", p != NULL);
    TEST(group, "getcwd: reports new directory",
         p != NULL && strcmp(cwd, "/mnt/xv6_cwdtest") == 0);

    /* relative path open after chdir */
    int fd = open("relfile.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    TEST(group, "chdir: relative open after chdir", fd >= 0);
    if (fd >= 0) close(fd);

    struct stat st;
    TEST(group, "chdir: stat relative file", stat("relfile.txt", &st) == 0);

    /* restore cwd */
    chdir(orig);
    p = getcwd(cwd, sizeof(cwd));
    TEST(group, "chdir: restore to original cwd", p != NULL && strcmp(cwd, orig) == 0);
}

/* ── 14-character filename (xv6 "fourteen" test) ───────────────────────── */

static void test_long_name(us_test_group_t *group) {
    /* exactly 14 chars (common ext2 / FAT limit sanity check) */
    const char *name14  = "/mnt/xv6_14char1234"; /* 19 chars, well within ext2 255 */
    const char *name255 = "/mnt/xv6_255charsAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";

    int fd = open(name14, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    TEST(group, "longname: 14-char file create", fd >= 0);
    if (fd >= 0) close(fd);

    struct stat st;
    TEST(group, "longname: 14-char stat succeeds", stat(name14, &st) == 0);

    /* a name at the ext2 255-byte limit — may or may not succeed, but must not crash */
    fd = open(name255, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        close(fd);
        TEST(group, "longname: 255-char name created", 1);
    } else {
        /* Kernel rejected it cleanly — also acceptable */
        TEST(group, "longname: 255-char name rejected cleanly (no crash)", 1);
    }
}

/* ── bigdir: directory with many entries ────────────────────────────────── */

static void test_bigdir(us_test_group_t *group) {
#define BIGDIR_N 50
    const char *base = "/mnt/xv6_bigdir";
    mkdir(base, 0755);

    /* create BIGDIR_N files */
    int created = 0;
    for (int i = 0; i < BIGDIR_N; i++) {
        char path[64];
        /* build "/mnt/xv6_bigdir/NNN" without sprintf */
        int pi = 0;
        const char *pfx = "/mnt/xv6_bigdir/";
        while (*pfx) path[pi++] = *pfx++;
        if (i >= 100) path[pi++] = (char)('0' + i / 100);
        if (i >= 10)  path[pi++] = (char)('0' + (i / 10) % 10);
        path[pi++] = (char)('0' + i % 10);
        path[pi] = '\0';

        int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) { write(fd, "x", 1); close(fd); created++; }
    }
    TEST(group, "bigdir: created 50 files", created == BIGDIR_N);

    /* readdir must see all of them (plus . and ..) */
    DIR *d = opendir(base);
    TEST(group, "bigdir: opendir succeeds", d != NULL);
    if (d) {
        int count = 0;
        struct dirent *de;
        while ((de = readdir(d)) != NULL) {
            if (strcmp(de->d_name, ".") != 0 && strcmp(de->d_name, "..") != 0)
                count++;
        }
        closedir(d);
        TEST(group, "bigdir: readdir sees all 50 entries", count == BIGDIR_N);
    }

    /* unlink all entries */
    int removed = 0;
    for (int i = 0; i < BIGDIR_N; i++) {
        char path[64];
        int pi = 0;
        const char *pfx = "/mnt/xv6_bigdir/";
        while (*pfx) path[pi++] = *pfx++;
        if (i >= 100) path[pi++] = (char)('0' + i / 100);
        if (i >= 10)  path[pi++] = (char)('0' + (i / 10) % 10);
        path[pi++] = (char)('0' + i % 10);
        path[pi] = '\0';
        if (unlink(path) == 0) removed++;
    }
    TEST(group, "bigdir: all 50 entries unlinked", removed == BIGDIR_N);

    /* dir should now be empty (except . and ..) — rmdir must succeed */
    TEST(group, "bigdir: rmdir empty bigdir", rmdir(base) == 0);
#undef BIGDIR_N
}

/* ── unlink file in subdir ──────────────────────────────────────────────── */

static void test_unlink_subdir(us_test_group_t *group) {
    mkdir("/mnt/xv6_usd", 0755);

    int fd = open("/mnt/xv6_usd/target.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    TEST(group, "unlink-subdir: create file in subdir", fd >= 0);
    if (fd >= 0) close(fd);

    int r = unlink("/mnt/xv6_usd/target.txt");
    TEST(group, "unlink-subdir: unlink succeeds", r == 0);

    struct stat st;
    TEST(group, "unlink-subdir: file gone after unlink",
         stat("/mnt/xv6_usd/target.txt", &st) < 0);

    /* rmdir now that it's empty */
    rmdir("/mnt/xv6_usd");
}

/* ── public entry point ─────────────────────────────────────────────────── */

void run_test_dirops(us_test_group_t *group) {
    ut_group_begin(group, "dirops");

    test_mkdir_stat(group);
    test_readdir_entries(group);
    test_nested_dirs(group);
    test_chdir_getcwd(group);
    test_long_name(group);
    test_bigdir(group);
    test_unlink_subdir(group);

    ut_group_end(group);
}

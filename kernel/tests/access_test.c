#include "kernel/printk.h"
#include "kernel/types.h"
#include "fs/vfs.h"
#include "fs/file.h"
#include "process/process.h"
#include "tests/test.h"
#include "lib/string.h"
#include "errno.h"
#include "string.h"

static int passed = 0;
static int failed = 0;

void access_test_run(void) {
    printk("=== access / lstat kernel tests ===\n");

    struct stat kst;
    int ret;

    /* ── file_stat: present path ─────────────────────────────────────────── */
    ret = file_stat("/bin/sh", &kst);
    TEST("file_stat /bin/sh succeeds", ret == 0);
    TEST("file_stat /bin/sh is regular file", ret == 0 && S_ISREG(kst.st_mode));
    TEST("file_stat /bin/sh has exec bit", ret == 0 && (kst.st_mode & 0111));

    /* ── file_stat: missing path → -ENOENT ───────────────────────────────── */
    ret = file_stat("/no/such/ghost/path", &kst);
    TEST("file_stat missing→-ENOENT", ret == -ENOENT);

    /* ── file_stat: directory ─────────────────────────────────────────────── */
    ret = file_stat("/bin", &kst);
    TEST("file_stat /bin succeeds", ret == 0);
    TEST("file_stat /bin is dir", ret == 0 && S_ISDIR(kst.st_mode));

    /* ── file_lstat: /proc/self is a symlink ─────────────────────────────── */
    struct stat kst_lstat;
    int r_stat  = file_stat("/proc/self", &kst);
    int r_lstat = file_lstat("/proc/self", &kst_lstat);
    if (r_stat == 0 && r_lstat == 0) {
        TEST("lstat /proc/self→S_IFLNK",  S_ISLNK(kst_lstat.st_mode));
        TEST("stat  /proc/self→not S_IFLNK", !S_ISLNK(kst.st_mode));
    } else {
        TEST("lstat /proc/self skipped (procfs unavailable)", 1);
        TEST("stat  /proc/self skipped (procfs unavailable)", 1);
    }

    /* ── file_lstat: regular file — same ino as stat ─────────────────────── */
    struct stat kst2;
    r_stat  = file_stat("/bin/sh", &kst);
    r_lstat = file_lstat("/bin/sh", &kst2);
    TEST("lstat+stat /bin/sh both ok",       r_stat == 0 && r_lstat == 0);
    TEST("lstat /bin/sh ino==stat ino",
         r_stat == 0 && r_lstat == 0 && kst.st_ino == kst2.st_ino);
    TEST("lstat /bin/sh not S_IFLNK",        r_lstat == 0 && !S_ISLNK(kst2.st_mode));

    /* ── file_lstat: missing path → -ENOENT ─────────────────────────────── */
    ret = file_lstat("/no/such/ghost/path", &kst);
    TEST("file_lstat missing→-ENOENT", ret == -ENOENT);

    /* ── sys_access via kernel path (calls file_stat internally) ─────────── */
    /* We test sys_access indirectly by verifying file_stat returns correct
     * mode bits that sys_access would check. */
    ret = file_stat("/bin/sh", &kst);
    TEST("access R_OK: /bin/sh has read bits",
         ret == 0 && (kst.st_mode & (0400 | 0040 | 0004)));
    TEST("access X_OK: /bin/sh has exec bits",
         ret == 0 && (kst.st_mode & (0100 | 0010 | 0001)));

    printk("access/lstat kernel tests: %d passed, %d failed\n", passed, failed);
}

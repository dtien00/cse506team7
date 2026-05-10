#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include "us_test_common.h"

void run_test_access(us_test_group_t *group) {
    ut_group_begin(group, "access");

    /* ── F_OK: existence checks ───────────────────────────────────────────── */
    ut_assert(group, access("/bin/sh", 0 /* F_OK */) == 0,  "F_OK /bin/sh present");
    ut_assert(group, access("/bin/us_test", 0 /* F_OK */) == 0, "F_OK self binary");

    int r = access("/no/such/path/xyz", 0 /* F_OK */);
    ut_assert(group, r == -1 && errno == ENOENT, "F_OK missing→ENOENT");

    /* ── R_OK / X_OK on known executables ────────────────────────────────── */
    ut_assert(group, access("/bin/sh", 1 /* X_OK */) == 0, "X_OK /bin/sh");
    ut_assert(group, access("/bin/sh", 4 /* R_OK */) == 0, "R_OK /bin/sh");

    /* ── python (needed by shebang test) ─────────────────────────────────── */
    ut_assert(group, access("/bin/python", 1 /* X_OK */) == 0, "X_OK /bin/python");

    /* ── NULL path → EFAULT/EINVAL ───────────────────────────────────────── */
    r = access((const char *)0, 0 /* F_OK */);
    ut_assert(group, r == -1 && (errno == EFAULT || errno == EINVAL),
              "NULL path→EFAULT/EINVAL");

    /* ── Directory: F_OK and R_OK ────────────────────────────────────────── */
    ut_assert(group, access("/bin", 0 /* F_OK */) == 0, "F_OK /bin dir");
    ut_assert(group, access("/tmp", 4 /* R_OK */) == 0, "R_OK /tmp dir");

    /* ── W_OK permission check on read-only file ─────────────────────────── */
    const char *ronly = "/tmp/test_access_ro.txt";
    int fd = open(ronly, O_CREAT | O_WRONLY | O_TRUNC, 0444);
    if (fd >= 0) {
        close(fd);
        ut_assert(group, access(ronly, 4 /* R_OK */) == 0,  "R_OK 0444 file");
        ut_assert(group, access(ronly, 2 /* W_OK */) == -1, "W_OK 0444→EACCES");
        if (access(ronly, 2) == -1)
            ut_assert(group, errno == EACCES, "W_OK 0444 errno==EACCES");
        unlink(ronly);
    }

    /* ── X_OK on non-executable regular file ─────────────────────────────── */
    const char *noexec = "/tmp/test_access_noexec.txt";
    fd = open(noexec, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd >= 0) {
        close(fd);
        ut_assert(group, access(noexec, 4 /* R_OK */) == 0,  "R_OK 0644 noexec");
        ut_assert(group, access(noexec, 1 /* X_OK */) == -1, "X_OK 0644→EACCES");
        if (access(noexec, 1) == -1)
            ut_assert(group, errno == EACCES, "X_OK 0644 errno==EACCES");
        unlink(noexec);
    }

    /* ── lstat: does not follow symlinks ─────────────────────────────────── */
    struct stat st_follow, st_nofollow;
    /* /proc/self is a symlink; stat follows it, lstat should not */
    int r_stat  = stat("/proc/self", &st_follow);
    int r_lstat = lstat("/proc/self", &st_nofollow);
    if (r_stat == 0 && r_lstat == 0) {
        /* lstat on a symlink must report S_IFLNK, stat must not */
        ut_assert(group,  S_ISLNK(st_nofollow.st_mode), "lstat symlink→S_IFLNK");
        ut_assert(group, !S_ISLNK(st_follow.st_mode),   "stat  symlink→not S_IFLNK");
    }

    /* ── lstat: non-existent path returns ENOENT ─────────────────────────── */
    r = lstat("/no/such/ghost", &st_nofollow);
    ut_assert(group, r == -1 && errno == ENOENT, "lstat missing→ENOENT");

    /* ── lstat: regular file same result as stat ─────────────────────────── */
    const char *regfile = "/tmp/test_access_lstat.txt";
    fd = open(regfile, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd >= 0) {
        close(fd);
        struct stat st1, st2;
        int r1 = stat(regfile, &st1);
        int r2 = lstat(regfile, &st2);
        ut_assert(group, r1 == 0 && r2 == 0,             "stat+lstat on reg file ok");
        ut_assert(group, st1.st_ino == st2.st_ino,        "stat==lstat ino for reg");
        ut_assert(group, !S_ISLNK(st2.st_mode),           "lstat reg file not symlink");
        unlink(regfile);
    }

    /* ── Combined mode bits ───────────────────────────────────────────────── */
    const char *rwfile = "/tmp/test_access_rw.txt";
    fd = open(rwfile, O_CREAT | O_WRONLY | O_TRUNC, 0666);
    if (fd >= 0) {
        close(fd);
        /* R_OK|W_OK together */
        ut_assert(group, access(rwfile, 4|2 /* R_OK|W_OK */) == 0, "R_OK|W_OK 0666");
        unlink(rwfile);
    }
}

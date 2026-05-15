/*
 * test_umask.c — Regression test for POSIX umask().
 *
 * Closes the "umask in struct proc + syscall + applied" feedback item.
 * Covers the three independent pieces:
 *
 *   1. The syscall itself: returns the previous mask, stores the new one,
 *      truncates input to nine bits.
 *   2. Applied at open(O_CREAT): the on-disk mode is `mode & ~umask`.
 *   3. Applied at mkdir: same masking.
 *   4. Inherited across fork.
 *
 * Uses /mnt (the persistent ext2 mount, writable for these tests) so the
 * create paths actually exercise the umask application; tarfs would
 * reject creates.
 */

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "us_test_common.h"

static int file_mode(const char *p) {
    struct stat st;
    if (stat(p, &st) != 0) return -1;
    return (int)(st.st_mode & 0777);
}

void run_test_umask(us_test_group_t *group) {
    ut_group_begin(group, "umask");

    /* ── 1. Syscall semantics ─────────────────────────────────────────── */
    /* Spec: umask(new) sets current->umask = new & 0777 and returns prior. */
    mode_t saved = umask(022);              /* normalize starting state */
    (void)saved;

    mode_t prev = umask(077);
    TEST(group, "umask(077) returns previous 022", prev == 022);

    prev = umask(033);
    TEST(group, "umask(033) returns previous 077", prev == 077);

    /* High bits outside 0777 must be discarded — only the low nine bits matter. */
    prev = umask(0177777);                  /* anything beyond 0777 is junk */
    TEST(group, "umask truncates to nine bits", (umask(022) & ~0777) == 0);
    (void)prev;

    /* ── 2. Applied at open(O_CREAT) ──────────────────────────────────── */
    umask(022);
    unlink("/mnt/umask_probe_a");
    int fd = open("/mnt/umask_probe_a", O_CREAT | O_WRONLY, 0666);
    TEST(group, "open(O_CREAT, 0666) succeeds", fd >= 0);
    if (fd >= 0) close(fd);
    TEST(group, "0666 & ~022 == 0644 stored on disk",
         file_mode("/mnt/umask_probe_a") == 0644);
    unlink("/mnt/umask_probe_a");

    umask(077);
    unlink("/mnt/umask_probe_b");
    fd = open("/mnt/umask_probe_b", O_CREAT | O_WRONLY, 0666);
    TEST(group, "open(O_CREAT, 0666) with umask 077 succeeds", fd >= 0);
    if (fd >= 0) close(fd);
    TEST(group, "0666 & ~077 == 0600 stored on disk",
         file_mode("/mnt/umask_probe_b") == 0600);
    unlink("/mnt/umask_probe_b");

    /* Plain open (no O_CREAT) on an existing file must NOT touch its mode. */
    umask(022);
    fd = open("/mnt/umask_probe_c", O_CREAT | O_WRONLY, 0644);
    if (fd >= 0) close(fd);
    umask(077);                              /* changed since create */
    fd = open("/mnt/umask_probe_c", O_RDONLY);   /* no O_CREAT */
    if (fd >= 0) close(fd);
    TEST(group, "open without O_CREAT leaves mode untouched",
         file_mode("/mnt/umask_probe_c") == 0644);
    unlink("/mnt/umask_probe_c");

    /* ── 3. Applied at mkdir ──────────────────────────────────────────── */
    umask(022);
    rmdir("/mnt/umask_probe_d");
    int rc = mkdir("/mnt/umask_probe_d", 0777);
    TEST(group, "mkdir(0777) succeeds", rc == 0);
    TEST(group, "0777 & ~022 == 0755 stored on disk",
         file_mode("/mnt/umask_probe_d") == 0755);
    rmdir("/mnt/umask_probe_d");

    umask(027);
    rmdir("/mnt/umask_probe_e");
    rc = mkdir("/mnt/umask_probe_e", 0777);
    TEST(group, "mkdir(0777) with umask 027 succeeds", rc == 0);
    TEST(group, "0777 & ~027 == 0750 stored on disk",
         file_mode("/mnt/umask_probe_e") == 0750);
    rmdir("/mnt/umask_probe_e");

    /* ── 4. Inherited across fork ─────────────────────────────────────── */
    umask(044);
    pid_t pid = fork();
    if (pid == 0) {
        /* Child should see parent's umask (044) and have it applied. */
        mode_t my = umask(0);  /* read by setting to 0 then we restore */
        if (my != 044) _exit(11);
        umask(044);            /* restore for the child's own create */
        unlink("/mnt/umask_probe_child");
        int cfd = open("/mnt/umask_probe_child", O_CREAT | O_WRONLY, 0666);
        if (cfd < 0) _exit(12);
        close(cfd);
        struct stat cst;
        if (stat("/mnt/umask_probe_child", &cst) != 0) _exit(13);
        if ((cst.st_mode & 0777) != 0622) _exit(14);  /* 0666 & ~044 = 0622 */
        unlink("/mnt/umask_probe_child");
        _exit(0);
    }
    TEST(group, "fork() succeeds", pid > 0);
    if (pid > 0) {
        int wst = 0;
        waitpid(pid, &wst, 0);
        TEST(group, "child inherits umask and applies it on create",
             WIFEXITED(wst) && WEXITSTATUS(wst) == 0);

        /* Parent's umask was not perturbed by child's umask(0) call. */
        TEST(group, "parent umask survives child's mutation",
             umask(044) == 044);
    }

    /* Restore default for downstream tests. */
    umask(022);
    ut_group_end(group);
}

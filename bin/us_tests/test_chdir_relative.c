/*
 * test_chdir_relative.c — Relative-path lookup after chdir().
 *
 * Companion to FEEDBACK_CHDIR.md.  Covers the chain:
 *   chdir(dir) → stat/open/access/pathconf(bare-name) resolves via
 *   current->fs->cwd, and the result agrees with the absolute path.
 *
 * Also exercises fork+exec cwd inheritance: a forked child must see the
 * same cwd, and bare-name lookups in the child must succeed.
 *
 * Picks /bin (which always contains sh on every build) as the test
 * directory so the test is hermetic — no dependence on /home/hello.c.
 */

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "us_test_common.h"

static int file_exists_abs(const char *p) {
    struct stat st;
    return stat(p, &st) == 0;
}

void run_test_chdir_relative(us_test_group_t *group) {
    ut_group_begin(group, "chdir_relative");

    /* Save cwd so we can restore it before returning. */
    char saved[256];
    if (getcwd(saved, sizeof(saved)) == NULL) saved[0] = '\0';

    /* Pick a directory + file that exist on every build. */
    const char *dir  = "/bin";
    const char *name = "sh";
    char abs[256];
    snprintf(abs, sizeof(abs), "%s/%s", dir, name);

    if (!file_exists_abs(abs)) {
        SKIP(group, "chdir_relative", "/bin/sh missing — cannot probe");
        if (saved[0]) (void)chdir(saved);
        ut_group_end(group);
        return;
    }

    /* ── chdir + getcwd round-trip ───────────────────────────────────────── */
    int rc = chdir(dir);
    TEST(group, "chdir(/bin) succeeds", rc == 0);

    char cwd[256];
    char *gc = getcwd(cwd, sizeof(cwd));
    TEST(group, "getcwd after chdir returns /bin",
         gc != NULL && strcmp(cwd, dir) == 0);

    /* ── bare-name stat vs absolute stat agree ───────────────────────────── */
    struct stat st_abs, st_rel;
    int rc_abs = stat(abs,  &st_abs);
    int rc_rel = stat(name, &st_rel);
    TEST(group, "stat(abs /bin/sh) succeeds", rc_abs == 0);
    TEST(group, "stat(rel sh) after chdir succeeds", rc_rel == 0);
    TEST(group, "rel and abs stat refer to same inode",
         rc_abs == 0 && rc_rel == 0 && st_abs.st_ino == st_rel.st_ino);

    /* ── access(bare, F_OK) ──────────────────────────────────────────────── */
    TEST(group, "access(rel sh, F_OK) after chdir succeeds",
         access(name, F_OK) == 0);

    /* ── open(bare, RDONLY) ──────────────────────────────────────────────── */
    int fd = open(name, O_RDONLY);
    TEST(group, "open(rel sh, RDONLY) after chdir succeeds", fd >= 0);
    if (fd >= 0) close(fd);

    /* ── pathconf(bare, _PC_NAME_MAX) — Issue L regression ───────────────── */
    long pc_rel = pathconf(name, _PC_NAME_MAX);
    long pc_abs = pathconf(abs,  _PC_NAME_MAX);
    TEST(group, "pathconf(rel sh) after chdir succeeds (Issue L)",
         pc_rel == pc_abs && pc_rel > 0);
    /* Negative: a bare name that does NOT exist in cwd must fail, even though
       a same-named entry exists at the filesystem root.  Confirms cwd-relative
       resolution rather than root-relative. */
    long pc_bogus = pathconf("definitely_not_here_xyz", _PC_NAME_MAX);
    TEST(group, "pathconf(bogus bare name) returns -1",
         pc_bogus == -1L);

    /* ── fork+wait: child inherits cwd, bare-name lookup still works ─────── */
    pid_t pid = fork();
    if (pid == 0) {
        char ccwd[256];
        if (getcwd(ccwd, sizeof(ccwd)) == NULL || strcmp(ccwd, dir) != 0) _exit(11);

        struct stat cst;
        if (stat(name, &cst) != 0) _exit(12);

        int cfd = open(name, O_RDONLY);
        if (cfd < 0) _exit(13);
        close(cfd);

        if (access(name, F_OK) != 0) _exit(14);
        _exit(0);
    }
    TEST(group, "fork() succeeds", pid > 0);
    if (pid > 0) {
        int wst = 0;
        waitpid(pid, &wst, 0);
        TEST(group, "child inherits cwd and bare-name lookups succeed",
             WIFEXITED(wst) && WEXITSTATUS(wst) == 0);
    }

    /* ── Restore cwd ─────────────────────────────────────────────────────── */
    if (saved[0]) (void)chdir(saved);
    else (void)chdir("/");

    ut_group_end(group);
}

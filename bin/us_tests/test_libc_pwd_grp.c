/*
 * test_libc_pwd_grp.c — pwd.h / grp.h conformance tests
 *
 * Exercises getpwuid/getpwnam (libc/passwd.c) and getgrgid/getgrnam
 * (libc/grp.c). Both parsers read from /etc/passwd / /etc/group.
 *
 * /etc/passwd and /etc/group are not yet present in the rootfs (TCC_BB.md
 * §12.6 — Wave 2 work). When the files are absent, the lookup functions
 * correctly return NULL; tests then mark those checks SKIP rather than
 * FAIL. When the files are added, the same test code activates and
 * validates lookup-by-uid, lookup-by-name, and the missing-entry path.
 */

#include <pwd.h>
#include <grp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "us_test_common.h"

static int file_present(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    fclose(f);
    return 1;
}

/* ---------- getpwuid / getpwnam ---------------------------------- */
static void test_passwd(us_test_group_t *g) {
    ut_group_begin(g, "getpwuid/getpwnam");

    if (!file_present("/etc/passwd")) {
        /* Verify the absent-file contract: NULL return, no crash. */
        TEST(g, "getpwuid(0) returns NULL when /etc/passwd missing",
             getpwuid(0) == NULL);
        TEST(g, "getpwnam(root) returns NULL when /etc/passwd missing",
             getpwnam("root") == NULL);
        SKIP(g, "live lookups", "/etc/passwd not yet in rootfs (TCC_BB §12.6)");
        ut_group_end(g);
        return;
    }

    /* Live tests when /etc/passwd is present. */
    struct passwd *pw = getpwuid(0);
    TEST(g, "getpwuid(0) finds root", pw != NULL && pw->pw_uid == 0);
    if (pw) {
        TEST(g, "root pw_name == 'root'",
             pw->pw_name && strcmp(pw->pw_name, "root") == 0);
        TEST(g, "root pw_dir non-null", pw->pw_dir != NULL);
        TEST(g, "root pw_shell non-null", pw->pw_shell != NULL);
    }

    pw = getpwnam("root");
    TEST(g, "getpwnam('root') finds entry", pw != NULL);
    if (pw) TEST(g, "getpwnam root uid == 0", pw->pw_uid == 0);

    /* Missing entry */
    TEST(g, "getpwnam(unknown) returns NULL",
         getpwnam("__no_such_user__") == NULL);
    TEST(g, "getpwuid(99999) returns NULL",
         getpwuid(99999) == NULL);

    /* Re-entrant variant: caller-provided buf */
    struct passwd  pw_r;
    struct passwd *pw_r_result = NULL;
    char           pw_buf[256];
    int rc = getpwnam_r("root", &pw_r, pw_buf, sizeof(pw_buf), &pw_r_result);
    TEST(g, "getpwnam_r succeeds", rc == 0);
    TEST(g, "getpwnam_r result points at caller struct",
         pw_r_result == &pw_r);
    if (pw_r_result)
        TEST(g, "getpwnam_r fields copied",
             strcmp(pw_r.pw_name, "root") == 0 && pw_r.pw_uid == 0);

    ut_group_end(g);
}

/* ---------- getgrgid / getgrnam ---------------------------------- */
static void test_group(us_test_group_t *g) {
    ut_group_begin(g, "getgrgid/getgrnam");

    if (!file_present("/etc/group")) {
        TEST(g, "getgrgid(0) returns NULL when /etc/group missing",
             getgrgid(0) == NULL);
        TEST(g, "getgrnam(root) returns NULL when /etc/group missing",
             getgrnam("root") == NULL);
        SKIP(g, "live lookups", "/etc/group not yet in rootfs (TCC_BB §12.6)");
        ut_group_end(g);
        return;
    }

    struct group *gr = getgrgid(0);
    TEST(g, "getgrgid(0) finds root", gr != NULL && gr->gr_gid == 0);
    if (gr) {
        TEST(g, "root gr_name == 'root'",
             gr->gr_name && strcmp(gr->gr_name, "root") == 0);
        TEST(g, "root gr_mem NULL-terminated", gr->gr_mem != NULL);
    }

    gr = getgrnam("root");
    TEST(g, "getgrnam('root') finds entry", gr != NULL);
    if (gr) TEST(g, "getgrnam root gid == 0", gr->gr_gid == 0);

    TEST(g, "getgrnam(unknown) returns NULL",
         getgrnam("__no_such_group__") == NULL);
    TEST(g, "getgrgid(99999) returns NULL",
         getgrgid(99999) == NULL);

    /* Re-entrant variant */
    struct group  gr_r;
    struct group *gr_r_result = NULL;
    char          gr_buf[256];
    int rc = getgrnam_r("root", &gr_r, gr_buf, sizeof(gr_buf), &gr_r_result);
    TEST(g, "getgrnam_r succeeds", rc == 0);
    TEST(g, "getgrnam_r result points at caller struct",
         gr_r_result == &gr_r);
    if (gr_r_result)
        TEST(g, "getgrnam_r fields copied",
             strcmp(gr_r.gr_name, "root") == 0 && gr_r.gr_gid == 0);

    ut_group_end(g);
}

/* ================================================================ entry point */

void run_test_libc_pwd_grp(us_test_group_t groups[], int *idx) {
    test_passwd(&groups[(*idx)++]);
    test_group (&groups[(*idx)++]);
}

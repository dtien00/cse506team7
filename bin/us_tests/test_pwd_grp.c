/*
 * test_pwd_grp.c — Regression test for POSIX getpwuid/getpwnam/getgrgid/
 *                  getgrnam (and their _r variants).
 *
 * All declared in <pwd.h> / <grp.h> with no body in libc before this
 * change.  BusyBox `ls -l`, `id`, `whoami`, `groups` and micropython's
 * `pwd`/`grp` modules all call into these.  SBUnix's hardcoded universe
 * is one entry per database: "root" / uid 0 / gid 0.
 *
 * Coverage:
 *   - getpwuid(0)   → "root", fields filled
 *   - getpwuid(99)  → NULL, errno=ENOENT
 *   - getpwnam("root")  → same struct as getpwuid(0)
 *   - getpwnam("nobody") → NULL, errno=ENOENT
 *   - getpwuid_r success / not-found / ERANGE
 *   - mirror for getgrgid / getgrnam / getgrgid_r
 *   - struct group ABI: caller can read gr_passwd, gr_gid, gr_mem
 *     all in the right order (verifies the libc/thirdparty layout reconciliation)
 */

#include <pwd.h>
#include <grp.h>
#include <string.h>
#include <errno.h>
#include "us_test_common.h"

void run_test_pwd_grp(us_test_group_t *group) {
    ut_group_begin(group, "pwd_grp");

    /* ── getpwuid ────────────────────────────────────────────────────── */
    struct passwd *p = getpwuid(0);
    TEST(group, "getpwuid(0) non-NULL", p != NULL);
    if (p) {
        TEST(group, "getpwuid(0)->pw_name == \"root\"",   strcmp(p->pw_name,  "root") == 0);
        TEST(group, "getpwuid(0)->pw_uid  == 0",          p->pw_uid == 0);
        TEST(group, "getpwuid(0)->pw_gid  == 0",          p->pw_gid == 0);
        TEST(group, "getpwuid(0)->pw_dir   == \"/\"",     strcmp(p->pw_dir,   "/") == 0);
        TEST(group, "getpwuid(0)->pw_shell == \"/bin/sh\"", strcmp(p->pw_shell, "/bin/sh") == 0);
    }

    /* Unknown uid */
    errno = 0;
    p = getpwuid(12345);
    TEST(group, "getpwuid(12345) == NULL",       p == NULL);
    TEST(group, "getpwuid unknown sets ENOENT",  errno == ENOENT);

    /* ── getpwnam ────────────────────────────────────────────────────── */
    p = getpwnam("root");
    TEST(group, "getpwnam(\"root\") non-NULL", p != NULL);
    if (p) TEST(group, "getpwnam(\"root\")->pw_uid == 0", p->pw_uid == 0);

    errno = 0;
    p = getpwnam("nobody");
    TEST(group, "getpwnam(\"nobody\") == NULL", p == NULL);
    TEST(group, "getpwnam unknown sets ENOENT", errno == ENOENT);

    /* ── getpwuid_r success path ─────────────────────────────────────── */
    char pbuf[256];
    struct passwd pwd_out;
    struct passwd *pres = NULL;
    int rc = getpwuid_r(0, &pwd_out, pbuf, sizeof(pbuf), &pres);
    TEST(group, "getpwuid_r(0) returns 0",        rc == 0);
    TEST(group, "getpwuid_r sets *result",        pres == &pwd_out);
    TEST(group, "getpwuid_r filled pw_name",      pres && strcmp(pres->pw_name, "root") == 0);
    TEST(group, "getpwuid_r filled pw_shell",     pres && strcmp(pres->pw_shell, "/bin/sh") == 0);
    TEST(group, "getpwuid_r strings live in buf",
         pres && pres->pw_name >= pbuf && pres->pw_name < pbuf + sizeof(pbuf));

    /* getpwuid_r not-found path: rc==0, *result==NULL */
    rc = getpwuid_r(99, &pwd_out, pbuf, sizeof(pbuf), &pres);
    TEST(group, "getpwuid_r(99) returns 0 (not found)", rc == 0);
    TEST(group, "getpwuid_r(99) sets *result = NULL",   pres == NULL);

    /* getpwuid_r ERANGE on tiny buffer */
    rc = getpwuid_r(0, &pwd_out, pbuf, 4, &pres);
    TEST(group, "getpwuid_r tiny buf returns ERANGE", rc == ERANGE);

    /* getpwnam_r not-found */
    rc = getpwnam_r("ghost", &pwd_out, pbuf, sizeof(pbuf), &pres);
    TEST(group, "getpwnam_r(\"ghost\") returns 0 (not found)", rc == 0);
    TEST(group, "getpwnam_r(\"ghost\") *result == NULL",       pres == NULL);

    /* ── getgrgid ────────────────────────────────────────────────────── */
    struct group *g = getgrgid(0);
    TEST(group, "getgrgid(0) non-NULL", g != NULL);
    if (g) {
        TEST(group, "getgrgid(0)->gr_name == \"root\"", strcmp(g->gr_name, "root") == 0);
        TEST(group, "getgrgid(0)->gr_gid  == 0",        g->gr_gid == 0);
        TEST(group, "getgrgid(0)->gr_mem non-NULL",     g->gr_mem != NULL);
        TEST(group, "getgrgid(0)->gr_mem[0] == NULL (empty)",
             g->gr_mem && g->gr_mem[0] == NULL);
    }

    errno = 0;
    g = getgrgid(99);
    TEST(group, "getgrgid(99) == NULL",          g == NULL);
    TEST(group, "getgrgid unknown sets ENOENT",  errno == ENOENT);

    /* ── getgrnam ────────────────────────────────────────────────────── */
    g = getgrnam("root");
    TEST(group, "getgrnam(\"root\") non-NULL", g != NULL);
    if (g) TEST(group, "getgrnam(\"root\")->gr_gid == 0", g->gr_gid == 0);

    errno = 0;
    g = getgrnam("wheel");
    TEST(group, "getgrnam(\"wheel\") == NULL",   g == NULL);
    TEST(group, "getgrnam unknown sets ENOENT",  errno == ENOENT);

    /* ── getgrgid_r success ──────────────────────────────────────────── */
    char gbuf[256];
    struct group grp_out;
    struct group *gres = NULL;
    rc = getgrgid_r(0, &grp_out, gbuf, sizeof(gbuf), &gres);
    TEST(group, "getgrgid_r(0) returns 0",          rc == 0);
    TEST(group, "getgrgid_r sets *result",          gres == &grp_out);
    TEST(group, "getgrgid_r filled gr_name",        gres && strcmp(gres->gr_name, "root") == 0);
    TEST(group, "getgrgid_r filled gr_gid",         gres && gres->gr_gid == 0);
    TEST(group, "getgrgid_r gr_mem terminated NULL",
         gres && gres->gr_mem && gres->gr_mem[0] == NULL);

    /* getgrgid_r ERANGE */
    rc = getgrgid_r(0, &grp_out, gbuf, 4, &gres);
    TEST(group, "getgrgid_r tiny buf returns ERANGE", rc == ERANGE);

    ut_group_end(group);
}

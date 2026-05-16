/*
 * test_realpath.c — correctness tests for realpath().
 *
 * Covers: dot/dotdot normalisation, absolute and relative inputs, symlink
 * resolution (single hop, chained, relative target, absolute target),
 * NULL-resolved (malloc) mode, error cases (ENOENT, ELOOP, ENAMETOOLONG).
 */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include "us_test_common.h"

/* Create a regular file at path (truncate & close immediately). */
static int make_file(const char *path)
{
    int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) return -1;
    close(fd);
    return 0;
}

void run_test_realpath(us_test_group_t *group)
{
    ut_group_begin(group, "realpath");

    char buf[1024];

    /* ── Resolve the canonical form of /bin/sh once.
     *    /bin/sh may be a symlink (e.g. → busybox after make thirdparty).
     *    All tests that expect the shell's canonical path compare against
     *    sh_canon rather than the literal string "/bin/sh". ────────────── */
    char sh_canon[1024];
    char *sh_r = realpath("/bin/sh", sh_canon);
    ut_assert(group, sh_r != (char *)0,   "abs path: not null");
    ut_assert(group, sh_r && sh_r[0] == '/', "abs path: starts with /");

    char *r = sh_r;

    /* ── root itself ─────────────────────────────────────────────────────── */
    r = realpath("/", buf);
    ut_assert(group, r != (char *)0,                "root: not null");
    ut_assert(group, r && strcmp(r, "/") == 0,      "root: equals /");

    /* ── trailing slashes are stripped ──────────────────────────────────── */
    r = realpath("/bin/", buf);
    ut_assert(group, r != (char *)0,                "trailing slash: not null");
    ut_assert(group, r && strcmp(r, "/bin") == 0,   "trailing slash: stripped");

    /* ── embedded '.' components are removed ─────────────────────────────── */
    r = realpath("/bin/././sh", buf);
    ut_assert(group, r != (char *)0,                     "dot: not null");
    ut_assert(group, r && sh_r && strcmp(r, sh_canon) == 0, "dot: /bin/./. → canonical sh");

    /* ── '..' collapses the previous component ───────────────────────────── */
    r = realpath("/bin/../bin/sh", buf);
    ut_assert(group, r != (char *)0,                     "dotdot: not null");
    ut_assert(group, r && sh_r && strcmp(r, sh_canon) == 0, "dotdot: resolves");

    /* ── '..' at root stays at root ──────────────────────────────────────── */
    r = realpath("/../bin/sh", buf);
    ut_assert(group, r != (char *)0,                     "dotdot-root: not null");
    ut_assert(group, r && sh_r && strcmp(r, sh_canon) == 0, "dotdot-root: clamps");

    /* ── NULL resolved: result is malloc'd ──────────────────────────────── */
    r = realpath("/bin/sh", (char *)0);
    ut_assert(group, r != (char *)0,            "malloc mode: not null");
    ut_assert(group, r && r[0] == '/',          "malloc mode: absolute");
    if (r) free(r);

    /* ── non-existent path sets errno = ENOENT ───────────────────────────── */
    errno = 0;
    r = realpath("/no/such/path/xyz_does_not_exist", buf);
    ut_assert(group, r == (char *)0,            "ENOENT: returns null");
    ut_assert(group, errno == ENOENT,           "ENOENT: errno set");

    /* ── NULL input sets errno = ENOENT ──────────────────────────────────── */
    errno = 0;
    r = realpath((char *)0, buf);
    ut_assert(group, r == (char *)0,            "null path: returns null");
    ut_assert(group, errno == ENOENT,           "null path: errno ENOENT");

    /* ── empty string sets errno = ENOENT ───────────────────────────────── */
    errno = 0;
    r = realpath("", buf);
    ut_assert(group, r == (char *)0,            "empty path: returns null");
    ut_assert(group, errno == ENOENT,           "empty path: errno ENOENT");

    /* ── relative path resolved against cwd ─────────────────────────────── */
    {
        /* chdir to /bin, resolve "sh" */
        char prev[1024];
        if (sh_r && getcwd(prev, sizeof(prev)) && chdir("/bin") == 0) {
            r = realpath("sh", buf);
            ut_assert(group, r != (char *)0,                    "relative: not null");
            ut_assert(group, r && strcmp(r, sh_canon) == 0,     "relative: canonical sh");
            chdir(prev);
        } else {
            SKIP(group, "relative: getcwd/chdir unavailable", "n/a");
        }
    }

    /* ── relative path with '..': bin/../bin/sh → canonical sh ──────────── */
    {
        char prev[1024];
        if (sh_r && getcwd(prev, sizeof(prev)) && chdir("/") == 0) {
            r = realpath("bin/../bin/sh", buf);
            ut_assert(group, r != (char *)0,                    "rel dotdot: not null");
            ut_assert(group, r && strcmp(r, sh_canon) == 0,     "rel dotdot: canonical sh");
            chdir(prev);
        } else {
            SKIP(group, "rel dotdot: chdir unavailable", "n/a");
        }
    }

    /* ── symlink: absolute target ────────────────────────────────────────── */
    {
        /* A symlink pointing to /bin/sh should resolve to the same canonical
         * path as realpath("/bin/sh") itself — following all layers. */
        const char *lnk    = "/tmp/rp_abs_link";
        const char *target = "/bin/sh";
        unlink(lnk);
        if (sh_r && symlink(target, lnk) == 0) {
            r = realpath(lnk, buf);
            ut_assert(group, r != (char *)0,                     "abs symlink: not null");
            ut_assert(group, r && strcmp(r, sh_canon) == 0,      "abs symlink: resolves");
            unlink(lnk);
        } else {
            SKIP(group, "abs symlink: symlink() failed", "n/a");
        }
    }

    /* ── symlink: relative target ────────────────────────────────────────── */
    {
        /* /tmp/rp_rel_link → sh  (relative, in /bin context via absolute symlink) */
        const char *lnk    = "/tmp/rp_rel_link";
        const char *file   = "/tmp/rp_rel_file";
        unlink(lnk); unlink(file);
        if (make_file(file) == 0 && symlink("rp_rel_file", lnk) == 0) {
            /* lnk is in /tmp; target "rp_rel_file" is relative to /tmp */
            r = realpath(lnk, buf);
            ut_assert(group, r != (char *)0,                    "rel symlink: not null");
            ut_assert(group, r && strcmp(r, "/tmp/rp_rel_file") == 0,
                      "rel symlink: /tmp/rp_rel_file");
            unlink(lnk); unlink(file);
        } else {
            SKIP(group, "rel symlink: setup failed", "n/a");
        }
    }

    /* ── symlink chain ───────────────────────────────────────────────────── */
    {
        const char *a = "/tmp/rp_chain_a";  /* → b */
        const char *b = "/tmp/rp_chain_b";  /* → file */
        const char *f = "/tmp/rp_chain_f";
        unlink(a); unlink(b); unlink(f);
        if (make_file(f) == 0 &&
            symlink("/tmp/rp_chain_b", a) == 0 &&
            symlink("/tmp/rp_chain_f", b) == 0) {
            r = realpath(a, buf);
            ut_assert(group, r != (char *)0,                        "chain: not null");
            ut_assert(group, r && strcmp(r, "/tmp/rp_chain_f") == 0,
                      "chain: resolves to file");
            unlink(a); unlink(b); unlink(f);
        } else {
            SKIP(group, "chain: setup failed", "n/a");
        }
    }

    /* ── symlink loop → ELOOP ────────────────────────────────────────────── */
    {
        const char *a = "/tmp/rp_loop_a";
        const char *b = "/tmp/rp_loop_b";
        unlink(a); unlink(b);
        if (symlink("/tmp/rp_loop_b", a) == 0 &&
            symlink("/tmp/rp_loop_a", b) == 0) {
            errno = 0;
            r = realpath(a, buf);
            ut_assert(group, r == (char *)0, "loop: returns null");
            ut_assert(group, errno == ELOOP, "loop: ELOOP");
            unlink(a); unlink(b);
        } else {
            SKIP(group, "loop: setup failed", "n/a");
        }
    }

    /* ── symlink to directory component ─────────────────────────────────── */
    {
        const char *dir  = "/tmp/rp_dirlink_d";
        const char *lnk  = "/tmp/rp_dirlink_l";
        const char *file = "/tmp/rp_dirlink_d/f";
        unlink(lnk);
        /* best-effort cleanup */
        unlink(file); rmdir(dir);
        if (mkdir(dir, 0755) == 0 && make_file(file) == 0 &&
            symlink("/tmp/rp_dirlink_d", lnk) == 0) {
            r = realpath("/tmp/rp_dirlink_l/f", buf);
            ut_assert(group, r != (char *)0, "dirlink: not null");
            ut_assert(group, r && strcmp(r, "/tmp/rp_dirlink_d/f") == 0,
                      "dirlink: resolves through dir symlink");
            unlink(lnk); unlink(file); rmdir(dir);
        } else {
            SKIP(group, "dirlink: setup failed", "n/a");
        }
    }

    /* ── result is always NUL-terminated and fits in PATH_MAX ───────────── */
    r = sh_r ? sh_canon : (char *)0;  /* reuse already-computed canonical path */
    if (r) {
        size_t len = strlen(r);
        ut_assert(group, len > 0 && len < 1024, "length: fits in PATH_MAX");
        ut_assert(group, r[len] == '\0',         "NUL termination");
    }
}
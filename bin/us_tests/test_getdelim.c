/*
 * test_getdelim.c — Regression test for POSIX getline()/getdelim().
 *
 * Both declared in <stdio.h>:114-115; before this change libc.a contained
 * no body for either, so any caller (notably BusyBox line-oriented
 * applets) failed to link.
 *
 * Strategy: write a known fixture file to /mnt, fopen it, then drive
 * getline()/getdelim() through it.  Verifies:
 *   - first line auto-allocates *lineptr when NULL
 *   - subsequent lines reuse / grow the same buffer
 *   - delimiter byte is included in the returned length and stored before NUL
 *   - last unterminated line is still returned
 *   - EOF after the last line returns -1
 *   - getdelim with custom delimiter (':') works
 *   - long line forces buffer growth (capacity > initial 128)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include "us_test_common.h"

#define FIXTURE_PATH "/mnt/getdelim_probe.txt"

static int write_fixture(const char *path, const char *data, size_t len) {
    int fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (fd < 0) return -1;
    ssize_t w = write(fd, data, len);
    close(fd);
    return (w == (ssize_t)len) ? 0 : -1;
}

void run_test_getdelim(us_test_group_t *group) {
    ut_group_begin(group, "getdelim");

    /* ── Build a fixture with three newline-terminated lines + one
     *    unterminated trailing line; the third line is intentionally
     *    longer than GETDELIM_INITIAL (128) to force a realloc. ────── */
    static char fixture[600];
    /* "alpha\nbeta\n" + 200 chars of 'x' + "\nlast" */
    size_t off = 0;
    memcpy(fixture + off, "alpha\n", 6); off += 6;
    memcpy(fixture + off, "beta\n",  5); off += 5;
    for (int i = 0; i < 200; i++) fixture[off++] = 'x';
    fixture[off++] = '\n';
    memcpy(fixture + off, "last", 4); off += 4;
    /* No trailing newline on "last". */

    if (write_fixture(FIXTURE_PATH, fixture, off) != 0) {
        SKIP(group, "getdelim", "could not write /mnt fixture (mnt mounted?)");
        ut_group_end(group);
        return;
    }

    FILE *fp = fopen(FIXTURE_PATH, "r");
    TEST(group, "fopen fixture", fp != NULL);
    if (!fp) { unlink(FIXTURE_PATH); ut_group_end(group); return; }

    /* ── getline() auto-allocates on first call ─────────────────────── */
    char *line = NULL;
    size_t cap = 0;
    ssize_t rc = getline(&line, &cap, fp);
    TEST(group, "getline 1 returns 6 (\"alpha\\n\")", rc == 6);
    TEST(group, "line buffer allocated",             line != NULL);
    TEST(group, "cap >= initial 128",                cap >= 128);
    TEST(group, "line 1 contents == \"alpha\\n\"",   rc == 6 && memcmp(line, "alpha\n", 6) == 0);
    TEST(group, "line 1 NUL-terminated",             rc == 6 && line[6] == '\0');

    /* ── getline() reuses buffer for next line ──────────────────────── */
    char *saved_ptr = line;
    size_t saved_cap = cap;
    rc = getline(&line, &cap, fp);
    TEST(group, "getline 2 returns 5 (\"beta\\n\")", rc == 5);
    TEST(group, "buffer not reallocated yet",         line == saved_ptr && cap == saved_cap);
    TEST(group, "line 2 contents == \"beta\\n\"",     rc == 5 && memcmp(line, "beta\n", 5) == 0);

    /* ── Long line: 200 'x' + '\n' = 201 bytes; forces realloc beyond 128 ── */
    rc = getline(&line, &cap, fp);
    TEST(group, "getline 3 returns 201 (200×x + \\n)", rc == 201);
    TEST(group, "cap grew past 128",                   cap > 128);
    TEST(group, "long line ends in \\n",               rc == 201 && line[200] == '\n');
    TEST(group, "long line first byte is 'x'",         rc == 201 && line[0] == 'x');

    /* ── Last unterminated line returned without newline ────────────── */
    rc = getline(&line, &cap, fp);
    TEST(group, "getline 4 returns 4 (\"last\")",       rc == 4);
    TEST(group, "line 4 contents == \"last\"",          rc == 4 && memcmp(line, "last", 4) == 0);
    TEST(group, "line 4 NUL-terminated at offset 4",    rc == 4 && line[4] == '\0');

    /* ── EOF: getline returns -1 ────────────────────────────────────── */
    rc = getline(&line, &cap, fp);
    TEST(group, "getline at EOF returns -1", rc == -1);

    free(line);
    fclose(fp);

    /* ── getdelim() with custom delimiter ':' ───────────────────────── */
    if (write_fixture(FIXTURE_PATH, "key1:value1:key2:value2", 23) == 0) {
        fp = fopen(FIXTURE_PATH, "r");
        if (fp) {
            char *tok = NULL;
            size_t tcap = 0;
            rc = getdelim(&tok, &tcap, ':', fp);
            TEST(group, "getdelim ':' token 1 returns 5 (\"key1:\")",
                 rc == 5 && memcmp(tok, "key1:", 5) == 0);
            rc = getdelim(&tok, &tcap, ':', fp);
            TEST(group, "getdelim ':' token 2 returns 7 (\"value1:\")",
                 rc == 7 && memcmp(tok, "value1:", 7) == 0);
            /* skip the rest to EOF */
            while (getdelim(&tok, &tcap, ':', fp) != -1) { /* drain */ }
            free(tok);
            fclose(fp);
        }
    }

    /* ── getline argument validation ────────────────────────────────── */
    char *np = NULL;
    size_t nz = 0;
    errno = 0;
    rc = getline(NULL, &nz, NULL);
    TEST(group, "getline(NULL, _, NULL) returns -1", rc == -1);
    TEST(group, "errno == EINVAL on bad args",      errno == EINVAL);
    (void)np;

    unlink(FIXTURE_PATH);
    ut_group_end(group);
}

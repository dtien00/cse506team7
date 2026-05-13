/*
 * test_libc_stdio.c — comprehensive stdio.h conformance tests
 *
 * Tests sprintf/snprintf/sscanf format specifiers, FILE* I/O (fopen, fread,
 * fwrite, fseek, ftell, rewind, fclose), and the standard stream helpers.
 *
 * Only functionality reachable via the current kernel (tmpfs write + read)
 * is tested live; anything unimplemented is marked SKIP.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <unistd.h>
#include "us_test_common.h"

/* ================================================================== groups */

/* ---------- sprintf / snprintf / vsprintf specifiers --------------- */
static void test_sprintf_fmt(us_test_group_t *g) {
    char buf[128];
    int n;

    ut_group_begin(g, "sprintf formats");

    /* %d / %i */
    n = sprintf(buf, "%d", 0);
    TEST(g, "%d zero", n == 1 && strcmp(buf, "0") == 0);

    n = sprintf(buf, "%d", -1);
    TEST(g, "%d negative", n == 2 && strcmp(buf, "-1") == 0);

    n = sprintf(buf, "%i", 2147483647);
    TEST(g, "%i INT_MAX", strcmp(buf, "2147483647") == 0);

    /* %u */
    n = sprintf(buf, "%u", 0u);
    TEST(g, "%u zero", n == 1 && strcmp(buf, "0") == 0);

    sprintf(buf, "%u", 4294967295u);
    TEST(g, "%u UINT_MAX", strcmp(buf, "4294967295") == 0);

    /* %o */
    sprintf(buf, "%o", 8u);
    TEST(g, "%o octal 8 -> 10", strcmp(buf, "10") == 0);

    sprintf(buf, "%o", 255u);
    TEST(g, "%o octal 255 -> 377", strcmp(buf, "377") == 0);

    /* %x / %X */
    sprintf(buf, "%x", 0xdeadbeefu);
    TEST(g, "%x lowercase hex", strcmp(buf, "deadbeef") == 0);

    sprintf(buf, "%X", 0xCAFEu);
    TEST(g, "%X uppercase hex", strcmp(buf, "CAFE") == 0);

    /* %s */
    sprintf(buf, "%s", "hello");
    TEST(g, "%s string", strcmp(buf, "hello") == 0);

    sprintf(buf, "%s", "");
    TEST(g, "%s empty string", strcmp(buf, "") == 0);

    /* %c */
    sprintf(buf, "%c", 'A');
    TEST(g, "%c char", buf[0] == 'A' && buf[1] == '\0');

    /* %% */
    sprintf(buf, "100%%");
    TEST(g, "%% literal percent", strcmp(buf, "100%") == 0);

    /* %ld / %lu */
    sprintf(buf, "%ld", -123456789L);
    TEST(g, "%ld long negative", strcmp(buf, "-123456789") == 0);

    sprintf(buf, "%lu", 123456789ul);
    TEST(g, "%lu unsigned long", strcmp(buf, "123456789") == 0);

    /* %lld / %llu */
    sprintf(buf, "%lld", -1LL);
    TEST(g, "%lld long long -1", strcmp(buf, "-1") == 0);

    sprintf(buf, "%llu", 18446744073709551615ULL);
    TEST(g, "%llu ULLONG_MAX", strcmp(buf, "18446744073709551615") == 0);

    /* width and zero-padding */
    sprintf(buf, "%05d", 42);
    TEST(g, "%05d zero-padded", strcmp(buf, "00042") == 0);

    sprintf(buf, "%8d", 7);
    TEST(g, "%8d right-justified", strcmp(buf, "       7") == 0);

    sprintf(buf, "%-8d|", 7);
    TEST(g, "%-8d left-justified", strcmp(buf, "7       |") == 0);

    /* precision on strings */
    sprintf(buf, "%.3s", "abcdef");
    TEST(g, "%.3s truncates string", strcmp(buf, "abc") == 0);

    /* multiple args */
    n = sprintf(buf, "%s=%d", "val", 99);
    TEST(g, "multiple args", n > 0 && strcmp(buf, "val=99") == 0);

    /* %p (pointer) — just verify non-empty, not the exact value */
    void *ptr = (void *)0xABCD;
    n = sprintf(buf, "%p", ptr);
    TEST(g, "%p produces output", n > 0 && buf[0] != '\0');

    ut_group_end(g);
}

static void test_snprintf_limits(us_test_group_t *g) {
    char buf[16];
    int n;

    ut_group_begin(g, "snprintf limits");

    /* exact fit */
    n = snprintf(buf, 6, "hello");
    TEST(g, "snprintf exact fit", n == 5 && strcmp(buf, "hello") == 0);

    /* truncation: returns full (would-be) length */
    n = snprintf(buf, 4, "hello");
    TEST(g, "snprintf truncation return value", n == 5);
    TEST(g, "snprintf truncated content", strcmp(buf, "hel") == 0);

    /* size == 1: only NUL written */
    buf[0] = 'X';
    n = snprintf(buf, 1, "abc");
    TEST(g, "snprintf size=1 writes NUL", buf[0] == '\0');
    TEST(g, "snprintf size=1 returns full length", n == 3);

    /* size == 0: nothing written */
    buf[0] = 'Z';
    n = snprintf(buf, 0, "abc");
    TEST(g, "snprintf size=0 no write", buf[0] == 'Z');
    TEST(g, "snprintf size=0 returns full length", n == 3);

    ut_group_end(g);
}

static void test_sscanf(us_test_group_t *g) {
    int i;
    unsigned u;
    int j;
    char s[32];
    int n;

    ut_group_begin(g, "sscanf");

    n = sscanf("42", "%d", &i);
    TEST(g, "sscanf %d", n == 1 && i == 42);

    n = sscanf("-99", "%d", &i);
    TEST(g, "sscanf %d negative", n == 1 && i == -99);

    n = sscanf("0xff", "%x", &u);
    TEST(g, "sscanf %x", n == 1 && u == 255u);

    n = sscanf("  123", "%d", &i);
    TEST(g, "sscanf skips leading whitespace", n == 1 && i == 123);

    n = sscanf("hello world", "%s", s);
    TEST(g, "sscanf %s stops at space", n == 1 && strcmp(s, "hello") == 0);

    n = sscanf("10 20", "%d %d", &i, &j);
    (void)j;
    TEST(g, "sscanf two integers", n == 2 && i == 10);

    /* mismatch: no match */
    n = sscanf("abc", "%d", &i);
    TEST(g, "sscanf no match returns 0", n == 0);

    ut_group_end(g);
}

/* ---------- FILE* I/O ---------------------------------------------- */
static void test_fopen_fclose(us_test_group_t *g) {
    FILE *f;

    ut_group_begin(g, "fopen/fclose");

    /* write mode creates the file */
    f = fopen("/tmp/libc_test_fc.txt", "w");
    TEST(g, "fopen 'w' returns non-null", f != NULL);
    if (f) {
        TEST(g, "fclose returns 0", fclose(f) == 0);
    }

    /* read mode on existing file */
    f = fopen("/tmp/libc_test_fc.txt", "r");
    TEST(g, "fopen 'r' on existing file", f != NULL);
    if (f) fclose(f);

    /* read mode on missing file */
    f = fopen("/tmp/no_such_libc_file_xyz.txt", "r");
    TEST(g, "fopen 'r' on missing file returns null", f == NULL);

    ut_group_end(g);
}

static void test_fwrite_fread(us_test_group_t *g) {
    FILE *f;
    char wbuf[] = "Hello, fread/fwrite!";
    char rbuf[64];
    size_t wn, rn;

    ut_group_begin(g, "fwrite/fread");

    f = fopen("/tmp/libc_test_rw.txt", "w");
    TEST(g, "fopen write", f != NULL);
    if (!f) { ut_group_end(g); return; }

    wn = fwrite(wbuf, 1, sizeof(wbuf) - 1, f);
    TEST(g, "fwrite returns byte count", (int)wn == (int)(sizeof(wbuf) - 1));
    fclose(f);

    f = fopen("/tmp/libc_test_rw.txt", "r");
    TEST(g, "fopen read-back", f != NULL);
    if (!f) { ut_group_end(g); return; }

    rn = fread(rbuf, 1, sizeof(rbuf) - 1, f);
    rbuf[rn] = '\0';
    TEST(g, "fread returns byte count", rn == wn);
    TEST(g, "fread content matches", strcmp(rbuf, wbuf) == 0);
    TEST(g, "feof after full read", feof(f) != 0);
    fclose(f);

    ut_group_end(g);
}

static void test_fgetc_fputc(us_test_group_t *g) {
    FILE *f;
    int c;

    ut_group_begin(g, "fgetc/fputc");

    f = fopen("/tmp/libc_test_gc.txt", "w");
    TEST(g, "fopen for fputc", f != NULL);
    if (!f) { ut_group_end(g); return; }

    TEST(g, "fputc 'A' returns 'A'", fputc('A', f) == 'A');
    TEST(g, "fputc 'B' returns 'B'", fputc('B', f) == 'B');
    TEST(g, "fputc '\\n' returns '\\n'", fputc('\n', f) == '\n');
    fclose(f);

    f = fopen("/tmp/libc_test_gc.txt", "r");
    TEST(g, "fopen for fgetc", f != NULL);
    if (!f) { ut_group_end(g); return; }

    c = fgetc(f);
    TEST(g, "fgetc first char 'A'", c == 'A');
    c = fgetc(f);
    TEST(g, "fgetc second char 'B'", c == 'B');
    c = fgetc(f);
    TEST(g, "fgetc third char '\\n'", c == '\n');
    c = fgetc(f);
    TEST(g, "fgetc at EOF returns EOF", c == EOF);
    fclose(f);

    ut_group_end(g);
}

static void test_fgets_fputs(us_test_group_t *g) {
    FILE *f;
    char line[64];

    ut_group_begin(g, "fgets/fputs");

    f = fopen("/tmp/libc_test_gs.txt", "w");
    TEST(g, "fopen for fputs", f != NULL);
    if (!f) { ut_group_end(g); return; }

    fputs("line one\n", f);
    fputs("line two\n", f);
    fclose(f);

    f = fopen("/tmp/libc_test_gs.txt", "r");
    TEST(g, "fopen for fgets", f != NULL);
    if (!f) { ut_group_end(g); return; }

    fgets(line, sizeof(line), f);
    TEST(g, "fgets first line", strcmp(line, "line one\n") == 0);

    fgets(line, sizeof(line), f);
    TEST(g, "fgets second line", strcmp(line, "line two\n") == 0);

    /* at EOF, fgets returns NULL */
    TEST(g, "fgets at EOF returns NULL", fgets(line, sizeof(line), f) == NULL);
    fclose(f);

    /* fgets truncates to n-1 characters */
    f = fopen("/tmp/libc_test_gs.txt", "r");
    if (f) {
        fgets(line, 5, f);  /* "line one\n" truncated to "line" + NUL */
        TEST(g, "fgets truncates to n-1", strcmp(line, "line") == 0);
        fclose(f);
    }

    ut_group_end(g);
}

static void test_fseek_ftell_rewind(us_test_group_t *g) {
    FILE *f;
    long pos;
    char buf[16];

    ut_group_begin(g, "fseek/ftell/rewind");

    f = fopen("/tmp/libc_test_seek.txt", "w");
    TEST(g, "fopen for seek test", f != NULL);
    if (!f) { ut_group_end(g); return; }
    fwrite("0123456789", 1, 10, f);
    fclose(f);

    f = fopen("/tmp/libc_test_seek.txt", "r");
    TEST(g, "fopen read for seek test", f != NULL);
    if (!f) { ut_group_end(g); return; }

    /* initial position is 0 */
    pos = ftell(f);
    TEST(g, "ftell at start is 0", pos == 0);

    /* seek to offset 5 from start */
    TEST(g, "fseek SEEK_SET returns 0", fseek(f, 5, SEEK_SET) == 0);
    pos = ftell(f);
    TEST(g, "ftell after SEEK_SET 5", pos == 5);

    /* read one byte */
    fread(buf, 1, 1, f);
    buf[1] = '\0';
    TEST(g, "read after seek", buf[0] == '5');

    /* seek relative from current */
    fseek(f, 2, SEEK_CUR);
    pos = ftell(f);
    TEST(g, "ftell after SEEK_CUR +2", pos == 8);

    /* seek from end */
    fseek(f, -3, SEEK_END);
    pos = ftell(f);
    TEST(g, "ftell after SEEK_END -3", pos == 7);

    /* rewind */
    rewind(f);
    pos = ftell(f);
    TEST(g, "ftell after rewind is 0", pos == 0);

    fclose(f);

    ut_group_end(g);
}

static void test_fprintf(us_test_group_t *g) {
    FILE *f;
    char rbuf[64];
    size_t n;

    ut_group_begin(g, "fprintf");

    f = fopen("/tmp/libc_test_fprintf.txt", "w");
    TEST(g, "fopen for fprintf", f != NULL);
    if (!f) { ut_group_end(g); return; }

    fprintf(f, "num=%d str=%s\n", 42, "hello");
    fclose(f);

    f = fopen("/tmp/libc_test_fprintf.txt", "r");
    TEST(g, "fopen read-back fprintf", f != NULL);
    if (!f) { ut_group_end(g); return; }

    n = fread(rbuf, 1, sizeof(rbuf) - 1, f);
    rbuf[n] = '\0';
    TEST(g, "fprintf output correct", strcmp(rbuf, "num=42 str=hello\n") == 0);
    fclose(f);

    ut_group_end(g);
}

static void test_fileno(us_test_group_t *g) {
    FILE *f;

    ut_group_begin(g, "fileno");

    /* standard streams */
    TEST(g, "fileno(stdin)==0", fileno(stdin) == 0);
    TEST(g, "fileno(stdout)==1", fileno(stdout) == 1);
    TEST(g, "fileno(stderr)==2", fileno(stderr) == 2);

    f = fopen("/tmp/libc_test_fileno.txt", "w");
    TEST(g, "fileno opened file >= 3", f != NULL && fileno(f) >= 3);
    if (f) fclose(f);

    ut_group_end(g);
}

/* ================================================================ entry point */

/* ---------- getline / getdelim ----------------------------------- */
static void test_getline_getdelim(us_test_group_t *g) {
    FILE *f;
    const char *path = "/tmp/libc_test_getline.txt";

    ut_group_begin(g, "getline/getdelim");

    /* Create a multi-line file */
    f = fopen(path, "w");
    TEST(g, "fopen write for getline source", f != NULL);
    if (!f) { ut_group_end(g); return; }
    fputs("alpha\n", f);
    fputs("beta\n", f);
    fputs("no-newline-tail", f);
    fclose(f);

    f = fopen(path, "r");
    TEST(g, "fopen read for getline source", f != NULL);
    if (!f) { ut_group_end(g); return; }

    char  *line = NULL;
    size_t cap  = 0;
    ssize_t n;

    /* getline allocates on first call */
    n = getline(&line, &cap, f);
    TEST(g, "getline first read returns 6 chars (incl \\n)", n == 6);
    TEST(g, "getline allocated buffer", line != NULL && cap > 0);
    TEST(g, "getline first line content", line && strcmp(line, "alpha\n") == 0);

    n = getline(&line, &cap, f);
    TEST(g, "getline second line", n == 5 && strcmp(line, "beta\n") == 0);

    /* trailing line without newline */
    n = getline(&line, &cap, f);
    TEST(g, "getline trailing-no-newline returns chars",
         n == (ssize_t)strlen("no-newline-tail"));
    TEST(g, "getline trailing content matches",
         strcmp(line, "no-newline-tail") == 0);

    /* EOF: returns -1 */
    n = getline(&line, &cap, f);
    TEST(g, "getline at EOF returns -1", n == -1);

    fclose(f);

    /* getdelim with custom delimiter */
    f = fopen(path, "w");
    if (f) { fputs("one|two|three", f); fclose(f); }
    f = fopen(path, "r");
    TEST(g, "fopen for getdelim", f != NULL);
    if (f) {
        n = getdelim(&line, &cap, '|', f);
        TEST(g, "getdelim first token incl delim", n == 4);
        TEST(g, "getdelim first content", strcmp(line, "one|") == 0);
        n = getdelim(&line, &cap, '|', f);
        TEST(g, "getdelim second token", n == 4 && strcmp(line, "two|") == 0);
        n = getdelim(&line, &cap, '|', f);
        TEST(g, "getdelim trailing no-delim", n == 5 && strcmp(line, "three") == 0);
        fclose(f);
    }

    free(line);
    unlink(path);

    ut_group_end(g);
}

/* ================================================================ entry point */

void run_test_libc_stdio(us_test_group_t groups[], int *idx) {
    test_sprintf_fmt       (&groups[(*idx)++]);
    test_snprintf_limits   (&groups[(*idx)++]);
    test_sscanf            (&groups[(*idx)++]);
    test_fopen_fclose      (&groups[(*idx)++]);
    test_fwrite_fread      (&groups[(*idx)++]);
    test_fgetc_fputc       (&groups[(*idx)++]);
    test_fgets_fputs       (&groups[(*idx)++]);
    test_fseek_ftell_rewind(&groups[(*idx)++]);
    test_fprintf           (&groups[(*idx)++]);
    test_fileno            (&groups[(*idx)++]);
    test_getline_getdelim  (&groups[(*idx)++]);
}

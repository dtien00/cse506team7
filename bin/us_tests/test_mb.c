/*
 * test_mb.c — Regression test for POSIX C-locale mblen() / mbtowc() / wctomb().
 *
 * All three are declared in <stdlib.h>:66-68; before this change libc.a
 * had no body for any of them.  Covers the POSIX-conformant single-byte
 * (MB_CUR_MAX == 1) behavior:
 *
 *   - mblen(NULL, _)  returns 0  (no state-dependent encoding query)
 *   - mblen("",  n)   returns 0  (NUL is length 0)
 *   - mblen("a", n)   returns 1  for any n >= 1
 *   - mblen("a", 0)   returns -1 (can't form a char from zero bytes)
 *
 *   - mbtowc(NULL, _, _) returns 0
 *   - mbtowc(pwc, "", _) returns 0, *pwc == 0
 *   - mbtowc(pwc, "X", n>=1) returns 1, *pwc == 'X'
 *   - mbtowc(_, _, 0) returns -1
 *
 *   - wctomb(NULL, _)  returns 0
 *   - wctomb(buf, 'A') returns 1, buf[0] == 'A'
 *   - wctomb(buf, 0x1234) returns -1 (out of single-byte range), errno EILSEQ
 */

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "us_test_common.h"

void run_test_mb(us_test_group_t *group) {
    ut_group_begin(group, "mb");

    /* ── MB_CUR_MAX is the C-locale constant 1 per <stdlib.h> ────────── */
    TEST(group, "MB_CUR_MAX == 1 (C locale)", MB_CUR_MAX == 1);

    /* ── mblen ───────────────────────────────────────────────────────── */
    TEST(group, "mblen(NULL, 0)    == 0 (state query)",  mblen(NULL, 0) == 0);
    TEST(group, "mblen(NULL, 4)    == 0 (state query)",  mblen(NULL, 4) == 0);
    TEST(group, "mblen(\"\", 4)    == 0 (NUL byte)",     mblen("", 4)   == 0);
    TEST(group, "mblen(\"a\", 1)   == 1 (one-byte char)", mblen("a", 1) == 1);
    TEST(group, "mblen(\"abc\", 3) == 1 (first byte)",   mblen("abc", 3) == 1);
    TEST(group, "mblen(\"\\x80\", 1) == 1 (high byte)",  mblen("\x80", 1) == 1);
    TEST(group, "mblen(\"a\", 0)   == -1 (no bytes)",    mblen("a", 0) == -1);

    /* ── mbtowc ──────────────────────────────────────────────────────── */
    wchar_t wc = (wchar_t)-1;
    TEST(group, "mbtowc(NULL, NULL, 0) == 0", mbtowc(NULL, NULL, 0) == 0);
    TEST(group, "mbtowc(&wc, NULL, 0)  == 0", mbtowc(&wc,  NULL, 0) == 0);

    wc = (wchar_t)-1;
    int rc = mbtowc(&wc, "", 4);
    TEST(group, "mbtowc(&wc, \"\", 4) == 0",  rc == 0);
    TEST(group, "mbtowc on NUL stores 0 in *pwc", wc == 0);

    wc = (wchar_t)-1;
    rc = mbtowc(&wc, "X", 1);
    TEST(group, "mbtowc(&wc, \"X\", 1) == 1", rc == 1);
    TEST(group, "mbtowc('X') stores 'X' in *pwc", wc == (wchar_t)'X');

    /* High byte must round-trip as unsigned (no sign extension). */
    wc = (wchar_t)-1;
    rc = mbtowc(&wc, "\x80", 1);
    TEST(group, "mbtowc(\"\\x80\", 1) returns 1", rc == 1);
    TEST(group, "mbtowc(\\x80) stores 0x80 (zero-extended)", wc == (wchar_t)0x80);

    /* n == 0 must fail since no byte can be consumed. */
    rc = mbtowc(&wc, "anything", 0);
    TEST(group, "mbtowc(_, _, 0) == -1", rc == -1);

    /* pwc == NULL is legal — just decode without storing. */
    rc = mbtowc(NULL, "Y", 1);
    TEST(group, "mbtowc(NULL, \"Y\", 1) == 1 (decode-only)", rc == 1);

    /* ── wctomb ──────────────────────────────────────────────────────── */
    TEST(group, "wctomb(NULL, 0)   == 0 (state query)", wctomb(NULL, 0) == 0);
    TEST(group, "wctomb(NULL, 'A') == 0 (state query)", wctomb(NULL, (wchar_t)'A') == 0);

    char buf[4] = {0};
    rc = wctomb(buf, (wchar_t)'A');
    TEST(group, "wctomb(buf, 'A') == 1",   rc == 1);
    TEST(group, "wctomb stored 'A' in buf", buf[0] == 'A');

    buf[0] = 0;
    rc = wctomb(buf, (wchar_t)0xFF);
    TEST(group, "wctomb(buf, 0xFF) == 1",  rc == 1);
    TEST(group, "wctomb stored 0xFF in buf", (unsigned char)buf[0] == 0xFF);

    /* Out-of-range wchar → -1 + errno EILSEQ. */
    errno = 0;
    rc = wctomb(buf, (wchar_t)0x1234);
    TEST(group, "wctomb(buf, 0x1234) == -1",   rc == -1);
    TEST(group, "wctomb out-of-range sets errno EILSEQ", errno == EILSEQ);

    /* ── Round-trip: encode 'Z' and decode it back ───────────────────── */
    char rt[2] = {0};
    int enc_rc = wctomb(rt, (wchar_t)'Z');
    wchar_t dec_wc = 0;
    int dec_rc = mbtowc(&dec_wc, rt, 1);
    TEST(group, "round-trip wctomb/mbtowc preserves byte",
         enc_rc == 1 && dec_rc == 1 && dec_wc == (wchar_t)'Z');

    ut_group_end(group);
}

/*
 * mb.c — POSIX multibyte conversions for the C locale.
 *
 * <stdlib.h>:66-68 declares mblen / mbtowc / wctomb with no body in libc.
 * Without these, third-party tools that pull in any of them (TCC's
 * source-identifier handling, BusyBox `tr` / `expand`, micropython's
 * decimal printing) fail to link.
 *
 * SBUnix has no real locale support — every char is one byte (the POSIX
 * "C" locale, with MB_CUR_MAX == 1 per <stdlib.h>:16).  The implementations
 * below therefore degenerate to "treat each byte as one wide character",
 * which is exactly what POSIX permits when MB_CUR_MAX is 1.
 *
 * Conformance notes from POSIX.1-2017:
 *   - mblen(NULL, _)  returns 0 because the C locale has no state-dependent
 *     encoding.
 *   - mbtowc(NULL, _) likewise returns 0 (state query).
 *   - wctomb(NULL)    same — return 0.
 *   - Any single byte > 0xFF passed to wctomb cannot be represented in
 *     one byte; return -1 and set errno to EILSEQ.
 */

#include "stdlib.h"
#include "errno.h"

int mblen(const char *s, size_t n) {
    if (s == NULL) return 0;             /* "no state-dependent encoding" query */
    if (n == 0)    return -1;            /* can't read a byte from an empty buffer */
    return *s ? 1 : 0;                   /* NUL is length 0, anything else is one byte */
}

int mbtowc(wchar_t *restrict pwc, const char *restrict s, size_t n) {
    if (s == NULL) return 0;
    if (n == 0)    return -1;
    unsigned char b = (unsigned char)*s;
    if (pwc) *pwc = (wchar_t)b;
    return b ? 1 : 0;
}

int wctomb(char *s, wchar_t wc) {
    if (s == NULL) return 0;
    if ((unsigned)wc > 0xFF) {
        errno = EILSEQ;
        return -1;
    }
    *s = (char)wc;
    return 1;
}

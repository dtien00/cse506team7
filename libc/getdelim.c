/*
 * getdelim.c — POSIX line/delimited input.
 *
 * <stdio.h>:114-115 declares getdelim() and getline() with no body in libc.
 * BusyBox applets and any line-oriented tool that calls either would fail
 * to link.  Pure libc, no kernel work: reads via fgetc() and grows the
 * caller's buffer through realloc().
 *
 * POSIX semantics:
 *   - *lineptr may be NULL initially; first call allocates.
 *   - *n is updated whenever the buffer grows.
 *   - Returns the number of bytes copied (delimiter included, NUL excluded),
 *     or -1 on EOF before any byte was read, or on error.
 *   - On any return, *lineptr is NUL-terminated when non-NULL.
 */

#include "stdio.h"
#include "stdlib.h"
#include "errno.h"

#define GETDELIM_INITIAL  128
#define GETDELIM_GROWTH   2

ssize_t getdelim(char **restrict lineptr, size_t *restrict n,
                 int delim, FILE *restrict stream)
{
    if (!lineptr || !n || !stream) {
        errno = EINVAL;
        return -1;
    }

    if (*lineptr == NULL || *n == 0) {
        char *buf = realloc(*lineptr, GETDELIM_INITIAL);
        if (!buf) { errno = ENOMEM; return -1; }
        *lineptr = buf;
        *n = GETDELIM_INITIAL;
    }

    size_t len = 0;
    int c;
    while ((c = fgetc(stream)) != EOF) {
        /* Need room for the new byte plus the trailing NUL. */
        if (len + 2 > *n) {
            size_t new_n = *n * GETDELIM_GROWTH;
            char *new_buf = realloc(*lineptr, new_n);
            if (!new_buf) {
                /* Best-effort terminate what we already have, then fail. */
                (*lineptr)[len] = '\0';
                errno = ENOMEM;
                return -1;
            }
            *lineptr = new_buf;
            *n = new_n;
        }
        (*lineptr)[len++] = (char)c;
        if (c == delim) break;
    }

    if (len == 0) {
        /* Either EOF on first read or read error.  POSIX: return -1 and
         * leave *lineptr buffer NUL-terminated. */
        if (*lineptr) (*lineptr)[0] = '\0';
        return -1;
    }

    (*lineptr)[len] = '\0';
    return (ssize_t)len;
}

ssize_t getline(char **restrict lineptr, size_t *restrict n,
                FILE *restrict stream)
{
    return getdelim(lineptr, n, '\n', stream);
}

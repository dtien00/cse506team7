/*
 * mkstemp.c — POSIX temporary-file / temporary-directory creators.
 *
 * <stdlib.h>:35-37 declares mkdtemp / mkstemp / mkostemp with no body in
 * libc.  TCC writes intermediate .o / .s files through mkstemp, and
 * BusyBox `mktemp` is a thin wrapper around it; both fail to link
 * without an implementation.
 *
 * Strategy (POSIX-conformant):
 *   1. Template must end with at least six trailing 'X' characters.
 *   2. Replace each 'X' with a random char from [A-Za-z0-9].
 *   3. Try to create the file (open with O_RDWR|O_CREAT|O_EXCL, mode 0600)
 *      or directory (mkdir with mode 0700).  On EEXIST, regenerate the
 *      suffix and retry; on any other error fail immediately.
 *   4. Give up after MKSTEMP_RETRIES attempts to avoid livelock when the
 *      target directory really is full of every possible suffix.
 *
 * The seed mixes gettimeofday() (sub-second variation), getpid()
 * (per-process variation), and a static xorshift state so successive
 * calls from the same process don't recompute the same byte stream.
 * SBUnix is single-user; this is not a security-grade RNG and need not
 * be — the only guarantee POSIX requires is "unique enough that the
 * race window between candidate-name generation and O_EXCL create is
 * small in practice."
 */

#include "stdlib.h"
#include "string.h"
#include "fcntl.h"
#include "unistd.h"
#include "errno.h"
#include "sys/stat.h"
#include "sys/time.h"
#include "types.h"

#define MKSTEMP_RETRIES   100
#define MKSTEMP_MIN_XS    6

static const char _alphabet[62] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";

/* xorshift64 over a state seeded once per process. */
static uint64_t _rand64(void) {
    static uint64_t state = 0;
    if (state == 0) {
        struct timeval tv;
        if (gettimeofday(&tv, NULL) != 0) {
            tv.tv_sec = 0;
            tv.tv_usec = 0;
        }
        state = ((uint64_t)tv.tv_sec << 20)
              ^ ((uint64_t)tv.tv_usec)
              ^ ((uint64_t)getpid() << 32);
        if (state == 0) state = 0x9e3779b97f4a7c15ULL;  /* fixed fallback */
    }
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
}

/*
 * Verify the template ends in >= 6 trailing 'X' characters.  Returns 0 on
 * success and writes the start index + length of the X-run; returns -1
 * with errno=EINVAL on a bad template.
 */
static int _find_xs(char *tmpl, size_t *xstart_out, size_t *xlen_out) {
    if (!tmpl) { errno = EINVAL; return -1; }
    size_t len = strlen(tmpl);
    if (len < MKSTEMP_MIN_XS) { errno = EINVAL; return -1; }
    size_t xlen = 0;
    while (xlen < len && tmpl[len - 1 - xlen] == 'X') xlen++;
    if (xlen < MKSTEMP_MIN_XS) { errno = EINVAL; return -1; }
    *xstart_out = len - xlen;
    *xlen_out   = xlen;
    return 0;
}

/* Replace the X-run with random characters from _alphabet. */
static void _fill_xs(char *tmpl, size_t xstart, size_t xlen) {
    uint64_t r = _rand64();
    for (size_t i = 0; i < xlen; i++) {
        tmpl[xstart + i] = _alphabet[r % 62];
        r /= 62;
        if (r == 0) r = _rand64();
    }
}

int mkstemp(char *tmpl) {
    size_t xstart, xlen;
    if (_find_xs(tmpl, &xstart, &xlen) < 0) return -1;
    for (int i = 0; i < MKSTEMP_RETRIES; i++) {
        _fill_xs(tmpl, xstart, xlen);
        int fd = open(tmpl, O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd >= 0) return fd;
        if (errno != EEXIST) return -1;
    }
    errno = EEXIST;
    return -1;
}

int mkostemp(char *tmpl, int flags) {
    size_t xstart, xlen;
    if (_find_xs(tmpl, &xstart, &xlen) < 0) return -1;
    /* Caller may pass O_APPEND / O_CLOEXEC / O_SYNC etc.  Strip any
     * conflicting access-mode bits and force the mandatory ones. */
    flags = (flags & ~(O_RDONLY | O_WRONLY | O_RDWR | O_CREAT | O_EXCL))
          | O_RDWR | O_CREAT | O_EXCL;
    for (int i = 0; i < MKSTEMP_RETRIES; i++) {
        _fill_xs(tmpl, xstart, xlen);
        int fd = open(tmpl, flags, 0600);
        if (fd >= 0) return fd;
        if (errno != EEXIST) return -1;
    }
    errno = EEXIST;
    return -1;
}

char *mkdtemp(char *tmpl) {
    size_t xstart, xlen;
    if (_find_xs(tmpl, &xstart, &xlen) < 0) return NULL;
    for (int i = 0; i < MKSTEMP_RETRIES; i++) {
        _fill_xs(tmpl, xstart, xlen);
        if (mkdir(tmpl, 0700) == 0) return tmpl;
        if (errno != EEXIST) return NULL;
    }
    errno = EEXIST;
    return NULL;
}

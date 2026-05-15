/*
 * pwd.c — POSIX password-database lookups.
 *
 * <pwd.h> declares getpwuid / getpwnam / getpwuid_r / getpwnam_r with no
 * body in libc.  BusyBox `ls -l`, `id`, `whoami`, `login`, `su`,
 * `passwd`, and micropython's `pwd` module all reference these.
 *
 * SBUnix is effectively single-user (the kernel's getuid returns 0), so
 * the entire user database is one entry: "root".  Unknown lookups
 * return NULL with errno=ENOENT.  When SBUnix grows real users this
 * file should switch to parsing /etc/passwd; callers don't need to
 * change.
 *
 * The non-_r entry points return a pointer into a static struct; the
 * _r variants copy strings into the caller's buffer and update the
 * pointers in the supplied struct, returning ERANGE if the buffer is
 * too small.  SBUnix has no threads so the static-buffer approach in
 * the non-_r variants is safe.
 */

#include "pwd.h"
#include "string.h"
#include "errno.h"
#include "stddef.h"

static struct passwd _root_pw = {
    .pw_name   = "root",
    .pw_passwd = "x",
    .pw_uid    = 0,
    .pw_gid    = 0,
    .pw_gecos  = "root",
    .pw_dir    = "/",
    .pw_shell  = "/bin/sh",
};

struct passwd *getpwuid(uid_t uid) {
    if (uid == 0) return &_root_pw;
    errno = ENOENT;
    return NULL;
}

struct passwd *getpwnam(const char *name) {
    if (name && strcmp(name, "root") == 0) return &_root_pw;
    errno = ENOENT;
    return NULL;
}

/* Stash a NUL-terminated string at *offp inside buf, advance *offp.
 * Returns the new pointer or NULL if there isn't room.  The trailing
 * NUL is always copied so the caller's struct holds a real C string. */
static char *_stash(char *buf, size_t buflen, size_t *offp, const char *src) {
    size_t srclen = strlen(src) + 1;
    if (*offp + srclen > buflen) return NULL;
    char *dst = buf + *offp;
    memcpy(dst, src, srclen);
    *offp += srclen;
    return dst;
}

static int _fill_pwd(struct passwd *out, char *buf, size_t buflen,
                     const struct passwd *src) {
    size_t off = 0;
    char *n  = _stash(buf, buflen, &off, src->pw_name);
    char *pw = _stash(buf, buflen, &off, src->pw_passwd);
    char *g  = _stash(buf, buflen, &off, src->pw_gecos);
    char *d  = _stash(buf, buflen, &off, src->pw_dir);
    char *sh = _stash(buf, buflen, &off, src->pw_shell);
    if (!n || !pw || !g || !d || !sh) return ERANGE;
    out->pw_name   = n;
    out->pw_passwd = pw;
    out->pw_uid    = src->pw_uid;
    out->pw_gid    = src->pw_gid;
    out->pw_gecos  = g;
    out->pw_dir    = d;
    out->pw_shell  = sh;
    return 0;
}

int getpwuid_r(uid_t uid, struct passwd *pwd, char *buf, size_t buflen,
               struct passwd **result) {
    if (!pwd || !buf || !result) return EINVAL;
    *result = NULL;
    if (uid != 0) return 0;       /* not found, not an error */
    int rc = _fill_pwd(pwd, buf, buflen, &_root_pw);
    if (rc != 0) return rc;
    *result = pwd;
    return 0;
}

int getpwnam_r(const char *name, struct passwd *pwd, char *buf, size_t buflen,
               struct passwd **result) {
    if (!name || !pwd || !buf || !result) return EINVAL;
    *result = NULL;
    if (strcmp(name, "root") != 0) return 0;
    int rc = _fill_pwd(pwd, buf, buflen, &_root_pw);
    if (rc != 0) return rc;
    *result = pwd;
    return 0;
}

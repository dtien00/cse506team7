/*
 * grp.c — POSIX group-database lookups.
 *
 * <grp.h> declares getgrgid / getgrnam / getgrgid_r / getgrnam_r with no
 * body in libc.  Pairs with pwd.c; same single-user assumption — the
 * only group SBUnix exposes is "root" (gid 0) with no extra members.
 *
 * struct group layout was previously mismatched between libc and
 * thirdparty headers (the thirdparty version had a gr_passwd field
 * that libc was missing).  libc/include/grp.h has been reconciled to
 * match the thirdparty layout so binaries linked against either set of
 * headers see the same offsets for gr_gid and gr_mem.
 */

#include "grp.h"
#include "string.h"
#include "errno.h"
#include "stddef.h"

static char *_root_grp_mem[] = { NULL };  /* empty members list */

static struct group _root_group = {
    .gr_name   = "root",
    .gr_passwd = "x",
    .gr_gid    = 0,
    .gr_mem    = _root_grp_mem,
};

struct group *getgrgid(gid_t gid) {
    if (gid == 0) return &_root_group;
    errno = ENOENT;
    return NULL;
}

struct group *getgrnam(const char *name) {
    if (name && strcmp(name, "root") == 0) return &_root_group;
    errno = ENOENT;
    return NULL;
}

static char *_stash(char *buf, size_t buflen, size_t *offp, const char *src) {
    size_t srclen = strlen(src) + 1;
    if (*offp + srclen > buflen) return NULL;
    char *dst = buf + *offp;
    memcpy(dst, src, srclen);
    *offp += srclen;
    return dst;
}

static int _fill_grp(struct group *out, char *buf, size_t buflen,
                     const struct group *src) {
    size_t off = 0;
    char *nm = _stash(buf, buflen, &off, src->gr_name);
    char *pw = _stash(buf, buflen, &off, src->gr_passwd);
    if (!nm || !pw) return ERANGE;

    /* Align off to a pointer boundary so gr_mem[] is properly aligned. */
    while (off & (sizeof(char *) - 1)) {
        if (off >= buflen) return ERANGE;
        buf[off++] = 0;
    }
    /* Empty members list: a single NULL terminator slot. */
    if (off + sizeof(char *) > buflen) return ERANGE;
    char **mem = (char **)(buf + off);
    mem[0] = NULL;
    off += sizeof(char *);

    out->gr_name   = nm;
    out->gr_passwd = pw;
    out->gr_gid    = src->gr_gid;
    out->gr_mem    = mem;
    return 0;
}

int getgrgid_r(gid_t gid, struct group *grp, char *buf, size_t buflen,
               struct group **result) {
    if (!grp || !buf || !result) return EINVAL;
    *result = NULL;
    if (gid != 0) return 0;
    int rc = _fill_grp(grp, buf, buflen, &_root_group);
    if (rc != 0) return rc;
    *result = grp;
    return 0;
}

int getgrnam_r(const char *name, struct group *grp, char *buf, size_t buflen,
               struct group **result) {
    if (!name || !grp || !buf || !result) return EINVAL;
    *result = NULL;
    if (strcmp(name, "root") != 0) return 0;
    int rc = _fill_grp(grp, buf, buflen, &_root_group);
    if (rc != 0) return rc;
    *result = grp;
    return 0;
}

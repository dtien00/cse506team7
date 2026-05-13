/* libc/passwd.c — /etc/passwd lookups (POSIX getpwuid/getpwnam) */
#include "pwd.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "errno.h"

#define PWLINE_MAX 256

static struct passwd s_pw_entry;
static char          s_pw_buf[PWLINE_MAX];

/* Parse one /etc/passwd line into `pwd`, using `buf` (size `buflen`) as the
   backing store for the string fields. Returns 0 on success or an errno on
   error. Format: name:passwd:uid:gid:gecos:dir:shell */
static int parse_line(char *line, struct passwd *pwd,
                      char *buf, size_t buflen) {
    size_t llen = strlen(line);
    while (llen && (line[llen - 1] == '\n' || line[llen - 1] == '\r'))
        line[--llen] = '\0';
    if (llen == 0) return EINVAL;

    char *fields[7];
    char *p = line;
    for (int i = 0; i < 7; i++) {
        fields[i] = p;
        if (i < 6) {
            char *colon = strchr(p, ':');
            if (!colon) return EINVAL;
            *colon = '\0';
            p = colon + 1;
        }
    }

    size_t total = 0;
    char *out[5];
    int   idx[5] = { 0, 1, 4, 5, 6 };
    for (int i = 0; i < 5; i++) {
        size_t flen = strlen(fields[idx[i]]) + 1;
        if (total + flen > buflen) return ERANGE;
        out[i] = buf + total;
        memcpy(buf + total, fields[idx[i]], flen);
        total += flen;
    }

    pwd->pw_name   = out[0];
    pwd->pw_passwd = out[1];
    pwd->pw_uid    = (uid_t)atoi(fields[2]);
    pwd->pw_gid    = (gid_t)atoi(fields[3]);
    pwd->pw_gecos  = out[2];
    pwd->pw_dir    = out[3];
    pwd->pw_shell  = out[4];
    return 0;
}

static int find_passwd(int by_uid, uid_t uid, const char *name,
                       struct passwd *pwd, char *buf, size_t buflen,
                       struct passwd **result) {
    *result = NULL;
    FILE *f = fopen("/etc/passwd", "r");
    if (!f) return errno ? errno : ENOENT;

    char line[PWLINE_MAX];
    while (fgets(line, sizeof(line), f)) {
        char tmp[PWLINE_MAX];
        memcpy(tmp, line, sizeof(tmp));
        int rc = parse_line(tmp, pwd, buf, buflen);
        if (rc == ERANGE) { fclose(f); return ERANGE; }
        if (rc != 0) continue;
        int match = by_uid ? (pwd->pw_uid == uid)
                           : (name && strcmp(pwd->pw_name, name) == 0);
        if (match) {
            *result = pwd;
            fclose(f);
            return 0;
        }
    }
    fclose(f);
    return 0;
}

struct passwd *getpwuid(uid_t uid) {
    struct passwd *r;
    int rc = find_passwd(1, uid, NULL,
                         &s_pw_entry, s_pw_buf, sizeof(s_pw_buf), &r);
    if (rc != 0) errno = rc;
    return r;
}

struct passwd *getpwnam(const char *name) {
    if (!name) { errno = EINVAL; return NULL; }
    struct passwd *r;
    int rc = find_passwd(0, 0, name,
                         &s_pw_entry, s_pw_buf, sizeof(s_pw_buf), &r);
    if (rc != 0) errno = rc;
    return r;
}

int getpwuid_r(uid_t uid, struct passwd *pwd,
               char *buf, size_t buflen, struct passwd **result) {
    return find_passwd(1, uid, NULL, pwd, buf, buflen, result);
}

int getpwnam_r(const char *name, struct passwd *pwd,
               char *buf, size_t buflen, struct passwd **result) {
    if (!name) { *result = NULL; return EINVAL; }
    return find_passwd(0, 0, name, pwd, buf, buflen, result);
}

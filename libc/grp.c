/* libc/grp.c — /etc/group lookups (POSIX getgrgid/getgrnam) */
#include "grp.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "errno.h"

#define GRLINE_MAX  256
#define GR_MEM_MAX   16

static struct group s_gr_entry;
static char         s_gr_buf[GRLINE_MAX];
static char        *s_gr_mem[GR_MEM_MAX + 1];

/* Parse one /etc/group line into `grp`, using `buf` (size `buflen`) for
   strings and `mem`/`mem_max` for the gr_mem pointer array. Format:
   name:passwd:gid:user1,user2,user3 */
static int parse_line(char *line, struct group *grp,
                      char *buf, size_t buflen,
                      char **mem, size_t mem_max) {
    size_t llen = strlen(line);
    while (llen && (line[llen - 1] == '\n' || line[llen - 1] == '\r'))
        line[--llen] = '\0';
    if (llen == 0) return EINVAL;

    char *fields[4];
    char *p = line;
    for (int i = 0; i < 4; i++) {
        fields[i] = p;
        if (i < 3) {
            char *colon = strchr(p, ':');
            if (!colon) return EINVAL;
            *colon = '\0';
            p = colon + 1;
        }
    }

    size_t total = 0;
    size_t namelen = strlen(fields[0]) + 1;
    if (total + namelen > buflen) return ERANGE;
    char *name_out = buf + total;
    memcpy(name_out, fields[0], namelen);
    total += namelen;

    size_t mem_count = 0;
    char *m = fields[3];
    if (*m) {
        for (;;) {
            char *comma = strchr(m, ',');
            size_t mlen = comma ? (size_t)(comma - m) : strlen(m);
            if (mem_count >= mem_max) return ERANGE;
            if (total + mlen + 1 > buflen) return ERANGE;
            mem[mem_count] = buf + total;
            memcpy(buf + total, m, mlen);
            buf[total + mlen] = '\0';
            total += mlen + 1;
            mem_count++;
            if (!comma) break;
            m = comma + 1;
        }
    }
    mem[mem_count] = NULL;

    grp->gr_name = name_out;
    grp->gr_gid  = (gid_t)atoi(fields[2]);
    grp->gr_mem  = mem;
    return 0;
}

static int find_group(int by_gid, gid_t gid, const char *name,
                      struct group *grp, char *buf, size_t buflen,
                      char **mem, size_t mem_max,
                      struct group **result) {
    *result = NULL;
    FILE *f = fopen("/etc/group", "r");
    if (!f) return errno ? errno : ENOENT;

    char line[GRLINE_MAX];
    while (fgets(line, sizeof(line), f)) {
        char tmp[GRLINE_MAX];
        memcpy(tmp, line, sizeof(tmp));
        int rc = parse_line(tmp, grp, buf, buflen, mem, mem_max);
        if (rc == ERANGE) { fclose(f); return ERANGE; }
        if (rc != 0) continue;
        int match = by_gid ? (grp->gr_gid == gid)
                           : (name && strcmp(grp->gr_name, name) == 0);
        if (match) {
            *result = grp;
            fclose(f);
            return 0;
        }
    }
    fclose(f);
    return 0;
}

struct group *getgrgid(gid_t gid) {
    struct group *r;
    int rc = find_group(1, gid, NULL,
                        &s_gr_entry, s_gr_buf, sizeof(s_gr_buf),
                        s_gr_mem, GR_MEM_MAX, &r);
    if (rc != 0) errno = rc;
    return r;
}

struct group *getgrnam(const char *name) {
    if (!name) { errno = EINVAL; return NULL; }
    struct group *r;
    int rc = find_group(0, 0, name,
                        &s_gr_entry, s_gr_buf, sizeof(s_gr_buf),
                        s_gr_mem, GR_MEM_MAX, &r);
    if (rc != 0) errno = rc;
    return r;
}

int getgrgid_r(gid_t gid, struct group *grp,
               char *buf, size_t buflen, struct group **result) {
    /* Caller-supplied buf must hold both the group string fields and the
       gr_mem pointer array. We reserve the trailing portion of buf for the
       pointer array (GR_MEM_MAX + 1 pointers). */
    if (buflen < (GR_MEM_MAX + 1) * sizeof(char *)) {
        *result = NULL;
        return ERANGE;
    }
    size_t ptr_bytes = (GR_MEM_MAX + 1) * sizeof(char *);
    char **mem = (char **)(buf + buflen - ptr_bytes);
    return find_group(1, gid, NULL, grp, buf, buflen - ptr_bytes,
                      mem, GR_MEM_MAX, result);
}

int getgrnam_r(const char *name, struct group *grp,
               char *buf, size_t buflen, struct group **result) {
    if (!name) { *result = NULL; return EINVAL; }
    if (buflen < (GR_MEM_MAX + 1) * sizeof(char *)) {
        *result = NULL;
        return ERANGE;
    }
    size_t ptr_bytes = (GR_MEM_MAX + 1) * sizeof(char *);
    char **mem = (char **)(buf + buflen - ptr_bytes);
    return find_group(0, 0, name, grp, buf, buflen - ptr_bytes,
                      mem, GR_MEM_MAX, result);
}

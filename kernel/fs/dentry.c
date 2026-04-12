#include "fs/dentry.h"
#include "fs/vfs.h" // for vnode_t full definition
#include "kernel/printk.h"
#include "lib/string.h"
#include "mm/kmalloc.h"
#include "mm/pool.h"
#include "errno.h"

POOL_DECLARE(dentry_pool);
#define MAX_DENTRIES 1024

void dentry_pool_init(void) { POOL_INIT(dentry_pool, dentry_t, MAX_DENTRIES); }

// ── Lifecycle ────────────────────────────────────────────────────

dentry_t *dentry_alloc(const char *name, vnode_t *vnode) {
    if (!name)
        return NULL;

    dentry_t *d = POOL_ALLOC(dentry_pool, dentry_t);
    if (!d)
        return NULL;

    int i = 0;
    while (i < 255 && name[i]) {
        d->d_name[i] = name[i];
        i++;
    }
    d->d_name[i] = '\0';

    d->d_parent = NULL;
    d->d_children = NULL;
    d->d_sibling = NULL;
    d->d_vnode = vnode;
    d->d_ref = 0;
    d->d_flags = DENTRY_NONE;

    return d;
}


void dentry_free(dentry_t *d) {
    if (!d)
        return;

    if (d->d_vnode) {
        kfree(d->d_vnode);
        d->d_vnode = NULL;
    }

    POOL_FREE(dentry_pool, d);
}

void dentry_get(dentry_t *d) {
    if (!d)
        return;

    d->d_ref++;

    if (d->d_vnode)
        d->d_vnode->ref_count++;
}

void dentry_put(dentry_t *d) {
    if (!d)
        return;

    if (d->d_ref <= 0) {
        printk("dentry_put: underflow on %s\n", d->d_name);
        return;
    }

    d->d_ref--;

    if (d->d_vnode && d->d_vnode->ref_count > 0)
        d->d_vnode->ref_count--;

    // Important: do NOT free here.
    // Cached dentries may remain in the namespace tree with d_ref == 0.
    // Actual reclamation is handled later by dentry_prune() calling dentry_free().
}

int dentry_is_root(const dentry_t *d) {
    if (!d)
        return 0;

    return (d->d_flags & DENTRY_ROOT) != 0;
}

int dentry_has_children(const dentry_t *d) {
    if (!d)
        return 0;

    return d->d_children != NULL;
}

// tree functionality

int dentry_attach(dentry_t *parent, dentry_t *child) {
    if (!parent || !child)
        return -1;

    // child must not already be attached somewhere
    if (child->d_parent || child->d_sibling)
        return -1;

    // don't attach duplicate names under the same parent
    for (dentry_t *cur = parent->d_children; cur; cur = cur->d_sibling) {
        if (strcmp(cur->d_name, child->d_name) == 0)
            return -1;
    }

    child->d_parent = parent;
    child->d_sibling = parent->d_children;
    parent->d_children = child;

    return 0;
}

void dentry_detach(dentry_t *child) {
    if (!child || !child->d_parent)
        return;

    dentry_t *parent = child->d_parent;

    dentry_t **link = &parent->d_children;
    while (*link) {
        if (*link == child) {
            *link = child->d_sibling;
            child->d_parent = NULL;
            child->d_sibling = NULL;
            return;
        }
        link = &(*link)->d_sibling;
    }

    // If we get here, tree links were inconsistent.
    printk("dentry_detach: child '%s' not found under parent\n", child->d_name);
}

// actually free the dentry from cache under strict conditions
void dentry_prune(dentry_t *d) {
    if (!d)
        return;

    // Never prune structural dentries
    if (d->d_flags & (DENTRY_ROOT | DENTRY_COVER))
        return;

    // Still externally referenced
    if (d->d_ref > 0)
        return;

    // Still part of the namespace tree
    if (d->d_parent)
        return;

    // Not a leaf
    if (d->d_children)
        return;

    dentry_free(d);
}

dentry_t *dentry_lookup_child(dentry_t *parent, const char *name) {
    if (!parent || !name)
        return NULL;

    for (dentry_t *child = parent->d_children; child; child = child->d_sibling) {
        if (strcmp(child->d_name, name) == 0)
            return child;
    }

    return NULL;
}

// ── Utilities ────────────────────────────────────────────────────

void dentry_dump(dentry_t *d) {
    if (!d) {
        printk("[dentry] NULL\n");
        return;
    }

    printk("[dentry] name='%s' d=%p parent=%p child=%p sibling=%p vnode=%p ref=%d flags=0x%x\n", d->d_name, d,
           d->d_parent, d->d_children, d->d_sibling, d->d_vnode, d->d_ref, d->d_flags);
}

// Walk d_parent chain up to root, build absolute path string
// Returns 0 on success, -1 if buf too small
int dentry_build_path(dentry_t *d, dentry_t *root, char *buf, size_t size) {
    if (!d || !buf || size == 0)
        return -1;

    // collect components walking upward
    const char *segs[128];
    int n = 0;
    dentry_t *cur = d;

    while (cur && cur != root) {
        if (cur == cur->d_parent) {
            /* Self-referencing mount root — cross to cover dentry in parent namespace */
            dentry_t *cover = vfs_find_cover_for_root(cur);
            if (!cover) break; /* global root or unknown mount */
            cur = cover;       /* jump to cover; next iteration collects its name */
        } else {
            if (n >= 128) return -1;
            segs[n++] = cur->d_name;
            cur = cur->d_parent;
        }
    }

    // build string by reversing
    char *p = buf;
    char *end = buf + size - 1;

    *p++ = '/';
    for (int i = n - 1; i >= 0 && p < end; i--) {
        const char *s = segs[i];
        while (*s && p < end)
            *p++ = *s++;
        if (i > 0 && p < end)
            *p++ = '/';
    }
    *p = '\0';
    return 0;
}

// ── Path utilities ───────────────────────────────────────────────

// Resolves input path relative to start, collapsing ., .., and repeated slashes.
// If input is absolute, start is ignored. root is the confinement boundary for "..".
int dentry_normalize_path(dentry_t *start, dentry_t *root,
                          const char *input, char *out, size_t size) {
    if (!input || !out || size == 0)
        return -EINVAL;

    char tmp[256]; // MAX_PATH
    size_t pos = 0;

    if (input[0] == '/') {
        size_t i = 0;
        while (input[i] && pos + 1 < sizeof(tmp))
            tmp[pos++] = input[i++];
        if (input[i] != '\0')
            return -ENAMETOOLONG;
        tmp[pos] = '\0';
    } else {
        char base[256];
        dentry_t *base_dentry = start ? start : root;
        if (!base_dentry)
            return -EINVAL;

        if (dentry_build_path(base_dentry, root, base, sizeof(base)) != 0)
            return -ENAMETOOLONG;

        size_t i = 0;
        while (base[i] && pos + 1 < sizeof(tmp))
            tmp[pos++] = base[i++];
        if (pos > 0 && tmp[pos - 1] != '/' && pos + 1 < sizeof(tmp))
            tmp[pos++] = '/';
        i = 0;
        while (input[i] && pos + 1 < sizeof(tmp))
            tmp[pos++] = input[i++];
        if (input[i] != '\0')
            return -ENAMETOOLONG;
        tmp[pos] = '\0';
    }

    out[0] = '/';
    size_t opos = 1;
    out[opos] = '\0';

    char *p = tmp;
    if (*p == '/')
        p++;

    while (*p) {
        char *slash = p;
        while (*slash && *slash != '/')
            slash++;

        size_t clen = (size_t)(slash - p);

        if (clen == 0) {
            // repeated slash — skip
        } else if (clen == 1 && p[0] == '.') {
            // current dir — skip
        } else if (clen == 2 && p[0] == '.' && p[1] == '.') {
            // parent dir — back up, clamped at root
            if (opos > 1) {
                opos--;
                while (opos > 1 && out[opos - 1] != '/')
                    opos--;
                if (opos > 1)
                    opos--;
                out[opos] = '\0';
            }
        } else {
            if (opos > 1) {
                if (opos + 1 >= size)
                    return -ENAMETOOLONG;
                out[opos++] = '/';
            }
            if (opos + clen >= size)
                return -ENAMETOOLONG;
            for (size_t i = 0; i < clen; i++)
                out[opos++] = p[i];
            out[opos] = '\0';
        }

        p = *slash ? slash + 1 : slash;
    }

    return 0;
}

// Splits a normalized absolute path into components in-place (writes NULs into path_buf).
// Returns component count, or -1 if max_comps exceeded.
// e.g. "/foo/bar" → comps=["foo","bar"], returns 2. "/" → returns 0.
int dentry_split_path(char *path_buf, char **comps, int max_comps) {
    if (!path_buf || !comps || max_comps <= 0)
        return -1;

    char *p = path_buf;
    if (*p == '/')
        p++;

    int n = 0;
    while (*p) {
        if (n >= max_comps)
            return -1;
        comps[n++] = p;
        while (*p && *p != '/')
            p++;
        if (*p == '/') {
            *p = '\0';
            p++;
        }
    }
    return n;
}

int dentry_get_parent_path(const char *path, char *buf, size_t size) {
    if (path[0] == '/' && path[1] == '\0')
        return -1; // root

    strncpy(buf, path, size - 1);
    buf[size - 1] = '\0';

    char *last_slash = NULL;
    for (char *p = buf; *p; p++)
        if (*p == '/')
            last_slash = p;

    if (last_slash && last_slash != buf)
        *last_slash = '\0';
    else {
        buf[0] = '/';
        buf[1] = '\0';
    }

    return 0;
}

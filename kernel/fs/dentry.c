#include "fs/vfs.h" // for vnode_t full definition
#include "kernel/printk.h"
#include "lib/string.h"
#include "mm/kmalloc.h"
#include "mm/pool.h"

POOL_DECLARE(dentry_pool);

void dentry_pool_init(void) {
    POOL_INIT(dentry_pool, dentry_t, 128);
}

// ── Lifecycle ────────────────────────────────────────────────────

void dentry_dump(dentry_t *d) {
    char d_path[256];
    if (!d) {
        printk("[dentry] d=NULL\n");
    } else {
        dentry_build_path(d, d, d_path, 256);
        printk("[dentry] path=%s (dentry=%p, d_ref=%d)\n",
               d_path, d, d->d_ref);
    }
}

dentry_t *dentry_alloc(const char *name, vnode_t *vnode, dentry_t *parent) {
    dentry_t *d = POOL_ALLOC(dentry_pool, dentry_t);
    if (!d) return NULL;

    int i = 0;
    while (i < 255 && name[i]) { d->d_name[i] = name[i]; i++; }
    d->d_name[i] = '\0';

    d->d_vnode  = vnode;
    d->d_parent = parent ? parent : d;  // root is its own parent
    d->d_ref    = 0;                    // unowned until dentry_get
    return d;
}

// some object has acquired this dentry
void dentry_get(dentry_t *d) {
    if (!d) return;
    d->d_ref++;
    if (d->d_vnode) d->d_vnode->ref_count++;
}

// some object has released this dentry
void dentry_put(dentry_t *d) {
    while (d) {
        d->d_ref--;
        if (d->d_vnode) d->d_vnode->ref_count--;

        // free vnode if nobody references it
        if (d->d_vnode && d->d_vnode->ref_count == 0) {
            kfree(d->d_vnode);
            d->d_vnode = NULL;
        }

        if (d->d_ref > 0) return;       // still held, stop here
        if (d->d_parent == d)  return;  // root — never free

        dentry_t *parent = d->d_parent;
        POOL_FREE(dentry_pool, d);
        d = parent;                     // walk up the chain
    }
}

// ── Utilities ────────────────────────────────────────────────────

// Walk d_parent chain up to root, build absolute path string
// Returns 0 on success, -1 if buf too small
int dentry_build_path(dentry_t *d, dentry_t *root,
                      char *buf, size_t size) {
    if (!d || !buf || size == 0) return -1;

    // collect components walking upward
    const char *segs[128];
    int n = 0;
    dentry_t *cur = d;
    while (cur && cur != root && cur != cur->d_parent) {
        segs[n++] = cur->d_name;
        cur = cur->d_parent;
    }

    // build string by reversing
    char *p = buf;
    char *end = buf + size - 1;

    *p++ = '/';
    for (int i = n - 1; i >= 0 && p < end; i--) {
        const char *s = segs[i];
        while (*s && p < end) *p++ = *s++;
        if (i > 0 && p < end) *p++ = '/';
    }
    *p = '\0';
    return 0;
}

int dentry_get_parent_path(const char *path, char *buf, size_t size) {
    if (path[0] == '/' && path[1] == '\0') return -1;  // root

    strncpy(buf, path, size - 1);
    buf[size - 1] = '\0';

    char *last_slash = NULL;
    for (char *p = buf; *p; p++)
        if (*p == '/') last_slash = p;

    if (last_slash && last_slash != buf)
        *last_slash = '\0';
    else {
        buf[0] = '/';
        buf[1] = '\0';
    }

    return 0;
}

#include "fs/vfs.h"
#include "errno.h"
#include "arch/riscv.h"
#include "kernel/types.h"
#include "kernel/printk.h"
#include "mm/kmalloc.h"
#include "mm/pool.h"
#include "lib/string.h"
#include "process/process.h"
#include "fs/dentry.h"
#include "tty/term.h"

// -----------------------------------------------------------------------------
// Internal state
// -----------------------------------------------------------------------------

#define MAX_COMPONENTS 16   // max path depth for iterative dentry chain build
#define VFS_MAX_SYMLINK_DEPTH 8

static fs_type_t *fs_types = NULL;  // Linked list of registered filesystems
static mount_t *mounts = NULL;      // Linked list of mount pointsj
static uint32_t dev_counter = 0;

// file-scope pool declarations for pool allocator
POOL_DECLARE(fs_struct_pool);
static dentry_t *vfs_root = NULL;

static void __attribute__((unused)) vfs_dump_mounts(void) {
    mount_t *curr = mounts;
    if (!curr) {
        printk("vfs: mount table is empty\n");
        return;
    }

    printk("vfs: active mount points:\n");
    char *path_buf = kmalloc(MAX_PATH);
    if (!path_buf) return;
    while (curr) {
        if (curr->cover) {
            if (dentry_build_path(curr->cover, vfs_root, path_buf, MAX_PATH) == 0)
                printk(" - '%s' (type: %s)\n", path_buf, curr->fs_type ? curr->fs_type->name : "unknown");
            else
                printk(" - '<cover?>' (type: %s)\n", curr->fs_type ? curr->fs_type->name : "unknown");
        } else {
            printk(" - '/' (type: %s)\n", curr->fs_type ? curr->fs_type->name : "unknown");
        }
        curr = curr->next;
    }
    kfree(path_buf);
}

// -----------------------------------------------------------------------------
// VFS Initialization
// -----------------------------------------------------------------------------

void vfs_init(void) {
    fs_types = NULL;
    mounts = NULL;
    dentry_pool_init();
    POOL_INIT(fs_struct_pool, fs_struct_t, 64); // as max number of processes is 64
    printk("vfs: initialized\n");
}

// -----------------------------------------------------------------------------
// Filesystem Registration
// -----------------------------------------------------------------------------

int vfs_register_fs(fs_type_t *fs_type) {
    if (!fs_type || !fs_type->name) {
        printk("vfs: invalid filesystem type\n");
        return -1;
    }

    // Check if already registered
    fs_type_t *curr = fs_types;
    while (curr) {
        if (strcmp(curr->name, fs_type->name) == 0) {
            printk("vfs: filesystem '%s' already registered\n", fs_type->name);
            return -1;
        }
        curr = curr->next;
    }

    // Add to front of list
    fs_type->next = fs_types;
    fs_types = fs_type;

    printk("vfs: registered filesystem '%s'\n", fs_type->name);
    return 0;
}

// -----------------------------------------------------------------------------
// Mount Point Management
// -----------------------------------------------------------------------------

int vfs_mount(const char *source, const char *target, const char *fstype) {
    if (!target || !fstype) {
        printk("vfs: invalid mount arguments\n");
        return -1;
    }

    fs_type_t *fs = fs_types;
    while (fs) {
        if (strcmp(fs->name, fstype) == 0)
            break;
        fs = fs->next;
    }

    if (!fs) {
        printk("vfs: unknown filesystem type '%s'\n", fstype);
        return -1;
    }

    for (mount_t *existing = mounts; existing; existing = existing->next) {
        if (strcmp(existing->mountpoint, target) == 0) {
            printk("vfs: '%s' already mounted\n", target);
            return -1;
        }
    }

    mount_t *mnt = kmalloc(sizeof(mount_t));
    if (!mnt) {
        printk("vfs: kmalloc failed for mount\n");
        return -1;
    }

    strncpy(mnt->mountpoint, target, sizeof(mnt->mountpoint) - 1);
    mnt->mountpoint[sizeof(mnt->mountpoint) - 1] = '\0';
    mnt->dev_id = ++dev_counter;
    mnt->fs_type = fs;
    mnt->fs_data = NULL;
    mnt->root = NULL;
    mnt->cover = NULL;
    mnt->next = NULL;

    vnode_t *root_vnode = fs->ops.mount(mnt, source);
    if (!root_vnode) {
        printk("vfs: mount failed for '%s'\n", fstype);
        kfree(mnt);
        return -1;
    }

    mnt->root = dentry_alloc("/", root_vnode);
    if (!mnt->root) {
        if (fs->ops.unmount)
            fs->ops.unmount(mnt);
        kfree(mnt);
        return -1;
    }

    mnt->root->d_parent = mnt->root;
    mnt->root->d_flags |= DENTRY_ROOT;

    if (strcmp(target, "/") != 0) {
        const char *leaf = strrchr(target, '/');
        leaf = leaf ? leaf + 1 : target;

        vnode_t *cover_vnode = kzalloc(sizeof(vnode_t));
        if (!cover_vnode) {
            dentry_free(mnt->root);
            if (fs->ops.unmount)
                fs->ops.unmount(mnt);
            kfree(mnt);
            return -1;
        }

        cover_vnode->type = VNODE_DIR;
        cover_vnode->mode = 0755;
        cover_vnode->mount = mnt;

        mnt->cover = dentry_alloc(leaf, cover_vnode);
        if (!mnt->cover) {
            kfree(cover_vnode);
            dentry_free(mnt->root);
            if (fs->ops.unmount)
                fs->ops.unmount(mnt);
            kfree(mnt);
            return -1;
        }

        mnt->cover->d_flags |= DENTRY_COVER;

        if (!vfs_root) {
            printk("vfs: cannot mount '%s' before root mount exists\n", target);
            dentry_free(mnt->cover);
            dentry_free(mnt->root);
            if (fs->ops.unmount)
                fs->ops.unmount(mnt);
            kfree(mnt);
            return -1;
        }

        // assumes that we only mount new file systems under vfs_root
        if (dentry_attach(vfs_root, mnt->cover) < 0) {
            printk("vfs: failed to attach cover dentry '%s'\n", leaf);
            dentry_free(mnt->cover);
            dentry_free(mnt->root);
            if (fs->ops.unmount)
                fs->ops.unmount(mnt);
            kfree(mnt);
            return -1;
        }
    } else {
        vfs_root = mnt->root;
    }

    mnt->next = mounts;
    mounts = mnt;

    printk("vfs: mounted '%s' at '%s' (type: %s)\n", source ? source : "none", target, fstype);
    return 0;
}

int vfs_unmount(const char *target) {
    if (!target)
        return -1;

    mount_t **prev = &mounts;
    mount_t *curr = mounts;

    while (curr) {
        if (strcmp(curr->mountpoint, target) == 0) {
            if (curr == vfs_find_mount("/")) {
                if (!curr->cover && curr->root == vfs_root) {
                    printk("vfs: refusing to unmount root filesystem '/'\n");
                    return -1;
                }
            }

            if (curr->fs_type->ops.unmount) {
                int ret = curr->fs_type->ops.unmount(curr);
                if (ret < 0)
                    return ret;
            }

            if (curr->cover) {
                dentry_detach(curr->cover);
                dentry_free(curr->cover);
                curr->cover = NULL;
            }

            if (curr->root) {
                // Mount root is not a part of namespace tree anyway.
                // Clear structural flag so raw prune/free rules don't preserve it forever.
                curr->root->d_flags &= ~DENTRY_ROOT;

                if (curr->root->d_ref == 0 && !dentry_has_children(curr->root)) {
                    dentry_free(curr->root);
                } else {
                    // If you later allow references into mounted roots to outlive unmount,
                    // you'll need a detached/busy policy here instead of raw free.
                    printk("vfs: warning: busy mount root at unmount '%s'\n", target);
                }

                curr->root = NULL;
            }

            *prev = curr->next;
            kfree(curr);

            printk("vfs: unmounted '%s'\n", target);
            return 0;
        }
        prev = &curr->next;
        curr = curr->next;
    }

    printk("vfs: mount point '%s' not found\n", target);
    return -1;
}

mount_t *vfs_find_mount(const char *path) {
    if (!path || path[0] != '/')
        return NULL;

    mount_t *best = NULL;
    size_t best_len = 0;

    char *mntpath = kmalloc(MAX_PATH);
    if (!mntpath) return NULL;
    for (mount_t *m = mounts; m; m = m->next) {
        if (!m->cover) {
            if (best_len == 0) {
                best = m;
                best_len = 1;
            }
            continue;
        }

        if (dentry_build_path(m->cover, vfs_root, mntpath, MAX_PATH) != 0)
            continue;

        size_t mnt_len = strlen(mntpath);
        if (mnt_len <= best_len)
            continue;

        if (strncmp(path, mntpath, mnt_len) != 0)
            continue;

        if (path[mnt_len] != '\0' && path[mnt_len] != '/')
            continue;

        best = m;
        best_len = mnt_len;
    }
    kfree(mntpath);
    return best;
}

dentry_t *vfs_find_cover_for_root(dentry_t *root_dentry) {
    if (!root_dentry) return NULL;
    for (mount_t *m = mounts; m; m = m->next) {
        if (m->root == root_dentry)
            return m->cover;
    }
    return NULL;
}

static mount_t *vfs_find_mount_for_cover(dentry_t *cover) {
    if (!cover) return NULL;
    for (mount_t *m = mounts; m; m = m->next) {
        if (m->cover == cover)
            return m;
    }
    return NULL;
}

static int vfs_mode_allows(vnode_t *vn, int perms) {
    uint32_t mode = vn ? (vn->mode & 0x1FF) : 0;

    if ((perms & VFS_PERM_READ) && !(mode & 0444))
        return 0;
    if ((perms & VFS_PERM_WRITE) && !(mode & 0222))
        return 0;
    if ((perms & VFS_PERM_EXEC) && !(mode & 0111))
        return 0;

    return 1;
}

int vfs_may_access(vnode_t *vn, int perms) {
    if (!vn)
        return -EINVAL;
    if (!vfs_mode_allows(vn, perms))
        return -EACCES;
    return 0;
}

static int vfs_require_parent_perms(const char *path, int perms) {
    if (!path || path[0] != '/')
        return -EINVAL;

    char *parent_path = kmalloc(MAX_PATH);
    if (!parent_path) return -ENOMEM;
    strncpy(parent_path, path, MAX_PATH - 1);
    parent_path[MAX_PATH - 1] = '\0';

    char *last_slash = strrchr(parent_path, '/');
    if (!last_slash) {
        kfree(parent_path);
        return -EINVAL;
    }

    if (last_slash == parent_path) {
        parent_path[1] = '\0';
    } else {
        *last_slash = '\0';
    }

    dentry_t *parent = NULL;
    int err = vfs_lookup(parent_path, &parent);
    kfree(parent_path);
    if (err)
        return err;

    err = vfs_may_access(parent->d_vnode, perms);
    dentry_put(parent);
    return err;
}

// -----------------------------------------------------------------------------
// Component-walk helpers (internal)
// -----------------------------------------------------------------------------

// Single-step resolver: advance from cur to the child named comp under *mnt.
// Precondition: cur != NULL, *mnt != NULL, comp has no '/' chars.
// Postcondition (success): *out has exactly 1 ref for the caller; cur's ref untouched.
// Sets *crossed=1 if a mount boundary was crossed (caller must reset rel_buf to comp).
static int vfs_step(dentry_t *cur, mount_t **mnt, const char *comp,
                    const char *rel_so_far, int is_last,
                    int *crossed, dentry_t **out) {
    const char *effective_rel = rel_so_far;

    if (cur->d_vnode) {
        int perm_err = vfs_may_access(cur->d_vnode, VFS_PERM_EXEC);
        if (perm_err)
            return perm_err;
    }

    // 1. Mount crossing: if cur is a cover dentry, follow to the mounted root
    if (cur->d_flags & DENTRY_COVER) {
        mount_t *m = vfs_find_mount_for_cover(cur);
        if (m) {
            cur = m->root;
            *mnt = m;
            *crossed = 1;
            effective_rel = "";
        }
    }

    // 2. Cache check
    dentry_t *child = dentry_lookup_child(cur, comp);
    if (child) {
        if (!is_last && child->d_vnode &&
            child->d_vnode->type != VNODE_DIR &&
            child->d_vnode->type != VNODE_LNK)
            return -ENOTDIR;
        dentry_get(child);

        // If the looked-up terminal component is a mount cover, expose mounted root.
        if (is_last && (child->d_flags & DENTRY_COVER)) {
            mount_t *cm = vfs_find_mount_for_cover(child);
            if (cm && cm->root) {
                dentry_get(cm->root);
                dentry_put(child);
                child = cm->root;
                *mnt = cm;
                *crossed = 1;
            }
        }

        *out = child;
        return 0;
    }

    // 3. Cache miss — build relative path within current mount
    char *rel_path = kmalloc(MAX_PATH);
    if (!rel_path) return -ENOMEM;
    size_t rel_len = strlen(effective_rel);
    size_t comp_len = strlen(comp);
    if (rel_len == 0) {
        if (comp_len >= (size_t)MAX_PATH) { kfree(rel_path); return -ENAMETOOLONG; }
        strncpy(rel_path, comp, MAX_PATH - 1);
        rel_path[comp_len] = '\0';
    } else {
        if (rel_len + 1 + comp_len >= (size_t)MAX_PATH) { kfree(rel_path); return -ENAMETOOLONG; }
        strncpy(rel_path, effective_rel, MAX_PATH - 1);
        rel_path[rel_len] = '/';
        strncpy(rel_path + rel_len + 1, comp, MAX_PATH - rel_len - 2);
        rel_path[rel_len + 1 + comp_len] = '\0';
    }

    vnode_t *vnode = (*mnt)->fs_type->ops.lookup(*mnt, rel_path);
    kfree(rel_path);
    if (!vnode)
        return -ENOENT;

    // 4. ENOTDIR check for non-final components (symlinks are ok; vfs_walk follows them)
    if (!is_last && vnode->type != VNODE_DIR && vnode->type != VNODE_LNK) {
        kfree(vnode);
        return -ENOTDIR;
    }

    // 5. Populate cache: alloc dentry and attach to tree
    child = dentry_alloc(comp, vnode);
    if (!child) {
        kfree(vnode);
        return -ENOMEM;
    }
    if (dentry_attach(cur, child) < 0) {
        // Another lookup beat us (single-core: shouldn't happen — defensive)
        dentry_free(child); // dentry_free also frees vnode
        child = dentry_lookup_child(cur, comp);
        if (!child)
            return -ENOMEM;
    }
    dentry_get(child);

    // Same handoff for cache-miss path: terminal cover resolves to mounted root.
    if (is_last && (child->d_flags & DENTRY_COVER)) {
        mount_t *cm = vfs_find_mount_for_cover(child);
        if (cm && cm->root) {
            dentry_get(cm->root);
            dentry_put(child);
            child = cm->root;
            *mnt = cm;
            *crossed = 1;
        }
    }

    *out = child;
    return 0;
}

// Forward declaration for recursive symlink following.
static int vfs_walk(dentry_t *start, mount_t *mnt,
                    char **comps, int n, dentry_t **out, int symlink_depth);

// Component walk loop. Calls vfs_step for each component in comps[0..n-1].
// Precondition: start != NULL, mnt != NULL, n > 0.
// Postcondition (success): *out has exactly 1 ref; start's external ref untouched.
// On failure: all refs from completed steps are released before returning.
static int vfs_walk(dentry_t *start, mount_t *mnt,
                    char **comps, int n, dentry_t **out, int symlink_depth) {
    if (symlink_depth > VFS_MAX_SYMLINK_DEPTH)
        return -ELOOP;

    dentry_t *cur = start;
    dentry_get(cur);

    char *rel_buf = kmalloc(MAX_PATH);
    if (!rel_buf) { dentry_put(cur); return -ENOMEM; }
    rel_buf[0] = '\0';

    for (int i = 0; i < n; i++) {
        int crossed = 0;
        dentry_t *next = NULL;
        dentry_t *parent = cur; // save before put, needed for relative symlink base
        int err = vfs_step(cur, &mnt, comps[i], rel_buf,
                           (i == n - 1), &crossed, &next);
        dentry_put(cur);
        if (err) {
            kfree(rel_buf);
            return err;
        }

        // If the resolved node is a non-terminal symlink, expand it and continue.
        // Terminal symlinks are returned as-is (lstat semantics); callers that
        // need follow-through (file_open, chdir) do an extra step themselves.
        if (i < n - 1 && next->d_vnode && next->d_vnode->type == VNODE_LNK) {
            if (!next->d_vnode->mount ||
                !next->d_vnode->mount->fs_type->ops.readlink) {
                dentry_put(next);
                kfree(rel_buf);
                return -EINVAL;
            }

            char *link_target = kmalloc(MAX_PATH);
            char *remainder   = kmalloc(MAX_PATH);
            if (!link_target || !remainder) {
                kfree(link_target);
                kfree(remainder);
                dentry_put(next);
                kfree(rel_buf);
                return -ENOMEM;
            }

            int r = next->d_vnode->mount->fs_type->ops.readlink(
                        next->d_vnode, link_target, MAX_PATH - 1);
            dentry_put(next);
            if (r < 0) {
                kfree(link_target);
                kfree(remainder);
                kfree(rel_buf);
                return r;
            }
            link_target[r] = '\0';

            // Relative symlinks resolve from the symlink's parent dir (parent),
            // not from cwd. Absolute symlinks normalize from root.
            dentry_t *norm_base = (link_target[0] == '/')
                ? vfs_root
                : parent;
            err = dentry_normalize_path(
                norm_base, vfs_root, link_target, link_target, MAX_PATH);
            if (err) {
                kfree(link_target);
                kfree(remainder);
                kfree(rel_buf);
                return err;
            }

            // Build remainder: normalized symlink target + remaining comps.
            strncpy(remainder, link_target, MAX_PATH - 1);
            remainder[MAX_PATH - 1] = '\0';
            kfree(link_target);

            for (int j = i + 1; j < n; j++) {
                size_t rlen = strlen(remainder);
                size_t clen = strlen(comps[j]);
                if (rlen + 1 + clen < (size_t)MAX_PATH) {
                    remainder[rlen] = '/';
                    strncpy(remainder + rlen + 1, comps[j],
                            MAX_PATH - rlen - 2);
                    remainder[rlen + 1 + clen] = '\0';
                }
            }

            kfree(rel_buf);

            if (remainder[0] == '/' && remainder[1] == '\0') {
                kfree(remainder);
                dentry_get(vfs_root);
                *out = vfs_root;
                return 0;
            }

            char *sub_comps[MAX_COMPONENTS];
            int sub_n = dentry_split_path(remainder, sub_comps, MAX_COMPONENTS);
            if (sub_n < 0) {
                kfree(remainder);
                return -ENAMETOOLONG;
            }

            mount_t *sub_mnt = vfs_find_mount(remainder);
            if (!sub_mnt) {
                kfree(remainder);
                return -ENOENT;
            }

            int sub_err = vfs_walk(vfs_root, sub_mnt, sub_comps, sub_n, out,
                                   symlink_depth + 1);
            kfree(remainder);
            return sub_err;
        }

        // Update rel_buf for next step.
        // After a mount crossing, current component is first entry in new mount.
        if (crossed) {
            size_t clen = strlen(comps[i]);
            if (clen < (size_t)MAX_PATH) {
                strncpy(rel_buf, comps[i], MAX_PATH - 1);
                rel_buf[clen] = '\0';
            }
        } else {
            size_t rlen = strlen(rel_buf);
            size_t clen = strlen(comps[i]);
            if (rlen == 0) {
                strncpy(rel_buf, comps[i], MAX_PATH - 1);
                rel_buf[clen < (size_t)(MAX_PATH - 1) ? clen : (size_t)(MAX_PATH - 1)] = '\0';
            } else if (rlen + 1 + clen < (size_t)MAX_PATH) {
                rel_buf[rlen] = '/';
                strncpy(rel_buf + rlen + 1, comps[i], MAX_PATH - rlen - 2);
                rel_buf[rlen + 1 + clen] = '\0';
            }
        }
        cur = next;
    }

    kfree(rel_buf);
    *out = cur;
    return 0;
}

// -----------------------------------------------------------------------------
// VFS-level operations
// -----------------------------------------------------------------------------

dentry_t *vfs_get_root(void) { return vfs_root; }

int vfs_lookup_from(dentry_t *start, const char *path, dentry_t **out) {
    if (!path || !out)
        return -EINVAL;

    char *normalized = kmalloc(MAX_PATH);
    if (!normalized) return -ENOMEM;

    int err = dentry_normalize_path(start ? start : vfs_root, vfs_root,
                                    path, normalized, MAX_PATH);
    if (err) { kfree(normalized); return err; }

    // Root lookup
    if (normalized[0] == '/' && normalized[1] == '\0') {
        kfree(normalized);
        dentry_get(vfs_root);
        *out = vfs_root;
        return 0;
    }

    char *path_buf = kmalloc(MAX_PATH);
    if (!path_buf) { kfree(normalized); return -ENOMEM; }
    strncpy(path_buf, normalized, MAX_PATH - 1);
    path_buf[MAX_PATH - 1] = '\0';

    char *comps[MAX_COMPONENTS];
    int n = dentry_split_path(path_buf, comps, MAX_COMPONENTS);
    if (n < 0) { kfree(path_buf); kfree(normalized); return -ENAMETOOLONG; }

    mount_t *mnt = vfs_find_mount(normalized);
    kfree(normalized);
    if (!mnt) { kfree(path_buf); return -ENOENT; }

    int ret = vfs_walk(vfs_root, mnt, comps, n, out, 0);
    kfree(path_buf);
    return ret;
}

int vfs_lookup(const char *path, dentry_t **out) {
    return vfs_lookup_from(vfs_root, path, out);
}

int vfs_create(const char *path, uint32_t mode) {
    printk("path: %s\n", path);
    if (!path) return -1;

    int perm_err = vfs_require_parent_perms(path, VFS_PERM_WRITE | VFS_PERM_EXEC);
    if (perm_err)
        return perm_err;

    mount_t *mnt = vfs_find_mount(path);
    if (!mnt){
        printk("mount not found.\n");
        return -1;
    }
    const char *rel_path = path + strlen(mnt->mountpoint);
    if (*rel_path == '/') rel_path++;

    printk("mount: %s\n", mnt->mountpoint);
    printk("mount type: %s\n", mnt->fs_type->name);

    if (mnt->fs_type->ops.create) {
        int ret = mnt->fs_type->ops.create(mnt, rel_path, mode);
        if (ret == 0) {
            // Populate dentry cache so pool_used reflects the new file.
            dentry_t *new_d = NULL;
            if (vfs_lookup(path, &new_d) == 0)
                dentry_put(new_d);
        }
        return ret;
    }

    return -1;
}

static int vfs_lookup_nofollow(dentry_t *start, const char *path, dentry_t **out);

int vfs_unlink(const char *path) {
    if (!path)
        return -EINVAL;

    int perm_err = vfs_require_parent_perms(path, VFS_PERM_WRITE | VFS_PERM_EXEC);
    if (perm_err)
        return perm_err;

    dentry_t *d = NULL;
    int err = vfs_lookup_nofollow(
        current && current->fs ? current->fs->cwd : vfs_root,
        path, &d);
    if (err)
        return err;

    mount_t *mnt = d->d_vnode ? d->d_vnode->mount : NULL;
    if (!mnt) {
        dentry_put(d);
        return -ENOENT;
    }
    if (d->d_vnode && d->d_vnode->type == VNODE_DIR) {
        dentry_put(d);
        return -EISDIR;
    }
    if (!mnt->fs_type->ops.unlink) {
        dentry_put(d);
        return -ENOSYS;
    }

    char *rel_path = kmalloc(MAX_PATH);
    if (!rel_path) { dentry_put(d); return -ENOMEM; }
    if (dentry_build_path(d, mnt->root, rel_path, MAX_PATH) != 0) {
        kfree(rel_path);
        dentry_put(d);
        return -ENAMETOOLONG;
    }

    const char *rel = rel_path;
    if (*rel == '/')
        rel++;

    int ret = mnt->fs_type->ops.unlink(mnt, rel);
    if (ret == 0) {
        // Drop non-structural cache entries so lookups do not resurrect removed files.
        if (!(d->d_flags & (DENTRY_ROOT | DENTRY_COVER)))
            dentry_detach(d);
        // If no fds are open (ref_count == 1: only this lookup holds a ref),
        // call release now so the fs can free blocks immediately rather than
        // leaking until the next open fd closes (or fsck).
        vnode_t *vn = d->d_vnode;
        if (vn && vn->ref_count == 1 && mnt->fs_type->ops.release)
            mnt->fs_type->ops.release(vn);
        dentry_put(d);
        dentry_prune(d);
    } else {
        dentry_put(d);
    }
    kfree(rel_path);
    return ret;
}

int vfs_readdir(dentry_t *dentry, uint32_t start_idx, void *buf, uint32_t bufsize, uint32_t *entries_out) {
    if (!dentry || !buf || bufsize == 0)
        return -EINVAL;

    vnode_t *vnode = dentry->d_vnode;
    if (!vnode || vnode->type != VNODE_DIR)
        return -ENOTDIR;

    mount_t *mnt = vnode->mount;
    if (!mnt || !mnt->fs_type->ops.readdir)
        return -ENOSYS;

    // 1. Ask the underlying FS to fill buf with its own entries
    uint32_t fs_entries = 0;
    int n = mnt->fs_type->ops.readdir(mnt, vnode, start_idx, buf, bufsize, &fs_entries);
    if (n < 0)
        return n;

    // 2. Inject cover dentries for mounts whose parent IS this dentry.
    // Only on the first batch (start_idx == 0): mount entries are always
    // appended after real entries.  On subsequent paginated calls the real
    // entries are exhausted, so skipping injection here correctly returns 0
    // bytes and signals end-of-directory to the caller.
    if (start_idx != 0)
        goto done_readdir;

    {
    char *ptr = (char *) buf + n;
    size_t remaining = bufsize - (size_t) n;

    mount_t *m = mounts;
    while (m) {
        if (m->cover && m->cover->d_parent == dentry) {
            size_t namelen = strlen(m->cover->d_name);

            // Skip if the underlying FS already emitted an entry with this name.
            int already_present = 0;
            char *scan = (char *)buf;
            char *scan_end = (char *)buf + n;
            while (scan < scan_end) {
                fs_dent_t *existing = (fs_dent_t *)scan;
                if (existing->d_reclen == 0) break;
                if (strcmp(existing->d_name, m->cover->d_name) == 0) {
                    already_present = 1;
                    break;
                }
                scan += existing->d_reclen;
            }
            if (already_present) { m = m->next; continue; }

            reclen_t reclen = FS_DENT_RECLEN(namelen);
            if (remaining < reclen)
                break;

            fs_dent_t *ent = (fs_dent_t *) ptr;
            ent->d_ino = (ino_t) m->root->d_vnode; // stable synthetic ino
            ent->d_reclen = reclen;
            ent->d_type = DT_DIR;
            // copy name including null terminator
            for (size_t i = 0; i <= namelen; i++)
                ent->d_name[i] = m->cover->d_name[i];

            ptr += reclen;
            remaining -= reclen;
            n += reclen;
            fs_entries++;
        }
        m = m->next;
    }
    } /* end start_idx == 0 block */

done_readdir:
    *entries_out = fs_entries;
    return n;
}

int vfs_mkdir(const char *path, uint32_t mode) {
    if (!path) return -1;

    /* POSIX: EEXIST must be returned when the path already exists, even
     * when the caller lacks write permission on the parent directory. */
    dentry_t *existing = NULL;
    if (vfs_lookup(path, &existing) == 0) {
        dentry_put(existing);
        return -EEXIST;
    }

    int perm_err = vfs_require_parent_perms(path, VFS_PERM_WRITE | VFS_PERM_EXEC);
    if (perm_err)
        return perm_err;

    mount_t *mnt = vfs_find_mount(path);
    if (!mnt) return -1;

    const char *rel_path = path + strlen(mnt->mountpoint);
    if (*rel_path == '/') rel_path++;

    if (mnt->fs_type->ops.mkdir) {
        return mnt->fs_type->ops.mkdir(mnt, rel_path, mode);
    }

    return -1;
}

int vfs_chmod(const char *path, mode_t mode) {
    if (!path) return -EINVAL;

    dentry_t *d = NULL;
    int err = vfs_lookup_from(
        current && current->fs ? current->fs->cwd : vfs_root,
        path, &d);
    if (err) return err;

    vnode_t *vn = d->d_vnode;
    int ret = -EROFS;
    if (vn->mount && vn->mount->fs_type->ops.chmod) {
        ret = vn->mount->fs_type->ops.chmod(vn->mount, vn, mode);
        if (ret == 0)
            vn->mode = mode & 0xFFF;
    }

    dentry_put(d);
    return ret;
}

int vfs_fchmod(vnode_t *vn, mode_t mode) {
    if (!vn) return -EINVAL;

    if (vn->mount && vn->mount->fs_type->ops.chmod) {
        int ret = vn->mount->fs_type->ops.chmod(vn->mount, vn, mode);
        if (ret == 0)
            vn->mode = mode & 0xFFF;
        return ret;
    }

    return -EROFS;
}

int vfs_rmdir(const char *path) {
    if (!path) return -EINVAL;

    int perm_err = vfs_require_parent_perms(path, VFS_PERM_WRITE | VFS_PERM_EXEC);
    if (perm_err)
        return perm_err;

    /* Verify the target exists and is a directory */
    dentry_t *d = NULL;
    int err = vfs_lookup_from(
        current && current->fs ? current->fs->cwd : vfs_root,
        path, &d);
    if (err) return err;

    if (!d->d_vnode || d->d_vnode->type != VNODE_DIR) {
        dentry_put(d);
        return -ENOTDIR;
    }

    mount_t *mnt = vfs_find_mount(path);
    if (!mnt) {
        dentry_put(d);
        return -ENOENT;
    }

    const char *rel_path = path + strlen(mnt->mountpoint);
    if (*rel_path == '/') rel_path++;

    int ret = -ENOSYS;
    if (mnt->fs_type->ops.rmdir)
        ret = mnt->fs_type->ops.rmdir(mnt, rel_path);

    if (ret == 0) {
        /* Evict from dentry cache so future lookups see it's gone */
        dentry_detach(d);
        dentry_put(d);
        dentry_prune(d);
    } else {
        dentry_put(d);
    }
    return ret;
}

int vfs_link(const char *oldpath, const char *newpath) {
    if (!oldpath || !newpath) return -EINVAL;

    int perm_err = vfs_require_parent_perms(newpath, VFS_PERM_WRITE | VFS_PERM_EXEC);
    if (perm_err)
        return perm_err;

    // Resolve oldpath without following symlinks for the final component so we
    // get the actual file entry (not what a symlink points to).
    dentry_t *old_d = NULL;
    int err = vfs_lookup_from(
        current && current->fs ? current->fs->cwd : vfs_root,
        oldpath, &old_d);
    if (err) return err;

    if (old_d->d_vnode && old_d->d_vnode->type == VNODE_DIR) {
        dentry_put(old_d);
        return -EPERM; // POSIX: cannot hard-link a directory
    }

    mount_t *mnt = old_d->d_vnode ? old_d->d_vnode->mount : NULL;
    if (!mnt || !mnt->fs_type->ops.link) {
        dentry_put(old_d);
        return -ENOSYS;
    }

    char *old_rel = kmalloc(MAX_PATH);
    if (!old_rel) { dentry_put(old_d); return -ENOMEM; }
    if (dentry_build_path(old_d, mnt->root, old_rel, MAX_PATH) != 0) {
        kfree(old_rel);
        dentry_put(old_d);
        return -ENAMETOOLONG;
    }

    const char *or = old_rel[0] == '/' ? old_rel + 1 : old_rel;

    // newpath relative to mount root.
    size_t mp_len = strlen(mnt->mountpoint);
    if (strncmp(newpath, mnt->mountpoint, mp_len) != 0) {
        kfree(old_rel);
        dentry_put(old_d);
        return -EXDEV; // different filesystem
    }

    const char *nr = newpath + mp_len;
    if (*nr == '/') nr++;

    int ret = mnt->fs_type->ops.link(mnt, or, nr);

    // On success, update the cached vnode's nlink so stat() returns the new count.
    if (ret == 0 && old_d->d_vnode)
        old_d->d_vnode->nlink++;

    kfree(old_rel);
    dentry_put(old_d);
    return ret;
}

int vfs_rename(const char *oldpath, const char *newpath) {
    if (!oldpath || !newpath) return -EINVAL;

    /* POSIX: rename(x, x) is a no-op that always succeeds. */
    if (strcmp(oldpath, newpath) == 0)
        return 0;

    dentry_t *old_d = NULL;
    int err = vfs_lookup_nofollow(
        current && current->fs ? current->fs->cwd : vfs_root,
        oldpath, &old_d);
    if (err) return err;

    mount_t *mnt = old_d->d_vnode ? old_d->d_vnode->mount : NULL;
    if (!mnt) { dentry_put(old_d); return -ENOENT; }
    if (!mnt->fs_type->ops.rename) { dentry_put(old_d); return -ENOSYS; }

    /* Cross-device check: newpath must be on the same mount */
    size_t mp_len = strlen(mnt->mountpoint);
    if (strncmp(newpath, mnt->mountpoint, mp_len) != 0) {
        dentry_put(old_d);
        return -EXDEV;
    }

    char *old_rel = kmalloc(MAX_PATH);
    if (!old_rel) { dentry_put(old_d); return -ENOMEM; }
    if (dentry_build_path(old_d, mnt->root, old_rel, MAX_PATH) != 0) {
        kfree(old_rel); dentry_put(old_d); return -ENAMETOOLONG;
    }
    const char *or = old_rel[0] == '/' ? old_rel + 1 : old_rel;

    const char *nr = newpath + mp_len;
    if (*nr == '/') nr++;

    /*
     * Look up the destination before calling into the fs so we can evict
     * its dentry cache entry on success.  It may not exist; that is fine.
     */
    dentry_t *new_d = NULL;
    vfs_lookup_nofollow(
        current && current->fs ? current->fs->cwd : vfs_root,
        newpath, &new_d);

    int ret = mnt->fs_type->ops.rename(mnt, or, nr);

    if (ret == 0) {
        /* Evict src dentry so future lookups hit the fs again */
        if (!(old_d->d_flags & (DENTRY_ROOT | DENTRY_COVER)))
            dentry_detach(old_d);
        dentry_put(old_d);
        dentry_prune(old_d);

        /* Evict dst dentry so it isn't served stale from cache */
        if (new_d) {
            if (!(new_d->d_flags & (DENTRY_ROOT | DENTRY_COVER)))
                dentry_detach(new_d);
            dentry_put(new_d);
            dentry_prune(new_d);
        }
    } else {
        dentry_put(old_d);
        if (new_d)
            dentry_put(new_d);
    }

    kfree(old_rel);
    return ret;
}

int vfs_symlink(const char *target, const char *linkpath) {
    if (!target || !linkpath) return -EINVAL;

    int perm_err = vfs_require_parent_perms(linkpath, VFS_PERM_WRITE | VFS_PERM_EXEC);
    if (perm_err)
        return perm_err;

    mount_t *mnt = vfs_find_mount(linkpath);
    if (!mnt || !mnt->fs_type->ops.symlink) return -ENOSYS;

    const char *rel = linkpath + strlen(mnt->mountpoint);
    if (*rel == '/') rel++;

    return mnt->fs_type->ops.symlink(mnt, target, rel);
}

// vfs_lookup_nofollow: like vfs_lookup_from but stops before following the
// final symlink component, so readlink can obtain the symlink vnode itself.
static int vfs_lookup_nofollow(dentry_t *start, const char *path, dentry_t **out) {
    if (!path || !out) return -EINVAL;

    char *normalized = kmalloc(MAX_PATH);
    if (!normalized) return -ENOMEM;

    int err = dentry_normalize_path(start ? start : vfs_root, vfs_root,
                                    path, normalized, MAX_PATH);
    if (err) { kfree(normalized); return err; }

    if (normalized[0] == '/' && normalized[1] == '\0') {
        kfree(normalized);
        dentry_get(vfs_root);
        *out = vfs_root;
        return 0;
    }

    char *path_buf = kmalloc(MAX_PATH);
    if (!path_buf) { kfree(normalized); return -ENOMEM; }
    strncpy(path_buf, normalized, MAX_PATH - 1);
    path_buf[MAX_PATH - 1] = '\0';

    char *comps[MAX_COMPONENTS];
    int n = dentry_split_path(path_buf, comps, MAX_COMPONENTS);
    if (n < 0) { kfree(path_buf); kfree(normalized); return -ENAMETOOLONG; }

    mount_t *mnt = vfs_find_mount(normalized);
    if (!mnt) { kfree(path_buf); kfree(normalized); return -ENOENT; }

    if (n == 1) {
        dentry_get(vfs_root);
        dentry_t *cur = vfs_root;
        char *rel_buf = kmalloc(MAX_PATH);
        if (!rel_buf) {
            dentry_put(cur);
            kfree(path_buf); kfree(normalized);
            return -ENOMEM;
        }
        rel_buf[0] = '\0';
        int crossed = 0;
        dentry_t *child = NULL;
        err = vfs_step(cur, &mnt, comps[0], rel_buf, 1, &crossed, &child);
        kfree(rel_buf);
        dentry_put(cur);
        kfree(path_buf); kfree(normalized);
        if (err) return err;
        *out = child;
        return 0;
    }

    // Walk all but the last component following symlinks normally, then do one
    // raw vfs_step for the final component to get the symlink vnode itself.
    char *parent_buf = kmalloc(MAX_PATH);
    if (!parent_buf) { kfree(path_buf); kfree(normalized); return -ENOMEM; }
    strncpy(parent_buf, normalized, MAX_PATH - 1);
    parent_buf[MAX_PATH - 1] = '\0';
    kfree(normalized);

    // Rebuild parent path by stripping the last component.
    char *last_slash = NULL;
    for (char *p = parent_buf; *p; p++)
        if (*p == '/') last_slash = p;
    if (last_slash) {
        if (last_slash == parent_buf)
            parent_buf[1] = '\0';
        else
            *last_slash = '\0';
    }

    dentry_t *parent_d = NULL;
    err = vfs_lookup_from(start, parent_buf, &parent_d);
    kfree(parent_buf);
    if (err) { kfree(path_buf); return err; }

    mount_t *parent_mnt = parent_d->d_vnode ? parent_d->d_vnode->mount : mnt;

    char *rel_buf = kmalloc(MAX_PATH);
    char *par_rel = kmalloc(MAX_PATH);
    if (!rel_buf || !par_rel) {
        kfree(rel_buf); kfree(par_rel);
        dentry_put(parent_d); kfree(path_buf);
        return -ENOMEM;
    }
    rel_buf[0] = '\0';

    if (dentry_build_path(parent_d, parent_mnt->root, par_rel, MAX_PATH) == 0) {
        const char *r = par_rel[0] == '/' ? par_rel + 1 : par_rel;
        strncpy(rel_buf, r, MAX_PATH - 1);
        rel_buf[MAX_PATH - 1] = '\0';
    }
    kfree(par_rel);

    int crossed = 0;
    dentry_t *child = NULL;
    err = vfs_step(parent_d, &parent_mnt, comps[n - 1], rel_buf, 1,
                   &crossed, &child);
    kfree(rel_buf);
    dentry_put(parent_d);
    kfree(path_buf);
    if (err) return err;
    *out = child;
    return 0;
}

int vfs_lookup_nofollow_pub(dentry_t *start, const char *path, dentry_t **out) {
    return vfs_lookup_nofollow(start, path, out);
}

ssize_t vfs_readlink(const char *path, char *buf, size_t size) {
    if (!path || !buf || size == 0) return -EINVAL;

    dentry_t *d = NULL;
    int err = vfs_lookup_nofollow(
        current && current->fs ? current->fs->cwd : vfs_root,
        path, &d);
    if (err) return err;

    vnode_t *vn = d->d_vnode;
    if (!vn || vn->type != VNODE_LNK) {
        dentry_put(d);
        return -EINVAL;
    }

    if (!vn->mount || !vn->mount->fs_type->ops.readlink) {
        dentry_put(d);
        return -ENOSYS;
    }

    ssize_t r = vn->mount->fs_type->ops.readlink(vn, buf, size);
    dentry_put(d);
    return r;
}

// -----------------------------------------------------------------------------
// File operations
// -----------------------------------------------------------------------------

int vfs_chdir(const char *kpath) {
    if (!current || !current->fs)
        return -EINVAL;

    char *resolved = kmalloc(MAX_PATH);
    if (!resolved) return -ENOMEM;

    if (dentry_normalize_path(current->fs->cwd, vfs_root, kpath, resolved, MAX_PATH) < 0) {
        kfree(resolved);
        return -ENAMETOOLONG;
    }

    dentry_t *d = NULL;
    int lookup_rc = vfs_lookup(resolved, &d);
    if (lookup_rc < 0) {
        kfree(resolved);
        return lookup_rc;
    }
    int perm_rc = vfs_may_access(d->d_vnode, VFS_PERM_EXEC);
    dentry_put(d);
    if (perm_rc < 0) {
        kfree(resolved);
        return perm_rc;
    }

    // 3. Hand off to fs_struct — it looks up, validates, and swaps the cwd dentry
    int rc = fs_struct_set_cwd(current->fs, resolved);
    kfree(resolved);
    return rc;
}

// -----------------------------------------------------------------------------
// fs_struct operations (at a process level)
// -----------------------------------------------------------------------------

void fs_struct_dump(const fs_struct_t *fs) {
    if (!fs) {
        printk("[fs_struct] NULL\n");
        return;
    }

    char *_pathbufs = kmalloc(2 * MAX_PATH);
    if (!_pathbufs) return;
    char *root_path = _pathbufs;
    char *cwd_path  = _pathbufs + MAX_PATH;

    if (!fs->root) {
        printk("[fs_struct] root=NULL\n");
    } else {
        dentry_build_path(fs->root, fs->root, root_path, MAX_PATH);
        printk("[fs_struct] root=%s (dentry=%p, d_ref=%d)\n",
               root_path, fs->root, fs->root->d_ref);
    }

    if (!fs->cwd) {
        printk("[fs_struct] cwd=NULL\n");
    } else {
        dentry_build_path(fs->cwd, fs->root, cwd_path, MAX_PATH);
        printk("[fs_struct] cwd=%s (dentry=%p, d_ref=%d)\n",
               cwd_path, fs->cwd, fs->cwd->d_ref);
    }
    kfree(_pathbufs);
}

fs_struct_t *fs_struct_alloc(void) {
    fs_struct_t *fs = POOL_ALLOC(fs_struct_pool, fs_struct_t);
    if (fs) { fs->root = NULL; fs->cwd = NULL; }
    return fs;
}

fs_struct_t *fs_struct_copy(fs_struct_t *src) {
    if (!src) return NULL;
    fs_struct_t *fs = POOL_ALLOC(fs_struct_pool, fs_struct_t);
    if (!fs) return NULL;
    fs->root = src->root;
    fs->cwd  = src->cwd;
    dentry_get(fs->root);   // root slot owns a ref
    dentry_get(fs->cwd);    // cwd slot owns a ref — even if same dentry as root
    return fs;
}

void fs_struct_free(fs_struct_t *fs) {
    if (!fs) return;
    dentry_put(fs->cwd);    // release cwd slot's ref
    dentry_put(fs->root);   // release root slot's ref
    POOL_FREE(fs_struct_pool, fs);
}

int fs_struct_set_cwd(fs_struct_t *fs, const char *abs_path) {
    if (!fs || !abs_path) return -EINVAL;

    dentry_t *d = NULL;
    int err = vfs_lookup(abs_path, &d);
    if (err) return err;

    if (d->d_vnode->type != VNODE_DIR) {
        dentry_put(d);
        return -ENOTDIR;
    }

    // vfs_lookup returns ref=1 — that ref becomes the cwd slot's ownership.
    dentry_put(fs->cwd);
    fs->cwd = d;
    return 0;
}

int fs_struct_set_root(fs_struct_t *fs, const char *abs_path) {
    if (!fs || !abs_path) return -EINVAL;

    dentry_t *d = NULL;
    int err = vfs_lookup(abs_path, &d);
    if (err) return err;

    if (d->d_vnode->type != VNODE_DIR) {
        dentry_put(d);
        return -ENOTDIR;
    }

    // vfs_lookup returns ref=1 — that ref becomes the root slot's ownership.
    dentry_put(fs->root);
    fs->root = d;
    return 0;
}

int fs_struct_get_cwd(fs_struct_t *fs, char *buf, size_t size) {
    if (!fs) return -1;
    return dentry_build_path(fs->cwd, fs->root, buf, size);
}

int fs_struct_get_root(fs_struct_t *fs, char *buf, size_t size) {
    if (!fs) return -1;
    return dentry_build_path(fs->root, fs->root, buf, size);
}

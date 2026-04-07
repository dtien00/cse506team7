#include "fs/tarfs.h"
#include "fs/vfs.h"
#include "errno.h"
#include "arch/riscv.h"
#include "kernel/types.h"
#include "kernel/printk.h"
#include "mm/kmalloc.h"
#include "mm/pool.h"
#include "lib/string.h"
#include "process/process.h"
#include "tty/term.h"

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

// -----------------------------------------------------------------------------
// Internal state
// -----------------------------------------------------------------------------

static fs_type_t *fs_types = NULL;  // Linked list of registered filesystems
static mount_t *mounts = NULL;      // Linked list of mount points

// file-scope pool declarations for pool allocator
POOL_DECLARE(fs_struct_pool);
static dentry_t *vfs_root = NULL;

static void vfs_dump_mounts(void) {
    mount_t *curr = mounts;
    if (!curr) {
        printk("vfs: mount table is empty\n");
        return;
    }
    printk("vfs: active mount points:\n");
    while (curr) {
        printk("  - '%s' (type: %s)\n",
               curr->mountpoint,
               curr->fs_type ? curr->fs_type->name : "unknown");
        curr = curr->next;
    }
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

    // Find the filesystem type
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

    // Check if already mounted at this target
    mount_t *existing = mounts;
    while (existing) {
        if (strcmp(existing->mountpoint, target) == 0) {
            printk("vfs: '%s' already mounted\n", target);
            return -1;
        }
        existing = existing->next;
    }

    // Allocate mount structure
    mount_t *mnt = kmalloc(sizeof(mount_t));
    if (!mnt) {
        printk("vfs: kmalloc failed for mount\n");
        return -1;
    }

    // Initialize mount
    strncpy(mnt->mountpoint, target, sizeof(mnt->mountpoint) - 1);
    mnt->mountpoint[sizeof(mnt->mountpoint) - 1] = '\0';
    mnt->fs_type = fs;
    mnt->fs_data = NULL;
    mnt->next = NULL;

    // Call filesystem-specific mount operation
    vnode_t *root_vnode = fs->ops.mount(mnt, source);
    if (!root_vnode) {
        printk("vfs: mount failed for '%s'\n", fstype);
        kfree(mnt);
        return -1;
    }

    // create root dentry for this mount and store it on the mount itself
    mnt->root = dentry_alloc("/", root_vnode, NULL);

    // Add to mount list
    mnt->next = mounts;
    mounts = mnt;

    // global VFS root is the root of the "/" mount
    if (strcmp(target, "/") == 0)
        vfs_root = mnt->root;

    printk("vfs: mounted '%s' at '%s' (type: %s)\n",
           source ? source : "none", target, fstype);
    return 0;
}

int vfs_unmount(const char *target) {
    if (!target) return -1;

    mount_t **prev = &mounts;
    mount_t *curr = mounts;

    while (curr) {
        if (strcmp(curr->mountpoint, target) == 0) {
            // Call filesystem-specific unmount
            if (curr->fs_type->ops.unmount) {
                curr->fs_type->ops.unmount(curr);
            }

            // Remove from list
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
    if (!path) return NULL;

    mount_t *best_match = NULL;
    size_t best_len = 0;

    mount_t *curr = mounts;
    while (curr) {
        size_t mnt_len = strlen(curr->mountpoint);

        // Check if path starts with this mount point
        int matches = 1;
        for (size_t i = 0; i < mnt_len; i++) {
            if (path[i] != curr->mountpoint[i]) {
                matches = 0;
                break;
            }
        }

        if (matches && mnt_len > best_len) {
            // Root mount "/" matches any absolute path
            if (mnt_len == 1 && curr->mountpoint[0] == '/') {
                best_match = curr;
                best_len = mnt_len;
            // Non-root mount must be followed by '/' or end of string
            } else if (path[mnt_len] == '/' || path[mnt_len] == '\0') {
                best_match = curr;
                best_len = mnt_len;
            }
        }

        curr = curr->next;
    }

    return best_match;
}

// -----------------------------------------------------------------------------
// VFS-level operations
// -----------------------------------------------------------------------------

dentry_t *vfs_get_root(void) { return vfs_root; }

dentry_t *vfs_lookup(const char *path) {
    if (!path) return NULL;

    mount_t *mnt = vfs_find_mount(path);
    if (!mnt) {
        printk("vfs: no mount point for path '%s'\n", path);
        vfs_dump_mounts();
        return NULL;
    }

    // Get relative path within the mount
    const char *rel_path = path;
    size_t mnt_len = strlen(mnt->mountpoint);

    // Skip past mount point
    if (mnt_len > 1) {  // Not root mount
        rel_path += mnt_len;
        if (*rel_path == '/') rel_path++;  // Skip leading slash
    } else if (mnt->mountpoint[0] == '/' && path[0] == '/') {
        rel_path++;  // Skip leading slash for root mount
    }

    // ask the filesystem if this thing exists → vnode
    vnode_t *vnode = mnt->fs_type->ops.lookup(mnt, rel_path);
    if (!vnode) return NULL;

    // extract last component name
    const char *last = path;
    for (const char *p = path; *p; p++)
        if (*p == '/' && *(p + 1)) last = p + 1;
    const char *name = (path[0] == '/' && path[1] == '\0') ? "/" : last;

    // determine parent
    dentry_t *parent = NULL;
    // d_parent: root is its own parent; everything else recurses up
    if (path[0] == '/' && path[1] == '\0') {
        parent = NULL;   // we are root
    } else {
        char parent_path[256];
        if (dentry_get_parent_path(path, parent_path, sizeof(parent_path)) < 0)
            return NULL;  // root

        parent = vfs_lookup(parent_path);   // d_ref = 0
        if (!parent) parent = vfs_root;
        dentry_get(parent);                 // child owns a ref to parent
    }

    // create dentry
    // dentry_alloc: sets d_ref=0, copies name, sets d_parent
    dentry_t *d = dentry_alloc(name, vnode, parent);
    if (!d) {
        if (parent) dentry_put(parent);   // undo the get on failure
        kfree(vnode);
        return NULL;
    }
    return d; // d_ref still 0, unowned — caller calls dentry_get if keeping it
}

int vfs_create(const char *path, uint32_t mode) {
    if (!path) return -1;

    mount_t *mnt = vfs_find_mount(path);
    if (!mnt) return -1;

    const char *rel_path = path + strlen(mnt->mountpoint);
    if (*rel_path == '/') rel_path++;

    if (mnt->fs_type->ops.create) {
        return mnt->fs_type->ops.create(mnt, rel_path, mode);
    }

    return -1;
}

int vfs_unlink(const char *path) {
    if (!path) return -1;

    mount_t *mnt = vfs_find_mount(path);
    if (!mnt) return -1;

    const char *rel_path = path + strlen(mnt->mountpoint);
    if (*rel_path == '/') rel_path++;

    if (mnt->fs_type->ops.unlink) {
        return mnt->fs_type->ops.unlink(mnt, rel_path);
    }

    return -1;
}

int vfs_mkdir(const char *path, uint32_t mode) {
    if (!path) return -1;

    mount_t *mnt = vfs_find_mount(path);
    if (!mnt) return -1;

    const char *rel_path = path + strlen(mnt->mountpoint);
    if (*rel_path == '/') rel_path++;

    if (mnt->fs_type->ops.mkdir) {
        return mnt->fs_type->ops.mkdir(mnt, rel_path, mode);
    }

    return -1;
}

/*
 * vfs_ref — acquire a shared reference to an already-open file.
 *
 * Call this whenever a file_t is handed to a second owner WITHOUT going
 * through vfs_open (i.e. fork, dup).
 */
void vfs_ref(file_t *f) {
    if (!f) return;
    f->ref_count++;
}

// -----------------------------------------------------------------------------
// File operations
// -----------------------------------------------------------------------------

file_t *vfs_open(const char *path) {
    dentry_t *dentry = vfs_lookup(path);
    if (!dentry) {
        printk("vfs: file not found: %s\n", path);
        return NULL;
    }

    file_t *f = kmalloc(sizeof(file_t));
    if (!f) {
        printk("vfs: kmalloc failed in vfs_open\n");
        dentry_put(dentry);
        return NULL;
    }

    // Determine file type from filesystem name
    if (strcmp(dentry->d_vnode->fs_type->name, "tmpfs") == 0) {
        f->type = FILE_TMPFS;
    } else if (strcmp(dentry->d_vnode->fs_type->name, "tarfs") == 0) {
        f->type = FILE_TARFS;
    } else {
        f->type = FILE_TARFS;  // Default fallback
    }

    f->pipe = NULL;
    f->pipe_flags = 0;
    f->offset = 0;
    f->ref_count = 1;
    dentry_get(dentry);
    f->dentry = dentry;

    return f;
}

void vfs_close(file_t *f) {
    if (!f) return;

    if (f->ref_count <= 0) {
        printk("vfs: double-close detected (ref_count=%d)\n", f->ref_count);
        return;
    }

    f->ref_count--;
    if (f->ref_count == 0) {
        if (f->type == FILE_PIPE && f->pipe) {
            pipe_t *p = f->pipe;
            if (f->pipe_flags & PIPE_END_READ) {
                p->readers--;
            }
            if (f->pipe_flags & PIPE_END_WRITE) {
                p->writers--;
            }
            proc_wakeup(p);
            if (p->readers <= 0 && p->writers <= 0) {
                kfree(p);
            }
        }
        if (f->dentry) {
            dentry_put(f->dentry);
            f->dentry = NULL;
        }
        kfree(f);
    }
}

int vfs_read(file_t *f, void *buf, size_t len) {
    if (!f || !buf) return -1;

    switch (f->type) {
        case FILE_TARFS:
        case FILE_TMPFS: {
            if (f->dentry->d_vnode && f->dentry->d_vnode->mount && f->dentry->d_vnode->mount->fs_type->ops.read) {
                int n = f->dentry->d_vnode->mount->fs_type->ops.read(f->dentry->d_vnode, buf, f->offset, len);
                if (n > 0)
                    f->offset += (size_t)n;
                return n;
            }
            return -1;
        }

        case FILE_STDIN:
            return term_read(&console_term, buf, (uint32_t)len);

        case FILE_PIPE: {
            pipe_t *p = f->pipe;
            char *dst = (char *)buf;
            size_t nread = 0;

            if (!p || !(f->pipe_flags & PIPE_END_READ))
                return -1;

            while (nread < len) {
                while (p->count == 0) {
                    if (p->writers <= 0) {
                        return (int)nread;
                    }
                    proc_sleep(p);
                }

                set_csr_bit(sstatus, SSTATUS_SUM);
                dst[nread] = p->buffer[p->read_pos];
                clear_csr_bit(sstatus, SSTATUS_SUM);
                nread++;
                p->read_pos = (p->read_pos + 1) % PIPE_CAPACITY;
                p->count--;
                proc_wakeup(p);
            }

            return (int)nread;
        }

        default:
            return -1;
    }
}

int vfs_write(file_t *f, const void *buf, size_t len) {
    if (!f || !buf) return -1;

    switch (f->type) {
        case FILE_STDOUT: {
            return term_write(&console_term, buf, (uint32_t)len);
        }

        case FILE_PIPE: {
            pipe_t *p = f->pipe;
            const char *src = (const char *)buf;
            size_t nwritten = 0;

            if (!p || !(f->pipe_flags & PIPE_END_WRITE))
                return -1;

            while (nwritten < len) {
                if (p->readers <= 0) {
                    return nwritten > 0 ? (int)nwritten : -EPIPE;
                }

                while (p->count == PIPE_CAPACITY) {
                    if (p->readers <= 0) {
                        return nwritten > 0 ? (int)nwritten : -EPIPE;
                    }
                    proc_sleep(p);
                }

                set_csr_bit(sstatus, SSTATUS_SUM);
                p->buffer[p->write_pos] = src[nwritten];
                clear_csr_bit(sstatus, SSTATUS_SUM);
                nwritten++;
                p->write_pos = (p->write_pos + 1) % PIPE_CAPACITY;
                p->count++;
                proc_wakeup(p);
            }

            return (int)nwritten;
        }

        case FILE_TMPFS: {
            if (f->dentry->d_vnode && f->dentry->d_vnode->mount && f->dentry->d_vnode->mount->fs_type->ops.write) {
                int n = f->dentry->d_vnode->mount->fs_type->ops.write(f->dentry->d_vnode, buf, f->offset, len);
                if (n > 0)
                    f->offset += (size_t)n;
                return n;
            }
            return -1;
        }

        case FILE_TARFS:
            printk("vfs: write to read-only tarfs file\n");
            return -1;

        default:
            return -1;
    }
}

// Seek to a new offset in the file
int vfs_lseek(file_t *f, size_t offset, int whence) {
    if (!f) return -1;

    size_t new_offset = 0;
    switch (whence) {
        case SEEK_SET:
            new_offset = offset;
            break;
        case SEEK_CUR:
            new_offset = f->offset + offset;
            break;
        case SEEK_END:
            if (f->dentry && f->dentry->d_vnode)
                new_offset = f->dentry->d_vnode->size + offset;
            else
                return -1;
            break;
        default:
            return -1;
    }

    // Bound seek by vnode size for regular VFS-backed files.
    if (f->dentry && f->dentry->d_vnode && new_offset > f->dentry->d_vnode->size)
        return -1;

    f->offset = new_offset;
    return new_offset;
}

/*
 * vfs_is_dir — test whether 'path' is an existing directory.
 * Returns 1 if path is a directory, 0 if not, -1 on error.
 * The root path "/" is always a directory.
 */
int vfs_is_dir(const char *path) {
    dentry_t *d = vfs_lookup(path);
    return d && d->d_vnode->type == VNODE_DIR;
}

// -----------------------------------------------------------------------------
// fs_struct operations (at a process level)
// -----------------------------------------------------------------------------

void fs_struct_dump(const fs_struct_t *fs) {
    if (!fs) {
        printk("[fs_struct] NULL\n");
        return;
    }

    char root_path[256];
    char cwd_path[256];

    if (!fs->root) {
        printk("[fs_struct] root=NULL\n");
    } else {
        dentry_build_path(fs->root, fs->root, root_path, 256);
        printk("[fs_struct] root=%s (dentry=%p, d_ref=%d)\n",
               root_path, fs->root, fs->root->d_ref);
    }

    if (!fs->cwd) {
        printk("[fs_struct] cwd=NULL\n");
    } else {
        dentry_build_path(fs->cwd, fs->root, cwd_path, 256);
        printk("[fs_struct] cwd=%s (dentry=%p, d_ref=%d)\n",
               cwd_path, fs->cwd, fs->cwd->d_ref);
    }
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

// vfs.c — implementations

int fs_struct_set_cwd(fs_struct_t *fs, const char *abs_path) {
    if (!fs || !abs_path) return -1;

    dentry_t *d = vfs_lookup(abs_path);
    if (!d) return -ENOENT;

    if (d->d_vnode->type != VNODE_DIR) {
        dentry_put(d);
        return -ENOTDIR;
    }

    dentry_get(d);          // cwd slot acquires ownership
    dentry_put(fs->cwd);    // release old slot
    fs->cwd = d;
    return 0;
}

int fs_struct_set_root(fs_struct_t *fs, const char *abs_path) {
    if (!fs || !abs_path) return -1;

    dentry_t *d = vfs_lookup(abs_path);
    if (!d) return -ENOENT;

    if (d->d_vnode->type != VNODE_DIR) {
        dentry_put(d);
        return -ENOTDIR;
    }

    dentry_get(d);
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

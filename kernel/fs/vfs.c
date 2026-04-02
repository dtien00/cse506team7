#include "fs/tarfs.h"
#include "fs/vfs.h"
#include "kernel/types.h"
#include "kernel/printk.h"
#include "mm/kmalloc.h"
#include "lib/string.h"
#include "tty/term.h"

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

// -----------------------------------------------------------------------------
// Internal state
// -----------------------------------------------------------------------------

static fs_type_t *fs_types = NULL;  // Linked list of registered filesystems
static mount_t *mounts = NULL;      // Linked list of mount points

// -----------------------------------------------------------------------------
// VFS Initialization
// -----------------------------------------------------------------------------

void vfs_init(void) {
    fs_types = NULL;
    mounts = NULL;
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
    if (fs->ops.mount) {
        int ret = fs->ops.mount(mnt, source);
        if (ret < 0) {
            printk("vfs: mount operation failed for '%s'\n", fstype);
            kfree(mnt);
            return -1;
        }
    }

    // Add to mount list
    mnt->next = mounts;
    mounts = mnt;

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

vnode_t *vfs_lookup(const char *path) {
    if (!path) return NULL;

    mount_t *mnt = vfs_find_mount(path);
    if (!mnt) {
        printk("vfs: no mount point for path '%s'\n", path);
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

    // Call filesystem-specific lookup
    if (mnt->fs_type->ops.lookup) {
        return mnt->fs_type->ops.lookup(mnt, rel_path);
    }

    return NULL;
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
// File operations (backward compatible with existing code)
// -----------------------------------------------------------------------------

file_t *vfs_open(const char *path, int flags) {
    vnode_t *vnode = vfs_lookup(path);
    if (!vnode) {
        printk("vfs: file not found: %s\n", path);
        return NULL;
    }

    file_t *f = kmalloc(sizeof(file_t));
    if (!f) {
        printk("vfs: kmalloc failed in vfs_open\n");
        kfree(vnode);
        return NULL;
    }

    // Determine file type from filesystem name
    if (strcmp(vnode->fs_type->name, "tmpfs") == 0) {
        f->type = FILE_TMPFS;
    } else if (strcmp(vnode->fs_type->name, "tarfs") == 0) {
        f->type = FILE_TARFS;
    } else if (strcmp(vnode->fs_type->name, "ext2") == 0) {
        f->type = FILE_TMPFS;  // Treat ext2 like tmpfs for read/write
    } else {
        f->type = FILE_TARFS;  // Default fallback
    }

    f->entry = NULL;
    f->vnode = vnode;
    f->offset = 0;
    f->ref_count = 1;
    f->flags = flags;
    vnode->ref_count++;

    return f;
}

void vfs_close(file_t *f) {
    if (!f) return;

    f->ref_count--;
    if (f->ref_count <= 0) {
        if (f->vnode) {
            f->vnode->ref_count--;
            if (f->vnode->ref_count <= 0) {
                kfree(f->vnode);
            }
        }
        kfree(f);
    }
}

int vfs_read(file_t *f, void *buf, size_t len) {
    if (!f || !buf) return -1;

    switch (f->type) {
        case FILE_TARFS:
        case FILE_TMPFS: {
            if (f->vnode && f->vnode->mount && f->vnode->mount->fs_type->ops.read) {
                int n = f->vnode->mount->fs_type->ops.read(f->vnode, buf, f->offset, len);
                if (n > 0)
                    f->offset += (size_t)n;
                return n;
            }
            return -1;
        }

        case FILE_STDIN:
            return term_read(&console_term, buf, (uint32_t)len);

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

        case FILE_TMPFS: {
            if (f->vnode && f->vnode->mount && f->vnode->mount->fs_type->ops.write) {
                int n = f->vnode->mount->fs_type->ops.write(f->vnode, buf, f->offset, len);
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
            if (f->vnode)
                new_offset = f->vnode->size + offset;
            else
                return -1;
            break;
        default:
            return -1;
    }

    // Bound seek by vnode size for regular VFS-backed files.
    if (f->vnode && new_offset > f->vnode->size)
        return -1;

    f->offset = new_offset;
    return new_offset;
}
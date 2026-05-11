#include "fs/tmpfs.h"

#include "errno.h"
#include "fs/vfs.h"
#include "kernel/printk.h"
#include "kernel/types.h"
#include "lib/string.h"
#include "mm/kmalloc.h"

// -----------------------------------------------------------------------------
// Helper functions
// -----------------------------------------------------------------------------

static tmpfs_inode_t *tmpfs_alloc_inode(tmpfs_mount_data_t *mnt_data, const char *name, uint32_t type, uint32_t mode) {
    if (mnt_data->file_count >= TMPFS_MAX_FILES) {
        printk("tmpfs: max files reached\n");
        return NULL;
    }

    tmpfs_inode_t *inode = kzalloc(sizeof(tmpfs_inode_t));
    if (!inode) {
        printk("tmpfs: failed to allocate inode\n");
        return NULL;
    }

    inode->ino = mnt_data->next_ino++;
    inode->mode = mode;
    inode->type = type;
    inode->size = 0;
    inode->nlink = 1;
    inode->data = NULL;
    inode->capacity = 0;
    inode->symlink_target = NULL;
    inode->parent = NULL;
    inode->next = NULL;
    inode->children = NULL;
    inode->inode_body = NULL;

    if (name) {
        strncpy(inode->name, name, TMPFS_MAX_FILENAME - 1);
        inode->name[TMPFS_MAX_FILENAME - 1] = '\0';
    }

    mnt_data->file_count++;
    return inode;
}

static void tmpfs_free_inode(tmpfs_mount_data_t *mnt_data, tmpfs_inode_t *inode) {
    if (!inode)
        return;

    // If this is a hard-link dentry node, just free the dentry shell.
    // The canonical body's nlink is managed by the caller (tmpfs_unlink).
    if (inode->inode_body) {
        mnt_data->file_count--;
        kfree(inode);
        return;
    }

    // Canonical inode: decrement link count; only destroy when no names remain.
    if (inode->nlink > 0)
        inode->nlink--;
    if (inode->nlink > 0)
        return;

    if (inode->data)
        kfree(inode->data);

    if (inode->symlink_target)
        kfree(inode->symlink_target);

    // Recursively free children if it's a directory
    if (inode->type == TMPFS_TYPE_DIR) {
        tmpfs_inode_t *child = inode->children;
        while (child) {
            tmpfs_inode_t *next = child->next;
            tmpfs_free_inode(mnt_data, child);
            child = next;
        }
    }

    mnt_data->file_count--;
    kfree(inode);
}

// Split path into parent and filename
// e.g., "foo/bar/baz.txt" -> parent="foo/bar", name="baz.txt"
// Returns 0 on success, -1 on error
static int tmpfs_split_path(const char *path, char *parent, char *name, size_t bufsize) {
    if (!path || !parent || !name)
        return -1;

    size_t len = strlen(path);
    if (len == 0 || len >= bufsize)
        return -1;

    // Find last '/'
    const char *last_slash = NULL;
    for (size_t i = 0; i < len; i++) {
        if (path[i] == '/')
            last_slash = path + i;
    }

    if (!last_slash) {
        // No slash, file is in root
        parent[0] = '\0';
        strncpy(name, path, bufsize - 1);
        name[bufsize - 1] = '\0';
        return 0;
    }

    // Copy parent path
    size_t parent_len = last_slash - path;
    if (parent_len >= bufsize)
        return -1;
    memcpy(parent, path, parent_len);
    parent[parent_len] = '\0';

    // Copy filename
    strncpy(name, last_slash + 1, bufsize - 1);
    name[bufsize - 1] = '\0';

    return 0;
}

// Find inode by path within a tmpfs mount
static tmpfs_inode_t *tmpfs_find_inode(tmpfs_inode_t *root, const char *path) {
    if (!root || !path)
        return NULL;

    // Empty path or "/" means root
    if (path[0] == '\0' || (path[0] == '/' && path[1] == '\0')) {
        return root;
    }

    // Skip leading slash
    if (path[0] == '/')
        path++;

    // Parse path component by component
    char component[TMPFS_MAX_FILENAME];
    const char *next_slash = path;
    tmpfs_inode_t *current = root;

    while (*path) {
        // Find next component
        next_slash = path;
        while (*next_slash && *next_slash != '/')
            next_slash++;

        size_t comp_len = next_slash - path;
        if (comp_len == 0) {
            path = next_slash + 1;
            continue;
        }

        if (comp_len >= TMPFS_MAX_FILENAME)
            return NULL;
        memcpy(component, path, comp_len);
        component[comp_len] = '\0';

        // Search in current directory
        if (current->type != TMPFS_TYPE_DIR)
            return NULL;

        tmpfs_inode_t *child = current->children;
        int found = 0;
        while (child) {
            if (strcmp(child->name, component) == 0) {
                current = child;
                found = 1;
                break;
            }
            child = child->next;
        }

        if (!found)
            return NULL;

        // Move to next component
        path = next_slash;
        if (*path == '/')
            path++;
    }

    return current;
}

// Add a child inode to a directory
static int tmpfs_add_child(tmpfs_inode_t *parent, tmpfs_inode_t *child) {
    if (!parent || !child || parent->type != TMPFS_TYPE_DIR)
        return -1;

    child->parent = parent;
    child->next = parent->children;
    parent->children = child;

    return 0;
}

// Remove a child inode from a directory
static int tmpfs_remove_child(tmpfs_inode_t *parent, tmpfs_inode_t *child) {
    if (!parent || !child || parent->type != TMPFS_TYPE_DIR)
        return -1;

    tmpfs_inode_t **prev = &parent->children;
    tmpfs_inode_t *curr = parent->children;

    while (curr) {
        if (curr == child) {
            *prev = curr->next;
            curr->parent = NULL;
            curr->next = NULL;
            return 0;
        }
        prev = &curr->next;
        curr = curr->next;
    }

    return -1;
}

// -----------------------------------------------------------------------------
// tmpfs filesystem operations
// -----------------------------------------------------------------------------

static vnode_t *tmpfs_mount(mount_t *mnt, const char *source) {
    if (!mnt)
        return NULL;

    tmpfs_mount_data_t *mnt_data = kzalloc(sizeof(tmpfs_mount_data_t));
    if (!mnt_data) {
        printk("tmpfs: failed to allocate mount data\n");
        return NULL;
    }

    // Create root directory
    mnt_data->root = tmpfs_alloc_inode(mnt_data, "/", TMPFS_TYPE_DIR, 0755);
    if (!mnt_data->root) {
        kfree(mnt_data);
        return NULL;
    }

    mnt_data->next_ino = 2; // Root is inode 1, start at 2
    mnt_data->file_count = 1;

    mnt->fs_data = mnt_data;

    vnode_t *root = kzalloc(sizeof(vnode_t));
    if (!root) {
        tmpfs_free_inode(mnt_data, mnt_data->root);
        kfree(mnt_data);
        return NULL;
    }
    root->ino = 1;
    root->mode = 0755;
    root->size = 0;
    root->ref_count = 0;
    root->mount = mnt;
    root->fs_data = mnt_data->root;
    root->fs_type = mnt->fs_type;
    root->type = VNODE_DIR;

    printk("tmpfs: mounted successfully\n");
    return root;
}

static int tmpfs_unmount(mount_t *mnt) {
    if (!mnt || !mnt->fs_data)
        return -1;

    tmpfs_mount_data_t *mnt_data = (tmpfs_mount_data_t *) mnt->fs_data;

    // Free all inodes starting from root
    tmpfs_free_inode(mnt_data, mnt_data->root);

    kfree(mnt_data);
    mnt->fs_data = NULL;

    printk("tmpfs: unmounted successfully\n");
    return 0;
}

static vnode_t *tmpfs_lookup(mount_t *mnt, const char *path) {
    if (!mnt || !mnt->fs_data || !path)
        return NULL;

    tmpfs_mount_data_t *mnt_data = (tmpfs_mount_data_t *) mnt->fs_data;
    tmpfs_inode_t *inode = tmpfs_find_inode(mnt_data->root, path);

    if (!inode)
        return NULL;

    // Resolve to the canonical inode body (handles hard links).
    tmpfs_inode_t *body = inode->inode_body ? inode->inode_body : inode;

    // Allocate a vnode
    vnode_t *vnode = kzalloc(sizeof(vnode_t));
    if (!vnode)
        return NULL;

    vnode->ino = body->ino;
    vnode->mode = body->mode;
    vnode->size = body->size;
    vnode->nlink = body->nlink;
    vnode->ref_count = 0;
    vnode->mount = mnt;
    vnode->fs_data = body;  // always point to canonical body
    vnode->fs_type = mnt->fs_type;
    // printk("tmpfs_lookup: path='%s' ino=%u nlink=%u size=%u\n",
    //        path, body->ino, body->nlink, body->size);
    if (body->type == TMPFS_TYPE_DIR)
        vnode->type = VNODE_DIR;
    else if (body->type == TMPFS_TYPE_SYMLINK)
        vnode->type = VNODE_LNK;
    else
        vnode->type = VNODE_REG;

    return vnode;
}

static int tmpfs_read(vnode_t *vnode, void *buf, size_t offset, size_t len) {
    if (!vnode || !buf)
        return -1;

    tmpfs_inode_t *inode = (tmpfs_inode_t *) vnode->fs_data;
    if (!inode || inode->type != TMPFS_TYPE_FILE)
        return -1;

    if (offset >= inode->size)
        return 0; // Past EOF

    size_t available = inode->size - offset;
    size_t to_copy = len < available ? len : available;

    if (to_copy > 0 && inode->data) {
        memcpy(buf, inode->data + offset, to_copy);
    }

    // printk("tmpfs_read: inode='%s' ino=%u offset=%u len=%u inode_size=%u to_copy=%u\n",
    //        inode->name, inode->ino, (unsigned)offset, (unsigned)len, inode->size, (unsigned)to_copy);
    return (int) to_copy;
}

static int tmpfs_write(vnode_t *vnode, const void *buf, size_t offset, size_t len) {
    if (!vnode || !buf)
        return -1;

    tmpfs_inode_t *inode = (tmpfs_inode_t *) vnode->fs_data;
    if (!inode || inode->type != TMPFS_TYPE_FILE)
        return -1;

    size_t needed = offset + len;
    if (needed > TMPFS_MAX_FILESIZE) {
        printk("tmpfs: file size limit exceeded\n");
        return -1;
    }

    // Expand buffer if needed
    if (needed > inode->capacity) {
        size_t new_capacity = needed;
        // Round up to next power of 2 or page size
        if (new_capacity < 4096)
            new_capacity = 4096;
        else {
            new_capacity--;
            new_capacity |= new_capacity >> 1;
            new_capacity |= new_capacity >> 2;
            new_capacity |= new_capacity >> 4;
            new_capacity |= new_capacity >> 8;
            new_capacity |= new_capacity >> 16;
            new_capacity |= new_capacity >> 32;
            new_capacity++;
        }

        if (new_capacity > TMPFS_MAX_FILESIZE) {
            new_capacity = TMPFS_MAX_FILESIZE;
        }

        char *new_data = krealloc(inode->data, new_capacity);
        if (!new_data) {
            printk("tmpfs: failed to expand file buffer\n");
            return -1;
        }

        // Zero out the new space
        if (new_capacity > inode->capacity) {
            for (size_t i = inode->capacity; i < new_capacity; i++) {
                new_data[i] = 0;
            }
        }

        inode->data = new_data;
        inode->capacity = new_capacity;
    }

    // Write data
    memcpy(inode->data + offset, buf, len);

    // Update size if we wrote past the end
    if (needed > inode->size) {
        inode->size = needed;
        vnode->size = needed;
    }

    // printk("tmpfs_write: inode='%s' ino=%u offset=%u len=%u new_size=%u\n",
    //        inode->name, inode->ino, (unsigned)offset, (unsigned)len, inode->size);
    return (int) len;
}

static int tmpfs_create(mount_t *mnt, const char *path, uint32_t mode) {
    if (!mnt || !mnt->fs_data || !path)
        return -1;

    tmpfs_mount_data_t *mnt_data = (tmpfs_mount_data_t *) mnt->fs_data;

    // Split path into parent directory and filename
    char parent_path[TMPFS_MAX_FILENAME];
    char filename[TMPFS_MAX_FILENAME];

    if (tmpfs_split_path(path, parent_path, filename, TMPFS_MAX_FILENAME) < 0) {
        printk("tmpfs: invalid path '%s'\n", path);
        return -1;
    }

    // Find parent directory
    tmpfs_inode_t *parent = tmpfs_find_inode(mnt_data->root, parent_path);
    if (!parent) {
        printk("tmpfs: parent directory not found\n");
        return -1;
    }

    if (parent->type != TMPFS_TYPE_DIR) {
        printk("tmpfs: parent is not a directory\n");
        return -1;
    }

    // Check if file already exists
    tmpfs_inode_t *child = parent->children;
    while (child) {
        if (strcmp(child->name, filename) == 0) {
            printk("tmpfs: file already exists\n");
            return -1;
        }
        child = child->next;
    }

    // Create new file inode
    tmpfs_inode_t *new_file = tmpfs_alloc_inode(mnt_data, filename, TMPFS_TYPE_FILE, mode);
    if (!new_file)
        return -1;

    // Add to parent directory
    if (tmpfs_add_child(parent, new_file) < 0) {
        tmpfs_free_inode(mnt_data, new_file);
        return -1;
    }

    return 0;
}

static int tmpfs_unlink(mount_t *mnt, const char *path) {
    if (!mnt || !mnt->fs_data || !path)
        return -1;

    tmpfs_mount_data_t *mnt_data = (tmpfs_mount_data_t *) mnt->fs_data;

    // Split path into parent directory and filename
    char parent_path[TMPFS_MAX_FILENAME];
    char filename[TMPFS_MAX_FILENAME];

    if (tmpfs_split_path(path, parent_path, filename, TMPFS_MAX_FILENAME) < 0) {
        return -1;
    }

    // Find parent directory
    tmpfs_inode_t *parent = tmpfs_find_inode(mnt_data->root, parent_path);
    if (!parent || parent->type != TMPFS_TYPE_DIR)
        return -1;

    // Find the file to unlink
    tmpfs_inode_t *child = parent->children;
    while (child) {
        if (strcmp(child->name, filename) == 0) {
            if (child->type == TMPFS_TYPE_DIR && child->children != NULL) {
                printk("tmpfs: directory not empty\n");
                return -1;
            }

            tmpfs_inode_t *body = child->inode_body ? child->inode_body : child;
            tmpfs_remove_child(parent, child);
            body->nlink--;
            if (child->inode_body) {
                // Hard-link dentry shell: free the shell; free body if nlink hits 0.
                mnt_data->file_count--;
                kfree(child);
                if (body->nlink == 0) {
                    if (body->data) kfree(body->data);
                    mnt_data->file_count--;
                    kfree(body);
                }
            } else {
                if (body->nlink == 0)
                    tmpfs_free_inode(mnt_data, child);
            }
            return 0;
        }
        child = child->next;
    }

    printk("tmpfs: file not found\n");
    return -1;
}

static uint32_t tmpfs_get_nlink(vnode_t *vnode) {
    if (!vnode || !vnode->fs_data) return vnode ? vnode->nlink : 0;
    tmpfs_inode_t *body = (tmpfs_inode_t *)vnode->fs_data;
    // printk("tmpfs_get_nlink: inode='%s' ino=%u body->nlink=%u vnode->nlink=%u\n",
    //        body->name, body->ino, body->nlink, vnode->nlink);
    return body->nlink;
}

static int tmpfs_truncate(vnode_t *vnode, off_t length) {
    if (!vnode || length < 0) return -EINVAL;
    tmpfs_inode_t *inode = (tmpfs_inode_t *)vnode->fs_data;
    if (!inode) return -EINVAL;
    tmpfs_inode_t *body = inode->inode_body ? inode->inode_body : inode;
    if (body->type != TMPFS_TYPE_FILE) return -EINVAL;

    if ((uint32_t)length > body->capacity) {
        char *newdata = kmalloc((uint32_t)length);
        if (!newdata) return -ENOMEM;
        if (body->data && body->size > 0)
            memcpy(newdata, body->data, body->size);
        memset(newdata + body->size, 0, (uint32_t)length - body->size);
        kfree(body->data);
        body->data     = newdata;
        body->capacity = (uint32_t)length;
    }
    body->size  = (uint32_t)length;
    vnode->size = (uint32_t)length;
    return 0;
}

static int tmpfs_chmod(mount_t *mnt, vnode_t *vnode, mode_t mode) {
    (void)mnt;
    if (!vnode || !vnode->fs_data)
        return -EINVAL;

    tmpfs_inode_t *inode = (tmpfs_inode_t *)vnode->fs_data;
    inode->mode = mode & 0x0FFF;
    vnode->mode = inode->mode;
    return 0;
}

static int tmpfs_link(mount_t *mnt, const char *oldpath, const char *newpath) {
    if (!mnt || !mnt->fs_data || !oldpath || !newpath) return -EINVAL;

    tmpfs_mount_data_t *mnt_data = (tmpfs_mount_data_t *)mnt->fs_data;

    // Find the existing inode.
    tmpfs_inode_t *target = tmpfs_find_inode(mnt_data->root, oldpath);
    if (!target) {
        // printk("tmpfs_link: oldpath '%s' not found\n", oldpath);
        return -ENOENT;
    }
    if (target->type == TMPFS_TYPE_DIR) return -EPERM;

    // Find the parent of newpath and extract the new name.
    char parent_path[TMPFS_MAX_FILENAME];
    char new_name[TMPFS_MAX_FILENAME];
    if (tmpfs_split_path(newpath, parent_path, new_name, TMPFS_MAX_FILENAME) < 0)
        return -EINVAL;

    tmpfs_inode_t *new_parent = tmpfs_find_inode(mnt_data->root, parent_path);
    if (!new_parent || new_parent->type != TMPFS_TYPE_DIR) return -ENOENT;

    // Check new name does not already exist.
    for (tmpfs_inode_t *c = new_parent->children; c; c = c->next)
        if (strcmp(c->name, new_name) == 0) return -EEXIST;

    // Resolve to canonical body (in case target is itself a hard-link dentry).
    tmpfs_inode_t *body = target->inode_body ? target->inode_body : target;

    // Create a lightweight dentry node that delegates to the canonical body.
    tmpfs_inode_t *link_node = kzalloc(sizeof(tmpfs_inode_t));
    if (!link_node) return -ENOMEM;
    strncpy(link_node->name, new_name, TMPFS_MAX_FILENAME - 1);
    link_node->name[TMPFS_MAX_FILENAME - 1] = '\0';
    link_node->type = body->type;
    link_node->next = NULL;
    link_node->parent = NULL;
    link_node->children = NULL;
    link_node->inode_body = body;  // point to shared canonical inode

    body->nlink++;
    // printk("tmpfs_link: '%s' -> '%s', body ino=%u nlink now %u\n",
    //        oldpath, newpath, body->ino, body->nlink);

    tmpfs_add_child(new_parent, link_node);
    mnt_data->file_count++;
    return 0;
}

static int tmpfs_symlink(mount_t *mnt, const char *target, const char *linkpath) {
    if (!mnt || !mnt->fs_data || !target || !linkpath) return -EINVAL;

    tmpfs_mount_data_t *mnt_data = (tmpfs_mount_data_t *)mnt->fs_data;

    char parent_path[TMPFS_MAX_FILENAME];
    char link_name[TMPFS_MAX_FILENAME];
    if (tmpfs_split_path(linkpath, parent_path, link_name, TMPFS_MAX_FILENAME) < 0)
        return -EINVAL;

    tmpfs_inode_t *parent = tmpfs_find_inode(mnt_data->root, parent_path);
    if (!parent || parent->type != TMPFS_TYPE_DIR) return -ENOENT;

    // Check for name collision.
    for (tmpfs_inode_t *c = parent->children; c; c = c->next)
        if (strcmp(c->name, link_name) == 0) return -EEXIST;

    tmpfs_inode_t *sym = tmpfs_alloc_inode(mnt_data, link_name,
                                            TMPFS_TYPE_SYMLINK, 0777);
    if (!sym) return -ENOMEM;

    size_t tlen = strlen(target);
    sym->symlink_target = kmalloc(tlen + 1);
    if (!sym->symlink_target) {
        tmpfs_free_inode(mnt_data, sym);
        return -ENOMEM;
    }
    memcpy(sym->symlink_target, target, tlen + 1);
    sym->size = (uint32_t)tlen;

    tmpfs_add_child(parent, sym);
    return 0;
}

static int tmpfs_readlink(vnode_t *vnode, char *buf, size_t size) {
    if (!vnode || !buf || size == 0) return -EINVAL;

    tmpfs_inode_t *inode = (tmpfs_inode_t *)vnode->fs_data;
    if (!inode || inode->type != TMPFS_TYPE_SYMLINK || !inode->symlink_target)
        return -EINVAL;

    size_t len = strlen(inode->symlink_target);
    if (len > size) len = size;
    memcpy(buf, inode->symlink_target, len);
    // readlink does NOT null-terminate (POSIX); return byte count.
    return (int)len;
}

static int tmpfs_readdir(mount_t *mnt, vnode_t *vnode, uint32_t start_idx, void *buf, uint32_t bufsize,
                         uint32_t *entries_out) {
    if (!mnt || !vnode)
        return -EINVAL;

    // vnode->private set at lookup/create time — already the inode
    tmpfs_inode_t *dir = (tmpfs_inode_t *) vnode->fs_data;
    if (!dir)
        return -ENOENT;
    if (dir->type != TMPFS_TYPE_DIR)
        return -ENOTDIR;

    char *out = (char *) buf;
    uint32_t written = 0;
    uint32_t found = 0;
    uint32_t idx = 0;

    /* emit "." and ".." as virtual entries at index 0 and 1 */
    struct { const char *name; uint32_t ino; } dots[2] = {
        { ".",  dir->ino },
        { "..", dir->parent ? dir->parent->ino : dir->ino },
    };
    for (int di = 0; di < 2; di++) {
        if (idx++ < start_idx)
            continue;
        size_t nlen = strlen(dots[di].name);
        uint16_t reclen = FS_DENT_RECLEN(nlen);
        if (written + reclen > bufsize)
            goto done;
        struct fs_dent *d = (struct fs_dent *) (out + written);
        memset(d, 0, reclen);
        d->d_ino = dots[di].ino;
        d->d_reclen = reclen;
        d->d_type = DT_DIR;
        memcpy(d->d_name, dots[di].name, nlen + 1);
        written += reclen;
        found++;
    }

    for (tmpfs_inode_t *child = dir->children; child; child = child->next) {
        if (idx++ < start_idx)
            continue;

        size_t nlen = strlen(child->name);
        uint16_t reclen = FS_DENT_RECLEN(nlen);
        if (written + reclen > bufsize)
            break;

        struct fs_dent *d = (struct fs_dent *) (out + written);
        memset(d, 0, reclen);
        d->d_ino = child->ino;
        d->d_reclen = reclen;
        if (child->type == TMPFS_TYPE_DIR)
            d->d_type = DT_DIR;
        else if (child->type == TMPFS_TYPE_SYMLINK)
            d->d_type = DT_LNK;
        else
            d->d_type = DT_REG;
        memcpy(d->d_name, child->name, nlen + 1);

        written += reclen;
        found++;
    }
done:

    if (entries_out)
        *entries_out = found;
    return (int) written;
}

static int tmpfs_mkdir(mount_t *mnt, const char *path, uint32_t mode) {
    if (!mnt || !mnt->fs_data || !path)
        return -1;

    tmpfs_mount_data_t *mnt_data = (tmpfs_mount_data_t *) mnt->fs_data;

    // Split path into parent directory and dirname
    char parent_path[TMPFS_MAX_FILENAME];
    char dirname[TMPFS_MAX_FILENAME];

    if (tmpfs_split_path(path, parent_path, dirname, TMPFS_MAX_FILENAME) < 0) {
        printk("tmpfs: invalid path '%s'\n", path);
        return -1;
    }

    // Find parent directory
    tmpfs_inode_t *parent = tmpfs_find_inode(mnt_data->root, parent_path);
    if (!parent) {
        printk("tmpfs: parent directory not found\n");
        return -1;
    }

    if (parent->type != TMPFS_TYPE_DIR) {
        printk("tmpfs: parent is not a directory\n");
        return -1;
    }

    // Check if directory already exists
    tmpfs_inode_t *child = parent->children;
    while (child) {
        if (strcmp(child->name, dirname) == 0) {
            printk("tmpfs: directory already exists\n");
            return -1;
        }
        child = child->next;
    }

    // Create new directory inode
    tmpfs_inode_t *new_dir = tmpfs_alloc_inode(mnt_data, dirname, TMPFS_TYPE_DIR, mode);
    if (!new_dir)
        return -1;

    // Add to parent directory
    if (tmpfs_add_child(parent, new_dir) < 0) {
        tmpfs_free_inode(mnt_data, new_dir);
        return -1;
    }

    return 0;
}

/*
 * tmpfs_rename — POSIX rename within a tmpfs mount.
 * Both paths are relative to the mount root (no leading /).
 *
 * POSIX guarantees:
 *   - Atomic: if newpath exists it is replaced in one step (no window where
 *     neither name exists).
 *   - src == dst (same relative path): handled by vfs_rename before we arrive.
 *   - Replacing a dir: newpath must be an empty directory.
 *   - Replacing a file: newpath must be a regular file (not a dir).
 *   - Moving a dir: update ".." in the moved dir to point at new parent.
 */
static int tmpfs_rename(mount_t *mnt, const char *oldpath, const char *newpath) {
    if (!mnt || !mnt->fs_data || !oldpath || !newpath) return -EINVAL;

    tmpfs_mount_data_t *mnt_data = (tmpfs_mount_data_t *)mnt->fs_data;

    /* --- resolve src --- */
    char src_parent_path[TMPFS_MAX_FILENAME];
    char src_name[TMPFS_MAX_FILENAME];
    if (tmpfs_split_path(oldpath, src_parent_path, src_name, TMPFS_MAX_FILENAME) < 0)
        return -ENAMETOOLONG;

    tmpfs_inode_t *src_parent = tmpfs_find_inode(mnt_data->root, src_parent_path);
    if (!src_parent || src_parent->type != TMPFS_TYPE_DIR)
        return -ENOENT;

    tmpfs_inode_t *src = NULL;
    for (tmpfs_inode_t *c = src_parent->children; c; c = c->next) {
        if (strcmp(c->name, src_name) == 0) { src = c; break; }
    }
    if (!src) return -ENOENT;

    /* --- resolve dst parent --- */
    char dst_parent_path[TMPFS_MAX_FILENAME];
    char dst_name[TMPFS_MAX_FILENAME];
    if (tmpfs_split_path(newpath, dst_parent_path, dst_name, TMPFS_MAX_FILENAME) < 0)
        return -ENAMETOOLONG;

    tmpfs_inode_t *dst_parent = tmpfs_find_inode(mnt_data->root, dst_parent_path);
    if (!dst_parent || dst_parent->type != TMPFS_TYPE_DIR)
        return -ENOENT;

    /* --- check for dst collision --- */
    tmpfs_inode_t *dst = NULL;
    for (tmpfs_inode_t *c = dst_parent->children; c; c = c->next) {
        if (strcmp(c->name, dst_name) == 0) { dst = c; break; }
    }

    if (dst) {
        tmpfs_inode_t *dst_body = dst->inode_body ? dst->inode_body : dst;
        tmpfs_inode_t *src_body = src->inode_body ? src->inode_body : src;

        /* Same inode — same-name rename already handled above; hard-link alias. */
        if (src_body == dst_body)
            return 0;

        /* Type compatibility: POSIX EISDIR / ENOTDIR */
        if (src_body->type == TMPFS_TYPE_DIR && dst_body->type != TMPFS_TYPE_DIR)
            return -ENOTDIR;
        if (src_body->type != TMPFS_TYPE_DIR && dst_body->type == TMPFS_TYPE_DIR)
            return -EISDIR;

        /* Replacing a directory: it must be empty. */
        if (dst_body->type == TMPFS_TYPE_DIR && dst_body->children != NULL)
            return -ENOTEMPTY;

        /* Atomically remove dst from dst_parent. */
        tmpfs_remove_child(dst_parent, dst);
        dst_body->nlink--;
        if (dst->inode_body) {
            mnt_data->file_count--;
            kfree(dst);
            if (dst_body->nlink == 0) {
                if (dst_body->data) kfree(dst_body->data);
                mnt_data->file_count--;
                kfree(dst_body);
            }
        } else {
            if (dst_body->nlink == 0)
                tmpfs_free_inode(mnt_data, dst_body);
        }
    }

    /* --- move: detach src from old parent, rename, attach to dst parent --- */
    tmpfs_remove_child(src_parent, src);
    strncpy(src->name, dst_name, TMPFS_MAX_FILENAME - 1);
    src->name[TMPFS_MAX_FILENAME - 1] = '\0';
    tmpfs_add_child(dst_parent, src);

    /* --- if moving a directory, update its ".." back-pointer --- */
    tmpfs_inode_t *src_body = src->inode_body ? src->inode_body : src;
    if (src_body->type == TMPFS_TYPE_DIR)
        src_body->parent = dst_parent;

    return 0;
}

// -----------------------------------------------------------------------------
// tmpfs filesystem type registration
// -----------------------------------------------------------------------------

static fs_type_t tmpfs_type = {.name = "tmpfs",
                               .ops =
                                       {
                                               .mount = tmpfs_mount,
                                               .unmount = tmpfs_unmount,
                                               .lookup = tmpfs_lookup,
                                               .read = tmpfs_read,
                                               .write = tmpfs_write,
                                               .create = tmpfs_create,
                                               .unlink = tmpfs_unlink,
                                               .mkdir = tmpfs_mkdir,
                                               .rmdir = tmpfs_unlink,
                                               .readdir = tmpfs_readdir,
                                               .rename = tmpfs_rename,
                                               .link = tmpfs_link,
                                               .symlink = tmpfs_symlink,
                                               .readlink = tmpfs_readlink,
                                               .truncate = tmpfs_truncate,
                                               .chmod = tmpfs_chmod,
                                               .get_nlink = tmpfs_get_nlink,
                                       },
                               .next = NULL};

void tmpfs_init(void) {
    vfs_register_fs(&tmpfs_type);
    printk("tmpfs: registered\n");
}

fs_type_t *tmpfs_get_type(void) { return &tmpfs_type; }

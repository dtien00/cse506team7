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
    inode->data = NULL;
    inode->capacity = 0;
    inode->parent = NULL;
    inode->next = NULL;
    inode->children = NULL;

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

    if (inode->data) {
        kfree(inode->data);
    }

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

    // Allocate a vnode
    vnode_t *vnode = kzalloc(sizeof(vnode_t));
    if (!vnode)
        return NULL;

    vnode->ino = inode->ino;
    vnode->mode = inode->mode;
    vnode->size = inode->size;
    vnode->ref_count = 0;
    vnode->mount = mnt;
    vnode->fs_data = inode;
    vnode->fs_type = mnt->fs_type;
    vnode->type = (inode->type == TMPFS_TYPE_DIR) ? VNODE_DIR : VNODE_REG;

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

            tmpfs_remove_child(parent, child);
            tmpfs_free_inode(mnt_data, child);
            return 0;
        }
        child = child->next;
    }

    printk("tmpfs: file not found\n");
    return -1;
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
        d->d_type = (child->type == TMPFS_TYPE_DIR) ? DT_DIR : DT_REG;
        memcpy(d->d_name, child->name, nlen + 1);

        written += reclen;
        found++;
    }

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
                                       },
                               .next = NULL};

void tmpfs_init(void) {
    vfs_register_fs(&tmpfs_type);
    printk("tmpfs: registered\n");
}

fs_type_t *tmpfs_get_type(void) { return &tmpfs_type; }
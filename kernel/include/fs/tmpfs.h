#pragma once
#include "kernel/types.h"
#include "fs/vfs.h"

#define TMPFS_MAX_FILES 128
#define TMPFS_MAX_FILENAME 256
#define TMPFS_MAX_FILESIZE (1024 * 1024)  // 1MB max per file

// File types for tmpfs
#define TMPFS_TYPE_FILE 0x1
#define TMPFS_TYPE_DIR  0x2

// tmpfs inode structure
typedef struct tmpfs_inode {
    uint64_t ino;                      // Inode number
    uint32_t mode;                     // File mode/permissions
    uint32_t type;                     // TMPFS_TYPE_FILE or TMPFS_TYPE_DIR
    uint32_t size;                     // File size
    char name[TMPFS_MAX_FILENAME];     // Filename
    char *data;                        // File data (for regular files)
    uint32_t capacity;                 // Allocated capacity for data
    struct tmpfs_inode *parent;        // Parent directory
    struct tmpfs_inode *next;          // Next sibling in directory
    struct tmpfs_inode *children;      // First child (for directories)
} tmpfs_inode_t;

// tmpfs mount data
typedef struct tmpfs_mount_data {
    tmpfs_inode_t *root;               // Root directory inode
    uint64_t next_ino;                 // Next inode number to allocate
    uint32_t file_count;               // Number of files in filesystem
} tmpfs_mount_data_t;

// Function declarations
void tmpfs_init(void);
int tmpfs_mount(mount_t *mnt, const char *source);
int tmpfs_unmount(mount_t *mnt);
vnode_t *tmpfs_lookup(mount_t *mnt, const char *path);
int tmpfs_read(vnode_t *vnode, void *buf, size_t offset, size_t len);
int tmpfs_write(vnode_t *vnode, const void *buf, size_t offset, size_t len);
int tmpfs_create(mount_t *mnt, const char *path, uint32_t mode);
int tmpfs_unlink(mount_t *mnt, const char *path);
int tmpfs_mkdir(mount_t *mnt, const char *path, uint32_t mode);

// Get the registered tmpfs filesystem type
fs_type_t *tmpfs_get_type(void);

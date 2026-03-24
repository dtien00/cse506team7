// vfs.h
#pragma once
#include "kernel/types.h"

// Forward declarations
struct vnode;
struct mount;
struct filesystem_type;

// File types
typedef enum {
    FILE_TARFS,
    FILE_TMPFS,
    FILE_STDIN,
    FILE_STDOUT
} file_type_t;

// VFS node - represents an inode in the VFS layer
typedef struct vnode {
    uint64_t ino;                    // Inode number
    uint32_t mode;                   // File mode (permissions, type)
    uint32_t size;                   // File size in bytes
    uint32_t ref_count;              // Reference count
    struct mount *mount;             // Mount point this vnode belongs to
    void *fs_data;                   // Filesystem-specific data
    struct filesystem_type *fs_type; // Filesystem type
} vnode_t;

// A generic open file
typedef struct file {
    file_type_t type;
    vnode_t *vnode;
    size_t offset;
    int ref_count;
} file_t;

// Filesystem operations
typedef struct filesystem_operations {
    int (*mount)(struct mount *mnt, const char *source);
    int (*unmount)(struct mount *mnt);
    vnode_t* (*lookup)(struct mount *mnt, const char *path);
    int (*read)(vnode_t *vnode, void *buf, size_t offset, size_t len);
    int (*write)(vnode_t *vnode, const void *buf, size_t offset, size_t len);
    int (*create)(struct mount *mnt, const char *path, uint32_t mode);
    int (*unlink)(struct mount *mnt, const char *path);
    int (*mkdir)(struct mount *mnt, const char *path, uint32_t mode);
} fs_ops_t;

// Filesystem type
typedef struct filesystem_type {
    const char *name;
    fs_ops_t ops;
    struct filesystem_type *next;
} fs_type_t;

// Mount point
typedef struct mount {
    char mountpoint[256];            // Where it's mounted (e.g., "/", "/tmp")
    fs_type_t *fs_type;              // Filesystem type
    void *fs_data;                   // Filesystem-specific mount data
    struct mount *next;              // Next mount in the list
} mount_t;

// VFS initialization and management
void vfs_init(void);
int vfs_register_fs(fs_type_t *fs_type);
int vfs_mount(const char *source, const char *target, const char *fstype);
int vfs_unmount(const char *target);
mount_t *vfs_find_mount(const char *path);

// File operations
file_t *vfs_open(const char *path);
void    vfs_close(file_t *f);
int     vfs_read(file_t *f, void *buf, size_t len);
int     vfs_write(file_t *f, const void *buf, size_t len);

// VFS-level operations
vnode_t *vfs_lookup(const char *path);
int vfs_create(const char *path, uint32_t mode);
int vfs_unlink(const char *path);
int vfs_mkdir(const char *path, uint32_t mode);
// vfs.h
#pragma once
#include "fs/file.h"
#include "kernel/types.h"
#include "kernel/limits.h"

// Forward declarations
struct vnode;
struct mount;
struct filesystem_type;

typedef unsigned long ino_t; // inode number

typedef enum {
    VNODE_REG, // regular file
    VNODE_DIR, // directory
    VNODE_LNK  // symbolic link
} vnode_type_t;

#define VFS_PERM_EXEC  0x1
#define VFS_PERM_WRITE 0x2
#define VFS_PERM_READ  0x4

#define PIPE_CAPACITY  SBUNIX_PIPE_BUF   /* 4096 */
#define PIPE_END_READ 0x1
#define PIPE_END_WRITE 0x2

typedef struct pipe {
    char buffer[PIPE_CAPACITY];
    size_t read_pos;
    size_t write_pos;
    size_t count;
    int readers;
    int writers;
} pipe_t;

// VFS node - represents an inode in the VFS layer
typedef struct vnode {
    vnode_type_t type;
    uint64_t ino; // Inode number
    uint32_t mode; // File mode (permissions, type)
    uint32_t size; // File size in bytes
    uint32_t nlink; // count number of links to this file
    uint32_t ref_count; // Reference count
    time_t atime;
    time_t mtime;
    time_t ctime;
    struct mount *mount; // Mount point this vnode belongs to - also stores st_dev
    void *fs_data; // Filesystem-specific data
    struct filesystem_type *fs_type; // Filesystem type
} vnode_t;

// Filesystem operations
typedef struct filesystem_operations {
    vnode_t *(*mount)(struct mount *mnt, const char *source); // should return the root vnode of the file system mounted
    int (*unmount)(struct mount *mnt);
    vnode_t *(*lookup)(struct mount *mnt, const char *path);
    int (*read)(vnode_t *vnode, void *buf, size_t offset, size_t len);
    int (*write)(vnode_t *vnode, const void *buf, size_t offset, size_t len);
    int (*create)(struct mount *mnt, const char *path, uint32_t mode);
    int (*unlink)(struct mount *mnt, const char *path);
    int (*mkdir)(struct mount *mnt, const char *path, uint32_t mode);
    int (*truncate)(vnode_t *vnode);
    int (*readdir)(struct mount *mnt, vnode_t *vnode, uint32_t start_idx, void *buf, uint32_t buf_size,
                   uint32_t *entries_out);
    int (*chmod)(struct mount *mnt, vnode_t *vnode, mode_t mode);
    int (*rmdir)(struct mount *mnt, const char *path);
    int (*link)(struct mount *mnt, const char *oldpath, const char *newpath);
    int (*symlink)(struct mount *mnt, const char *target, const char *linkpath);
    int (*readlink)(vnode_t *vnode, char *buf, size_t size);
    uint32_t (*get_nlink)(vnode_t *vnode); // optional: return fresh nlink from fs
} fs_ops_t;

typedef struct fs_struct {
    dentry_t *root; // process root (usually global root)
    dentry_t *cwd; // current working directory
} fs_struct_t;

// Filesystem type
typedef struct filesystem_type {
    const char *name;
    fs_ops_t ops;
    struct filesystem_type *next;
} fs_type_t;

// Mount point
typedef struct mount {
    char mountpoint[256]; // Where it's mounted (e.g., "/", "/tmp")
    uint32_t dev_id; // simple numeric device id, starts from 1
    fs_type_t *fs_type; // Filesystem type
    void *fs_data; // Filesystem-specific mount data
    dentry_t *root;
    dentry_t *cover;
    struct mount *next; // Next mount in the list
} mount_t;

// VFS initialization and management
void vfs_init(void);
int vfs_register_fs(fs_type_t *fs_type);
int vfs_mount(const char *source, const char *target, const char *fstype);
int vfs_unmount(const char *target);
mount_t *vfs_find_mount(const char *path);
dentry_t *vfs_find_cover_for_root(dentry_t *root_dentry);
void vnode_put(vnode_t *vn);

// VFS-level operations
dentry_t *vfs_get_root(void);
int vfs_lookup_from(dentry_t *start, const char *path, dentry_t **out);
int vfs_lookup(const char *path, dentry_t **out);
int vfs_may_access(vnode_t *vn, int perms);
int vfs_create(const char *path, uint32_t mode);
int vfs_unlink(const char *path);
int vfs_mkdir(const char *path, uint32_t mode);
int vfs_chmod(const char *path, mode_t mode);
int vfs_fchmod(vnode_t *vn, mode_t mode);
int vfs_rmdir(const char *path);
int vfs_chdir(const char *kpath);
int vfs_readdir(dentry_t *dentry, uint32_t start_idx, void *buf, uint32_t bufsize, uint32_t *entries_out);
int vfs_link(const char *oldpath, const char *newpath);
int vfs_symlink(const char *target, const char *linkpath);
ssize_t vfs_readlink(const char *path, char *buf, size_t size);
int vfs_lookup_nofollow_pub(dentry_t *start, const char *path, dentry_t **out);

fs_struct_t *fs_struct_alloc(void);
fs_struct_t *fs_struct_copy(fs_struct_t *src); // used in fork
void fs_struct_free(fs_struct_t *fs);
void fs_struct_dump(const fs_struct_t *fs);

// Set cwd to a new dentry (resolved path)
int fs_struct_set_cwd(fs_struct_t *fs, const char *abs_path);

// Set root (e.g. for chroot)
int fs_struct_set_root(fs_struct_t *fs, const char *abs_path);

// Get the current cwd dentry — read only, no ref bump.
// Caller must NOT store this pointer long-term without dentry_get.
int fs_struct_get_cwd(fs_struct_t *fs, char *buf, size_t size);
int fs_struct_get_root(fs_struct_t *fs, char *buf, size_t size);

// fs_dent

typedef uint16_t reclen_t;

typedef struct fs_dent {
    ino_t d_ino; // file serial number
    reclen_t d_reclen; // bytes occupied by this entry in buf, including padding
    unsigned char d_type; // file type
    char d_name[]; // null-terminated filename (flexible array member)
} fs_dent_t;

// d_type values
#define DT_UNKNOWN 0
#define DT_DIR 4
#define DT_REG 8
#define DT_LNK 10

// d_ino + d_reclen + d_type = 8 + 2 + 1 = 11 bytes of fixed header.
// d_name is variable. Pad the whole record to alignof(fs_dent_t) = 8 bytes.
#define FS_DENT_RECLEN(namelen) ((reclen_t) (((sizeof(fs_dent_t) + (namelen) + 1) + 7u) & ~7u))

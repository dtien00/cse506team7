// vfs.h
#pragma once
#include "fs/file.h"
#include "kernel/types.h"
// #include "types.h"

// Forward declarations
struct vnode;
struct mount;
struct filesystem_type;

typedef unsigned long  ino_t;   // inode number

typedef enum {
    VNODE_REG, // regular file
    VNODE_DIR // directory
} vnode_type_t;

#define PIPE_CAPACITY 4096
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
    struct mount *next; // Next mount in the list
} mount_t;

// VFS initialization and management
void vfs_init(void);
int vfs_register_fs(fs_type_t *fs_type);
int vfs_mount(const char *source, const char *target, const char *fstype);
int vfs_unmount(const char *target);
mount_t *vfs_find_mount(const char *path);
void vnode_put(vnode_t *vn);

// VFS-level operations
dentry_t *vfs_get_root(void);
dentry_t *vfs_lookup(const char *path);
int vfs_create(const char *path, uint32_t mode);
int vfs_unlink(const char *path);
int vfs_mkdir(const char *path, uint32_t mode);
int vfs_chdir(const char *kpath);
int vfs_readdir(vnode_t *vnode, uint32_t start_idx, void *buf, uint32_t bufsize, uint32_t *entries_out);

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

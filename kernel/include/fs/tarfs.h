
#pragma once
#include "kernel/types.h"

#define TARFS_MAX_ENTRIES 64
#define TAR_BLOCK_SIZE    512

// The tar header structure (512 bytes)
typedef struct tar_header {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];   // octal string
    char mtime[12];
    char checksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];   // "ustar"
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char pad[12];    // pad to 512 bytes
} tar_header_t;

// A parsed/indexed file entry
typedef struct tarfs_entry {
    tar_header_t *header;
    void *data;      // pointer into kernel memory
    size_t size;
} tarfs_entry_t;

// Forward declaration
struct mount;
struct vnode;
struct filesystem_type;

// Public API
void tarfs_init(void);
struct filesystem_type *tarfs_get_type(void);

// VFS interface
int tarfs_mount(struct mount *mnt, const char *source);
int tarfs_unmount(struct mount *mnt);
struct vnode *tarfs_lookup(struct mount *mnt, const char *path);
int tarfs_read(struct vnode *vnode, void *buf, size_t offset, size_t len);
int tarfs_is_dir(struct mount *mnt, const char *rel_path);
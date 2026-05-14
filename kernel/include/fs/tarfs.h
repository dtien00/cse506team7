#pragma once
#include "kernel/types.h"

#define TARFS_MAX_ENTRIES 256
#define TAR_BLOCK_SIZE 512

// The tar header structure (512 bytes)
typedef struct tar_header {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12]; // octal string
    char mtime[12];
    char checksum[8];
    char typeflag;
    char linkname[100];
    char magic[6]; // "ustar"
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char pad[12]; // pad to 512 bytes
} tar_header_t;

// A parsed/indexed file entry
typedef struct tarfs_entry {
    tar_header_t *header;
    void *data; // pointer into kernel memory
    size_t size;
    const char *symlink_target; // non-NULL for typeflag '2'; points into header->linkname
} tarfs_entry_t;

// Forward declaration
struct mount;
struct vnode;
struct filesystem_type;

// Public API
void tarfs_init(void);
struct filesystem_type *tarfs_get_type(void);

// Copy all tarfs entries into /mnt/initrd/ on the ext2 mount (one-time).
// Does nothing if the superblock flag (s_reserved[203] == 0xAB) is already set.
void tarfs_populate_ext2(struct mount *ext2_mnt);
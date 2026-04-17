#pragma once
#include "kernel/types.h"
#include "fs/vfs.h"

#define TMPFS_MAX_FILES 128
#define TMPFS_MAX_FILENAME 256
#define TMPFS_MAX_FILESIZE (1024 * 1024)  // 1MB max per file

// File types for tmpfs
#define TMPFS_TYPE_FILE    0x1
#define TMPFS_TYPE_DIR     0x2
#define TMPFS_TYPE_SYMLINK 0x3

// tmpfs inode structure
typedef struct tmpfs_inode {
    uint64_t ino;                      // Inode number
    uint32_t mode;                     // File mode/permissions
    uint32_t type;                     // TMPFS_TYPE_FILE, TMPFS_TYPE_DIR, or TMPFS_TYPE_SYMLINK
    uint32_t size;                     // File size (canonical; hard link dentries use body->size)
    uint32_t nlink;                    // Hard link count (canonical)
    char name[TMPFS_MAX_FILENAME];     // Filename (this directory entry's name)
    char *data;                        // File data (for regular files)
    uint32_t capacity;                 // Allocated capacity for data
    char *symlink_target;              // Non-NULL only for TMPFS_TYPE_SYMLINK
    struct tmpfs_inode *parent;        // Parent directory
    struct tmpfs_inode *next;          // Next sibling in directory
    struct tmpfs_inode *children;      // First child (for directories)
    // For hard links: points to the canonical inode that owns data/size/nlink.
    // NULL for canonical inodes (they are their own body).
    struct tmpfs_inode *inode_body;
} tmpfs_inode_t;

// tmpfs mount data
typedef struct tmpfs_mount_data {
    tmpfs_inode_t *root;               // Root directory inode
    uint64_t next_ino;                 // Next inode number to allocate
    uint32_t file_count;               // Number of files in filesystem
} tmpfs_mount_data_t;

// Function declarations
void tmpfs_init(void);

// Get the registered tmpfs filesystem type
fs_type_t *tmpfs_get_type(void);
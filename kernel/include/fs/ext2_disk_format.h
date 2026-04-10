#pragma once
#include <stdint.h>

// ext2 magic number
#define EXT2_SUPER_MAGIC    0xEF53

// Block size (we'll use 1KB for simplicity)
#define EXT2_BLOCK_SIZE     1024

// Inode size
#define EXT2_INODE_SIZE     128

// Superblock location (starts at byte 1024)
#define EXT2_SUPERBLOCK_OFFSET  1024

// File types
#define EXT2_FT_UNKNOWN     0
#define EXT2_FT_REG_FILE    1
#define EXT2_FT_DIR         2
#define EXT2_FT_CHRDEV      3
#define EXT2_FT_BLKDEV      4
#define EXT2_FT_FIFO        5
#define EXT2_FT_SOCK        6
#define EXT2_FT_SYMLINK     7

// Inode mode bits
#define EXT2_S_IFSOCK   0xC000
#define EXT2_S_IFLNK    0xA000
#define EXT2_S_IFREG    0x8000
#define EXT2_S_IFBLK    0x6000
#define EXT2_S_IFDIR    0x4000
#define EXT2_S_IFCHR    0x2000
#define EXT2_S_IFIFO    0x1000

// Permission bits
#define EXT2_S_ISUID    0x0800
#define EXT2_S_ISGID    0x0400
#define EXT2_S_ISVTX    0x0200
#define EXT2_S_IRUSR    0x0100
#define EXT2_S_IWUSR    0x0080
#define EXT2_S_IXUSR    0x0040
#define EXT2_S_IRGRP    0x0020
#define EXT2_S_IWGRP    0x0010
#define EXT2_S_IXGRP    0x0008
#define EXT2_S_IROTH    0x0004
#define EXT2_S_IWOTH    0x0002
#define EXT2_S_IXOTH    0x0001

// Root inode number
#define EXT2_ROOT_INO       2

// Direct/indirect block pointers
#define EXT2_NDIR_BLOCKS    12
#define EXT2_IND_BLOCK      12
#define EXT2_DIND_BLOCK     13
#define EXT2_TIND_BLOCK     14
#define EXT2_N_BLOCKS       15

// ext2 superblock (simplified)
typedef struct ext2_superblock {
    uint32_t s_inodes_count;        // Total inodes
    uint32_t s_blocks_count;        // Total blocks
    uint32_t s_r_blocks_count;      // Reserved blocks
    uint32_t s_free_blocks_count;   // Free blocks
    uint32_t s_free_inodes_count;   // Free inodes
    uint32_t s_first_data_block;    // First data block
    uint32_t s_log_block_size;      // Block size = 1024 << s_log_block_size
    uint32_t s_log_frag_size;       // Fragment size
    uint32_t s_blocks_per_group;    // Blocks per group
    uint32_t s_frags_per_group;     // Fragments per group
    uint32_t s_inodes_per_group;    // Inodes per group
    uint32_t s_mtime;               // Mount time
    uint32_t s_wtime;               // Write time
    uint16_t s_mnt_count;           // Mount count
    uint16_t s_max_mnt_count;       // Max mount count
    uint16_t s_magic;               // Magic signature (0xEF53)
    uint16_t s_state;               // File system state
    uint16_t s_errors;              // Error handling
    uint16_t s_minor_rev_level;     // Minor revision level
    uint32_t s_lastcheck;           // Last check time
    uint32_t s_checkinterval;       // Check interval
    uint32_t s_creator_os;          // Creator OS
    uint32_t s_rev_level;           // Revision level
    uint16_t s_def_resuid;          // Default reserved user ID
    uint16_t s_def_resgid;          // Default reserved group ID

    // Extended fields (for rev_level >= 1)
    uint32_t s_first_ino;           // First non-reserved inode
    uint16_t s_inode_size;          // Inode size
    uint16_t s_block_group_nr;      // Block group number of this superblock
    uint32_t s_feature_compat;      // Compatible features
    uint32_t s_feature_incompat;    // Incompatible features
    uint32_t s_feature_ro_compat;   // Read-only compatible features
    uint8_t  s_uuid[16];            // Volume UUID
    char     s_volume_name[16];     // Volume name
    char     s_last_mounted[64];    // Last mounted path
    uint32_t s_algorithm_usage_bitmap; // Compression algorithm

    // Performance hints
    uint8_t  s_prealloc_blocks;     // Blocks to preallocate
    uint8_t  s_prealloc_dir_blocks; // Dir blocks to preallocate
    uint16_t s_padding1;

    uint8_t  s_reserved[204];       // Padding to 1024 bytes
} __attribute__((packed)) ext2_superblock_t;

// ext2 block group descriptor
typedef struct ext2_group_desc {
    uint32_t bg_block_bitmap;       // Block bitmap block
    uint32_t bg_inode_bitmap;       // Inode bitmap block
    uint32_t bg_inode_table;        // Inode table block
    uint16_t bg_free_blocks_count;  // Free blocks count
    uint16_t bg_free_inodes_count;  // Free inodes count
    uint16_t bg_used_dirs_count;    // Directories count
    uint16_t bg_pad;
    uint8_t  bg_reserved[12];
} __attribute__((packed)) ext2_group_desc_t;

// ext2 inode
typedef struct ext2_inode {
    uint16_t i_mode;                // File mode
    uint16_t i_uid;                 // Owner UID
    uint32_t i_size;                // Size in bytes
    uint32_t i_atime;               // Access time
    uint32_t i_ctime;               // Creation time
    uint32_t i_mtime;               // Modification time
    uint32_t i_dtime;               // Deletion time
    uint16_t i_gid;                 // Group ID
    uint16_t i_links_count;         // Hard links count
    uint32_t i_blocks;              // Blocks count (512-byte blocks)
    uint32_t i_flags;               // File flags
    uint32_t i_osd1;                // OS dependent 1
    uint32_t i_block[EXT2_N_BLOCKS]; // Block pointers
    uint32_t i_generation;          // File version
    uint32_t i_file_acl;            // File ACL
    uint32_t i_dir_acl;             // Directory ACL
    uint32_t i_faddr;               // Fragment address
    uint8_t  i_osd2[12];            // OS dependent 2
} __attribute__((packed)) ext2_inode_t;

// ext2 directory entry
typedef struct ext2_dirent {
    uint32_t inode;                 // Inode number
    uint16_t rec_len;               // Directory entry length
    uint8_t  name_len;              // Name length
    uint8_t  file_type;             // File type
    char     name[];                // File name (variable length)
} __attribute__((packed)) ext2_dirent_t;

// ext2 mount data
typedef struct ext2_mount_data {
    ext2_superblock_t *sb;          // Superblock
    ext2_group_desc_t *group_desc;  // Block group descriptors
    uint32_t block_size;            // Block size in bytes
    uint32_t groups_count;          // Number of block groups
    uint32_t inodes_per_group;      // Inodes per group
    uint32_t blocks_per_group;      // Blocks per group
} ext2_mount_data_t;
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "fs/ext2_disk_format.h"

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

// Write a block at the given block number (block 0 = byte 0)
static void write_block(FILE *f, uint32_t block_num, const void *data) {
    fseek(f, block_num * EXT2_BLOCK_SIZE, SEEK_SET);
    fwrite(data, EXT2_BLOCK_SIZE, 1, f);
}

// Block 1: superblock
static void write_superblock(FILE *f, uint32_t blocks, uint32_t inodes) {
    uint8_t buf[EXT2_BLOCK_SIZE];
    memset(buf, 0, sizeof(buf));

    ext2_superblock_t *sb = (ext2_superblock_t *)buf;
    sb->s_inodes_count      = inodes;
    sb->s_blocks_count      = blocks;
    sb->s_r_blocks_count    = blocks / 20;   // 5% reserved
    uint32_t inode_table_blocks_sb = (inodes * EXT2_INODE_SIZE + EXT2_BLOCK_SIZE - 1) / EXT2_BLOCK_SIZE;
    sb->s_free_blocks_count = blocks - 5 - inode_table_blocks_sb - 1; // matches group descriptor
    sb->s_free_inodes_count = inodes - 1;    // root inode used
    sb->s_first_data_block  = 1;             // superblock is in block 1 for 1KB blocks
    sb->s_log_block_size    = 0;             // 1024 << 0 = 1024
    sb->s_log_frag_size     = 0;
    sb->s_blocks_per_group  = blocks;
    sb->s_frags_per_group   = blocks;
    sb->s_inodes_per_group  = inodes;
    sb->s_magic             = EXT2_SUPER_MAGIC;
    sb->s_state             = 1;             // clean
    sb->s_rev_level         = 1;
    sb->s_first_ino         = 11;
    sb->s_inode_size        = EXT2_INODE_SIZE;
    // sb->s_reserved[203] is the tarfs-population flag.
    // It is left 0x00 (not done) because buf was zeroed above.
    // The kernel sets it to 0xAB after copying tarfs into /mnt/initrd/.

    write_block(f, 1, buf);
}

// Block 2: block group descriptor table
static void write_group_desc(FILE *f, uint32_t blocks, uint32_t inodes) {
    uint8_t buf[EXT2_BLOCK_SIZE];
    memset(buf, 0, sizeof(buf));

    uint32_t inode_table_blocks = (inodes * EXT2_INODE_SIZE + EXT2_BLOCK_SIZE - 1) / EXT2_BLOCK_SIZE;

    ext2_group_desc_t *gd = (ext2_group_desc_t *)buf;
    gd->bg_block_bitmap      = 3;
    gd->bg_inode_bitmap      = 4;
    gd->bg_inode_table       = 5;
    gd->bg_free_blocks_count = (uint16_t)(blocks - 5 - inode_table_blocks - 1);
    gd->bg_free_inodes_count = (uint16_t)(inodes - 1);
    gd->bg_used_dirs_count   = 1;

    write_block(f, 2, buf);
}

// Blocks 3 (block bitmap) and 4 (inode bitmap)
static void write_bitmaps(FILE *f, uint32_t blocks, uint32_t inodes) {
    uint8_t buf[EXT2_BLOCK_SIZE];

    // Block bitmap: mark blocks 0-5+inode_table as used
    uint32_t inode_table_blocks = (inodes * EXT2_INODE_SIZE + EXT2_BLOCK_SIZE - 1) / EXT2_BLOCK_SIZE;
    uint32_t used_blocks = 5 + inode_table_blocks + 1; // +1 for root dir block
    memset(buf, 0, sizeof(buf));
    for (uint32_t i = 0; i < used_blocks && i < blocks; i++)
        buf[i / 8] |= (1 << (i % 8));
    write_block(f, 3, buf);

    // Inode bitmap: mark inode 0 (unused) and inode 2 (root) as used
    // ext2 inodes are 1-based; inode 1 is bad blocks inode, inode 2 is root
    memset(buf, 0, sizeof(buf));
    buf[0] = 0x03; // bits 0 and 1 set => inodes 1 and 2 allocated
    write_block(f, 4, buf);
}

// Block 5+: inode table — write root inode (inode 2) at its slot
static void write_root_inode(FILE *f, uint32_t inodes, uint32_t root_block) {
    uint32_t inode_table_blocks = (inodes * EXT2_INODE_SIZE + EXT2_BLOCK_SIZE - 1) / EXT2_BLOCK_SIZE;
    uint8_t buf[EXT2_BLOCK_SIZE * inode_table_blocks];  // VLA: fine for host-side tool
    memset(buf, 0, EXT2_BLOCK_SIZE * inode_table_blocks);

    // Inode 2 is at index 1 (0-based) in the table
    ext2_inode_t *root = (ext2_inode_t *)(buf + EXT2_INODE_SIZE);
    root->i_mode        = EXT2_S_IFDIR | 0755;
    root->i_uid         = 0;
    root->i_gid         = 0;
    root->i_size        = EXT2_BLOCK_SIZE;
    root->i_links_count = 2;            // "." and parent ".."
    root->i_blocks      = 2;            // 2 x 512-byte sectors = 1 block
    root->i_block[0]    = root_block;

    fseek(f, 5 * EXT2_BLOCK_SIZE, SEEK_SET);
    fwrite(buf, EXT2_BLOCK_SIZE, inode_table_blocks, f);
}

// Root directory block: "." and ".." entries
static void write_root_dir(FILE *f, uint32_t root_block) {
    uint8_t buf[EXT2_BLOCK_SIZE];
    memset(buf, 0, sizeof(buf));

    uint8_t *p = buf;

    // "." entry
    ext2_dirent_t *dot = (ext2_dirent_t *)p;
    dot->inode    = EXT2_ROOT_INO;
    dot->name_len = 1;
    dot->file_type = EXT2_FT_DIR;
    dot->rec_len  = 12;   // 8 header + 1 name, rounded to 4 bytes => 12
    p[8]          = '.';
    p += 12;

    // ".." entry — spans the rest of the block
    ext2_dirent_t *dotdot = (ext2_dirent_t *)p;
    dotdot->inode    = EXT2_ROOT_INO;
    dotdot->name_len = 2;
    dotdot->file_type = EXT2_FT_DIR;
    dotdot->rec_len  = EXT2_BLOCK_SIZE - 12;
    p[8]  = '.';
    p[9]  = '.';

    write_block(f, root_block, buf);
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: mkfs <image> <size_mb>\n");
        return 1;
    }
    
    long mb = atol(argv[2]);
    if (mb <= 0) {
        fprintf(stderr, "invalid size\n");
        return 1;
    }
    
    uint32_t blocks = (mb * 1024 * 1024) / EXT2_BLOCK_SIZE;
    uint32_t inodes = blocks / 4;
    
    if (inodes < 100) inodes = 100;
    
    printf("Creating ext2 filesystem:\n");
    printf("  Size: %ld MB\n", mb);
    printf("  Blocks: %u (1KB each)\n", blocks);
    printf("  Inodes: %u\n", inodes);
    
    FILE *f = fopen(argv[1], "wb");
    if (!f) {
        perror(argv[1]);
        return 1;
    }
    
    fseek(f, mb * 1024L * 1024 - 1, SEEK_SET);
    fputc(0, f);
    
    write_superblock(f, blocks, inodes);
    write_group_desc(f, blocks, inodes);
    write_bitmaps(f, blocks, inodes);
    
    uint32_t inode_table_blocks = (inodes * EXT2_INODE_SIZE + EXT2_BLOCK_SIZE - 1) / EXT2_BLOCK_SIZE;
    uint32_t root_block = 5 + inode_table_blocks;
    
    write_root_inode(f, inodes, root_block);
    write_root_dir(f, root_block);
    
    fclose(f);
    
    printf("ext2 filesystem created successfully\n");
    printf("  Root directory at block %u\n", root_block);
    
    return 0;
}

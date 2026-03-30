#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define EXT2_SUPER_MAGIC    0xEF53
#define EXT2_BLOCK_SIZE     1024
#define EXT2_INODE_SIZE     128
#define EXT2_ROOT_INO       2
#define EXT2_FIRST_INO      11

#define EXT2_S_IFREG    0x8000
#define EXT2_S_IFDIR    0x4000

#define EXT2_FT_REG_FILE    1
#define EXT2_FT_DIR         2

#define EXT2_NDIR_BLOCKS    12
#define EXT2_N_BLOCKS       15

typedef struct {
    uint32_t s_inodes_count;
    uint32_t s_blocks_count;
    uint32_t s_r_blocks_count;
    uint32_t s_free_blocks_count;
    uint32_t s_free_inodes_count;
    uint32_t s_first_data_block;
    uint32_t s_log_block_size;
    uint32_t s_log_frag_size;
    uint32_t s_blocks_per_group;
    uint32_t s_frags_per_group;
    uint32_t s_inodes_per_group;
    uint32_t s_mtime;
    uint32_t s_wtime;
    uint16_t s_mnt_count;
    uint16_t s_max_mnt_count;
    uint16_t s_magic;
    uint16_t s_state;
    uint16_t s_errors;
    uint16_t s_minor_rev_level;
    uint32_t s_lastcheck;
    uint32_t s_checkinterval;
    uint32_t s_creator_os;
    uint32_t s_rev_level;
    uint16_t s_def_resuid;
    uint16_t s_def_resgid;
    uint32_t s_first_ino;
    uint16_t s_inode_size;
    uint16_t s_block_group_nr;
    uint32_t s_feature_compat;
    uint32_t s_feature_incompat;
    uint32_t s_feature_ro_compat;
    uint8_t  s_uuid[16];
    char     s_volume_name[16];
    char     s_last_mounted[64];
    uint32_t s_algorithm_usage_bitmap;
    uint8_t  s_prealloc_blocks;
    uint8_t  s_prealloc_dir_blocks;
    uint16_t s_padding1;
    uint8_t  s_reserved[204];
} __attribute__((packed)) ext2_superblock_t;

typedef struct {
    uint32_t bg_block_bitmap;
    uint32_t bg_inode_bitmap;
    uint32_t bg_inode_table;
    uint16_t bg_free_blocks_count;
    uint16_t bg_free_inodes_count;
    uint16_t bg_used_dirs_count;
    uint16_t bg_pad;
    uint8_t  bg_reserved[12];
} __attribute__((packed)) ext2_group_desc_t;

typedef struct {
    uint16_t i_mode;
    uint16_t i_uid;
    uint32_t i_size;
    uint32_t i_atime;
    uint32_t i_ctime;
    uint32_t i_mtime;
    uint32_t i_dtime;
    uint16_t i_gid;
    uint16_t i_links_count;
    uint32_t i_blocks;
    uint32_t i_flags;
    uint32_t i_osd1;
    uint32_t i_block[EXT2_N_BLOCKS];
    uint32_t i_generation;
    uint32_t i_file_acl;
    uint32_t i_dir_acl;
    uint32_t i_faddr;
    uint8_t  i_osd2[12];
} __attribute__((packed)) ext2_inode_t;

typedef struct {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t  name_len;
    uint8_t  file_type;
    char     name[];
} __attribute__((packed)) ext2_dirent_t;

void write_superblock(FILE *f, uint32_t blocks, uint32_t inodes) {
    ext2_superblock_t sb;
    memset(&sb, 0, sizeof(sb));
    
    sb.s_inodes_count = inodes;
    sb.s_blocks_count = blocks;
    sb.s_r_blocks_count = blocks / 20;
    sb.s_free_blocks_count = blocks - 10;
    sb.s_free_inodes_count = inodes - 10;
    sb.s_first_data_block = 1;
    sb.s_log_block_size = 0;
    sb.s_log_frag_size = 0;
    sb.s_blocks_per_group = 8192;
    sb.s_frags_per_group = 8192;
    sb.s_inodes_per_group = inodes;
    sb.s_mtime = time(NULL);
    sb.s_wtime = time(NULL);
    sb.s_mnt_count = 0;
    sb.s_max_mnt_count = 20;
    sb.s_magic = EXT2_SUPER_MAGIC;
    sb.s_state = 1;
    sb.s_errors = 1;
    sb.s_minor_rev_level = 0;
    sb.s_lastcheck = time(NULL);
    sb.s_checkinterval = 0;
    sb.s_creator_os = 0;
    sb.s_rev_level = 1;
    sb.s_def_resuid = 0;
    sb.s_def_resgid = 0;
    sb.s_first_ino = EXT2_FIRST_INO;
    sb.s_inode_size = EXT2_INODE_SIZE;
    sb.s_block_group_nr = 0;
    
    strcpy(sb.s_volume_name, "sbunix");
    
    fseek(f, 1024, SEEK_SET);
    fwrite(&sb, sizeof(sb), 1, f);
}

void write_group_desc(FILE *f, uint32_t blocks, uint32_t inodes) {
    ext2_group_desc_t gd;
    memset(&gd, 0, sizeof(gd));
    
    uint32_t inode_table_blocks = (inodes * EXT2_INODE_SIZE + EXT2_BLOCK_SIZE - 1) / EXT2_BLOCK_SIZE;
    
    gd.bg_block_bitmap = 3;
    gd.bg_inode_bitmap = 4;
    gd.bg_inode_table = 5;
    gd.bg_free_blocks_count = blocks - (5 + inode_table_blocks + 1);
    gd.bg_free_inodes_count = inodes - 10;
    gd.bg_used_dirs_count = 1;
    
    fseek(f, 2048, SEEK_SET);
    fwrite(&gd, sizeof(gd), 1, f);
}

void write_bitmaps(FILE *f, uint32_t blocks, uint32_t inodes) {
    uint8_t *block_bitmap = calloc(1, EXT2_BLOCK_SIZE);
    
    uint32_t inode_table_blocks = (inodes * EXT2_INODE_SIZE + EXT2_BLOCK_SIZE - 1) / EXT2_BLOCK_SIZE;
    uint32_t used_blocks = 5 + inode_table_blocks + 1;
    
    for (uint32_t i = 0; i < used_blocks && i < blocks; i++) {
        block_bitmap[i / 8] |= (1 << (i % 8));
    }
    
    fseek(f, 3072, SEEK_SET);
    fwrite(block_bitmap, EXT2_BLOCK_SIZE, 1, f);
    free(block_bitmap);
    
    uint8_t *inode_bitmap = calloc(1, EXT2_BLOCK_SIZE);
    
    for (int i = 0; i < 10; i++) {
        inode_bitmap[i / 8] |= (1 << (i % 8));
    }
    
    fseek(f, 4096, SEEK_SET);
    fwrite(inode_bitmap, EXT2_BLOCK_SIZE, 1, f);
    free(inode_bitmap);
}

void write_root_inode(FILE *f, uint32_t inodes, uint32_t root_block) {
    uint32_t inode_table_blocks = (inodes * EXT2_INODE_SIZE + EXT2_BLOCK_SIZE - 1) / EXT2_BLOCK_SIZE;
    uint32_t inode_table_start = 5;
    
    uint32_t inode_offset = inode_table_start * EXT2_BLOCK_SIZE + 1 * EXT2_INODE_SIZE;
    
    ext2_inode_t root_inode;
    memset(&root_inode, 0, sizeof(root_inode));
    
    root_inode.i_mode = EXT2_S_IFDIR | 0755;
    root_inode.i_uid = 0;
    root_inode.i_gid = 0;
    root_inode.i_size = EXT2_BLOCK_SIZE;
    root_inode.i_atime = time(NULL);
    root_inode.i_ctime = time(NULL);
    root_inode.i_mtime = time(NULL);
    root_inode.i_dtime = 0;
    root_inode.i_links_count = 2;
    root_inode.i_blocks = 2;
    root_inode.i_block[0] = root_block;
    
    fseek(f, inode_offset, SEEK_SET);
    fwrite(&root_inode, sizeof(root_inode), 1, f);
}

void write_root_dir(FILE *f, uint32_t root_block) {
    uint8_t *block = calloc(1, EXT2_BLOCK_SIZE);
    
    ext2_dirent_t *dot = (ext2_dirent_t *)block;
    dot->inode = EXT2_ROOT_INO;
    dot->rec_len = 12;
    dot->name_len = 1;
    dot->file_type = EXT2_FT_DIR;
    dot->name[0] = '.';
    
    ext2_dirent_t *dotdot = (ext2_dirent_t *)(block + 12);
    dotdot->inode = EXT2_ROOT_INO;
    dotdot->rec_len = EXT2_BLOCK_SIZE - 12;
    dotdot->name_len = 2;
    dotdot->file_type = EXT2_FT_DIR;
    dotdot->name[0] = '.';
    dotdot->name[1] = '.';
    
    fseek(f, root_block * EXT2_BLOCK_SIZE, SEEK_SET);
    fwrite(block, EXT2_BLOCK_SIZE, 1, f);
    free(block);
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

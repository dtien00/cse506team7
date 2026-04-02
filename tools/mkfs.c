#include <stdio.h>
#include <stdlib.h>
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

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

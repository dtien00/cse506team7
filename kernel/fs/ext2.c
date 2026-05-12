#include "fs/ext2.h"
#include "fs/block_cache.h"
#include "drivers/virtio.h"
#include "kernel/printk.h"
#include "mm/kmalloc.h"
#include "lib/string.h"
#include "process/process.h"
#include "trap/trap.h"

// Global sleeping mutex that serializes all ext2 top-level operations.
// Prevents directory-block, inode-allocation, and inode-table races that
// occur when multiple processes concurrently create, write, or unlink files.
static int ext2_fs_lock = 0;

static void ext2_lock(void) {
    interrupts_off();
    while (ext2_fs_lock) {
        proc_sleep(&ext2_fs_lock);
        // proc_sleep returns with interrupts ON; re-disable before rechecking
        interrupts_off();
    }
    ext2_fs_lock = 1;
    interrupts_on();
}

static void ext2_unlock(void) {
    ext2_fs_lock = 0;
    proc_wakeup(&ext2_fs_lock);
}

static fs_type_t ext2_fs_type;

// Forward declarations for helpers used before their definitions
static uint32_t ext2_alloc_block(ext2_mount_data_t *mnt);

// Helper: Write superblock back to disk (sector 2, 2 sectors)
static void ext2_write_superblock(ext2_mount_data_t *mnt) {
    uint8_t *sb_buf = (uint8_t *)kmalloc(1024);
    if (!sb_buf) return;
    memset(sb_buf, 0, 1024);
    memcpy(sb_buf, mnt->sb, sizeof(ext2_superblock_t));
    // ("ext2: write_superblock enter\n");
    virtio_blk_write(2, sb_buf, 2);
    // printk("ext2: write_superblock done\n");
    kfree(sb_buf);
}

// Helper: Write group descriptor table back to disk (block 2 for 1KB blocks)
static void ext2_write_gdt(ext2_mount_data_t *mnt) {
    uint32_t gdt_block = (mnt->block_size == 1024) ? 2 : 1;
    uint32_t gdt_size = mnt->groups_count * sizeof(ext2_group_desc_t);

    // GDT may span multiple blocks; write one block at a time
    uint32_t blocks_needed = (gdt_size + mnt->block_size - 1) / mnt->block_size;
    uint8_t *buf = (uint8_t *)kmalloc(mnt->block_size);
    if (!buf) return;

    for (uint32_t b = 0; b < blocks_needed; b++) {
        memset(buf, 0, mnt->block_size);
        uint32_t off = b * mnt->block_size;
        uint32_t copy = gdt_size - off;
        if (copy > mnt->block_size) copy = mnt->block_size;
        memcpy(buf, (uint8_t *)mnt->group_desc + off, copy);
        block_cache_write(gdt_block + b, buf);
    }
    kfree(buf);
}

// Helper: Read an inode from disk
static int ext2_read_inode(ext2_mount_data_t *mnt, uint32_t ino, ext2_inode_t *inode) {
    if (ino == 0) return -1;
    
    // Calculate which block group contains this inode
    uint32_t group = (ino - 1) / mnt->inodes_per_group;
    uint32_t index = (ino - 1) % mnt->inodes_per_group;
    
    if (group >= mnt->groups_count) {
        printk("ext2: inode %u out of range\n", ino);
        return -1;
    }
    
    // Get inode table block from group descriptor
    uint32_t inode_table = mnt->group_desc[group].bg_inode_table;
    
    // Calculate block and offset within block
    uint32_t inodes_per_block = mnt->block_size / EXT2_INODE_SIZE;
    uint32_t block = inode_table + (index / inodes_per_block);
    uint32_t offset = (index % inodes_per_block) * EXT2_INODE_SIZE;
    
    // Read the block containing the inode
    uint8_t *block_buf = (uint8_t *)kmalloc(mnt->block_size);
    if (!block_buf) return -1;
    
    if (block_cache_read(block, block_buf) != 0) {
        kfree(block_buf);
        return -1;
    }
    
    // Copy inode data
    memcpy(inode, block_buf + offset, sizeof(ext2_inode_t));
    
    kfree(block_buf);
    return 0;
}

// Helper: Write an inode to disk
static int ext2_write_inode(ext2_mount_data_t *mnt, uint32_t ino, ext2_inode_t *inode) {
    if (ino == 0) return -1;
    
    uint32_t group = (ino - 1) / mnt->inodes_per_group;
    uint32_t index = (ino - 1) % mnt->inodes_per_group;
    
    if (group >= mnt->groups_count) return -1;
    
    uint32_t inode_table = mnt->group_desc[group].bg_inode_table;
    uint32_t inodes_per_block = mnt->block_size / EXT2_INODE_SIZE;
    uint32_t block = inode_table + (index / inodes_per_block);
    uint32_t offset = (index % inodes_per_block) * EXT2_INODE_SIZE;
    
    // Read block, modify inode, write back
    uint8_t *block_buf = (uint8_t *)kmalloc(mnt->block_size);
    if (!block_buf) return -1;
    
    if (block_cache_read(block, block_buf) != 0) {
        kfree(block_buf);
        return -1;
    }
    
    memcpy(block_buf + offset, inode, sizeof(ext2_inode_t));
    
    if (block_cache_write(block, block_buf) != 0) {
        kfree(block_buf);
        return -1;
    }
    
    kfree(block_buf);
    return 0;
}

// Helper: Read a data block from an inode (supports direct + singly-indirect)
static int ext2_read_inode_block(ext2_mount_data_t *mnt, ext2_inode_t *inode,
                                  uint32_t block_idx, void *buf) {
    uint32_t ptrs_per_block = mnt->block_size / sizeof(uint32_t);

    if (block_idx < EXT2_NDIR_BLOCKS) {
        // Direct block
        uint32_t block_num = inode->i_block[block_idx];
        if (block_num == 0) {
            memset(buf, 0, mnt->block_size);
            return 0;
        }
        return block_cache_read(block_num, buf);
    }

    block_idx -= EXT2_NDIR_BLOCKS;
    if (block_idx < ptrs_per_block) {
        // Singly-indirect block
        uint32_t ind_block = inode->i_block[EXT2_IND_BLOCK];
        if (ind_block == 0) {
            memset(buf, 0, mnt->block_size);
            return 0;
        }
        uint32_t *ind_buf = (uint32_t *)kmalloc(mnt->block_size);
        if (!ind_buf) return -1;
        if (block_cache_read(ind_block, ind_buf) != 0) {
            kfree(ind_buf);
            return -1;
        }
        uint32_t block_num = ind_buf[block_idx];
        kfree(ind_buf);
        if (block_num == 0) {
            memset(buf, 0, mnt->block_size);
            return 0;
        }
        return block_cache_read(block_num, buf);
    }

    printk("ext2: doubly-indirect blocks not supported\n");
    return -1;
}

// Helper: Write a data block through an inode (supports direct + singly-indirect)
// Allocates blocks as needed. Returns 0 on success, -1 on error.
static int ext2_write_inode_block(ext2_mount_data_t *mnt, ext2_inode_t *inode,
                                   uint32_t block_idx, const void *buf) {
    uint32_t ptrs_per_block = mnt->block_size / sizeof(uint32_t);

    if (block_idx < EXT2_NDIR_BLOCKS) {
        if (inode->i_block[block_idx] == 0) {
            uint32_t nb = ext2_alloc_block(mnt);
            if (nb == 0) return -1;
            inode->i_block[block_idx] = nb;
            inode->i_blocks += mnt->block_size / 512;
        }
        return block_cache_write(inode->i_block[block_idx], buf);
    }

    block_idx -= EXT2_NDIR_BLOCKS;
    if (block_idx < ptrs_per_block) {
        // Allocate indirect block if needed
        if (inode->i_block[EXT2_IND_BLOCK] == 0) {
            uint32_t ind = ext2_alloc_block(mnt);
            if (ind == 0) return -1;
            inode->i_block[EXT2_IND_BLOCK] = ind;
            inode->i_blocks += mnt->block_size / 512;
            // Zero the indirect block
            uint8_t *zero = (uint8_t *)kmalloc(mnt->block_size);
            if (!zero) return -1;
            memset(zero, 0, mnt->block_size);
            block_cache_write(ind, zero);
            kfree(zero);
        }
        uint32_t *ind_buf = (uint32_t *)kmalloc(mnt->block_size);
        if (!ind_buf) return -1;
        if (block_cache_read(inode->i_block[EXT2_IND_BLOCK], ind_buf) != 0) {
            kfree(ind_buf);
            return -1;
        }
        if (ind_buf[block_idx] == 0) {
            uint32_t nb = ext2_alloc_block(mnt);
            if (nb == 0) { kfree(ind_buf); return -1; }
            ind_buf[block_idx] = nb;
            inode->i_blocks += mnt->block_size / 512;
            block_cache_write(inode->i_block[EXT2_IND_BLOCK], ind_buf);
        }
        uint32_t data_block = ind_buf[block_idx];
        kfree(ind_buf);
        return block_cache_write(data_block, buf);
    }

    printk("ext2: doubly-indirect blocks not supported\n");
    return -1;
}

// Helper: Allocate a new block
static uint32_t ext2_alloc_block(ext2_mount_data_t *mnt) {
    // Simple allocation: scan block bitmap for first free block
    for (uint32_t group = 0; group < mnt->groups_count; group++) {
        if (mnt->group_desc[group].bg_free_blocks_count == 0) {
            continue;
        }
        
        uint32_t bitmap_block = mnt->group_desc[group].bg_block_bitmap;
        uint8_t *bitmap = (uint8_t *)kmalloc(mnt->block_size);
        if (!bitmap) return 0;
        
        if (block_cache_read(bitmap_block, bitmap) != 0) {
            kfree(bitmap);
            continue;
        }
        
        // Find first free bit
        for (uint32_t byte = 0; byte < mnt->block_size; byte++) {
            if (bitmap[byte] != 0xFF) {
                for (int bit = 0; bit < 8; bit++) {
                    if (!(bitmap[byte] & (1 << bit))) {
                        // Found free block
                        bitmap[byte] |= (1 << bit);
                        
                        uint32_t block_num = mnt->sb->s_first_data_block +
                                            group * mnt->blocks_per_group +
                                            byte * 8 + bit;
                        
                        // Write back bitmap
                        block_cache_write(bitmap_block, bitmap);

                        // Update group descriptor and superblock counts in memory
                        mnt->group_desc[group].bg_free_blocks_count--;
                        mnt->sb->s_free_blocks_count--;

                        kfree(bitmap);
                        return block_num;
                    }
                }
            }
        }
        
        kfree(bitmap);
    }
    
    printk("ext2: no free blocks available\n");
    return 0;
}

// Helper: Allocate a new inode
static uint32_t ext2_alloc_inode(ext2_mount_data_t *mnt) {
    // Simple allocation: scan inode bitmap for first free inode
    for (uint32_t group = 0; group < mnt->groups_count; group++) {
        if (mnt->group_desc[group].bg_free_inodes_count == 0) {
            continue;
        }
        
        uint32_t bitmap_block = mnt->group_desc[group].bg_inode_bitmap;
        uint8_t *bitmap = (uint8_t *)kmalloc(mnt->block_size);
        if (!bitmap) return 0;
        
        if (block_cache_read(bitmap_block, bitmap) != 0) {
            kfree(bitmap);
            continue;
        }
        
        // Find first free bit
        for (uint32_t byte = 0; byte < mnt->inodes_per_group / 8; byte++) {
            if (bitmap[byte] != 0xFF) {
                for (int bit = 0; bit < 8; bit++) {
                    if (!(bitmap[byte] & (1 << bit))) {
                        // Found free inode
                        uint32_t ino = group * mnt->inodes_per_group + byte * 8 + bit + 1;
                        
                        // Skip reserved inodes (1-10 typically)
                        if (ino < 11) continue;
                        
                        bitmap[byte] |= (1 << bit);
                        
                        // Write back bitmap
                        block_cache_write(bitmap_block, bitmap);

                        // Update group descriptor and superblock counts in memory
                        mnt->group_desc[group].bg_free_inodes_count--;
                        mnt->sb->s_free_inodes_count--;

                        kfree(bitmap);
                        return ino;
                    }
                }
            }
        }
        
        kfree(bitmap);
    }
    
    printk("ext2: no free inodes available\n");
    return 0;
}

// Mount ext2 filesystem
vnode_t *ext2_mount(mount_t *mnt, const char *source) {
    printk("ext2: mounting filesystem\n");

    // Allocate mount data
    ext2_mount_data_t *ext2_mnt = (ext2_mount_data_t *)kmalloc(sizeof(ext2_mount_data_t));
    if (!ext2_mnt) {
        printk("ext2: failed to allocate mount data\n");
        return NULL;
    }
    
    // Allocate and read superblock (at byte 1024 = block 1 for 1KB blocks)
    ext2_mnt->sb = (ext2_superblock_t *)kmalloc(sizeof(ext2_superblock_t));
    if (!ext2_mnt->sb) {
        kfree(ext2_mnt);
        return NULL;
    }

    // Read superblock (it's at sector 2, since 1024 bytes / 512 bytes per sector = 2)
    uint8_t *sb_buf = (uint8_t *)kmalloc(1024);
    if (!sb_buf) {
        kfree(ext2_mnt->sb);
        kfree(ext2_mnt);
        return NULL;
    }

    if (virtio_blk_read(2, sb_buf, 2) != 0) {
        printk("ext2: failed to read superblock\n");
        kfree(sb_buf);
        kfree(ext2_mnt->sb);
        kfree(ext2_mnt);
        return NULL;
    }

    memcpy(ext2_mnt->sb, sb_buf, sizeof(ext2_superblock_t));
    kfree(sb_buf);

    // Verify magic number
    if (ext2_mnt->sb->s_magic != EXT2_SUPER_MAGIC) {
        printk("ext2: invalid magic number 0x%x (expected 0x%x)\n",
               ext2_mnt->sb->s_magic, EXT2_SUPER_MAGIC);
        kfree(ext2_mnt->sb);
        kfree(ext2_mnt);
        return NULL;
    }

    // Calculate block size
    ext2_mnt->block_size = 1024 << ext2_mnt->sb->s_log_block_size;
    ext2_mnt->inodes_per_group = ext2_mnt->sb->s_inodes_per_group;
    ext2_mnt->blocks_per_group = ext2_mnt->sb->s_blocks_per_group;

    // Calculate number of block groups
    ext2_mnt->groups_count = (ext2_mnt->sb->s_blocks_count +
                              ext2_mnt->sb->s_blocks_per_group - 1) /
                             ext2_mnt->sb->s_blocks_per_group;

    printk("ext2: block_size=%d, groups=%d, inodes=%d, blocks=%d\n",
           ext2_mnt->block_size, ext2_mnt->groups_count,
           ext2_mnt->sb->s_inodes_count, ext2_mnt->sb->s_blocks_count);

    // Read block group descriptor table
    // It starts immediately after the superblock
    uint32_t gdt_block = (ext2_mnt->block_size == 1024) ? 2 : 1;
    uint32_t gdt_size = ext2_mnt->groups_count * sizeof(ext2_group_desc_t);

    ext2_mnt->group_desc = (ext2_group_desc_t *)kmalloc(gdt_size);
    if (!ext2_mnt->group_desc) {
        kfree(ext2_mnt->sb);
        kfree(ext2_mnt);
        return NULL;
    }

    // Read group descriptors (use BLOCK_SIZE since cache uses 1KB blocks)
    uint8_t *gdt_buf = (uint8_t *)kmalloc(1024);
    if (!gdt_buf) {
        kfree(ext2_mnt->group_desc);
        kfree(ext2_mnt->sb);
        kfree(ext2_mnt);
        return NULL;
    }

    if (block_cache_read(gdt_block, gdt_buf) != 0) {
        printk("ext2: failed to read group descriptors\n");
        kfree(gdt_buf);
        kfree(ext2_mnt->group_desc);
        kfree(ext2_mnt->sb);
        kfree(ext2_mnt);
        return NULL;
    }

    memcpy(ext2_mnt->group_desc, gdt_buf, gdt_size);
    kfree(gdt_buf);

    mnt->fs_data = ext2_mnt;

    // Read root inode and return root vnode
    ext2_inode_t *root_inode = (ext2_inode_t *)kmalloc(sizeof(ext2_inode_t));
    if (!root_inode) {
        kfree(ext2_mnt->group_desc);
        kfree(ext2_mnt->sb);
        kfree(ext2_mnt);
        return NULL;
    }

    if (ext2_read_inode(ext2_mnt, EXT2_ROOT_INO, root_inode) != 0) {
        printk("ext2: failed to read root inode\n");
        kfree(root_inode);
        kfree(ext2_mnt->group_desc);
        kfree(ext2_mnt->sb);
        kfree(ext2_mnt);
        return NULL;
    }

    vnode_t *root_vnode = (vnode_t *)kmalloc(sizeof(vnode_t));
    if (!root_vnode) {
        kfree(root_inode);
        kfree(ext2_mnt->group_desc);
        kfree(ext2_mnt->sb);
        kfree(ext2_mnt);
        return NULL;
    }

    root_vnode->ino = EXT2_ROOT_INO;
    root_vnode->mode = root_inode->i_mode & 0x0FFF;
    root_vnode->size = root_inode->i_size;
    root_vnode->type = ((root_inode->i_mode & 0xF000) == EXT2_S_IFDIR) ? VNODE_DIR : VNODE_REG;
    root_vnode->ref_count = 0;
    root_vnode->mount = mnt;
    root_vnode->fs_data = root_inode;
    root_vnode->fs_type = mnt->fs_type;

    printk("ext2: mounted successfully\n");
    return root_vnode;
}

// Unmount ext2 filesystem
int ext2_unmount(mount_t *mnt) {
    if (!mnt || !mnt->fs_data) return -1;
    
    ext2_mount_data_t *ext2_mnt = (ext2_mount_data_t *)mnt->fs_data;
    
    // Flush all cached blocks
    block_cache_flush();

    // Write superblock and GDT back to disk
    ext2_write_superblock(ext2_mnt);
    ext2_write_gdt(ext2_mnt);

    // Free mount data
    if (ext2_mnt->group_desc) kfree(ext2_mnt->group_desc);
    if (ext2_mnt->sb) kfree(ext2_mnt->sb);
    kfree(ext2_mnt);
    
    mnt->fs_data = NULL;
    
    printk("ext2: unmounted\n");
    return 0;
}

// Lookup a file in a directory
static uint32_t ext2_lookup_in_dir(ext2_mount_data_t *mnt, ext2_inode_t *dir_inode,
                                    const char *name) {
    if ((dir_inode->i_mode & 0xF000) != EXT2_S_IFDIR) {
        return 0;  // Not a directory
    }
    
    size_t name_len = strlen(name);
    uint8_t *block_buf = (uint8_t *)kmalloc(mnt->block_size);
    if (!block_buf) return 0;
    
    // Iterate through directory blocks
    uint32_t blocks = (dir_inode->i_size + mnt->block_size - 1) / mnt->block_size;
    
    for (uint32_t i = 0; i < blocks && i < EXT2_NDIR_BLOCKS; i++) {
        if (dir_inode->i_block[i] == 0) continue;
        
        if (block_cache_read(dir_inode->i_block[i], block_buf) != 0) {
            continue;
        }
        
        // Parse directory entries
        uint32_t offset = 0;
        while (offset < mnt->block_size) {
            ext2_dirent_t *dirent = (ext2_dirent_t *)(block_buf + offset);
            
            if (dirent->rec_len == 0) break;
            
            if (dirent->inode != 0 && 
                dirent->name_len == name_len &&
                strncmp(dirent->name, name, name_len) == 0) {
                uint32_t ino = dirent->inode;
                kfree(block_buf);
                return ino;
            }
            
            offset += dirent->rec_len;
        }
    }
    
    kfree(block_buf);
    return 0;  // Not found
}

// Lookup a path in ext2 filesystem
vnode_t *ext2_lookup(mount_t *mnt, const char *path) {
    if (!mnt || !mnt->fs_data || !path) return NULL;
    
    ext2_mount_data_t *ext2_mnt = (ext2_mount_data_t *)mnt->fs_data;
    
    // Start from root inode
    uint32_t ino = EXT2_ROOT_INO;
    
    // Handle root directory
    if (strcmp(path, "") == 0 || strcmp(path, "/") == 0) {
        ext2_inode_t *inode = (ext2_inode_t *)kmalloc(sizeof(ext2_inode_t));
        if (!inode) return NULL;
        
        if (ext2_read_inode(ext2_mnt, ino, inode) != 0) {
            kfree(inode);
            return NULL;
        }
        
        vnode_t *vnode = (vnode_t *)kmalloc(sizeof(vnode_t));
        if (!vnode) {
            kfree(inode);
            return NULL;
        }
        
        vnode->ino = ino;
        vnode->mode = inode->i_mode & 0x0FFF;
        vnode->size = inode->i_size;
        vnode->ref_count = 0;
        vnode->mount = mnt;
        vnode->fs_data = inode;
        vnode->fs_type = mnt->fs_type;
        {
            uint16_t fmt = inode->i_mode & 0xF000;
            if (fmt == EXT2_S_IFDIR)
                vnode->type = VNODE_DIR;
            else if (fmt == EXT2_S_IFLNK)
                vnode->type = VNODE_LNK;
            else
                vnode->type = VNODE_REG;
        }

        return vnode;
    }

    // Parse path components
    char *path_copy = (char *)kmalloc(strlen(path) + 1);
    if (!path_copy) return NULL;
    strcpy(path_copy, path);
    
    char *token = path_copy;
    char *next_token;
    
    // Skip leading slash
    if (*token == '/') token++;
    
    ext2_inode_t *current_inode = (ext2_inode_t *)kmalloc(sizeof(ext2_inode_t));
    if (!current_inode) {
        kfree(path_copy);
        return NULL;
    }
    
    // Read root inode
    if (ext2_read_inode(ext2_mnt, ino, current_inode) != 0) {
        kfree(current_inode);
        kfree(path_copy);
        return NULL;
    }
    
    // Traverse path
    while (*token) {
        // Find next slash
        next_token = token;
        while (*next_token && *next_token != '/') {
            next_token++;
        }
        
        // Null-terminate current component
        char saved = *next_token;
        *next_token = '\0';
        
        // Lookup component in current directory
        ino = ext2_lookup_in_dir(ext2_mnt, current_inode, token);
        if (ino == 0) {
            kfree(current_inode);
            kfree(path_copy);
            return NULL;  // Component not found
        }
        
        // Read the inode
        if (ext2_read_inode(ext2_mnt, ino, current_inode) != 0) {
            kfree(current_inode);
            kfree(path_copy);
            return NULL;
        }
        
        // Move to next component
        if (saved == '/') {
            token = next_token + 1;
        } else {
            break;
        }
    }
    
    kfree(path_copy);
    
    // Create vnode
    vnode_t *vnode = (vnode_t *)kmalloc(sizeof(vnode_t));
    if (!vnode) {
        kfree(current_inode);
        return NULL;
    }
    
    vnode->ino = ino;
    vnode->mode = current_inode->i_mode & 0x0FFF;
    vnode->size = current_inode->i_size;
    vnode->nlink = current_inode->i_links_count;
    vnode->ref_count = 0;
    vnode->mount = mnt;
    vnode->fs_data = current_inode;
    vnode->fs_type = mnt->fs_type;
    {
        uint16_t fmt = current_inode->i_mode & 0xF000;
        if (fmt == EXT2_S_IFDIR)
            vnode->type = VNODE_DIR;
        else if (fmt == EXT2_S_IFLNK)
            vnode->type = VNODE_LNK;
        else
            vnode->type = VNODE_REG;
    }

    return vnode;
}

// Read from ext2 file
int ext2_read(vnode_t *vnode, void *buf, size_t offset, size_t len) {
    if (!vnode || !vnode->fs_data || !buf) return -1;

    ext2_lock();

    ext2_inode_t *inode = (ext2_inode_t *)vnode->fs_data;
    ext2_mount_data_t *mnt = (ext2_mount_data_t *)vnode->mount->fs_data;

    // Refresh inode from disk so hardlinks see writes made via other vnodes.
    ext2_read_inode(mnt, (uint32_t)vnode->ino, inode);
    vnode->size = inode->i_size;

    // Check if reading past end of file
    if (offset >= inode->i_size) {
        ext2_unlock();
        return 0;
    }

    // Adjust length if reading past EOF
    if (offset + len > inode->i_size) {
        len = inode->i_size - offset;
    }

    size_t bytes_read = 0;
    uint8_t *block_buf = (uint8_t *)kmalloc(mnt->block_size);
    if (!block_buf) { ext2_unlock(); return -1; }

    while (bytes_read < len) {
        uint32_t block_idx = (offset + bytes_read) / mnt->block_size;
        uint32_t block_offset = (offset + bytes_read) % mnt->block_size;
        uint32_t to_read = mnt->block_size - block_offset;

        if (to_read > len - bytes_read) {
            to_read = len - bytes_read;
        }

        // Read the block
        if (ext2_read_inode_block(mnt, inode, block_idx, block_buf) != 0) {
            kfree(block_buf);
            ext2_unlock();
            return bytes_read > 0 ? (int)bytes_read : -1;
        }

        // Copy data to output buffer
        memcpy((uint8_t *)buf + bytes_read, block_buf + block_offset, to_read);
        bytes_read += to_read;
    }

    kfree(block_buf);
    ext2_unlock();
    return (int)bytes_read;
}

// Write to ext2 file
int ext2_write(vnode_t *vnode, const void *buf, size_t offset, size_t len) {
    if (!vnode || !vnode->fs_data || !buf) return -1;

    ext2_lock();

    ext2_inode_t *inode = (ext2_inode_t *)vnode->fs_data;
    ext2_mount_data_t *mnt = (ext2_mount_data_t *)vnode->mount->fs_data;

    // Refresh inode from disk so hardlinks see the current block map.
    ext2_read_inode(mnt, (uint32_t)vnode->ino, inode);
    vnode->size = inode->i_size;

    size_t bytes_written = 0;
    uint8_t *block_buf = (uint8_t *)kmalloc(mnt->block_size);
    if (!block_buf) { ext2_unlock(); return -1; }

    while (bytes_written < len) {
        uint32_t block_idx    = (offset + bytes_written) / mnt->block_size;
        uint32_t block_offset = (offset + bytes_written) % mnt->block_size;
        uint32_t to_write     = mnt->block_size - block_offset;
        if (to_write > len - bytes_written)
            to_write = len - bytes_written;

        // Read existing block for partial writes so we don't clobber existing data
        if (block_offset != 0 || to_write != mnt->block_size) {
            if (ext2_read_inode_block(mnt, inode, block_idx, block_buf) != 0) {
                kfree(block_buf);
                ext2_unlock();
                return bytes_written > 0 ? (int)bytes_written : -1;
            }
        } else {
            memset(block_buf, 0, mnt->block_size);
        }

        memcpy(block_buf + block_offset, (const uint8_t *)buf + bytes_written, to_write);

        if (ext2_write_inode_block(mnt, inode, block_idx, block_buf) != 0) {
            kfree(block_buf);
            ext2_unlock();
            return bytes_written > 0 ? (int)bytes_written : -1;
        }

        bytes_written += to_write;
    }

    kfree(block_buf);

    // Update inode size and persist
    if (offset + bytes_written > inode->i_size) {
        inode->i_size = offset + bytes_written;
        vnode->size   = inode->i_size;
    }
    ext2_write_inode(mnt, vnode->ino, inode);

    // Persist superblock and GDT counts once per write (not per block)
    ext2_write_superblock(mnt);
    ext2_write_gdt(mnt);

    ext2_unlock();
    return (int)bytes_written;
}

/*
 * ext2_dirent_append — insert a new directory entry into an already-loaded
 * directory block, splitting the last entry's slack space.
 *
 * Returns 0 on success, -1 if no space (caller must allocate a new block).
 */
static int ext2_dirent_append(uint8_t *block_buf, uint32_t block_size,
                               uint32_t new_ino, const char *name,
                               uint8_t file_type) {
    uint16_t name_len  = (uint16_t)strlen(name);
    uint16_t need      = (uint16_t)((8 + name_len + 3) & ~3u); /* aligned */

    uint32_t offset = 0;
    while (offset < block_size) {
        ext2_dirent_t *de = (ext2_dirent_t *)(block_buf + offset);
        if (de->rec_len == 0)
            break;

        /* Minimum space occupied by this entry */
        uint16_t min_len = (uint16_t)((8 + de->name_len + 3) & ~3u);
        uint16_t slack   = de->rec_len - min_len;

        if (de->inode == 0) {
            /* Coalesce adjacent deleted slots so larger entries can be served.
             * ext2 unlink leaves rec_len intact, so a directory that saw many
             * short-lived small files accumulates a run of tiny deleted slots
             * that on their own are too small for a longer new name. */
            uint32_t next_off = offset + de->rec_len;
            while (next_off + 8 <= block_size) {
                ext2_dirent_t *nx = (ext2_dirent_t *)(block_buf + next_off);
                if (nx->rec_len == 0 || nx->inode != 0) break;
                de->rec_len = (uint16_t)(de->rec_len + nx->rec_len);
                next_off += nx->rec_len;
            }

            /* Deleted entry — reuse it if big enough, splitting the remainder */
            if (de->rec_len >= need) {
                uint16_t old_rec_len = de->rec_len;
                if (old_rec_len - need >= 8) {
                    ext2_dirent_t *rump = (ext2_dirent_t *)(block_buf + offset + need);
                    rump->inode    = 0;
                    rump->rec_len  = old_rec_len - need;
                    rump->name_len = 0;
                    de->rec_len    = need;
                }
                de->inode     = new_ino;
                de->name_len  = name_len;
                de->file_type = file_type;
                memcpy(de->name, name, name_len);
                return 0;
            }
        } else if (slack >= need) {
            /* Split the slack from the last (or any) entry with enough room */
            ext2_dirent_t *new_de = (ext2_dirent_t *)(block_buf + offset + min_len);
            new_de->inode     = new_ino;
            new_de->rec_len   = de->rec_len - min_len;
            new_de->name_len  = name_len;
            new_de->file_type = file_type;
            memcpy(new_de->name, name, name_len);
            de->rec_len = min_len;
            return 0;
        }

        offset += de->rec_len;
    }

    /* Block is full or empty with no entries yet */
    if (offset + need <= block_size) {
        ext2_dirent_t *de = (ext2_dirent_t *)(block_buf + offset);
        de->inode     = new_ino;
        de->rec_len   = block_size - offset; /* last entry spans to end */
        de->name_len  = name_len;
        de->file_type = file_type;
        memcpy(de->name, name, name_len);
        return 0;
    }

    return -1; /* no space */
}

// Create a new file in ext2
int ext2_create(mount_t *mnt, const char *path, uint32_t mode) {
    if (!mnt || !mnt->fs_data || !path) return -1;

    ext2_lock();

    ext2_mount_data_t *ext2_mnt = (ext2_mount_data_t *)mnt->fs_data;

    // Parse path to get parent directory and filename
    char *path_copy = (char *)kmalloc(strlen(path) + 1);
    if (!path_copy) { ext2_unlock(); return -1; }
    strcpy(path_copy, path);

    // Find last slash
    char *last_slash = NULL;
    for (char *p = path_copy; *p; p++) {
        if (*p == '/') last_slash = p;
    }

    char *filename;
    char *parent_path;

    if (last_slash) {
        *last_slash = '\0';
        filename = last_slash + 1;
        parent_path = path_copy;
        if (*parent_path == '\0') parent_path = "/";
    } else {
        filename = path_copy;
        parent_path = "/";
    }

    // Lookup parent directory
    vnode_t *parent_vnode = ext2_lookup(mnt, parent_path);
    if (!parent_vnode) {
        printk("ext2: parent directory not found: %s\n", parent_path);
        kfree(path_copy);
        ext2_unlock();
        return -1;
    }

    ext2_inode_t *parent_inode = (ext2_inode_t *)parent_vnode->fs_data;

    // Allocate new inode
    uint32_t new_ino = ext2_alloc_inode(ext2_mnt);
    if (new_ino == 0) {
        kfree(parent_vnode->fs_data);
        kfree(parent_vnode);
        kfree(path_copy);
        ext2_unlock();
        return -1;
    }

    // Initialize new inode
    ext2_inode_t new_inode;
    memset(&new_inode, 0, sizeof(ext2_inode_t));
    new_inode.i_mode = EXT2_S_IFREG | (mode & 0xFFF);
    new_inode.i_uid = 0;
    new_inode.i_gid = 0;
    new_inode.i_size = 0;
    new_inode.i_links_count = 1;
    new_inode.i_blocks = 0;

    // Write new inode to disk
    if (ext2_write_inode(ext2_mnt, new_ino, &new_inode) != 0) {
        kfree(parent_vnode->fs_data);
        kfree(parent_vnode);
        kfree(path_copy);
        ext2_unlock();
        return -1;
    }

    // Add directory entry to parent
    // Find space in parent directory
    uint8_t *block_buf = (uint8_t *)kmalloc(ext2_mnt->block_size);
    if (!block_buf) {
        kfree(parent_vnode->fs_data);
        kfree(parent_vnode);
        kfree(path_copy);
        ext2_unlock();
        return -1;
    }

    // Load (or allocate) the first directory block of the parent
    if (parent_inode->i_block[0] == 0) {
        uint32_t new_block = ext2_alloc_block(ext2_mnt);
        if (new_block == 0) {
            kfree(block_buf);
            kfree(parent_vnode->fs_data);
            kfree(parent_vnode);
            kfree(path_copy);
            ext2_unlock();
            return -1;
        }
        parent_inode->i_block[0] = new_block;
        parent_inode->i_blocks  += 2;
        memset(block_buf, 0, ext2_mnt->block_size);
    } else {
        if (block_cache_read(parent_inode->i_block[0], block_buf) != 0) {
            kfree(block_buf);
            kfree(parent_vnode->fs_data);
            kfree(parent_vnode);
            kfree(path_copy);
            ext2_unlock();
            return -1;
        }
    }

    // Insert new directory entry by splitting the last entry's slack space
    if (ext2_dirent_append(block_buf, ext2_mnt->block_size,
                           new_ino, filename, EXT2_FT_REG_FILE) != 0) {
        printk("ext2: directory block full, cannot create %s\n", path);
        kfree(block_buf);
        kfree(parent_vnode->fs_data);
        kfree(parent_vnode);
        kfree(path_copy);
        ext2_unlock();
        return -1;
    }

    // Write directory block back
    if (block_cache_write(parent_inode->i_block[0], block_buf) != 0) {
        kfree(block_buf);
        kfree(parent_vnode->fs_data);
        kfree(parent_vnode);
        kfree(path_copy);
        ext2_unlock();
        return -1;
    }

    // Update parent directory size to cover the full block and persist
    if (parent_inode->i_size < ext2_mnt->block_size)
        parent_inode->i_size = ext2_mnt->block_size;
    ext2_write_inode(ext2_mnt, parent_vnode->ino, parent_inode);
    ext2_write_superblock(ext2_mnt);
    ext2_write_gdt(ext2_mnt);

    kfree(block_buf);
    kfree(parent_vnode->fs_data);
    kfree(parent_vnode);
    kfree(path_copy);

    ext2_unlock();
    printk("ext2: created file %s (inode %d)\n", path, new_ino);
    return 0;
}

// Delete a file from ext2
int ext2_unlink(mount_t *mnt, const char *path) {
    if (!mnt || !mnt->fs_data || !path) return -1;

    ext2_lock();

    ext2_mount_data_t *ext2_mnt = (ext2_mount_data_t *)mnt->fs_data;

    // Parse path to get parent directory and filename
    char *path_copy = (char *)kmalloc(strlen(path) + 1);
    if (!path_copy) { ext2_unlock(); return -1; }
    strcpy(path_copy, path);

    char *last_slash = NULL;
    for (char *p = path_copy; *p; p++) {
        if (*p == '/') last_slash = p;
    }

    char *filename;
    char *parent_path;

    if (last_slash) {
        *last_slash = '\0';
        filename = last_slash + 1;
        parent_path = path_copy;
        if (*parent_path == '\0') parent_path = "/";
    } else {
        filename = path_copy;
        parent_path = "/";
    }

    // Lookup parent directory
    vnode_t *parent_vnode = ext2_lookup(mnt, parent_path);
    if (!parent_vnode) {
        kfree(path_copy);
        ext2_unlock();
        return -1;
    }

    ext2_inode_t *parent_inode = (ext2_inode_t *)parent_vnode->fs_data;

    // Find and remove directory entry
    uint8_t *block_buf = (uint8_t *)kmalloc(ext2_mnt->block_size);
    if (!block_buf) {
        kfree(parent_vnode->fs_data);
        kfree(parent_vnode);
        kfree(path_copy);
        ext2_unlock();
        return -1;
    }

    int found = 0;
    uint32_t file_ino = 0;
    size_t name_len = strlen(filename);

    uint32_t dir_blocks = (parent_inode->i_size + ext2_mnt->block_size - 1) / ext2_mnt->block_size;
    for (uint32_t bi = 0; bi < dir_blocks && bi < EXT2_NDIR_BLOCKS && !found; bi++) {
        if (parent_inode->i_block[bi] == 0) continue;
        if (block_cache_read(parent_inode->i_block[bi], block_buf) != 0) continue;

        uint32_t offset = 0;
        while (offset + 8 <= ext2_mnt->block_size) {
            ext2_dirent_t *dirent = (ext2_dirent_t *)(block_buf + offset);
            if (dirent->rec_len == 0) break;

            if (dirent->inode != 0 &&
                dirent->name_len == name_len &&
                strncmp(dirent->name, filename, name_len) == 0) {
                file_ino = dirent->inode;
                dirent->inode = 0;
                found = 1;
                block_cache_write(parent_inode->i_block[bi], block_buf);
                break;
            }

            offset += dirent->rec_len;
        }
    }

    kfree(block_buf);
    kfree(parent_vnode->fs_data);
    kfree(parent_vnode);
    kfree(path_copy);

    if (!found) {
        printk("ext2: file not found for unlink: %s\n", path);
        ext2_unlock();
        return -1;
    }

    // Per POSIX, data blocks must remain accessible via open fds until the last
    // fd closes. We only decrement links and mark the inode deleted here; the
    // actual block/bitmap reclaim happens in ext2_release, which is called from
    // file_release on last close, or from vfs_unlink when ref_count == 1.
    ext2_inode_t inode;
    if (ext2_read_inode(ext2_mnt, file_ino, &inode) == 0) {
        if (inode.i_links_count > 0)
            inode.i_links_count--;

        if (inode.i_links_count == 0)
            inode.i_dtime = 1;

        ext2_write_inode(ext2_mnt, file_ino, &inode);
        ext2_write_superblock(ext2_mnt);
        ext2_write_gdt(ext2_mnt);
    }

    ext2_unlock();
    printk("ext2: unlinked %s\n", path);
    return 0;
}

// Create a directory in ext2
int ext2_mkdir(mount_t *mnt, const char *path, uint32_t mode) {
    if (!mnt || !mnt->fs_data || !path) return -1;
    
    ext2_mount_data_t *ext2_mnt = (ext2_mount_data_t *)mnt->fs_data;
    
    // Parse path
    char *path_copy = (char *)kmalloc(strlen(path) + 1);
    if (!path_copy) return -1;
    strcpy(path_copy, path);
    
    char *last_slash = NULL;
    for (char *p = path_copy; *p; p++) {
        if (*p == '/') last_slash = p;
    }
    
    char *dirname;
    char *parent_path;
    
    if (last_slash) {
        *last_slash = '\0';
        dirname = last_slash + 1;
        parent_path = path_copy;
        if (*parent_path == '\0') parent_path = "/";
    } else {
        dirname = path_copy;
        parent_path = "/";
    }
    
    // Lookup parent directory
    vnode_t *parent_vnode = ext2_lookup(mnt, parent_path);
    if (!parent_vnode) {
        kfree(path_copy);
        return -1;
    }

    ext2_inode_t *parent_inode = (ext2_inode_t *)parent_vnode->fs_data;

    // Check if the directory already exists
    if (ext2_lookup_in_dir(ext2_mnt, parent_inode, dirname) != 0) {
        kfree(parent_vnode->fs_data);
        kfree(parent_vnode);
        kfree(path_copy);
        return -17; // -EEXIST
    }

    // Allocate new inode
    uint32_t new_ino = ext2_alloc_inode(ext2_mnt);
    if (new_ino == 0) {
        kfree(parent_vnode->fs_data);
        kfree(parent_vnode);
        kfree(path_copy);
        return -1;
    }
    
    // Allocate block for directory
    uint32_t dir_block = ext2_alloc_block(ext2_mnt);
    if (dir_block == 0) {
        kfree(parent_vnode->fs_data);
        kfree(parent_vnode);
        kfree(path_copy);
        return -1;
    }
    
    // Initialize directory inode
    ext2_inode_t new_inode;
    memset(&new_inode, 0, sizeof(ext2_inode_t));
    new_inode.i_mode = EXT2_S_IFDIR | (mode & 0xFFF);
    new_inode.i_uid = 0;
    new_inode.i_gid = 0;
    new_inode.i_size = ext2_mnt->block_size;
    new_inode.i_links_count = 2;  // . and parent
    new_inode.i_blocks = 2;  // In 512-byte blocks
    new_inode.i_block[0] = dir_block;

    // Create . and .. entries
    uint8_t *block_buf = (uint8_t *)kmalloc(ext2_mnt->block_size);
    if (!block_buf) {
        kfree(parent_vnode->fs_data);
        kfree(parent_vnode);
        kfree(path_copy);
        return -1;
    }
    memset(block_buf, 0, ext2_mnt->block_size);

    // Entry for "."
    ext2_dirent_t *dot = (ext2_dirent_t *)block_buf;
    dot->inode = new_ino;
    dot->rec_len = 12;
    dot->name_len = 1;
    dot->file_type = EXT2_FT_DIR;
    dot->name[0] = '.';

    // Entry for ".."
    ext2_dirent_t *dotdot = (ext2_dirent_t *)(block_buf + 12);
    dotdot->inode = parent_vnode->ino;
    dotdot->rec_len = ext2_mnt->block_size - 12;
    dotdot->name_len = 2;
    dotdot->file_type = EXT2_FT_DIR;
    dotdot->name[0] = '.';
    dotdot->name[1] = '.';
    
    // Write directory block
    block_cache_write(dir_block, block_buf);
    kfree(block_buf);
    
    // Write new inode
    ext2_write_inode(ext2_mnt, new_ino, &new_inode);
    
    // Add entry to parent directory (similar to create)
    block_buf = (uint8_t *)kmalloc(ext2_mnt->block_size);
    if (!block_buf) {
        kfree(parent_vnode->fs_data);
        kfree(parent_vnode);
        kfree(path_copy);
        return -1;
    }

    if (parent_inode->i_block[0] == 0) {
        uint32_t new_block = ext2_alloc_block(ext2_mnt);
        if (new_block == 0) {
            kfree(block_buf);
            kfree(parent_vnode->fs_data);
            kfree(parent_vnode);
            kfree(path_copy);
            return -1;
        }
        parent_inode->i_block[0] = new_block;
        parent_inode->i_blocks  += 2;
        memset(block_buf, 0, ext2_mnt->block_size);
    } else {
        if (block_cache_read(parent_inode->i_block[0], block_buf) != 0) {
            kfree(block_buf);
            kfree(parent_vnode->fs_data);
            kfree(parent_vnode);
            kfree(path_copy);
            return -1;
        }
    }

    // Insert new directory entry by splitting the last entry's slack space
    if (ext2_dirent_append(block_buf, ext2_mnt->block_size,
                           new_ino, dirname, EXT2_FT_DIR) != 0) {
        printk("ext2: directory block full, cannot mkdir %s\n", path);
        kfree(block_buf);
        kfree(parent_vnode->fs_data);
        kfree(parent_vnode);
        kfree(path_copy);
        return -1;
    }

    block_cache_write(parent_inode->i_block[0], block_buf);

    // Update parent
    if (parent_inode->i_size < ext2_mnt->block_size)
        parent_inode->i_size = ext2_mnt->block_size;
    parent_inode->i_links_count++;  // New subdirectory adds link
    ext2_write_inode(ext2_mnt, parent_vnode->ino, parent_inode);
    ext2_write_superblock(ext2_mnt);
    ext2_write_gdt(ext2_mnt);
    
    kfree(block_buf);
    kfree(parent_vnode->fs_data);
    kfree(parent_vnode);
    kfree(path_copy);

    block_cache_flush();
    printk("ext2: created directory %s (inode %u)\n", path, new_ino);
    return 0;
}

// Truncate an ext2 file to zero length: free all data blocks and reset size.
static void ext2_free_block(ext2_mount_data_t *mnt, uint32_t blk)
{
    uint32_t group = (blk - mnt->sb->s_first_data_block) / mnt->blocks_per_group;
    uint32_t bit   = (blk - mnt->sb->s_first_data_block) % mnt->blocks_per_group;
    uint8_t *bitmap = (uint8_t *)kmalloc(mnt->block_size);
    if (!bitmap) return;
    if (block_cache_read(mnt->group_desc[group].bg_block_bitmap, bitmap) == 0) {
        bitmap[bit / 8] &= ~(1u << (bit % 8));
        block_cache_write(mnt->group_desc[group].bg_block_bitmap, bitmap);
        mnt->group_desc[group].bg_free_blocks_count++;
        mnt->sb->s_free_blocks_count++;
        ext2_write_superblock(mnt);
        ext2_write_gdt(mnt);
    }
    kfree(bitmap);
}

static int ext2_truncate_to(vnode_t *vnode, off_t length)
{
    if (!vnode || !vnode->fs_data || !vnode->mount) return -EINVAL;
    if (length < 0) return -EINVAL;

    ext2_inode_t *inode = (ext2_inode_t *)vnode->fs_data;
    ext2_mount_data_t *mnt = (ext2_mount_data_t *)vnode->mount->fs_data;

    /* Refresh from disk to avoid clobbering shared fields (e.g. hardlinks). */
    ext2_read_inode(mnt, (uint32_t)vnode->ino, inode);

    off_t cur_size = (off_t)inode->i_size;
    uint32_t bsz   = mnt->block_size;

    /* Extension: just advance EOF; unallocated blocks read back as zero. */
    if (length >= cur_size) {
        if (length != cur_size) {
            inode->i_size = (uint32_t)length;
            vnode->size   = (size_t)length;
            ext2_write_inode(mnt, vnode->ino, inode);
        }
        return 0;
    }

    /* Shrink: walk direct blocks only (same scope as the original truncate). */
    for (int i = 0; i < EXT2_NDIR_BLOCKS; i++) {
        uint32_t blk = inode->i_block[i];
        if (blk == 0) continue;

        off_t block_start = (off_t)i * (off_t)bsz;
        off_t block_end   = block_start + (off_t)bsz;

        if (block_start >= length) {
            ext2_free_block(mnt, blk);
            inode->i_block[i] = 0;
        } else if (block_end > length) {
            /* Zero the tail of the partial last block. */
            uint32_t tail = (uint32_t)(length - block_start);
            uint8_t *buf = (uint8_t *)kmalloc(bsz);
            if (buf) {
                if (block_cache_read(blk, buf) == 0) {
                    memset(buf + tail, 0, bsz - tail);
                    block_cache_write(blk, buf);
                }
                kfree(buf);
            }
        }
    }

    uint32_t kept = 0;
    for (int i = 0; i < EXT2_NDIR_BLOCKS; i++)
        if (inode->i_block[i]) kept++;
    inode->i_blocks = kept * (bsz / 512);

    inode->i_size = (uint32_t)length;
    vnode->size   = (size_t)length;
    ext2_write_inode(mnt, vnode->ino, inode);
    return 0;
}

// Enumerate directory entries — implements the readdir VFS op used by ls/getdents
static int ext2_readdir(mount_t *mnt, vnode_t *vnode, uint32_t start_idx,
                        void *buf, uint32_t bufsize, uint32_t *entries_out) {
    if (!mnt || !mnt->fs_data || !vnode || !vnode->fs_data || !buf || !entries_out)
        return -1;

    ext2_mount_data_t *ext2_mnt = (ext2_mount_data_t *)mnt->fs_data;
    ext2_inode_t *inode = (ext2_inode_t *)vnode->fs_data;

    *entries_out = 0;
    uint32_t buf_off = 0;   // write cursor into caller's buf
    uint32_t idx    = 0;    // absolute entry index (for start_idx skip)

    uint8_t *block_buf = (uint8_t *)kmalloc(ext2_mnt->block_size);
    if (!block_buf) return -1;

    uint32_t blocks = (inode->i_size + ext2_mnt->block_size - 1) / ext2_mnt->block_size;

    for (uint32_t b = 0; b < blocks && b < EXT2_NDIR_BLOCKS; b++) {
        if (inode->i_block[b] == 0) continue;
        if (block_cache_read(inode->i_block[b], block_buf) != 0) continue;

        uint32_t off = 0;
        while (off + 8 <= ext2_mnt->block_size) {
            ext2_dirent_t *de = (ext2_dirent_t *)(block_buf + off);
            if (de->rec_len == 0) break;
            off += de->rec_len;

            if (de->inode == 0) continue;  // deleted entry

            if (idx++ < start_idx) continue;

            uint32_t reclen = FS_DENT_RECLEN(de->name_len);
            if (buf_off + reclen > bufsize) goto done;

            fs_dent_t *out = (fs_dent_t *)((char *)buf + buf_off);
            out->d_ino    = de->inode;
            out->d_reclen = (reclen_t)reclen;
            if (de->file_type == EXT2_FT_DIR)
                out->d_type = DT_DIR;
            else if (de->file_type == EXT2_FT_SYMLINK)
                out->d_type = DT_LNK;
            else
                out->d_type = DT_REG;
            memcpy(out->d_name, de->name, de->name_len);
            out->d_name[de->name_len] = '\0';

            buf_off += reclen;
            (*entries_out)++;
        }
    }

done:
    kfree(block_buf);
    return (int)buf_off;
}

static int ext2_chmod(mount_t *mnt, vnode_t *vnode, mode_t mode) {
    if (!mnt || !mnt->fs_data || !vnode) return -EINVAL;

    ext2_mount_data_t *ext2_mnt = (ext2_mount_data_t *)mnt->fs_data;

    ext2_inode_t inode;
    if (ext2_read_inode(ext2_mnt, (uint32_t)vnode->ino, &inode) != 0)
        return -EIO;

    /* Preserve file-type bits (top 4 bits), replace permission bits */
    inode.i_mode = (inode.i_mode & ~(uint16_t)0x0FFF) | (uint16_t)(mode & 0x0FFF);

    return ext2_write_inode(ext2_mnt, (uint32_t)vnode->ino, &inode);
}

static int ext2_rmdir(mount_t *mnt, const char *path) {
    if (!mnt || !mnt->fs_data || !path) return -EINVAL;

    ext2_mount_data_t *ext2_mnt = (ext2_mount_data_t *)mnt->fs_data;

    /* Look up the target — must be a directory */
    vnode_t *target = ext2_lookup(mnt, path);
    if (!target) return -ENOENT;
    if (target->type != VNODE_DIR) {
        kfree(target->fs_data);
        kfree(target);
        return -ENOTDIR;
    }

    /* Check directory is empty: scan entries, fail on anything besides . and .. */
    uint8_t *dir_buf = (uint8_t *)kmalloc(ext2_mnt->block_size);
    if (!dir_buf) {
        kfree(target->fs_data);
        kfree(target);
        return -ENOMEM;
    }

    ext2_inode_t *dir_inode = (ext2_inode_t *)target->fs_data;
    if (block_cache_read(dir_inode->i_block[0], dir_buf) == 0) {
        uint32_t offset = 0;
        while (offset < ext2_mnt->block_size) {
            ext2_dirent_t *de = (ext2_dirent_t *)(dir_buf + offset);
            if (de->rec_len == 0) break;
            if (de->inode != 0) {
                char name[256];
                memcpy(name, de->name, de->name_len);
                name[de->name_len] = '\0';
                if (strcmp(name, ".") != 0 && strcmp(name, "..") != 0) {
                    kfree(dir_buf);
                    kfree(target->fs_data);
                    kfree(target);
                    return -ENOTEMPTY;
                }
            }
            offset += de->rec_len;
        }
    }

    kfree(dir_buf);
    kfree(target->fs_data);
    kfree(target);

    /* Directory is empty — reuse unlink to remove the entry from the parent */
    return ext2_unlink(mnt, path);
}

// Create a hard link: newpath is a new directory entry pointing to the same
// inode as oldpath. Both paths must be on the same mount.
int ext2_link(mount_t *mnt, const char *oldpath, const char *newpath) {
    if (!mnt || !mnt->fs_data || !oldpath || !newpath) return -EINVAL;

    ext2_mount_data_t *ext2_mnt = (ext2_mount_data_t *)mnt->fs_data;

    // 1. Resolve oldpath to its inode number.
    vnode_t *old_vnode = ext2_lookup(mnt, oldpath);
    if (!old_vnode) return -ENOENT;
    if (old_vnode->type == VNODE_DIR) {
        kfree(old_vnode->fs_data); kfree(old_vnode);
        return -EPERM;
    }
    uint32_t target_ino = (uint32_t)old_vnode->ino;
    uint8_t  file_type  = EXT2_FT_REG_FILE;
    kfree(old_vnode->fs_data); kfree(old_vnode);

    // Fail if newpath already exists.
    vnode_t *existing = ext2_lookup(mnt, newpath);
    if (existing) {
        kfree(existing->fs_data); kfree(existing);
        return -EEXIST;
    }

    // 2. Parse newpath into parent directory and filename.
    char *path_copy = kmalloc(strlen(newpath) + 1);
    if (!path_copy) return -ENOMEM;
    strcpy(path_copy, newpath);

    char *last_slash = NULL;
    for (char *p = path_copy; *p; p++)
        if (*p == '/') last_slash = p;

    char *new_name, *parent_path;
    if (last_slash) {
        *last_slash = '\0';
        new_name    = last_slash + 1;
        parent_path = path_copy;
        if (*parent_path == '\0') parent_path = "/";
    } else {
        new_name    = path_copy;
        parent_path = "/";
    }

    // 3. Look up parent directory.
    vnode_t *parent = ext2_lookup(mnt, parent_path);
    if (!parent) { kfree(path_copy); return -ENOENT; }
    ext2_inode_t *par_inode = (ext2_inode_t *)parent->fs_data;

    // 4. Insert directory entry.
    uint8_t *block_buf = kmalloc(ext2_mnt->block_size);
    if (!block_buf) {
        kfree(parent->fs_data); kfree(parent); kfree(path_copy);
        return -ENOMEM;
    }
    if (par_inode->i_block[0] == 0 ||
        block_cache_read(par_inode->i_block[0], block_buf) != 0) {
        kfree(block_buf); kfree(parent->fs_data);
        kfree(parent); kfree(path_copy);
        return -EIO;
    }
    if (ext2_dirent_append(block_buf, ext2_mnt->block_size,
                           target_ino, new_name, file_type) != 0) {
        kfree(block_buf); kfree(parent->fs_data);
        kfree(parent); kfree(path_copy);
        return -ENOSPC;
    }
    block_cache_write(par_inode->i_block[0], block_buf);
    kfree(block_buf); kfree(parent->fs_data); kfree(parent); kfree(path_copy);

    // 5. Increment i_links_count on the target inode.
    ext2_inode_t tgt_inode;
    if (ext2_read_inode(ext2_mnt, target_ino, &tgt_inode) != 0) return -EIO;
    tgt_inode.i_links_count++;
    ext2_write_inode(ext2_mnt, target_ino, &tgt_inode);

    ext2_write_superblock(ext2_mnt);
    ext2_write_gdt(ext2_mnt);
    block_cache_flush();
    return 0;
}

// Create a symbolic link: linkpath is a new inode whose data is the string target.
int ext2_symlink(mount_t *mnt, const char *target, const char *linkpath) {
    if (!mnt || !mnt->fs_data || !target || !linkpath) return -EINVAL;

    ext2_mount_data_t *ext2_mnt = (ext2_mount_data_t *)mnt->fs_data;

    // Fail if linkpath already exists.
    vnode_t *existing = ext2_lookup(mnt, linkpath);
    if (existing) {
        kfree(existing->fs_data); kfree(existing);
        return -EEXIST;
    }

    // Parse linkpath into parent + name.
    char *path_copy = kmalloc(strlen(linkpath) + 1);
    if (!path_copy) return -ENOMEM;
    strcpy(path_copy, linkpath);

    char *last_slash = NULL;
    for (char *p = path_copy; *p; p++)
        if (*p == '/') last_slash = p;

    char *link_name, *parent_path;
    if (last_slash) {
        *last_slash = '\0';
        link_name   = last_slash + 1;
        parent_path = path_copy;
        if (*parent_path == '\0') parent_path = "/";
    } else {
        link_name   = path_copy;
        parent_path = "/";
    }

    // Look up parent directory.
    vnode_t *parent = ext2_lookup(mnt, parent_path);
    if (!parent) { kfree(path_copy); return -ENOENT; }
    ext2_inode_t *par_inode = (ext2_inode_t *)parent->fs_data;

    // Allocate a new inode.
    uint32_t new_ino = ext2_alloc_inode(ext2_mnt);
    if (new_ino == 0) {
        kfree(parent->fs_data); kfree(parent); kfree(path_copy);
        return -ENOSPC;
    }

    size_t tlen = strlen(target);

    // Build the new symlink inode.
    ext2_inode_t new_inode;
    memset(&new_inode, 0, sizeof(ext2_inode_t));
    new_inode.i_mode        = (uint16_t)(EXT2_S_IFLNK | 0777);
    new_inode.i_links_count = 1;
    new_inode.i_size        = (uint32_t)tlen;

    if (tlen <= 60) {
        // Fast symlink: store target directly in i_block array.
        memcpy((char *)new_inode.i_block, target, tlen);
        new_inode.i_blocks = 0;
    } else {
        // Slow symlink: allocate a data block.
        uint32_t blk = ext2_alloc_block(ext2_mnt);
        if (blk == 0) {
            kfree(parent->fs_data); kfree(parent); kfree(path_copy);
            return -ENOSPC;
        }
        uint8_t *blk_buf = kmalloc(ext2_mnt->block_size);
        if (!blk_buf) {
            kfree(parent->fs_data); kfree(parent); kfree(path_copy);
            return -ENOMEM;
        }
        memset(blk_buf, 0, ext2_mnt->block_size);
        memcpy(blk_buf, target, tlen);
        block_cache_write(blk, blk_buf);
        kfree(blk_buf);

        new_inode.i_block[0] = blk;
        new_inode.i_blocks   = 2; // 512-byte units
    }

    ext2_write_inode(ext2_mnt, new_ino, &new_inode);

    // Insert directory entry in parent.
    uint8_t *block_buf = kmalloc(ext2_mnt->block_size);
    if (!block_buf) {
        kfree(parent->fs_data); kfree(parent); kfree(path_copy);
        return -ENOMEM;
    }

    if (par_inode->i_block[0] == 0) {
        uint32_t new_blk = ext2_alloc_block(ext2_mnt);
        if (new_blk == 0) {
            kfree(block_buf); kfree(parent->fs_data); kfree(parent); kfree(path_copy);
            return -ENOSPC;
        }
        par_inode->i_block[0] = new_blk;
        par_inode->i_blocks  += 2;
        memset(block_buf, 0, ext2_mnt->block_size);
    } else {
        if (block_cache_read(par_inode->i_block[0], block_buf) != 0) {
            kfree(block_buf); kfree(parent->fs_data); kfree(parent); kfree(path_copy);
            return -EIO;
        }
    }

    if (ext2_dirent_append(block_buf, ext2_mnt->block_size,
                           new_ino, link_name, EXT2_FT_SYMLINK) != 0) {
        kfree(block_buf); kfree(parent->fs_data); kfree(parent); kfree(path_copy);
        return -ENOSPC;
    }

    block_cache_write(par_inode->i_block[0], block_buf);

    if (par_inode->i_size < ext2_mnt->block_size)
        par_inode->i_size = ext2_mnt->block_size;
    ext2_write_inode(ext2_mnt, parent->ino, par_inode);
    ext2_write_superblock(ext2_mnt);
    ext2_write_gdt(ext2_mnt);

    kfree(block_buf); kfree(parent->fs_data); kfree(parent); kfree(path_copy);
    block_cache_flush();
    return 0;
}

// Read the target of a symlink vnode into buf (up to size bytes, no null terminator).
int ext2_readlink(vnode_t *vnode, char *buf, size_t size) {
    if (!vnode || !vnode->fs_data || !buf || size == 0) return -EINVAL;

    ext2_inode_t *inode = (ext2_inode_t *)vnode->fs_data;
    if ((inode->i_mode & 0xF000) != EXT2_S_IFLNK) return -EINVAL;

    size_t link_len = inode->i_size;
    if (link_len > size) link_len = size;

    if (inode->i_blocks == 0) {
        // Fast symlink: target is stored in i_block array.
        memcpy(buf, (char *)inode->i_block, link_len);
        return (int)link_len;
    }

    // Slow symlink: target is in a data block.
    ext2_mount_data_t *mnt = (ext2_mount_data_t *)vnode->mount->fs_data;
    uint8_t *block_buf = kmalloc(mnt->block_size);
    if (!block_buf) return -ENOMEM;
    if (block_cache_read(inode->i_block[0], block_buf) != 0) {
        kfree(block_buf); return -EIO;
    }
    memcpy(buf, block_buf, link_len);
    kfree(block_buf);
    return (int)link_len;
}

// Called when the last fd referencing this vnode is closed. If the inode was
// unlinked while open (i_links_count==0, i_dtime!=0), free its blocks and
// inode bitmap entry now — this is the POSIX "free on last close" step.
static int ext2_release(vnode_t *vnode) {
    if (!vnode || !vnode->fs_data || !vnode->mount) return 0;

    ext2_lock();

    ext2_mount_data_t *mnt = (ext2_mount_data_t *)vnode->mount->fs_data;
    ext2_inode_t inode;
    if (ext2_read_inode(mnt, (uint32_t)vnode->ino, &inode) != 0) { ext2_unlock(); return 0; }

    if (inode.i_links_count != 0 || inode.i_dtime == 0) {
        ext2_unlock();
        return 0; // still linked or not deleted — nothing to do
    }

    // Free direct data blocks
    for (int i = 0; i < EXT2_NDIR_BLOCKS; i++) {
        uint32_t blk = inode.i_block[i];
        if (blk == 0) continue;
        uint32_t grp = (blk - mnt->sb->s_first_data_block) / mnt->blocks_per_group;
        uint32_t bit = (blk - mnt->sb->s_first_data_block) % mnt->blocks_per_group;
        uint8_t *bmap = (uint8_t *)kmalloc(mnt->block_size);
        if (bmap && block_cache_read(mnt->group_desc[grp].bg_block_bitmap, bmap) == 0) {
            bmap[bit / 8] &= ~(uint8_t)(1 << (bit % 8));
            block_cache_write(mnt->group_desc[grp].bg_block_bitmap, bmap);
            mnt->group_desc[grp].bg_free_blocks_count++;
            mnt->sb->s_free_blocks_count++;
        }
        if (bmap) kfree(bmap);
        inode.i_block[i] = 0;
    }

    // Free single-indirect tree: every data block referenced by the index
    // block, then the index block itself. The write path (ext2_write_inode_block)
    // only allocates through single-indirect, so DIND/TIND need not be walked.
    if (inode.i_block[EXT2_IND_BLOCK] != 0) {
        uint32_t ind_blk = inode.i_block[EXT2_IND_BLOCK];
        uint32_t *ind_buf = (uint32_t *)kmalloc(mnt->block_size);
        if (ind_buf && block_cache_read(ind_blk, ind_buf) == 0) {
            uint32_t ptrs = mnt->block_size / sizeof(uint32_t);
            for (uint32_t i = 0; i < ptrs; i++) {
                uint32_t blk = ind_buf[i];
                if (blk == 0) continue;
                uint32_t grp = (blk - mnt->sb->s_first_data_block) / mnt->blocks_per_group;
                uint32_t bit = (blk - mnt->sb->s_first_data_block) % mnt->blocks_per_group;
                uint8_t *bmap = (uint8_t *)kmalloc(mnt->block_size);
                if (bmap && block_cache_read(mnt->group_desc[grp].bg_block_bitmap, bmap) == 0) {
                    bmap[bit / 8] &= ~(uint8_t)(1 << (bit % 8));
                    block_cache_write(mnt->group_desc[grp].bg_block_bitmap, bmap);
                    mnt->group_desc[grp].bg_free_blocks_count++;
                    mnt->sb->s_free_blocks_count++;
                }
                if (bmap) kfree(bmap);
            }
        }
        if (ind_buf) kfree(ind_buf);

        uint32_t grp = (ind_blk - mnt->sb->s_first_data_block) / mnt->blocks_per_group;
        uint32_t bit = (ind_blk - mnt->sb->s_first_data_block) % mnt->blocks_per_group;
        uint8_t *bmap = (uint8_t *)kmalloc(mnt->block_size);
        if (bmap && block_cache_read(mnt->group_desc[grp].bg_block_bitmap, bmap) == 0) {
            bmap[bit / 8] &= ~(uint8_t)(1 << (bit % 8));
            block_cache_write(mnt->group_desc[grp].bg_block_bitmap, bmap);
            mnt->group_desc[grp].bg_free_blocks_count++;
            mnt->sb->s_free_blocks_count++;
        }
        if (bmap) kfree(bmap);
        inode.i_block[EXT2_IND_BLOCK] = 0;
    }

    inode.i_blocks = 0;

    // Free inode bitmap entry
    uint32_t igrp = ((uint32_t)vnode->ino - 1) / mnt->inodes_per_group;
    uint32_t ibit = ((uint32_t)vnode->ino - 1) % mnt->inodes_per_group;
    uint8_t *imap = (uint8_t *)kmalloc(mnt->block_size);
    if (imap && block_cache_read(mnt->group_desc[igrp].bg_inode_bitmap, imap) == 0) {
        imap[ibit / 8] &= ~(uint8_t)(1 << (ibit % 8));
        block_cache_write(mnt->group_desc[igrp].bg_inode_bitmap, imap);
        mnt->group_desc[igrp].bg_free_inodes_count++;
        mnt->sb->s_free_inodes_count++;
    }
    if (imap) kfree(imap);

    ext2_write_inode(mnt, (uint32_t)vnode->ino, &inode);
    ext2_write_superblock(mnt);
    ext2_write_gdt(mnt);
    block_cache_flush();
    ext2_unlock();
    return 0;
}

static uint32_t ext2_get_nlink(vnode_t *vnode) {
    if (!vnode || !vnode->fs_data) return 1;
    ext2_inode_t *inode = (ext2_inode_t *)vnode->fs_data;
    ext2_mount_data_t *ext2_mnt = (ext2_mount_data_t *)vnode->mount->fs_data;
    ext2_inode_t fresh;
    if (ext2_read_inode(ext2_mnt, (uint32_t)vnode->ino, &fresh) != 0)
        return inode->i_links_count;
    return fresh.i_links_count;
}

/*
 * ext2_rename — atomic rename within the same ext2 mount.
 * Both oldpath and newpath are relative to the mount root (no leading /).
 * Handles file→file and dir→dir renames. If newpath exists it is atomically
 * replaced (per POSIX), provided it is the same type as oldpath.
 */
static int ext2_rename(mount_t *mnt, const char *oldpath, const char *newpath) {
    if (!mnt || !mnt->fs_data || !oldpath || !newpath) return -EINVAL;
    ext2_mount_data_t *ext2_mnt = (ext2_mount_data_t *)mnt->fs_data;

    /* --- resolve src --- */
    vnode_t *src_vn = ext2_lookup(mnt, oldpath);
    if (!src_vn) return -ENOENT;
    uint32_t src_ino  = (uint32_t)src_vn->ino;
    int      src_is_dir = (src_vn->type == VNODE_DIR);
    uint8_t  ft = src_is_dir ? EXT2_FT_DIR : EXT2_FT_REG_FILE;
    kfree(src_vn->fs_data); kfree(src_vn);

    /* --- check dst --- */
    uint32_t dst_ino = 0;
    vnode_t *dst_vn = ext2_lookup(mnt, newpath);
    if (dst_vn) {
        if (src_is_dir && dst_vn->type != VNODE_DIR) {
            kfree(dst_vn->fs_data); kfree(dst_vn);
            return -ENOTDIR;
        }
        if (!src_is_dir && dst_vn->type == VNODE_DIR) {
            kfree(dst_vn->fs_data); kfree(dst_vn);
            return -EISDIR;
        }
        dst_ino = (uint32_t)dst_vn->ino;
        kfree(dst_vn->fs_data); kfree(dst_vn);
    }

    /* --- parse paths into parent + name --- */
    char *old_copy = kmalloc(strlen(oldpath) + 1);
    char *new_copy = kmalloc(strlen(newpath) + 1);
    if (!old_copy || !new_copy) { kfree(old_copy); kfree(new_copy); return -ENOMEM; }
    strcpy(old_copy, oldpath);
    strcpy(new_copy, newpath);

    char *old_slash = NULL, *new_slash = NULL;
    for (char *p = old_copy; *p; p++) if (*p == '/') old_slash = p;
    for (char *p = new_copy; *p; p++) if (*p == '/') new_slash = p;

    char *old_name, *old_parent_path;
    char *new_name, *new_parent_path;

    if (old_slash) { *old_slash = '\0'; old_name = old_slash + 1; old_parent_path = (*old_copy ? old_copy : "/"); }
    else           { old_name = old_copy; old_parent_path = "/"; }

    if (new_slash) { *new_slash = '\0'; new_name = new_slash + 1; new_parent_path = (*new_copy ? new_copy : "/"); }
    else           { new_name = new_copy; new_parent_path = "/"; }

    /* --- look up parent directories --- */
    vnode_t *src_parent = ext2_lookup(mnt, old_parent_path);
    if (!src_parent) { kfree(old_copy); kfree(new_copy); return -ENOENT; }

    vnode_t *dst_parent = ext2_lookup(mnt, new_parent_path);
    if (!dst_parent) {
        kfree(src_parent->fs_data); kfree(src_parent);
        kfree(old_copy); kfree(new_copy);
        return -ENOENT;
    }

    ext2_inode_t *sp_inode = (ext2_inode_t *)src_parent->fs_data;
    ext2_inode_t *dp_inode = (ext2_inode_t *)dst_parent->fs_data;

    uint8_t *block_buf = kmalloc(ext2_mnt->block_size);
    if (!block_buf) {
        kfree(src_parent->fs_data); kfree(src_parent);
        kfree(dst_parent->fs_data); kfree(dst_parent);
        kfree(old_copy); kfree(new_copy);
        return -ENOMEM;
    }

    /* --- if dst exists, remove its dirent from dst parent first --- */
    if (dst_ino) {
        size_t dn_len = strlen(new_name);
        uint32_t dp_blocks = (dp_inode->i_size + ext2_mnt->block_size - 1) / ext2_mnt->block_size;
        for (uint32_t bi = 0; bi < dp_blocks && bi < EXT2_NDIR_BLOCKS; bi++) {
            if (dp_inode->i_block[bi] == 0) continue;
            if (block_cache_read(dp_inode->i_block[bi], block_buf) != 0) continue;
            uint32_t offset = 0;
            while (offset + 8 <= ext2_mnt->block_size) {
                ext2_dirent_t *de = (ext2_dirent_t *)(block_buf + offset);
                if (de->rec_len == 0) break;
                if (de->inode != 0 && de->name_len == dn_len &&
                    strncmp(de->name, new_name, dn_len) == 0) {
                    de->inode = 0;
                    block_cache_write(dp_inode->i_block[bi], block_buf);
                    break;
                }
                offset += de->rec_len;
            }
        }
        /* Decrement link count on old dst inode */
        ext2_inode_t old_dst_inode;
        if (ext2_read_inode(ext2_mnt, dst_ino, &old_dst_inode) == 0) {
            if (old_dst_inode.i_links_count > 0) old_dst_inode.i_links_count--;
            if (old_dst_inode.i_links_count == 0) old_dst_inode.i_dtime = 1;
            ext2_write_inode(ext2_mnt, dst_ino, &old_dst_inode);
        }
    }

    /* --- add new dirent in dst parent --- */
    if (dp_inode->i_block[0] == 0 ||
        block_cache_read(dp_inode->i_block[0], block_buf) != 0) {
        kfree(block_buf);
        kfree(src_parent->fs_data); kfree(src_parent);
        kfree(dst_parent->fs_data); kfree(dst_parent);
        kfree(old_copy); kfree(new_copy);
        return -EIO;
    }
    if (ext2_dirent_append(block_buf, ext2_mnt->block_size, src_ino, new_name, ft) != 0) {
        kfree(block_buf);
        kfree(src_parent->fs_data); kfree(src_parent);
        kfree(dst_parent->fs_data); kfree(dst_parent);
        kfree(old_copy); kfree(new_copy);
        return -ENOSPC;
    }
    block_cache_write(dp_inode->i_block[0], block_buf);

    /* --- remove old dirent from src parent --- */
    size_t on_len = strlen(old_name);
    uint32_t sp_blocks = (sp_inode->i_size + ext2_mnt->block_size - 1) / ext2_mnt->block_size;
    for (uint32_t bi = 0; bi < sp_blocks && bi < EXT2_NDIR_BLOCKS; bi++) {
        if (sp_inode->i_block[bi] == 0) continue;
        if (block_cache_read(sp_inode->i_block[bi], block_buf) != 0) continue;
        uint32_t offset = 0;
        int found = 0;
        while (offset + 8 <= ext2_mnt->block_size) {
            ext2_dirent_t *de = (ext2_dirent_t *)(block_buf + offset);
            if (de->rec_len == 0) break;
            if (de->inode != 0 && de->name_len == on_len &&
                strncmp(de->name, old_name, on_len) == 0) {
                de->inode = 0;
                block_cache_write(sp_inode->i_block[bi], block_buf);
                found = 1;
                break;
            }
            offset += de->rec_len;
        }
        if (found) break;
    }

    /* --- for dir rename: update .. in the moved dir to point to new parent --- */
    if (src_is_dir) {
        ext2_inode_t src_inode;
        if (ext2_read_inode(ext2_mnt, src_ino, &src_inode) == 0 &&
            src_inode.i_block[0] != 0 &&
            block_cache_read(src_inode.i_block[0], block_buf) == 0) {

            uint32_t dst_parent_ino = (uint32_t)dst_parent->ino;
            uint32_t offset = 0;
            while (offset + 8 <= ext2_mnt->block_size) {
                ext2_dirent_t *de = (ext2_dirent_t *)(block_buf + offset);
                if (de->rec_len == 0) break;
                if (de->inode != 0 && de->name_len == 2 &&
                    de->name[0] == '.' && de->name[1] == '.') {
                    de->inode = dst_parent_ino;
                    block_cache_write(src_inode.i_block[0], block_buf);
                    break;
                }
                offset += de->rec_len;
            }
        }
    }

    kfree(block_buf);
    kfree(src_parent->fs_data); kfree(src_parent);
    kfree(dst_parent->fs_data); kfree(dst_parent);
    kfree(old_copy); kfree(new_copy);

    ext2_write_superblock(ext2_mnt);
    ext2_write_gdt(ext2_mnt);
    block_cache_flush();
    return 0;
}

// Initialize ext2 filesystem type
void ext2_init(void) {
    ext2_fs_type.name = "ext2";
    ext2_fs_type.ops.mount = ext2_mount;
    ext2_fs_type.ops.unmount = ext2_unmount;
    ext2_fs_type.ops.lookup = ext2_lookup;
    ext2_fs_type.ops.read = ext2_read;
    ext2_fs_type.ops.write = ext2_write;
    ext2_fs_type.ops.create = ext2_create;
    ext2_fs_type.ops.unlink = ext2_unlink;
    ext2_fs_type.ops.mkdir = ext2_mkdir;
    ext2_fs_type.ops.truncate_to = ext2_truncate_to;
    ext2_fs_type.ops.readdir  = ext2_readdir;
    ext2_fs_type.ops.chmod    = ext2_chmod;
    ext2_fs_type.ops.rmdir    = ext2_rmdir;
    ext2_fs_type.ops.rename   = ext2_rename;
    ext2_fs_type.ops.link     = ext2_link;
    ext2_fs_type.ops.symlink  = ext2_symlink;
    ext2_fs_type.ops.readlink = ext2_readlink;
    ext2_fs_type.ops.get_nlink = ext2_get_nlink;
    ext2_fs_type.ops.release = ext2_release;
    ext2_fs_type.next = NULL;
    
    vfs_register_fs(&ext2_fs_type);
    printk("ext2: filesystem registered\n");
}

// Get ext2 filesystem type
fs_type_t *ext2_get_type(void) {
    return &ext2_fs_type;
}

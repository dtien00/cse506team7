#pragma once
#include "fs/vfs.h"
#include "fs/ext2_disk_format.h"

// Function declarations
void ext2_init(void);
vnode_t *ext2_mount(mount_t *mnt, const char *source);
int ext2_unmount(mount_t *mnt);
vnode_t *ext2_lookup(mount_t *mnt, const char *path);
int ext2_read(vnode_t *vnode, void *buf, size_t offset, size_t len);
int ext2_write(vnode_t *vnode, const void *buf, size_t offset, size_t len);
int ext2_create(mount_t *mnt, const char *path, uint32_t mode);
int ext2_unlink(mount_t *mnt, const char *path);
int ext2_mkdir(mount_t *mnt, const char *path, uint32_t mode);

// Get the registered ext2 filesystem type
fs_type_t *ext2_get_type(void);

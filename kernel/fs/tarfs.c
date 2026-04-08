#include "fs/tarfs.h"

#include "errno.h"
#include "fs/vfs.h"
#include "kernel/types.h"
#include "kernel/printk.h"
#include "mm/kmalloc.h"
#include "lib/string.h"

// -----------------------------------------------------------------------------
// TAR blob — linked into the kernel binary via .incbin in your linker/assembly.
// In your start.S or a dedicated asm file, add:
//
//   .section .rodata
//   .global _tarfs_start
//   .global _tarfs_end
//   _tarfs_start:
//       .incbin "initrd.tar"
//   _tarfs_end:
//
// Then declare the symbols here so we can find the blob at runtime.
// -----------------------------------------------------------------------------
extern char _tarfs_start[];
extern char _tarfs_end[];

// -----------------------------------------------------------------------------
// Internal state
// -----------------------------------------------------------------------------

static tarfs_entry_t entries[TARFS_MAX_ENTRIES];
static int           entry_count = 0;

// Forward declaration so the ops initializer below can reference it.
int tarfs_is_dir(mount_t *mnt, const char *rel_path);

// Filesystem type for tarfs
static fs_type_t tarfs_type = {
    .name = "tarfs",
    .ops = {
        .mount = tarfs_mount,
        .unmount = tarfs_unmount,
        .lookup = tarfs_lookup,
        .read = tarfs_read,
        .write = NULL,     // Read-only
        .create = NULL,    // Read-only
        .unlink = NULL,    // Read-only
        .mkdir  = NULL,    // Read-only
    },
    .next = NULL
};

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

fs_type_t *tarfs_get_type(void) {
    return &tarfs_type;
}

// Parse an octal string (null or space terminated) into a size_t.
// TAR stores file sizes as ASCII octal, e.g. "00000000trucks\0"
static size_t octal_to_size(const char *s, size_t field_len) {
    size_t val = 0;
    for (size_t i = 0; i < field_len; i++) {
        if (s[i] < '0' || s[i] > '7') break;  // hit null terminator or space
        val = (val << 3) | (size_t)(s[i] - '0');
    }
    return val;
}

// Round up to the next TAR block boundary (512 bytes).
static size_t round_up_block(size_t size) {
    return (size + TAR_BLOCK_SIZE - 1) & ~(size_t)(TAR_BLOCK_SIZE - 1);
}

// -----------------------------------------------------------------------------
// tarfs_init
// Walks the TAR blob and indexes every regular file entry.
// Must be called after kmalloc is ready.
// Also registers tarfs with the VFS.
// -----------------------------------------------------------------------------
void tarfs_init(void) {
    entry_count = 0;

    char *ptr = _tarfs_start;
    char *end = _tarfs_end;

    printk("tarfs: scanning from %p to %p\n", ptr, end);

    while (ptr + TAR_BLOCK_SIZE <= end) {
        tar_header_t *hdr = (tar_header_t *)ptr;

        // Two consecutive zero blocks signal end-of-archive
        if (hdr->name[0] == '\0') break;

        // Validate ustar magic — skip malformed blocks
        if (hdr->magic[0] != 'u' ||
            hdr->magic[1] != 's' ||
            hdr->magic[2] != 't' ||
            hdr->magic[3] != 'a' ||
            hdr->magic[4] != 'r') {
            printk("tarfs: bad magic at %p, stopping\n", ptr);
            break;
        }

        size_t file_size = octal_to_size(hdr->size, sizeof(hdr->size));

        // typeflag '0' or '\0' = regular file, '5' = directory — index files only
        if (hdr->typeflag == '0' || hdr->typeflag == '\0' || hdr->typeflag == '5') {
            if (entry_count >= TARFS_MAX_ENTRIES) {
                printk("tarfs: too many entries, increase TARFS_MAX_ENTRIES\n");
                break;
            }

            tarfs_entry_t *e = &entries[entry_count++];
            e->header = hdr;
            e->data   = ptr + TAR_BLOCK_SIZE;   // data immediately follows header
            e->size   = file_size;

            printk("tarfs: [%d] %s (%d bytes)\n", entry_count - 1, hdr->name, file_size);
        }

        // Advance past this header block + data blocks (rounded up to 512)
        ptr += TAR_BLOCK_SIZE + round_up_block(file_size);
    }

    printk("tarfs: indexed %d files\n", entry_count);
    
    // Register with VFS
    vfs_register_fs(&tarfs_type);
    printk("tarfs: registered with VFS\n");
}

// -----------------------------------------------------------------------------
// Internal helper: lookup entry by path
// Linear scan for a file by name. Returns NULL if not found.
// -----------------------------------------------------------------------------
static int tarfs_name_matches(const char *entry_name, int is_dir, const char *path) {
    if (!entry_name || !path) return 0;

    if (entry_name[0] == '.' && entry_name[1] == '/') entry_name += 2;
    if (path[0] == '.' && path[1] == '/') path += 2;

    size_t elen = strlen(entry_name);
    size_t plen = strlen(path);

    if (is_dir) {
        while (elen > 0 && entry_name[elen - 1] == '/') elen--;
        while (plen > 0 && path[plen - 1] == '/') plen--;
    }

    if (elen != plen) return 0;
    for (size_t i = 0; i < elen; i++) {
        if (entry_name[i] != path[i]) return 0;
    }
    return 1;
}

static tarfs_entry_t *lookup_entry(const char *path) {
    for (int i = 0; i < entry_count; i++) {
        const char *name = entries[i].header->name;
        int is_dir = (entries[i].header->typeflag == '5');

        if (tarfs_name_matches(name, is_dir, path))
            return &entries[i];
    }
    return NULL;
}

// -----------------------------------------------------------------------------
// Internal helper: read from entry
// Copy up to `len` bytes from `entry` starting at `offset` into `buf`.
// Returns number of bytes actually copied, or -1 on bad args.
// -----------------------------------------------------------------------------
static int read_entry(tarfs_entry_t *entry, void *buf, size_t offset, size_t len) {
    if (!entry || !buf) return -1;
    if (offset >= entry->size) return 0;         // past EOF

    size_t available = entry->size - offset;
    size_t to_copy   = len < available ? len : available;

    memcpy(buf, (char *)entry->data + offset, to_copy);
    return (int)to_copy;
}

// -----------------------------------------------------------------------------
// VFS operations for tarfs
// -----------------------------------------------------------------------------

vnode_t *tarfs_mount(mount_t *mnt, const char *source) {
    if (!mnt) return NULL;
    
    // tarfs is already initialized globally, just mark that it's mounted
    mnt->fs_data = NULL;  // No per-mount data needed

    vnode_t *root = kzalloc(sizeof(vnode_t));
    if (!root) return NULL;
    root->ino       = 1;
    root->mode      = 0755;
    root->size      = 0;
    root->ref_count = 0;
    root->mount     = mnt;
    root->fs_data   = NULL;
    root->fs_type   = mnt->fs_type;
    root->type      = VNODE_DIR;
    
    printk("tarfs: mounted at '%s'\n", mnt->mountpoint);
    return root;
}

int tarfs_unmount(mount_t *mnt) {
    if (!mnt) return -1;
    
    printk("tarfs: unmounted from '%s'\n", mnt->mountpoint);
    return 0;
}

vnode_t *tarfs_lookup(mount_t *mnt, const char *path) {
    if (!mnt || !path) return NULL;

    tarfs_entry_t *entry = lookup_entry(path);
    if (!entry) return NULL;

    // Allocate a vnode
    vnode_t *vnode = kzalloc(sizeof(vnode_t));
    if (!vnode) return NULL;

    vnode->ino = (uint64_t)entry;  // Use entry pointer as inode number
    vnode->mode = 0644;  // Default read permissions
    vnode->size = entry->size;
    vnode->ref_count = 0;
    vnode->mount = mnt;
    vnode->fs_data = entry;
    vnode->fs_type = mnt->fs_type;
    vnode->type = (entry->header->typeflag == '5') ? VNODE_DIR : VNODE_REG;

    return vnode;
}

int tarfs_read(vnode_t *vnode, void *buf, size_t offset, size_t len) {
    if (!vnode || !buf) return -1;

    tarfs_entry_t *entry = (tarfs_entry_t *)vnode->fs_data;
    return read_entry(entry, buf, offset, len);
}

int tarfs_write(vnode_t *vnode, const void *buf, size_t offset, size_t len) {
    printk("TARFS: can't write to read-only file system.\n");
    return -EBADF;
}

// not needed anymore, this information is stored at the vfs layer
 /*
 * tarfs_is_dir — check whether rel_path names an existing directory.
 *
 * tarfs only indexes regular files (typeflag '0'), not directory entries
 * (typeflag '5').  A directory is considered to exist if any indexed entry
 * has rel_path as a path component prefix, i.e. the directory contains at
 * least one file.  The root of the mount ("" or empty) is always a directory.
 *
 * Uses a character-by-character prefix scan because the kernel string library
 * does not provide strncmp.
 */
// int tarfs_is_dir(mount_t *mnt, const char *rel_path) {
//     (void)mnt;
//     if (!rel_path || rel_path[0] == '\0')
//         return 1;  /* root of mount is always a directory */
//
//     /* Strip leading "./" from the query path */
//     if (rel_path[0] == '.' && rel_path[1] == '/')
//         rel_path += 2;
//
//     size_t qlen = strlen(rel_path);
//
//     for (int i = 0; i < entry_count; i++) {
//         const char *name = entries[i].header->name;
//         if (name[0] == '.' && name[1] == '/')
//             name += 2;
//
//         /* Entry lives in this directory if name starts with rel_path + '/' */
//         size_t j = 0;
//         while (j < qlen && name[j] == rel_path[j])
//             j++;
//         if (j == qlen && name[j] == '/')
//             return 1;
//     }
//     return 0;
// }
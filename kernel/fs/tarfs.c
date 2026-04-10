#include "fs/tarfs.h"

#include "errno.h"
#include "fs/vfs.h"
#include "fs/ext2.h"
#include "drivers/virtio.h"
#include "kernel/printk.h"
#include "kernel/types.h"
#include "lib/string.h"
#include "mm/kmalloc.h"

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
static int entry_count = 0;
static fs_type_t tarfs_type;

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

// Parse an octal string (null or space terminated) into a size_t.
// TAR stores file sizes as ASCII octal, e.g. "00000000trucks\0"
static size_t octal_to_size(const char *s, size_t field_len) {
    size_t val = 0;
    for (size_t i = 0; i < field_len; i++) {
        if (s[i] < '0' || s[i] > '7')
            break; // hit null terminator or space
        val = (val << 3) | (size_t) (s[i] - '0');
    }
    return val;
}

// Round up to the next TAR block boundary (512 bytes).
static size_t round_up_block(size_t size) { return (size + TAR_BLOCK_SIZE - 1) & ~(size_t) (TAR_BLOCK_SIZE - 1); }

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
        tar_header_t *hdr = (tar_header_t *) ptr;

        // Two consecutive zero blocks signal end-of-archive
        if (hdr->name[0] == '\0')
            break;

        // Validate ustar magic — skip malformed blocks
        if (hdr->magic[0] != 'u' || hdr->magic[1] != 's' || hdr->magic[2] != 't' || hdr->magic[3] != 'a' ||
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
            e->data = ptr + TAR_BLOCK_SIZE; // data immediately follows header
            e->size = file_size;

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
    if (!entry_name || !path)
        return 0;

    if (entry_name[0] == '.' && entry_name[1] == '/')
        entry_name += 2;
    if (path[0] == '.' && path[1] == '/')
        path += 2;

    size_t elen = strlen(entry_name);
    size_t plen = strlen(path);

    if (is_dir) {
        while (elen > 0 && entry_name[elen - 1] == '/')
            elen--;
        while (plen > 0 && path[plen - 1] == '/')
            plen--;
    }

    if (elen != plen)
        return 0;
    for (size_t i = 0; i < elen; i++) {
        if (entry_name[i] != path[i])
            return 0;
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
    if (!entry || !buf)
        return -1;
    if (offset >= entry->size)
        return 0; // past EOF

    size_t available = entry->size - offset;
    size_t to_copy = len < available ? len : available;

    memcpy(buf, (char *) entry->data + offset, to_copy);
    return (int) to_copy;
}

// -----------------------------------------------------------------------------
// VFS operations for tarfs
// -----------------------------------------------------------------------------

vnode_t *tarfs_mount(mount_t *mnt, const char *source) {
    if (!mnt)
        return NULL;

    // tarfs is already initialized globally, just mark that it's mounted
    mnt->fs_data = NULL; // No per-mount data needed

    vnode_t *root = kzalloc(sizeof(vnode_t));
    if (!root)
        return NULL;
    root->ino = 1;
    root->mode = 0755;
    root->size = 0;
    root->ref_count = 0;
    root->mount = mnt;
    root->fs_data = NULL;
    root->fs_type = mnt->fs_type;
    root->type = VNODE_DIR;

    printk("tarfs: mounted at '%s'\n", mnt->mountpoint);
    return root;
}

int tarfs_unmount(mount_t *mnt) {
    if (!mnt)
        return -1;

    printk("tarfs: unmounted from '%s'\n", mnt->mountpoint);
    return 0;
}

vnode_t *tarfs_lookup(mount_t *mnt, const char *path) {
    if (!mnt || !path)
        return NULL;

    tarfs_entry_t *entry = lookup_entry(path);
    if (!entry)
        return NULL;

    // Allocate a vnode
    vnode_t *vnode = kzalloc(sizeof(vnode_t));
    if (!vnode)
        return NULL;

    vnode->ino = (uint64_t) entry; // Use entry pointer as inode number
    vnode->mode = (uint32_t)octal_to_size(entry->header->mode, sizeof(entry->header->mode));
    vnode->size = entry->size;
    vnode->ref_count = 0;
    vnode->mount = mnt;
    vnode->fs_data = entry;
    vnode->fs_type = mnt->fs_type;
    vnode->type = (entry->header->typeflag == '5') ? VNODE_DIR : VNODE_REG;

    return vnode;
}

static int tarfs_read(vnode_t *vnode, void *buf, size_t offset, size_t len) {
    if (!vnode || !buf)
        return -1;

    tarfs_entry_t *entry = (tarfs_entry_t *) vnode->fs_data;
    return read_entry(entry, buf, offset, len);
}

static int tarfs_write(vnode_t *vnode, const void *buf, size_t offset, size_t len) {
    printk("TARFS: can't write to read-only file system.\n");
    return -EBADF;
}

// Helper: strip trailing slashes from a name copy
static int strip_trailing_slash(const char *src, char *dst, size_t dstsize) {
    size_t len = strlen(src);
    if (len >= dstsize)
        len = dstsize - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
    while (len > 0 && dst[len - 1] == '/')
        dst[--len] = '\0';
    return (int) len;
}

// Returns 1 if `entry_name` (raw, may have trailing /) is a direct child of `dir`
// `dlen` is the length of `dir` after stripping its trailing slashes (0 = root)
static const char *direct_child_name(const char *entry_name, const char *dir, int dlen) {
    char stripped[256];
    int elen = strip_trailing_slash(entry_name, stripped, sizeof(stripped));
    if (elen == 0)
        return NULL; // skip the root entry itself

    if (dlen == 0) {
        // Root directory: child has no '/' in its name
        for (int i = 0; i < elen; i++)
            if (stripped[i] == '/')
                return NULL;
        return entry_name; // caller will use this pointer for name
    }

    // Non-root: must start with dir + '/'
    if (elen <= dlen + 1)
        return NULL;
    for (int i = 0; i < dlen; i++)
        if (stripped[i] != dir[i])
            return NULL;
    if (stripped[dlen] != '/')
        return NULL;

    // After the prefix there must be no more '/'
    const char *rest = stripped + dlen + 1;
    for (const char *p = rest; *p; p++)
        if (*p == '/')
            return NULL;

    // rest points into `stripped` (a local buffer!) — we need a stable pointer.
    // Return pointer into the *original* entry_name at the same offset.
    return entry_name + dlen + 1; // points past "dir/" in the real name
}

static int tarfs_readdir(mount_t *mnt, vnode_t *vnode,
                         uint32_t start_idx,
                         void *buf, uint32_t bufsize,
                         uint32_t *entries_out)
{
    (void)mnt;

    // vnode->private set at lookup time — no path string needed
    tarfs_entry_t *dir_entry = (tarfs_entry_t *)vnode->fs_data;

    // dir_entry is NULL only for the root vnode
    char dir[256];
    int dlen = dir_entry
        ? strip_trailing_slash(dir_entry->header->name, dir, sizeof(dir))
        : 0;   // dlen == 0 triggers the "root" branch in direct_child_name

    char *out     = (char *)buf;
    uint32_t written = 0;
    uint32_t found   = 0;
    uint32_t idx     = 0;

    for (int i = 0; i < entry_count; i++) {
        const char *raw_name = entries[i].header->name;
        int is_dir = (entries[i].header->typeflag == '5');

        const char *child = direct_child_name(raw_name, dir, dlen);
        if (!child) continue;

        char display[256];
        strip_trailing_slash(child, display, sizeof(display));
        size_t nlen = strlen(display);
        if (nlen == 0) continue;
        if (nlen == 1 && display[0] == '.') continue;
        if (nlen == 2 && display[0] == '.' && display[1] == '.') continue;

        if (idx++ < start_idx) continue;

        uint16_t reclen = FS_DENT_RECLEN(nlen);
        if (written + reclen > bufsize) break;

        struct fs_dent *d = (struct fs_dent *)(out + written);
        memset(d, 0, reclen);
        d->d_ino    = (uint64_t)&entries[i];
        d->d_reclen = reclen;
        d->d_type   = is_dir ? DT_DIR : DT_REG;
        memcpy(d->d_name, display, nlen + 1);

        written += reclen;
        found++;
    }

    if (entries_out) *entries_out = found;
    return (int)written;
}

// -----------------------------------------------------------------------------
// tarfs_populate_ext2
//
// Called once after ext2 is mounted at /.  Checks s_reserved[203] in the
// ext2 superblock:
//   0x00  → first boot on this disk image: copy every tarfs entry into the
//            ext2 root (/), then set the flag to 0xAB and persist the superblock.
//   0xAB  → already done, skip silently.
// -----------------------------------------------------------------------------

#define TARFS_INIT_FLAG_OFFSET 203
#define TARFS_INIT_FLAG_MAGIC  0xAB

void tarfs_populate_ext2(mount_t *ext2_mnt) {
    if (!ext2_mnt || !ext2_mnt->fs_data) {
        printk("tarfs_populate_ext2: no ext2 mount data\n");
        return;
    }

    ext2_mount_data_t *mdata = (ext2_mount_data_t *)ext2_mnt->fs_data;
    ext2_superblock_t *sb = mdata->sb;

    if (sb->s_reserved[TARFS_INIT_FLAG_OFFSET] == TARFS_INIT_FLAG_MAGIC) {
        printk("tarfs: ext2 already populated, skipping\n");
        return;
    }

    printk("tarfs: populating ext2 root from tarfs (%d entries)...\n", entry_count);

    // Walk every indexed tarfs entry
    for (int i = 0; i < entry_count; i++) {
        tarfs_entry_t *e = &entries[i];
        const char *raw = e->header->name;
        int is_dir = (e->header->typeflag == '5');

        // Strip leading "./" if present
        if (raw[0] == '.' && raw[1] == '/')
            raw += 2;

        // Skip the root "." entry itself
        if (raw[0] == '\0')
            continue;

        // Build destination path: /<raw>  (strip trailing '/')
        char dst[384];
        int dlen = 0;
        dst[dlen++] = '/';
        int rlen = 0;
        while (raw[rlen] && raw[rlen] != '\0')
            dst[dlen + rlen] = raw[rlen], rlen++;
        dst[dlen + rlen] = '\0';
        // Strip trailing slash for directories
        int total = dlen + rlen;
        while (total > 1 && dst[total - 1] == '/')
            dst[--total] = '\0';

        if (is_dir) {
            if (vfs_mkdir(dst, 0755) < 0)
                printk("tarfs: mkdir %s failed\n", dst);
            else
                printk("tarfs: mkdir %s\n", dst);
        } else {
            // Create the file
            if (vfs_create(dst, 0644) < 0) {
                printk("tarfs: create %s failed\n", dst);
                continue;
            }

            // Write its contents if non-empty
            if (e->size > 0) {
                dentry_t *dent = vfs_lookup(dst);
                if (dent && dent->d_vnode) {
                    vnode_t *vn = dent->d_vnode;
                    int written = vn->fs_type->ops.write(vn, e->data, 0, e->size);
                    if (written < 0)
                        printk("tarfs: write %s failed (%d)\n", dst, written);
                    else
                        printk("tarfs: copied %s (%d bytes)\n", dst, written);
                } else {
                    printk("tarfs: lookup after create failed for %s\n", dst);
                }
            } else {
                printk("tarfs: created empty %s\n", dst);
            }
        }
    }

    // Mark the superblock so we never repeat this
    sb->s_reserved[TARFS_INIT_FLAG_OFFSET] = TARFS_INIT_FLAG_MAGIC;

    // Persist the superblock to disk directly (same as ext2_write_superblock)
    uint8_t *sb_buf = (uint8_t *)kmalloc(1024);
    if (sb_buf) {
        memset(sb_buf, 0, 1024);
        memcpy(sb_buf, sb, sizeof(ext2_superblock_t));
        virtio_blk_write(2, sb_buf, 2);
        kfree(sb_buf);
        printk("tarfs: superblock flag written (0xAB), future boots will skip population\n");
    } else {
        printk("tarfs: WARNING: could not persist init flag, will repopulate next boot\n");
    }
}

// Filesystem type for tarfs
static fs_type_t tarfs_type = {.name = "tarfs",
                               .ops =
                                       {
                                               .mount = tarfs_mount,
                                               .unmount = tarfs_unmount,
                                               .lookup = tarfs_lookup,
                                               .read = tarfs_read,
                                               .write = tarfs_write, // stub
                                               .create = NULL, // Read-only
                                               .unlink = NULL, // Read-only
                                               .mkdir = NULL, // Read-only
                                               .readdir = tarfs_readdir,
                                       },
                               .next = NULL};

fs_type_t *tarfs_get_type(void) { return &tarfs_type; }

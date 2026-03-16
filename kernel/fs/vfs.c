#include "fs/vfs.h"
#include "fs/tarfs.h"
#include "kernel/types.h"
#include "kernel/printk.h"
#include "mm/kmalloc.h"
#include "lib/string.h"

// -----------------------------------------------------------------------------
// vfs_open
// Looks up path in tarfs, allocates a file_t, and returns it.
// Returns NULL if the file doesn't exist or kmalloc fails.
// -----------------------------------------------------------------------------
file_t *vfs_open(const char *path) {
    tarfs_entry_t *entry = tarfs_lookup(path);
    if (!entry) {
        printk("vfs: file not found: %s\n", path);
        return NULL;
    }

    file_t *f = kmalloc(sizeof(file_t));
    if (!f) {
        printk("vfs: kmalloc failed in vfs_open\n");
        return NULL;
    }

    f->type      = FILE_TARFS;
    f->entry     = entry;
    f->offset    = 0;
    f->ref_count = 1;

    return f;
}

// -----------------------------------------------------------------------------
// vfs_close
// Decrements ref count and frees the file_t when it hits zero.
// -----------------------------------------------------------------------------
void vfs_close(file_t *f) {
    if (!f) return;

    f->ref_count--;
    if (f->ref_count <= 0)
        kfree(f);
}

// -----------------------------------------------------------------------------
// vfs_read
// Dispatches to the appropriate backend based on file type.
// Advances f->offset by the number of bytes read.
// Returns bytes read, 0 on EOF, -1 on error.
// -----------------------------------------------------------------------------
int vfs_read(file_t *f, void *buf, size_t len) {
    if (!f || !buf) return -1;

    switch (f->type) {
        case FILE_TARFS: {
            int n = tarfs_read(f->entry, buf, f->offset, len);
            if (n > 0)
                f->offset += (size_t)n;
            return n;
        }

        case FILE_STDIN:
            // No keyboard driver yet — stub
            printk("vfs: vfs_read on stdin not supported\n");
            return -1;

        case FILE_STDOUT:
            // Reading from stdout doesn't make sense
            return -1;

        default:
            return -1;
    }
}

// -----------------------------------------------------------------------------
// vfs_write
// Only meaningful for stdout/stderr right now — routes through printk.
// tarfs is read-only, so writing to a FILE_TARFS fd is an error.
// -----------------------------------------------------------------------------
int vfs_write(file_t *f, const void *buf, size_t len) {
    if (!f || !buf) return -1;

    switch (f->type) {
        case FILE_STDOUT: {
            // printk expects a format string — write raw bytes via %.*s
            printk("%.*s", (int)len, (const char *)buf);
            return (int)len;
        }

        case FILE_TARFS:
            // tarfs is read-only
            printk("vfs: write to read-only tarfs file\n");
            return -1;

        case FILE_STDIN:
            return -1;

        default:
            return -1;
    }
}
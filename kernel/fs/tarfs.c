#include "fs/tarfs.h"
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

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

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
        if (hdr->typeflag == '0' || hdr->typeflag == '\0') {
            if (entry_count >= TARFS_MAX_ENTRIES) {
                printk("tarfs: too many entries, increase TARFS_MAX_ENTRIES\n");
                break;
            }

            tarfs_entry_t *e = &entries[entry_count++];
            e->header = hdr;
            e->data   = ptr + TAR_BLOCK_SIZE;   // data immediately follows header
            e->size   = file_size;

            printk("tarfs: [%d] %s (%zu bytes)\n", entry_count - 1, hdr->name, file_size);
        }

        // Advance past this header block + data blocks (rounded up to 512)
        ptr += TAR_BLOCK_SIZE + round_up_block(file_size);
    }

    printk("tarfs: indexed %d files\n", entry_count);
}

// -----------------------------------------------------------------------------
// tarfs_lookup
// Linear scan for a file by name. Returns NULL if not found.
// -----------------------------------------------------------------------------
tarfs_entry_t *tarfs_lookup(const char *path) {
    // TAR paths often start with "./" — strip it for convenience
    if (path[0] == '.' && path[1] == '/') path += 2;

    for (int i = 0; i < entry_count; i++) {
        const char *name = entries[i].header->name;
        // Also strip "./" from stored names
        if (name[0] == '.' && name[1] == '/') name += 2;

        if (strcmp(name, path) == 0)
            return &entries[i];
    }
    return NULL;
}

// -----------------------------------------------------------------------------
// tarfs_read
// Copy up to `len` bytes from `entry` starting at `offset` into `buf`.
// Returns number of bytes actually copied, or -1 on bad args.
// -----------------------------------------------------------------------------
int tarfs_read(tarfs_entry_t *entry, void *buf, size_t offset, size_t len) {
    if (!entry || !buf) return -1;
    if (offset >= entry->size) return 0;         // past EOF

    size_t available = entry->size - offset;
    size_t to_copy   = len < available ? len : available;

    memcpy(buf, (char *)entry->data + offset, to_copy);
    return (int)to_copy;
}

// -----------------------------------------------------------------------------
// tarfs_stat
// Return a copy of the header for a given path. Caller checks name[0] == '\0'
// for the not-found case (or we could return a pointer — see tarfs.h note).
// -----------------------------------------------------------------------------
tar_header_t tarfs_stat(const char *path) {
    tarfs_entry_t *e = tarfs_lookup(path);
    if (e) return *e->header;

    // Return an empty header to signal not found
    tar_header_t empty = {0};
    return empty;
}
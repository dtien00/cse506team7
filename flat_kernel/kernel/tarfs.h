#pragma once
#include "types.h"

// The tar header structure (512 bytes)
typedef struct tar_header {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];   // octal string
    char mtime[12];
    char checksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];   // "ustar"
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char pad[12];    // pad to 512 bytes
} tar_header_t;

// A parsed/indexed file entry
typedef struct tarfs_entry {
    tar_header_t *header;
    void *data;      // pointer into kernel memory
    size_t size;
} tarfs_entry_t;

// Function declarations
void tarfs_init(void);
tarfs_entry_t *tarfs_lookup(const char *path);
int tarfs_read(tarfs_entry_t *entry, void *buf, size_t offset, size_t len);
tar_header tarfs_stat(const char *path);
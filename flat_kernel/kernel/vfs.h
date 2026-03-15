#pragma once
#include "types.h"
#include "tarfs.h"
// FILE MANAGER: Keeps track of all open file stats.

// A generic open file
typedef struct file {
    enum { FILE_TARFS, FILE_STDIN, FILE_STDOUT } type;
    tarfs_entry_t *entry;  // for FILE_TARFS
    size_t offset;
    int ref_count;
} file_t;

// Function declarations
file_t *vfs_open(const char *path);
void    vfs_close(file_t *f);
int     vfs_read(file_t *f, void *buf, size_t len);
int     vfs_write(file_t *f, const void *buf, size_t len); // stub
#include "fs/file.h"
#include "fs/vfs.h"
#include "kernel/printk.h"
#include "mm/kmalloc.h"

// -----------------------------------------------------------------------------
// fd_init
// Initialize a file descriptor table with stdin (0), stdout (1), stderr (2).
// These are special pseudo-files that don't point to real tarfs entries.
// -----------------------------------------------------------------------------
void fd_init(fd_table_t *table) {
    if (!table) return;

    for (int i = 0; i < MAX_FDS; i++) {
        table->files[i] = NULL;
    }

    // Allocate stdin (fd 0)
    file_t *stdin_file = kmalloc(sizeof(file_t));
    if (stdin_file) {
        stdin_file->type = FILE_STDIN;
        stdin_file->vnode = NULL;
        stdin_file->offset = 0;
        stdin_file->ref_count = 1;
        stdin_file->flags = 0;
        table->files[0] = stdin_file;
    }

    // Allocate stdout (fd 1)
    file_t *stdout_file = kmalloc(sizeof(file_t));
    if (stdout_file) {
        stdout_file->type = FILE_STDOUT;
        stdout_file->vnode = NULL;
        stdout_file->offset = 0;
        stdout_file->ref_count = 1;
        stdout_file->flags = 1;
        table->files[1] = stdout_file;
    }

    // Allocate stderr (fd 2) - same as stdout for now
    file_t *stderr_file = kmalloc(sizeof(file_t));
    if (stderr_file) {
        stderr_file->type = FILE_STDOUT;
        stderr_file->vnode = NULL;
        stderr_file->offset = 0;
        stderr_file->ref_count = 1;
        stderr_file->flags = 1;
        table->files[2] = stderr_file;
    }
}

// -----------------------------------------------------------------------------
// fd_alloc
// Finds the first free slot in the fd table, assigns the file, and returns
// the fd number. Returns -1 if the table is full.
// -----------------------------------------------------------------------------
int fd_alloc(fd_table_t *table, file_t *f) {
    if (!table || !f) return -1;

    for (int i = 0; i < MAX_FDS; i++) {
        if (table->files[i] == NULL) {
            table->files[i] = f;
            return i;
        }
    }

    printk("fd_alloc: no free file descriptors\n");
    return -1;
}

// -----------------------------------------------------------------------------
// fd_get
// Returns the file_t* for a given fd, or NULL if invalid/closed.
// -----------------------------------------------------------------------------
file_t *fd_get(fd_table_t *table, int fd) {
    if (!table || fd < 0 || fd >= MAX_FDS)
        return NULL;
    return table->files[fd];
}

// -----------------------------------------------------------------------------
// fd_close
// Closes the file descriptor, calls vfs_close to decrement ref count,
// and clears the slot in the table.
// -----------------------------------------------------------------------------
void fd_close(fd_table_t *table, int fd) {
    if (!table || fd < 0 || fd >= MAX_FDS)
        return;

    file_t *f = table->files[fd];
    if (f) {
        vfs_close(f);
        table->files[fd] = NULL;
    }
}

// -----------------------------------------------------------------------------
// fd_copy
// Copies the file descriptor table from src to dst, incrementing ref counts.
// Used by fork() to duplicate parent's open files.
// Returns 0 on success, -1 on failure.
// -----------------------------------------------------------------------------
int fd_copy(fd_table_t *dst, fd_table_t *src) {
    if (!dst || !src) return -1;

    for (int i = 0; i < MAX_FDS; i++) {
        dst->files[i] = NULL;
    }

    for (int i = 0; i < MAX_FDS; i++) {
        if (src->files[i]) {
            dst->files[i] = src->files[i];
            src->files[i]->ref_count++;
        }
    }

    return 0;
}

// -----------------------------------------------------------------------------
// fd_cleanup
// Closes all open file descriptors in the table and frees resources.
// Used when a process exits.
// -----------------------------------------------------------------------------
void fd_cleanup(fd_table_t *table) {
    if (!table) return;

    for (int i = 0; i < MAX_FDS; i++) {
        if (table->files[i]) {
            fd_close(table, i);
        }
    }
}

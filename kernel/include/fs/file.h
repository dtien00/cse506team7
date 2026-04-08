#pragma once
#include "kernel/types.h"
#include "fs/vfs.h"
// process level manager

#define MAX_FDS 16  // or however many per process

// File types
typedef enum {
    FILE_TARFS,
    FILE_TMPFS,
    FILE_PIPE,
    FILE_STDIN,
    FILE_STDOUT
} file_type_t;

// A generic open file
typedef struct file {
    tarfs_entry_t *entry;
    file_type_t type;
    vnode_t *vnode;
    dentry_t *dentry;
    pipe_t *pipe;
    int pipe_flags;
    size_t offset;
    int ref_count;
} file_t;

// File descriptor table (lives in process_t)
typedef struct fd_table {
    file_t *files[MAX_FDS];
} fd_table_t;

// Function declarations
int   fd_alloc(fd_table_t *table, file_t *f);  // returns fd number
file_t *fd_get(fd_table_t *table, int fd);
void  fd_close(fd_table_t *table, int fd);
void fd_destroy(fd_table_t *table);
void  fd_init(fd_table_t *table);  // sets up stdin/stdout/stderr

// I/O operations (moved from vfs.h)
file_t *file_open(const char *path);
void    file_close(file_t *f);
int     file_read(file_t *f, void *buf, size_t len);
int     file_write(file_t *f, const void *buf, size_t len);
int     file_lseek(file_t *f, size_t offset, int whence);
void    file_ref(file_t *f);

struct proc;
// Safe fd → file_t lookup for syscalls
file_t *fget(struct proc *p, int fd);
void    fput(file_t *f);
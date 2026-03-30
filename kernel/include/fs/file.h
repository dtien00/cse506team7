#pragma once
#include "kernel/types.h"
#include "fs/vfs.h"
// process level manager

#define MAX_FDS 16  // or however many per process

// File descriptor table (lives in process_t)
typedef struct fd_table {
    file_t *files[MAX_FDS];
} fd_table_t;

// Function declarations
int   fd_alloc(fd_table_t *table, file_t *f);  // returns fd number
file_t *fd_get(fd_table_t *table, int fd);
void  fd_close(fd_table_t *table, int fd);
void  fd_init(fd_table_t *table);  // sets up stdin/stdout/stderr
int   fd_copy(fd_table_t *dst, fd_table_t *src);  // copy fd table for fork
void  fd_cleanup(fd_table_t *table);  // close all fds and free resources
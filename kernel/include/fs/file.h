#pragma once
#include "kernel/types.h"
#include "fs/dentry.h"
#include "kernel/limits.h"

// Open flags (must match libc/include/fcntl.h)
#define O_RDONLY    0x0000
#define O_WRONLY    0x0001
#define O_RDWR      0x0002
#define O_ACCMODE   0x0003
#define O_CREAT     0x0040
#define O_EXCL      0x0080
#define O_TRUNC     0x0200
#define O_APPEND    0x0400
#define O_NONBLOCK  0x0800
#define O_DIRECTORY 0x10000
#define O_CLOEXEC   0x80000
#define MAX_FDS   SBUNIX_OPEN_MAX   /* 20 — meets POSIX _POSIX_OPEN_MAX */
#define MAX_PATH  SBUNIX_PATH_MAX   /* 256 — matches current kernel enforcement */

struct vnode;

typedef int64_t off_t;
typedef unsigned int mode_t;
typedef unsigned long dev_t;
typedef unsigned long ino_t;
typedef unsigned int nlink_t;
typedef long blksize_t;
typedef long blkcnt_t;
typedef long time_t;
typedef int uid_t;
typedef int gid_t;

typedef struct vnode  vnode_t;
typedef struct pipe pipe_t;

// File types
typedef enum {
    FILE_VNODE, // backed by vfs
    FILE_PIPE,
    FILE_STDIN,
    FILE_STDOUT
} file_type_t;

// A generic open file
typedef struct file {
    file_type_t type;
    int ref_count;
    size_t offset; // offset into file, or index into directory

    // for type FILE_VNODE only
    vnode_t *vnode; // cached vnode, fetched from inside dentry on file open
    dentry_t *dentry;

    // for type FILE_PIPE only
    pipe_t *pipe;
    int pipe_flags;

    // actual file flags
    int flags;
} file_t;

// File descriptor table (lives in process_t)
typedef struct fd_table {
    file_t *files[MAX_FDS];
} fd_table_t;

struct stat {
    dev_t st_dev; // all fs
    ino_t st_ino; // all fs
    mode_t st_mode; // all fs
    nlink_t st_nlink; // hardcode 1 for files and calculate for directories
    uid_t st_uid; // 0
    gid_t st_gid; // 0
    off_t st_size; // all fs
    time_t st_atime; // all fs
    time_t st_mtime; // all fs
    time_t st_ctime; // all fs
};

// Function declarations
int fd_alloc(fd_table_t *table, file_t *f); // returns fd number
file_t *fd_get(fd_table_t *table, int fd);
void fd_close(fd_table_t *table, int fd);
void fd_destroy(fd_table_t *table);
void fd_init(fd_table_t *table); // sets up stdin/stdout/stderr

// I/O operations (moved from vfs.h)
int file_open(const char *path);
int file_open_with_flags(const char *path, int flags, mode_t mode);
int file_close(int fd);
int file_read(int fd, void *buf, size_t len);
int file_write(int fd, const void *buf, size_t len);
int file_lseek(int fd, size_t offset, int whence);
void file_ref(file_t *f);
int file_stat(const char *kpath, struct stat *kst);
int file_lstat(const char *kpath, struct stat *kst);
int file_fstat(int fd, struct stat *kst);

struct proc;
// Safe fd → file_t lookup for syscalls
file_t *f_get(struct proc *p, int fd);
void f_put(file_t *f);

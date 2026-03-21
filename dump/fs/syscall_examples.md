# System Call Examples Using the Filesystem

This document shows how to implement common POSIX-like system calls using the VFS layer.

## File System Calls

### sys_open

```c
#include "fs/vfs.h"
#include "fs/file.h"
#include "process/process.h"

// Open a file and return a file descriptor
// flags: O_RDONLY, O_WRONLY, O_RDWR, O_CREAT, etc.
int sys_open(const char *path, int flags, int mode) {
    process_t *proc = get_current_process();
    if (!proc) return -1;

    file_t *f = NULL;

    // Handle O_CREAT flag
    if (flags & O_CREAT) {
        // Try to create the file (ignore error if exists)
        vfs_create(path, mode);
    }

    // Open the file
    f = vfs_open(path);
    if (!f) return -1;

    // Allocate file descriptor
    int fd = fd_alloc(&proc->fd_table, f);
    if (fd < 0) {
        vfs_close(f);
        return -1;
    }

    return fd;
}
```

### sys_close

```c
int sys_close(int fd) {
    process_t *proc = get_current_process();
    if (!proc) return -1;

    fd_close(&proc->fd_table, fd);
    return 0;
}
```

### sys_read

```c
ssize_t sys_read(int fd, void *buf, size_t count) {
    process_t *proc = get_current_process();
    if (!proc) return -1;

    file_t *f = fd_get(&proc->fd_table, fd);
    if (!f) return -1;

    return vfs_read(f, buf, count);
}
```

### sys_write

```c
ssize_t sys_write(int fd, const void *buf, size_t count) {
    process_t *proc = get_current_process();
    if (!proc) return -1;

    file_t *f = fd_get(&proc->fd_table, fd);
    if (!f) return -1;

    return vfs_write(f, buf, count);
}
```

### sys_lseek

```c
off_t sys_lseek(int fd, off_t offset, int whence) {
    process_t *proc = get_current_process();
    if (!proc) return -1;

    file_t *f = fd_get(&proc->fd_table, fd);
    if (!f) return -1;

    switch (whence) {
        case SEEK_SET:
            f->offset = offset;
            break;
        case SEEK_CUR:
            f->offset += offset;
            break;
        case SEEK_END:
            // Need to get file size
            if (f->use_vnode && f->vnode) {
                f->offset = f->vnode->size + offset;
            } else if (f->type == FILE_TARFS && f->tarfs_entry) {
                f->offset = f->tarfs_entry->size + offset;
            }
            break;
        default:
            return -1;
    }

    return f->offset;
}
```

### sys_unlink

```c
int sys_unlink(const char *path) {
    return vfs_unlink(path);
}
```

### sys_mkdir

```c
int sys_mkdir(const char *path, mode_t mode) {
    return vfs_mkdir(path, mode);
}
```

### sys_rmdir

```c
int sys_rmdir(const char *path) {
    // For now, use unlink (should check if directory is empty)
    return vfs_unlink(path);
}
```

## Advanced System Calls

### sys_stat

```c
#include "fs/vfs.h"

struct stat {
    uint64_t st_ino;
    uint32_t st_mode;
    uint32_t st_size;
    // ... other fields
};

int sys_stat(const char *path, struct stat *buf) {
    if (!path || !buf) return -1;

    vnode_t *vnode = vfs_lookup(path);
    if (!vnode) return -1;

    buf->st_ino = vnode->ino;
    buf->st_mode = vnode->mode;
    buf->st_size = vnode->size;

    // Clean up vnode
    vnode->ref_count--;
    if (vnode->ref_count <= 0) {
        kfree(vnode);
    }

    return 0;
}
```

### sys_fstat

```c
int sys_fstat(int fd, struct stat *buf) {
    process_t *proc = get_current_process();
    if (!proc || !buf) return -1;

    file_t *f = fd_get(&proc->fd_table, fd);
    if (!f) return -1;

    if (f->use_vnode && f->vnode) {
        buf->st_ino = f->vnode->ino;
        buf->st_mode = f->vnode->mode;
        buf->st_size = f->vnode->size;
        return 0;
    } else if (f->type == FILE_TARFS && f->tarfs_entry) {
        buf->st_ino = (uint64_t)f->tarfs_entry;
        buf->st_mode = 0644;
        buf->st_size = f->tarfs_entry->size;
        return 0;
    }

    return -1;
}
```

### sys_dup

```c
// Duplicate a file descriptor
int sys_dup(int oldfd) {
    process_t *proc = get_current_process();
    if (!proc) return -1;

    file_t *f = fd_get(&proc->fd_table, oldfd);
    if (!f) return -1;

    // Increment ref count
    f->ref_count++;

    // Allocate new fd pointing to same file
    int newfd = fd_alloc(&proc->fd_table, f);
    if (newfd < 0) {
        f->ref_count--;
        return -1;
    }

    return newfd;
}
```

### sys_dup2

```c
// Duplicate oldfd to newfd
int sys_dup2(int oldfd, int newfd) {
    process_t *proc = get_current_process();
    if (!proc) return -1;

    if (oldfd == newfd) return newfd;

    file_t *f = fd_get(&proc->fd_table, oldfd);
    if (!f) return -1;

    // Close newfd if it's open
    if (proc->fd_table.files[newfd]) {
        fd_close(&proc->fd_table, newfd);
    }

    // Increment ref count
    f->ref_count++;

    // Assign to newfd
    proc->fd_table.files[newfd] = f;

    return newfd;
}
```

### sys_mount

```c
int sys_mount(const char *source, const char *target, const char *fstype) {
    // Add permission check here
    // if (!is_root(current_process())) return -EPERM;
    
    return vfs_mount(source, target, fstype);
}
```

### sys_umount

```c
int sys_umount(const char *target) {
    // Add permission check here
    // if (!is_root(current_process())) return -EPERM;
    
    return vfs_unmount(target);
}
```

## Process Initialization

When creating a new process, initialize its fd table:

```c
process_t *proc_create(const char *name) {
    process_t *proc = kmalloc(sizeof(process_t));
    if (!proc) return NULL;

    // Initialize file descriptor table
    fd_init(&proc->fd_table);

    // ... rest of process initialization ...

    return proc;
}
```

## Process Cleanup

When a process exits, close all open file descriptors:

```c
void proc_exit(process_t *proc) {
    if (!proc) return;

    // Close all open file descriptors
    for (int i = 0; i < MAX_FDS; i++) {
        if (proc->fd_table.files[i]) {
            fd_close(&proc->fd_table, i);
        }
    }

    // ... rest of process cleanup ...
}
```

## Fork Implementation

When forking, duplicate the fd table:

```c
process_t *proc_fork(process_t *parent) {
    process_t *child = kmalloc(sizeof(process_t));
    if (!child) return NULL;

    // Copy fd table
    for (int i = 0; i < MAX_FDS; i++) {
        child->fd_table.files[i] = parent->fd_table.files[i];
        
        // Increment ref counts
        if (child->fd_table.files[i]) {
            child->fd_table.files[i]->ref_count++;
        }
    }

    // ... rest of fork logic ...

    return child;
}
```

## Exec Implementation

When execing, close all fd's marked as close-on-exec:

```c
void proc_exec(process_t *proc, const char *path) {
    // For now, close all except stdin/stdout/stderr
    for (int i = 3; i < MAX_FDS; i++) {
        if (proc->fd_table.files[i]) {
            fd_close(&proc->fd_table, i);
        }
    }

    // ... rest of exec logic ...
}
```

## Pipe Implementation (Future)

```c
// Add FILE_PIPE to file_type_t enum
typedef enum {
    FILE_TARFS,
    FILE_TMPFS,
    FILE_PIPE,
    FILE_STDIN,
    FILE_STDOUT
} file_type_t;

// Pipe structure
typedef struct pipe {
    char buffer[4096];
    size_t read_pos;
    size_t write_pos;
    size_t count;
    int ref_count;
} pipe_t;

int sys_pipe(int pipefd[2]) {
    process_t *proc = get_current_process();
    if (!proc) return -1;

    // Allocate pipe
    pipe_t *pipe = kzalloc(sizeof(pipe_t));
    if (!pipe) return -1;
    pipe->ref_count = 2;

    // Create read end
    file_t *read_end = kmalloc(sizeof(file_t));
    read_end->type = FILE_PIPE;
    read_end->pipe = pipe;
    read_end->offset = 0;
    read_end->ref_count = 1;

    // Create write end
    file_t *write_end = kmalloc(sizeof(file_t));
    write_end->type = FILE_PIPE;
    write_end->pipe = pipe;
    write_end->offset = 0;
    write_end->ref_count = 1;

    // Allocate fds
    pipefd[0] = fd_alloc(&proc->fd_table, read_end);
    pipefd[1] = fd_alloc(&proc->fd_table, write_end);

    return 0;
}
```

## Complete Syscall Table Example

```c
typedef int (*syscall_handler_t)(void);

syscall_handler_t syscall_table[] = {
    [SYS_read]   = (syscall_handler_t)sys_read,
    [SYS_write]  = (syscall_handler_t)sys_write,
    [SYS_open]   = (syscall_handler_t)sys_open,
    [SYS_close]  = (syscall_handler_t)sys_close,
    [SYS_stat]   = (syscall_handler_t)sys_stat,
    [SYS_fstat]  = (syscall_handler_t)sys_fstat,
    [SYS_lseek]  = (syscall_handler_t)sys_lseek,
    [SYS_unlink] = (syscall_handler_t)sys_unlink,
    [SYS_mkdir]  = (syscall_handler_t)sys_mkdir,
    [SYS_rmdir]  = (syscall_handler_t)sys_rmdir,
    [SYS_dup]    = (syscall_handler_t)sys_dup,
    [SYS_dup2]   = (syscall_handler_t)sys_dup2,
    [SYS_pipe]   = (syscall_handler_t)sys_pipe,
    [SYS_mount]  = (syscall_handler_t)sys_mount,
    [SYS_umount] = (syscall_handler_t)sys_umount,
};
```

## Error Codes

Define standard error codes:

```c
#define ENOENT  2   // No such file or directory
#define EBADF   9   // Bad file descriptor
#define ENOMEM  12  // Out of memory
#define EACCES  13  // Permission denied
#define EEXIST  17  // File exists
#define ENOTDIR 20  // Not a directory
#define EISDIR  21  // Is a directory
#define EINVAL  22  // Invalid argument
#define EMFILE  24  // Too many open files
#define EROFS   30  // Read-only filesystem
```

Use them in syscalls:

```c
int sys_open(const char *path, int flags, int mode) {
    if (flags & O_CREAT) {
        int ret = vfs_create(path, mode);
        if (ret < 0 && ret != -EEXIST) {
            return -ENOENT;  // Parent directory doesn't exist
        }
    }

    file_t *f = vfs_open(path);
    if (!f) return -ENOENT;

    int fd = fd_alloc(&proc->fd_table, f);
    if (fd < 0) {
        vfs_close(f);
        return -EMFILE;
    }

    return fd;
}
```

## User-Space Integration

### libc Wrappers

```c
// In libc/syscall.h
#define SYS_read   0
#define SYS_write  1
#define SYS_open   2
#define SYS_close  3

// In libc/unistd.c
int open(const char *path, int flags, ...) {
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        mode = va_arg(ap, mode_t);
        va_end(ap);
    }
    return syscall(SYS_open, path, flags, mode);
}

ssize_t read(int fd, void *buf, size_t count) {
    return syscall(SYS_read, fd, buf, count);
}

ssize_t write(int fd, const void *buf, size_t count) {
    return syscall(SYS_write, fd, buf, count);
}

int close(int fd) {
    return syscall(SYS_close, fd);
}
```

### User Program Example

```c
// User-space program
#include <unistd.h>
#include <fcntl.h>

int main() {
    // Create and write to a file
    int fd = open("/tmp/output.txt", O_CREAT | O_WRONLY, 0644);
    if (fd < 0) {
        write(2, "Failed to open file\n", 20);
        return 1;
    }

    write(fd, "Hello from user space!\n", 23);
    close(fd);

    // Read it back
    fd = open("/tmp/output.txt", O_RDONLY);
    if (fd >= 0) {
        char buf[128];
        int n = read(fd, buf, sizeof(buf));
        write(1, buf, n);
        close(fd);
    }

    return 0;
}
```

## Shell Integration

### Shell Built-in Commands

```c
// Shell command: cat
void cmd_cat(char **args) {
    if (!args[1]) {
        printf("Usage: cat <file>\n");
        return;
    }

    int fd = open(args[1], O_RDONLY);
    if (fd < 0) {
        printf("cat: cannot open '%s'\n", args[1]);
        return;
    }

    char buf[1024];
    int n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        write(1, buf, n);
    }

    close(fd);
}

// Shell command: echo (with redirection)
void cmd_echo(char **args) {
    int fd = 1;  // stdout
    int redirect = 0;

    // Parse for '>' redirection
    for (int i = 1; args[i]; i++) {
        if (strcmp(args[i], ">") == 0 && args[i+1]) {
            redirect = 1;
            fd = open(args[i+1], O_CREAT | O_WRONLY, 0644);
            args[i] = NULL;  // Terminate args here
            break;
        }
    }

    // Write arguments
    for (int i = 1; args[i]; i++) {
        write(fd, args[i], strlen(args[i]));
        if (args[i+1]) write(fd, " ", 1);
    }
    write(fd, "\n", 1);

    if (redirect) close(fd);
}

// Shell command: mkdir
void cmd_mkdir(char **args) {
    if (!args[1]) {
        printf("Usage: mkdir <directory>\n");
        return;
    }

    if (mkdir(args[1], 0755) < 0) {
        printf("mkdir: cannot create directory '%s'\n", args[1]);
    }
}

// Shell command: rm
void cmd_rm(char **args) {
    if (!args[1]) {
        printf("Usage: rm <file>\n");
        return;
    }

    if (unlink(args[1]) < 0) {
        printf("rm: cannot remove '%s'\n", args[1]);
    }
}
```

## Standard I/O Redirection

```c
// Redirect stdout to a file
void redirect_stdout(const char *filename) {
    int fd = open(filename, O_CREAT | O_WRONLY, 0644);
    if (fd < 0) return;

    // Save old stdout
    int old_stdout = dup(1);

    // Redirect stdout to file
    dup2(fd, 1);
    close(fd);

    // Now all printf/write(1, ...) goes to file
    printf("This goes to the file\n");

    // Restore stdout
    dup2(old_stdout, 1);
    close(old_stdout);

    printf("This goes to console\n");
}
```

## Pipe Usage Example

```c
// Create a pipe and fork
int pipefd[2];
pipe(pipefd);

pid_t pid = fork();
if (pid == 0) {
    // Child: write to pipe
    close(pipefd[0]);  // Close read end
    write(pipefd[1], "Hello from child\n", 17);
    close(pipefd[1]);
    exit(0);
} else {
    // Parent: read from pipe
    close(pipefd[1]);  // Close write end
    char buf[128];
    int n = read(pipefd[0], buf, sizeof(buf));
    write(1, buf, n);
    close(pipefd[0]);
    wait(NULL);
}
```

## Integration Checklist

To fully integrate the filesystem with your kernel:

- [ ] Implement syscall handler dispatch
- [ ] Add filesystem syscalls to syscall table
- [ ] Create libc wrappers for syscalls
- [ ] Initialize fd_table in process creation
- [ ] Clean up fd_table in process exit
- [ ] Implement fd duplication for fork
- [ ] Add file descriptor flags (close-on-exec)
- [ ] Implement error code returns
- [ ] Add permission checks
- [ ] Test with user-space programs

## Testing Syscalls

```c
void test_syscalls(void) {
    // Test open/write/read
    int fd = sys_open("/tmp/test.txt", O_CREAT | O_WRONLY, 0644);
    sys_write(fd, "test data", 9);
    sys_close(fd);

    fd = sys_open("/tmp/test.txt", O_RDONLY, 0);
    char buf[64];
    int n = sys_read(fd, buf, sizeof(buf));
    sys_close(fd);

    // Test mkdir
    sys_mkdir("/tmp/testdir", 0755);

    // Test unlink
    sys_unlink("/tmp/test.txt");
}
```

## Notes

- All examples assume you have a `get_current_process()` function
- Error handling should be enhanced with proper error codes
- Permission checks should be added for security
- File descriptor flags (O_CLOEXEC, etc.) should be implemented
- Consider adding file descriptor limits per process

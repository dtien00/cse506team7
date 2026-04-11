# Write Support Implementation Guide

## Problem Statement

You have a **read-only system** with:
- **tarfs**: Read-only tar archive filesystem
- **tmpfs**: In-memory filesystem with write capability (but not fully exposed)

The backend (tmpfs) supports writes, but the **syscall layer doesn't properly create files** when opening with O_CREAT flag.

## Root Cause

The `sys_open()` syscall only calls `vfs_open()` → `vfs_lookup()`, which looks up existing files. If the file doesn't exist, it fails immediately without attempting to create it, even when O_CREAT is specified.

## Implementation Plan

### PHASE 1: Minimal Write Support (Core Functionality)

This phase makes file creation and writing work end-to-end.

#### Step 1.1: Add flags field to file_t structure

**File**: `kernel/include/fs/vfs.h`

**Current**:
```c
typedef struct file {
    tarfs_entry_t *entry;  // for FILE_TARFS
    file_type_t type;
    vnode_t *vnode;
    size_t offset;
    int ref_count;
} file_t;
```

**Change to**:
```c
typedef struct file {
    tarfs_entry_t *entry;  // for FILE_TARFS (legacy, may be removed)
    file_type_t type;
    vnode_t *vnode;
    size_t offset;
    int ref_count;
    int flags;            // ADD THIS: Open flags (O_RDONLY, O_WRONLY, O_RDWR, etc.)
} file_t;
```

#### Step 1.2: Update vfs_open() to accept flags

**File**: `kernel/include/fs/vfs.h`

**Current**:
```c
file_t *vfs_open(const char *path);
```

**Change to**:
```c
file_t *vfs_open(const char *path, int flags);
```

**File**: `kernel/fs/vfs.c`

**Current**:
```c
file_t *vfs_open(const char *path) {
    vnode_t *vnode = vfs_lookup(path);
    if (!vnode) {
        printk("vfs: file not found: %s\n", path);
        return NULL;
    }
    // ... rest of function
}
```

**Change to**:
```c
file_t *vfs_open(const char *path, int flags) {
    vnode_t *vnode = vfs_lookup(path);
    if (!vnode) {
        printk("vfs: file not found: %s\n", path);
        return NULL;
    }

    file_t *f = kmalloc(sizeof(file_t));
    if (!f) {
        printk("vfs: kmalloc failed in vfs_open\n");
        kfree(vnode);
        return NULL;
    }

    // Determine file type from filesystem name
    if (strcmp(vnode->fs_type->name, "tmpfs") == 0) {
        f->type = FILE_TMPFS;
    } else if (strcmp(vnode->fs_type->name, "tarfs") == 0) {
        f->type = FILE_TARFS;
    } else {
        f->type = FILE_TARFS;  // Default fallback
    }

    f->vnode = vnode;
    f->offset = 0;
    f->ref_count = 1;
    f->flags = flags;  // ADD THIS LINE
    vnode->ref_count++;

    return f;
}
```

#### Step 1.3: Fix sys_open() to handle O_CREAT

**File**: `kernel/trap/syscall.c`

**Current**:
```c
int sys_open(const char *path, int flags) {
    if (!current) return -1;

    set_csr_bit(sstatus, SSTATUS_SUM);
    file_t *f = vfs_open(path);
    clear_csr_bit(sstatus, SSTATUS_SUM);

    if (!f) {
        printk("sys_open: failed to open %s\n", path);
        return -1;
    }

    int fd = fd_alloc(&current->fd_table, f);
    if (fd < 0) {
        vfs_close(f);
        return -1;
    }

    return fd;
}
```

**Change to**:
```c
int sys_open(const char *path, int flags) {
    if (!current) return -1;

    set_csr_bit(sstatus, SSTATUS_SUM);
    
    // Try to open existing file
    file_t *f = vfs_open(path, flags);
    
    // If file doesn't exist and O_CREAT is set, create it
    if (!f && (flags & O_CREAT)) {
        mode_t mode = 0644;  // Default permissions (rw-r--r--)
        if (vfs_create(path, mode) == 0) {
            // Try to open again after creation
            f = vfs_open(path, flags);
        }
    }
    
    clear_csr_bit(sstatus, SSTATUS_SUM);

    if (!f) {
        printk("sys_open: failed to open %s\n", path);
        return -1;
    }

    // Handle O_TRUNC - truncate file to zero length
    if ((flags & O_TRUNC) && (flags & (O_WRONLY | O_RDWR))) {
        if (f->type == FILE_TMPFS && f->vnode && f->vnode->fs_data) {
            tmpfs_inode_t *inode = (tmpfs_inode_t *)f->vnode->fs_data;
            inode->size = 0;
            f->vnode->size = 0;
        }
    }
    
    // Handle O_APPEND - set offset to end of file
    if (flags & O_APPEND) {
        f->offset = f->vnode->size;
    }

    int fd = fd_alloc(&current->fd_table, f);
    if (fd < 0) {
        vfs_close(f);
        return -1;
    }

    return fd;
}
```

Note: For O_TRUNC, you'll need to include tmpfs.h in syscall.c or add a VFS-level truncate operation.

#### Step 1.4: Add O_CREAT flag definitions to kernel

**File**: `kernel/trap/syscall.c` (add at top)

```c
// File open flags (should match libc/include/fcntl.h)
#define O_RDONLY    0x0000
#define O_WRONLY    0x0001
#define O_RDWR      0x0002
#define O_CREAT     0x0040
#define O_EXCL      0x0080
#define O_TRUNC     0x0200
#define O_APPEND    0x0400
```

Or better yet, create `kernel/include/fs/fcntl.h` and include it.

### PHASE 2: File Seeking

#### Step 2.1: Add SYS_lseek syscall number

**File**: `kernel/include/trap/syscall.h`

Add after SYS_brk:
```c
#define SYS_lseek    17
```

Add function declaration:
```c
off_t sys_lseek(int fd, off_t offset, int whence);
```

#### Step 2.2: Implement sys_lseek()

**File**: `kernel/trap/syscall.c`

Add implementation:
```c
off_t sys_lseek(int fd, off_t offset, int whence) {
    if (!current) return -1;
    
    file_t *f = fd_get(&current->fd_table, fd);
    if (!f) {
        printk("sys_lseek: invalid fd %d\n", fd);
        return -1;
    }
    
    // Don't allow seeking on stdin/stdout/stderr
    if (f->type == FILE_STDIN || f->type == FILE_STDOUT) {
        return -1;
    }
    
    return vfs_lseek(f, offset, whence);
}
```

#### Step 2.3: Wire up SYS_lseek in syscall_handler

**File**: `kernel/trap/syscall.c`

In `syscall_handler()` switch statement, add:
```c
case SYS_lseek:
    ret = sys_lseek((int)arg0, (off_t)arg1, (int)arg2);
    break;
```

#### Step 2.4: Fix vfs_lseek() implementation

**File**: `kernel/fs/vfs.c`

**Current** (broken, references old f->entry field):
```c
int vfs_lseek(file_t *f, size_t offset, int whence) {
    if (!f) return -1;
    size_t new_offset = 0;
    switch (whence) {
        case SEEK_SET:
            new_offset = offset;
            break;
        case SEEK_CUR:
            new_offset = f->offset + offset;
            break;
        case SEEK_END:
            if (f->entry && f->entry->size)
                new_offset = f->entry->size + offset;
            else
                return -1;
            break;
        default:
            return -1;
    }
    if (f->entry && new_offset > f->entry->size)
        return -1;
    f->offset = new_offset;
    return new_offset;
}
```

**Change to**:
```c
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

off_t vfs_lseek(file_t *f, off_t offset, int whence) {
    if (!f) return -1;
    
    off_t new_offset = 0;
    
    switch (whence) {
        case SEEK_SET:
            new_offset = offset;
            break;
            
        case SEEK_CUR:
            new_offset = (off_t)f->offset + offset;
            break;
            
        case SEEK_END:
            if (f->vnode) {
                new_offset = (off_t)f->vnode->size + offset;
            } else {
                return -1;
            }
            break;
            
        default:
            return -1;
    }
    
    // Don't allow seeking to negative positions
    if (new_offset < 0) {
        return -1;
    }
    
    f->offset = (size_t)new_offset;
    return new_offset;
}
```

#### Step 2.5: Update vfs.h function signature

**File**: `kernel/include/fs/vfs.h`

Change:
```c
int vfs_lseek(file_t *f, size_t offset, int whence);
```

To:
```c
off_t vfs_lseek(file_t *f, off_t offset, int whence);
```

#### Step 2.6: Implement lseek() in libc

**File**: `libc/fileio.c`

**Current** (stub):
```c
off_t lseek(int fd, off_t offset, int whence) {
    (void)fd;
    (void)offset;
    (void)whence;
    return -1;
}
```

**Change to**:
```c
#define SYS_lseek 17

off_t lseek(int fd, off_t offset, int whence) {
    register long a0 asm("a0") = fd;
    register long a1 asm("a1") = offset;
    register long a2 asm("a2") = whence;
    register long a7 asm("a7") = SYS_lseek;
    register long ret asm("a0");
    asm volatile("ecall" : "=r"(ret) : "r"(a0), "r"(a1), "r"(a2), "r"(a7) : "memory");
    return (off_t)ret;
}
```

### PHASE 3: File Deletion

#### Step 3.1: Add SYS_unlink syscall

**File**: `kernel/include/trap/syscall.h`

Add:
```c
#define SYS_unlink   18
```

Add declaration:
```c
int sys_unlink(const char *path);
```

#### Step 3.2: Implement sys_unlink()

**File**: `kernel/trap/syscall.c`

```c
int sys_unlink(const char *path) {
    if (!current) return -1;
    
    set_csr_bit(sstatus, SSTATUS_SUM);
    int ret = vfs_unlink(path);
    clear_csr_bit(sstatus, SSTATUS_SUM);
    
    return ret;
}
```

#### Step 3.3: Wire up in syscall_handler

**File**: `kernel/trap/syscall.c`

```c
case SYS_unlink:
    ret = sys_unlink((const char *)arg0);
    break;
```

#### Step 3.4: Implement unlink() in libc

**File**: `libc/fileio.c`

```c
#define SYS_unlink 18

int unlink(const char *pathname) {
    register long a0 asm("a0") = (long)pathname;
    register long a7 asm("a7") = SYS_unlink;
    register long ret asm("a0");
    asm volatile("ecall" : "=r"(ret) : "r"(a0), "r"(a7) : "memory");
    return (int)ret;
}
```

### PHASE 4: Directory Creation

#### Step 4.1: Add SYS_mkdir syscall

**File**: `kernel/include/trap/syscall.h`

```c
#define SYS_mkdir    19
```

Add declaration:
```c
int sys_mkdir(const char *path, mode_t mode);
```

#### Step 4.2: Implement sys_mkdir()

**File**: `kernel/trap/syscall.c`

```c
int sys_mkdir(const char *path, mode_t mode) {
    if (!current) return -1;
    
    set_csr_bit(sstatus, SSTATUS_SUM);
    int ret = vfs_mkdir(path, mode);
    clear_csr_bit(sstatus, SSTATUS_SUM);
    
    return ret;
}
```

#### Step 4.3: Wire up in syscall_handler

```c
case SYS_mkdir:
    ret = sys_mkdir((const char *)arg0, (mode_t)arg1);
    break;
```

#### Step 4.4: Add mkdir() to libc

**File**: Create `libc/directory.c` or add to existing file

```c
#include <sys/stat.h>
#include <sys/types.h>

#define SYS_mkdir 19

int mkdir(const char *pathname, mode_t mode) {
    register long a0 asm("a0") = (long)pathname;
    register long a1 asm("a1") = mode;
    register long a7 asm("a7") = SYS_mkdir;
    register long ret asm("a0");
    asm volatile("ecall" : "=r"(ret) : "r"(a0), "r"(a1), "r"(a7) : "memory");
    return (int)ret;
}
```

### PHASE 5: Standard I/O Streams (Optional but Recommended)

Create `libc/stdio.c` with buffered FILE* operations.

#### Step 5.1: Define FILE structure

**File**: `libc/include/stdio.h`

Add:
```c
#define BUFSIZ 4096
#define EOF (-1)

#define _IOFBF 0  // Fully buffered
#define _IOLBF 1  // Line buffered
#define _IONBF 2  // Unbuffered

typedef struct {
    int fd;              // Underlying file descriptor
    int flags;           // Open mode flags
    int mode;            // Buffering mode
    char *buffer;        // I/O buffer
    size_t buffer_size;  // Buffer size
    size_t buffer_pos;   // Current position in buffer
    size_t buffer_len;   // Valid data in buffer (for reads)
    int eof;             // EOF reached flag
    int error;           // Error flag
} FILE;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

FILE *fopen(const char *pathname, const char *mode);
int fclose(FILE *stream);
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);
int fseek(FILE *stream, long offset, int whence);
long ftell(FILE *stream);
void rewind(FILE *stream);
int fflush(FILE *stream);
int fgetc(FILE *stream);
int fputc(int c, FILE *stream);
char *fgets(char *s, int size, FILE *stream);
int fputs(const char *s, FILE *stream);
int feof(FILE *stream);
int ferror(FILE *stream);
void clearerr(FILE *stream);
int setvbuf(FILE *stream, char *buf, int mode, size_t size);
```

#### Step 5.2: Implement stdio.c

**File**: `libc/stdio.c`

This is a large file (~500 lines). Key functions:

```c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

// Global standard streams
static FILE _stdin = {0, O_RDONLY, _IOLBF, NULL, 0, 0, 0, 0, 0};
static FILE _stdout = {1, O_WRONLY, _IOLBF, NULL, 0, 0, 0, 0, 0};
static FILE _stderr = {2, O_WRONLY, _IONBF, NULL, 0, 0, 0, 0, 0};

FILE *stdin = &_stdin;
FILE *stdout = &_stdout;
FILE *stderr = &_stderr;

FILE *fopen(const char *pathname, const char *mode) {
    int flags = 0;
    
    // Parse mode string
    if (mode[0] == 'r') {
        flags = O_RDONLY;
        if (mode[1] == '+') flags = O_RDWR;
    } else if (mode[0] == 'w') {
        flags = O_WRONLY | O_CREAT | O_TRUNC;
        if (mode[1] == '+') flags = O_RDWR | O_CREAT | O_TRUNC;
    } else if (mode[0] == 'a') {
        flags = O_WRONLY | O_CREAT | O_APPEND;
        if (mode[1] == '+') flags = O_RDWR | O_CREAT | O_APPEND;
    } else {
        return NULL;
    }
    
    int fd = open(pathname, flags, 0644);
    if (fd < 0) return NULL;
    
    FILE *stream = malloc(sizeof(FILE));
    if (!stream) {
        close(fd);
        return NULL;
    }
    
    stream->fd = fd;
    stream->flags = flags;
    stream->mode = _IOFBF;  // Fully buffered by default
    stream->buffer = malloc(BUFSIZ);
    stream->buffer_size = stream->buffer ? BUFSIZ : 0;
    stream->buffer_pos = 0;
    stream->buffer_len = 0;
    stream->eof = 0;
    stream->error = 0;
    
    return stream;
}

int fclose(FILE *stream) {
    if (!stream) return EOF;
    
    fflush(stream);
    
    int ret = close(stream->fd);
    
    if (stream->buffer) {
        free(stream->buffer);
    }
    
    // Don't free stdin/stdout/stderr
    if (stream != stdin && stream != stdout && stream != stderr) {
        free(stream);
    }
    
    return ret;
}

int fflush(FILE *stream) {
    if (!stream) return EOF;
    
    // Only flush write buffers
    if (stream->buffer_pos > 0 && (stream->flags & (O_WRONLY | O_RDWR))) {
        ssize_t written = write(stream->fd, stream->buffer, stream->buffer_pos);
        if (written < 0) {
            stream->error = 1;
            return EOF;
        }
        stream->buffer_pos = 0;
    }
    
    return 0;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream) {
    if (!stream || !ptr) return 0;
    
    size_t total = size * nmemb;
    const char *data = (const char *)ptr;
    size_t written = 0;
    
    // Unbuffered mode - write directly
    if (stream->mode == _IONBF || !stream->buffer) {
        ssize_t ret = write(stream->fd, data, total);
        return (ret > 0) ? (size_t)ret / size : 0;
    }
    
    // Buffered mode
    while (written < total) {
        size_t space = stream->buffer_size - stream->buffer_pos;
        size_t to_copy = (total - written < space) ? (total - written) : space;
        
        memcpy(stream->buffer + stream->buffer_pos, data + written, to_copy);
        stream->buffer_pos += to_copy;
        written += to_copy;
        
        // Flush if buffer is full or line buffered and newline found
        if (stream->buffer_pos >= stream->buffer_size ||
            (stream->mode == _IOLBF && memchr(stream->buffer, '\n', stream->buffer_pos))) {
            if (fflush(stream) != 0) {
                return written / size;
            }
        }
    }
    
    return nmemb;
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    if (!stream || !ptr) return 0;
    
    size_t total = size * nmemb;
    char *data = (char *)ptr;
    size_t bytes_read = 0;
    
    // Read from buffer first
    if (stream->buffer_len > stream->buffer_pos) {
        size_t available = stream->buffer_len - stream->buffer_pos;
        size_t to_copy = (total < available) ? total : available;
        memcpy(data, stream->buffer + stream->buffer_pos, to_copy);
        stream->buffer_pos += to_copy;
        bytes_read += to_copy;
    }
    
    // If more data needed, read directly or refill buffer
    while (bytes_read < total) {
        if (!stream->buffer) {
            // Unbuffered - read directly
            ssize_t ret = read(stream->fd, data + bytes_read, total - bytes_read);
            if (ret <= 0) {
                if (ret == 0) stream->eof = 1;
                else stream->error = 1;
                break;
            }
            bytes_read += ret;
        } else {
            // Buffered - refill buffer
            ssize_t ret = read(stream->fd, stream->buffer, stream->buffer_size);
            if (ret <= 0) {
                if (ret == 0) stream->eof = 1;
                else stream->error = 1;
                break;
            }
            stream->buffer_len = ret;
            stream->buffer_pos = 0;
            
            size_t to_copy = (total - bytes_read < (size_t)ret) ? (total - bytes_read) : (size_t)ret;
            memcpy(data + bytes_read, stream->buffer, to_copy);
            stream->buffer_pos += to_copy;
            bytes_read += to_copy;
        }
    }
    
    return bytes_read / size;
}

int fseek(FILE *stream, long offset, int whence) {
    if (!stream) return -1;
    
    // Flush write buffer before seeking
    if (stream->buffer_pos > 0 && (stream->flags & (O_WRONLY | O_RDWR))) {
        if (fflush(stream) != 0) return -1;
    }
    
    // Invalidate read buffer
    stream->buffer_pos = 0;
    stream->buffer_len = 0;
    stream->eof = 0;
    
    off_t ret = lseek(stream->fd, offset, whence);
    return (ret < 0) ? -1 : 0;
}

long ftell(FILE *stream) {
    if (!stream) return -1;
    
    off_t pos = lseek(stream->fd, 0, SEEK_CUR);
    if (pos < 0) return -1;
    
    // Adjust for buffered data
    if (stream->flags & O_RDONLY) {
        // For reads, subtract unread buffered data
        pos -= (stream->buffer_len - stream->buffer_pos);
    } else if (stream->flags & (O_WRONLY | O_RDWR)) {
        // For writes, add unflushed buffered data
        pos += stream->buffer_pos;
    }
    
    return (long)pos;
}

void rewind(FILE *stream) {
    if (stream) {
        fseek(stream, 0, SEEK_SET);
        stream->eof = 0;
        stream->error = 0;
    }
}

int fgetc(FILE *stream) {
    unsigned char c;
    size_t n = fread(&c, 1, 1, stream);
    return (n == 1) ? c : EOF;
}

int fputc(int c, FILE *stream) {
    unsigned char ch = (unsigned char)c;
    size_t n = fwrite(&ch, 1, 1, stream);
    return (n == 1) ? c : EOF;
}

char *fgets(char *s, int size, FILE *stream) {
    if (!s || size <= 0 || !stream) return NULL;
    
    int i = 0;
    while (i < size - 1) {
        int c = fgetc(stream);
        if (c == EOF) {
            if (i == 0) return NULL;
            break;
        }
        s[i++] = (char)c;
        if (c == '\n') break;
    }
    s[i] = '\0';
    return s;
}

int fputs(const char *s, FILE *stream) {
    if (!s || !stream) return EOF;
    
    size_t len = strlen(s);
    size_t written = fwrite(s, 1, len, stream);
    return (written == len) ? 0 : EOF;
}

int feof(FILE *stream) {
    return stream ? stream->eof : 0;
}

int ferror(FILE *stream) {
    return stream ? stream->error : 0;
}

void clearerr(FILE *stream) {
    if (stream) {
        stream->eof = 0;
        stream->error = 0;
    }
}
```

## Testing After Implementation

### Test 1: Basic File Creation and Writing

```c
// test_write.c
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>

int main() {
    // Create and write to a file
    int fd = open("/tmp/test.txt", O_CREAT | O_WRONLY, 0644);
    if (fd < 0) {
        printf("Failed to create file\n");
        return 1;
    }
    
    const char *msg = "Hello, tmpfs!\n";
    write(fd, msg, 15);
    close(fd);
    
    // Read it back
    fd = open("/tmp/test.txt", O_RDONLY);
    if (fd < 0) {
        printf("Failed to open file\n");
        return 1;
    }
    
    char buf[100];
    int n = read(fd, buf, 100);
    buf[n] = '\0';
    printf("Read: %s", buf);
    close(fd);
    
    return 0;
}
```

### Test 2: Using FILE* streams

```c
// test_stdio.c
#include <stdio.h>

int main() {
    FILE *f = fopen("/tmp/data.txt", "w");
    if (!f) {
        printf("fopen failed\n");
        return 1;
    }
    
    fprintf(f, "Line 1\n");
    fprintf(f, "Line 2\n");
    fclose(f);
    
    f = fopen("/tmp/data.txt", "r");
    if (!f) {
        printf("fopen for read failed\n");
        return 1;
    }
    
    char line[100];
    while (fgets(line, sizeof(line), f)) {
        printf("Read: %s", line);
    }
    fclose(f);
    
    return 0;
}
```

### Test 3: File seeking

```c
// test_seek.c
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>

int main() {
    int fd = open("/tmp/seek.txt", O_CREAT | O_RDWR, 0644);
    write(fd, "0123456789", 10);
    
    lseek(fd, 5, SEEK_SET);
    write(fd, "XXXXX", 5);
    
    lseek(fd, 0, SEEK_SET);
    char buf[11];
    read(fd, buf, 10);
    buf[10] = '\0';
    
    printf("Result: %s\n", buf);  // Should print "01234XXXXX"
    close(fd);
    
    return 0;
}
```

## Summary of Changes Needed

### Kernel Files to Modify:
1. `kernel/include/fs/vfs.h` - Add flags field to file_t, update vfs_open signature
2. `kernel/fs/vfs.c` - Update vfs_open to accept flags, fix vfs_lseek
3. `kernel/trap/syscall.c` - Fix sys_open, add sys_lseek, sys_unlink, sys_mkdir
4. `kernel/include/trap/syscall.h` - Add new syscall numbers and declarations

### Libc Files to Modify:
1. `libc/fileio.c` - Implement lseek, unlink
2. `libc/include/stdio.h` - Add FILE structure and function declarations

### Libc Files to Create:
1. `libc/stdio.c` - Buffered I/O implementation (~500 lines)
2. `libc/directory.c` - mkdir and directory operations (or add to fileio.c)

## Estimated Effort

- **Phase 1-4** (Basic write support): ~200 lines of code, 2-3 hours
- **Phase 5** (stdio.c): ~500 lines of code, 4-6 hours

## Why tmpfs is Sufficient for Now

tmpfs provides:
- ✅ In-memory file storage
- ✅ Full read/write capability
- ✅ Directory hierarchy
- ✅ File creation/deletion
- ❌ Persistence across reboots

For OS development and testing, tmpfs is perfectly adequate. Persistent storage would require:
- Block device driver
- Disk filesystem (ext2, FAT, etc.)
- Block cache
- Much more complexity

You can add persistent storage later as an enhancement.

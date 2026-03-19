# Filesystem API Quick Reference

## Initialization (in kernel boot sequence)

```c
#include "fs/vfs.h"
#include "fs/tarfs.h"
#include "fs/tmpfs.h"

// 1. Initialize VFS
vfs_init();

// 2. Initialize filesystem types
tarfs_init();  // Parse embedded TAR archive
tmpfs_init();  // Register tmpfs

// 3. Register filesystems (tarfs auto-registers in tmpfs_init)
vfs_register_fs(tarfs_get_type());

// 4. Mount filesystems
vfs_mount(NULL, "/", "tarfs");      // Mount root as tarfs
vfs_mount(NULL, "/tmp", "tmpfs");   // Mount tmpfs at /tmp
```

## Mount Management

### Mount a Filesystem
```c
int vfs_mount(const char *source, const char *target, const char *fstype);

// Examples:
vfs_mount(NULL, "/", "tarfs");           // Mount tarfs at root
vfs_mount(NULL, "/tmp", "tmpfs");        // Mount tmpfs at /tmp
vfs_mount(NULL, "/mnt/tmp2", "tmpfs");   // Mount another tmpfs
```

### Unmount a Filesystem
```c
int vfs_unmount(const char *target);

// Example:
vfs_unmount("/tmp");  // Unmount /tmp
```

### Find Mount Point
```c
mount_t *vfs_find_mount(const char *path);

// Example:
mount_t *mnt = vfs_find_mount("/tmp/foo/bar.txt");
// Returns the /tmp mount point
```

## File Operations

### Open a File
```c
file_t *vfs_open(const char *path);

// Examples:
file_t *f = vfs_open("/hello.txt");        // Open from tarfs
file_t *f = vfs_open("/tmp/myfile.txt");   // Open from tmpfs

if (!f) {
    // File doesn't exist or error
}
```

### Close a File
```c
void vfs_close(file_t *f);

// Example:
vfs_close(f);
```

### Read from a File
```c
int vfs_read(file_t *f, void *buf, size_t len);

// Example:
char buffer[256];
int bytes_read = vfs_read(f, buffer, sizeof(buffer));
if (bytes_read < 0) {
    // Error
} else if (bytes_read == 0) {
    // EOF
} else {
    // Successfully read bytes_read bytes
}
```

### Write to a File
```c
int vfs_write(file_t *f, const void *buf, size_t len);

// Example:
const char *data = "Hello, World!";
int bytes_written = vfs_write(f, data, strlen(data));
if (bytes_written < 0) {
    // Error (e.g., writing to read-only tarfs)
}
```

## File/Directory Creation

### Create a New File
```c
int vfs_create(const char *path, uint32_t mode);

// Example:
vfs_create("/tmp/newfile.txt", 0644);  // Create with rw-r--r-- permissions
```

### Create a Directory
```c
int vfs_mkdir(const char *path, uint32_t mode);

// Example:
vfs_mkdir("/tmp/mydir", 0755);  // Create with rwxr-xr-x permissions
```

### Delete a File
```c
int vfs_unlink(const char *path);

// Example:
vfs_unlink("/tmp/oldfile.txt");
```

## File Descriptor Management

### Initialize FD Table
```c
fd_table_t fd_table;
fd_init(&fd_table);
// Sets up stdin (fd 0), stdout (fd 1), stderr (fd 2)
```

### Allocate a File Descriptor
```c
int fd_alloc(fd_table_t *table, file_t *f);

// Example:
file_t *f = vfs_open("/tmp/data.txt");
int fd = fd_alloc(&fd_table, f);
// Returns 3 (first available after stdin/stdout/stderr)
```

### Get File from FD
```c
file_t *fd_get(fd_table_t *table, int fd);

// Example:
file_t *f = fd_get(&fd_table, 3);
if (f) {
    vfs_read(f, buffer, size);
}
```

### Close File Descriptor
```c
void fd_close(fd_table_t *table, int fd);

// Example:
fd_close(&fd_table, 3);
```

## Complete Usage Example

```c
#include "fs/vfs.h"
#include "fs/file.h"

void example_usage(void) {
    // 1. Create a new file in tmpfs
    if (vfs_create("/tmp/log.txt", 0644) < 0) {
        printk("Failed to create file\n");
        return;
    }

    // 2. Open the file
    file_t *f = vfs_open("/tmp/log.txt");
    if (!f) {
        printk("Failed to open file\n");
        return;
    }

    // 3. Write some data
    const char *message = "System started at boot time\n";
    int written = vfs_write(f, message, strlen(message));
    if (written < 0) {
        printk("Write failed\n");
        vfs_close(f);
        return;
    }

    // 4. Close the file
    vfs_close(f);

    // 5. Read it back
    f = vfs_open("/tmp/log.txt");
    if (f) {
        char buffer[128];
        int n = vfs_read(f, buffer, sizeof(buffer) - 1);
        if (n > 0) {
            buffer[n] = '\0';
            printk("Read: %s", buffer);
        }
        vfs_close(f);
    }

    // 6. Create a directory structure
    vfs_mkdir("/tmp/logs", 0755);
    vfs_create("/tmp/logs/boot.log", 0644);

    // 7. Write to nested file
    f = vfs_open("/tmp/logs/boot.log");
    if (f) {
        vfs_write(f, "Boot log entry\n", 15);
        vfs_close(f);
    }

    // 8. Clean up
    vfs_unlink("/tmp/logs/boot.log");
    vfs_unlink("/tmp/log.txt");
}
```

## Process-Level File Descriptor Example

```c
void process_fd_example(void) {
    // Initialize process fd table
    fd_table_t fd_table;
    fd_init(&fd_table);
    
    // stdin=0, stdout=1, stderr=2 are already set up

    // Open a file and get fd
    file_t *f = vfs_open("/tmp/data.txt");
    int fd = fd_alloc(&fd_table, f);  // Returns 3
    
    // Use the fd
    char buffer[256];
    file_t *file = fd_get(&fd_table, fd);
    vfs_read(file, buffer, sizeof(buffer));
    
    // Close when done
    fd_close(&fd_table, fd);
}
```

## Filesystem-Specific Notes

### TARFS (Read-Only)
- Backed by embedded TAR archive in kernel binary
- Mounted at root ("/") by default
- Supports: open, read, close
- Does NOT support: write, create, unlink, mkdir

### TMPFS (Read-Write)
- In-memory filesystem
- Mounted at "/tmp" by default
- Supports: open, read, write, close, create, unlink, mkdir
- Files are lost on reboot (volatile)
- Max 128 files per mount
- Max 1MB per file
- Max 256 character filenames

## Error Handling

All functions return:
- `0` or positive value on success
- `-1` on error
- `NULL` for pointer returns on error

Always check return values:

```c
if (vfs_create("/tmp/file.txt", 0644) < 0) {
    printk("Failed to create file\n");
    return;
}

file_t *f = vfs_open("/tmp/file.txt");
if (!f) {
    printk("Failed to open file\n");
    return;
}

int n = vfs_write(f, data, len);
if (n < 0) {
    printk("Write failed\n");
}

vfs_close(f);
```

## Common Patterns

### Write-Read Pattern
```c
// Create and write
vfs_create("/tmp/output.txt", 0644);
file_t *f = vfs_open("/tmp/output.txt");
vfs_write(f, data, size);
vfs_close(f);

// Read back
f = vfs_open("/tmp/output.txt");
char buf[1024];
int n = vfs_read(f, buf, sizeof(buf));
vfs_close(f);
```

### Append Pattern
```c
file_t *f = vfs_open("/tmp/log.txt");
// Manually seek to end by reading to EOF
char dummy[1];
while (vfs_read(f, dummy, 1) > 0);
// Now at EOF, can write
vfs_write(f, "New log entry\n", 14);
vfs_close(f);
```

### Directory Creation Pattern
```c
// Create nested directories one at a time
vfs_mkdir("/tmp/a", 0755);
vfs_mkdir("/tmp/a/b", 0755);
vfs_mkdir("/tmp/a/b/c", 0755);
vfs_create("/tmp/a/b/c/file.txt", 0644);
```

## Limitations

1. **No seek operation**: File position is tracked automatically; no explicit seek
2. **No readdir**: Cannot list directory contents yet
3. **No symbolic links**: Hard paths only
4. **No permissions enforcement**: Mode is stored but not checked
5. **No timestamps**: Files don't track creation/modification time
6. **No concurrent access control**: No locking mechanisms
7. **Single tmpfs instance limits**: 128 files, 1MB per file

## Performance Considerations

- **TARFS**: O(n) lookup (linear scan of all entries)
- **TMPFS**: O(n*m) lookup (n = path depth, m = avg children per directory)
- **Mount resolution**: O(m) where m = number of mounts
- **Memory**: Each tmpfs file allocates buffer on first write, grows as needed

## Debugging Tips

Enable debug output by checking printk messages:
- "vfs: mounted 'X' at 'Y'" - Mount succeeded
- "vfs: file not found" - Path doesn't exist
- "tmpfs: max files reached" - Hit TMPFS_MAX_FILES limit
- "tmpfs: file size limit exceeded" - Tried to write > 1MB

Check mount points:
```c
mount_t *mnt = vfs_find_mount("/tmp/foo");
if (mnt) {
    printk("Mount: %s, FS: %s\n", mnt->mountpoint, mnt->fs_type->name);
}
```

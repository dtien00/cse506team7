# Filesystem Quick Start Guide

## What's New

Your kernel now has:

1. **VFS Layer** - Mount multiple filesystems at different paths
2. **TMPFS** - In-memory read-write filesystem
3. **Mount Points** - Root ("/") is tarfs, "/tmp" is tmpfs
4. **File Descriptors** - Per-process fd tables

## Quick Examples

### Example 1: Write to Temporary File

```c
// Create a file in tmpfs
vfs_create("/tmp/log.txt", 0644);

// Write to it
file_t *f = vfs_open("/tmp/log.txt");
vfs_write(f, "Log entry\n", 10);
vfs_close(f);

// Read it back
f = vfs_open("/tmp/log.txt");
char buf[128];
int n = vfs_read(f, buf, sizeof(buf));
vfs_close(f);
```

### Example 2: Create Directory Structure

```c
// Create nested directories
vfs_mkdir("/tmp/var", 0755);
vfs_mkdir("/tmp/var/log", 0755);

// Create file in nested directory
vfs_create("/tmp/var/log/kernel.log", 0644);

// Write to it
file_t *f = vfs_open("/tmp/var/log/kernel.log");
vfs_write(f, "Kernel started\n", 15);
vfs_close(f);
```

### Example 3: Mount New Filesystem

```c
// Mount another tmpfs at /var
vfs_mount(NULL, "/var", "tmpfs");

// Use it
vfs_create("/var/data.txt", 0644);
file_t *f = vfs_open("/var/data.txt");
vfs_write(f, "data", 4);
vfs_close(f);
```

### Example 4: Using File Descriptors

```c
// In a process
fd_table_t fd_table;
fd_init(&fd_table);

// Open file and get fd
file_t *f = vfs_open("/tmp/data.txt");
int fd = fd_alloc(&fd_table, f);

// Use fd
file_t *file = fd_get(&fd_table, fd);
vfs_read(file, buffer, size);

// Close
fd_close(&fd_table, fd);
```

## Testing

Run tests:
```bash
make test
```

Expected: All tests pass with "PASS" messages.

## Files Overview

### Core Implementation
- `kernel/fs/vfs.c` - VFS layer
- `kernel/fs/file.c` - File descriptor management
- `kernel/fs/tmpfs.c` - In-memory filesystem
- `kernel/fs/tarfs.c` - TAR filesystem (extended)

### Headers
- `kernel/include/fs/vfs.h` - VFS structures and API
- `kernel/include/fs/file.h` - FD table API
- `kernel/include/fs/tmpfs.h` - TMPFS API
- `kernel/include/fs/tarfs.h` - TARFS API

### Tests
- `kernel/tests/fs_test.c` - Comprehensive filesystem tests

## Common Operations

| Operation | Function | Example |
|-----------|----------|---------|
| Open file | `vfs_open()` | `file_t *f = vfs_open("/tmp/file.txt");` |
| Read file | `vfs_read()` | `vfs_read(f, buf, 256);` |
| Write file | `vfs_write()` | `vfs_write(f, "data", 4);` |
| Close file | `vfs_close()` | `vfs_close(f);` |
| Create file | `vfs_create()` | `vfs_create("/tmp/new.txt", 0644);` |
| Delete file | `vfs_unlink()` | `vfs_unlink("/tmp/old.txt");` |
| Create dir | `vfs_mkdir()` | `vfs_mkdir("/tmp/dir", 0755);` |
| Mount fs | `vfs_mount()` | `vfs_mount(NULL, "/mnt", "tmpfs");` |
| Unmount fs | `vfs_unmount()` | `vfs_unmount("/mnt");` |

## Important Notes

### TARFS (Read-Only)
- Mounted at root ("/")
- Contains files from embedded TAR archive
- Cannot write, create, or delete files
- Example: `/hello.txt`, `/bin/ls`

### TMPFS (Read-Write)
- Mounted at "/tmp"
- In-memory, volatile (lost on reboot)
- Can create, write, delete files
- Can create directories
- Max 128 files, 1MB per file

### Path Resolution
- Paths starting with "/" are absolute
- VFS finds the longest matching mount point
- Relative path is passed to filesystem

### Error Handling
- Functions return -1 on error, 0+ on success
- Pointers return NULL on error
- Always check return values!

## Troubleshooting

### File Not Found
```c
file_t *f = vfs_open("/tmp/missing.txt");
if (!f) {
    // File doesn't exist - create it first
    vfs_create("/tmp/missing.txt", 0644);
    f = vfs_open("/tmp/missing.txt");
}
```

### Write to Read-Only File
```c
// This will fail - tarfs is read-only
file_t *f = vfs_open("/hello.txt");
int n = vfs_write(f, "data", 4);  // Returns -1, prints error
```

### Mount Point Not Found
```c
// Make sure filesystem is mounted
mount_t *mnt = vfs_find_mount("/tmp");
if (!mnt) {
    printk("Need to mount tmpfs first\n");
    vfs_mount(NULL, "/tmp", "tmpfs");
}
```

## Next Steps

1. Run `make test` to verify implementation
2. Check `dump/fs_api_reference.md` for detailed API docs
3. See `dump/adding_new_filesystem.md` to add more filesystem types
4. Integrate with your process management and syscalls

## Need Help?

- **API Reference**: `dump/fs_api_reference.md`
- **Architecture**: `dump/fs_implementation.md`
- **Adding Filesystems**: `dump/adding_new_filesystem.md`
- **Changes Made**: `dump/changes_summary.md`

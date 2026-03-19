# Filesystem Implementation Documentation

## Overview

This implementation provides a complete Virtual File System (VFS) layer with support for multiple filesystem types, mount points, and a fully functional in-memory tmpfs.

## Architecture

### VFS Layer (`vfs.h`, `vfs.c`)

The VFS provides a unified interface for different filesystem types:

- **Mount Point Management**: Track multiple mount points with different filesystem types
- **Filesystem Registration**: Register filesystem types dynamically
- **Path Resolution**: Find the appropriate mount point for any given path
- **File Operations**: Unified interface for open, close, read, write operations

#### Key Structures

**`vnode_t`**: Represents an inode in the VFS layer
- `ino`: Inode number
- `mode`: File permissions and type
- `size`: File size
- `ref_count`: Reference counting for proper cleanup
- `mount`: Pointer to the mount point
- `fs_data`: Filesystem-specific data (e.g., tmpfs_inode_t or tarfs_entry_t)
- `fs_type`: Pointer to filesystem type

**`mount_t`**: Represents a mount point
- `mountpoint`: Path where filesystem is mounted (e.g., "/", "/tmp")
- `fs_type`: Filesystem type (tarfs, tmpfs)
- `fs_data`: Filesystem-specific mount data
- `next`: Linked list of mounts

**`fs_type_t`**: Filesystem type descriptor
- `name`: Filesystem name ("tarfs", "tmpfs")
- `ops`: Filesystem operations (mount, unmount, lookup, read, write, create, unlink, mkdir)
- `next`: Linked list of registered filesystems

#### VFS Functions

- `vfs_init()`: Initialize the VFS subsystem
- `vfs_register_fs()`: Register a new filesystem type
- `vfs_mount()`: Mount a filesystem at a path
- `vfs_unmount()`: Unmount a filesystem
- `vfs_find_mount()`: Find the mount point for a given path (longest prefix match)
- `vfs_lookup()`: Look up a file and return a vnode
- `vfs_create()`: Create a new file
- `vfs_unlink()`: Delete a file
- `vfs_mkdir()`: Create a directory
- `vfs_open()`: Open a file (returns file_t)
- `vfs_close()`: Close a file
- `vfs_read()`: Read from a file
- `vfs_write()`: Write to a file

### File Descriptor Layer (`file.h`, `file.c`)

Process-level file descriptor management:

**`fd_table_t`**: Per-process file descriptor table
- Array of `file_t*` pointers (MAX_FDS = 16)
- Indices 0, 1, 2 reserved for stdin, stdout, stderr

**`file_t`**: Open file descriptor
- `type`: File type (FILE_TARFS, FILE_TMPFS, FILE_STDIN, FILE_STDOUT)
- Union of `tarfs_entry` or `vnode` pointer
- `offset`: Current read/write position
- `ref_count`: Reference count for shared file descriptors

#### FD Functions

- `fd_init()`: Initialize fd table with stdin/stdout/stderr
- `fd_alloc()`: Allocate a new file descriptor
- `fd_get()`: Get file_t for a given fd number
- `fd_close()`: Close a file descriptor

### TARFS (`tarfs.h`, `tarfs.c`)

Read-only filesystem backed by a TAR archive embedded in the kernel binary.

**Features:**
- Parses USTAR format TAR archives
- Indexes all files at boot time
- Read-only operations
- Integrated with VFS layer

**Key Functions:**
- `tarfs_init()`: Parse and index the embedded TAR archive
- `tarfs_lookup()`: Find a file by path (legacy interface)
- `tarfs_read()`: Read file data (legacy interface)
- `tarfs_mount()`: VFS mount operation
- `tarfs_vfs_lookup()`: VFS lookup operation
- `tarfs_vfs_read()`: VFS read operation

### TMPFS (`tmpfs.h`, `tmpfs.c`)

In-memory read-write filesystem for temporary files.

**Features:**
- Dynamic file and directory creation
- Read and write operations
- Hierarchical directory structure
- Automatic buffer expansion on write
- File deletion support

**Key Structures:**

**`tmpfs_inode_t`**: In-memory inode
- `ino`: Inode number
- `mode`: Permissions
- `type`: TMPFS_TYPE_FILE or TMPFS_TYPE_DIR
- `size`: Current file size
- `name`: Filename
- `data`: File contents (dynamically allocated)
- `capacity`: Allocated buffer size
- `parent`: Parent directory
- `next`: Next sibling
- `children`: First child (for directories)

**`tmpfs_mount_data_t`**: Per-mount data
- `root`: Root directory inode
- `next_ino`: Next inode number to allocate
- `file_count`: Total number of files

**Key Functions:**
- `tmpfs_init()`: Register tmpfs with VFS
- `tmpfs_mount()`: Create a new tmpfs instance
- `tmpfs_unmount()`: Destroy tmpfs instance
- `tmpfs_lookup()`: Find file/directory by path
- `tmpfs_read()`: Read file data
- `tmpfs_write()`: Write file data (with automatic buffer expansion)
- `tmpfs_create()`: Create a new file
- `tmpfs_unlink()`: Delete a file
- `tmpfs_mkdir()`: Create a directory

## Usage Examples

### Mounting Filesystems

```c
// Initialize VFS and filesystems
vfs_init();
tarfs_init();
tmpfs_init();

// Register filesystem types
vfs_register_fs(tarfs_get_type());
vfs_register_fs(tmpfs_get_type());

// Mount root as tarfs
vfs_mount(NULL, "/", "tarfs");

// Mount tmpfs at /tmp
vfs_mount(NULL, "/tmp", "tmpfs");
```

### Reading from TARFS

```c
// Open a file from the embedded TAR archive
file_t *f = vfs_open("/hello.txt");
if (f) {
    char buf[256];
    int n = vfs_read(f, buf, sizeof(buf));
    printk("Read %d bytes: %s\n", n, buf);
    vfs_close(f);
}
```

### Creating and Writing to TMPFS

```c
// Create a new file in tmpfs
vfs_create("/tmp/myfile.txt", 0644);

// Open and write to it
file_t *f = vfs_open("/tmp/myfile.txt");
if (f) {
    const char *data = "Hello, tmpfs!";
    vfs_write(f, data, strlen(data));
    vfs_close(f);
}

// Read it back
f = vfs_open("/tmp/myfile.txt");
if (f) {
    char buf[256];
    int n = vfs_read(f, buf, sizeof(buf));
    printk("Read back: %s\n", buf);
    vfs_close(f);
}
```

### Working with Directories

```c
// Create a directory
vfs_mkdir("/tmp/mydir", 0755);

// Create a file in the directory
vfs_create("/tmp/mydir/file.txt", 0644);

// Open and use the file
file_t *f = vfs_open("/tmp/mydir/file.txt");
// ... use file ...
vfs_close(f);

// Delete the file
vfs_unlink("/tmp/mydir/file.txt");
```

### Using File Descriptors

```c
// Initialize fd table for a process
fd_table_t fd_table;
fd_init(&fd_table);  // Sets up stdin (0), stdout (1), stderr (2)

// Open a file and get an fd
file_t *f = vfs_open("/tmp/data.txt");
int fd = fd_alloc(&fd_table, f);  // Returns 3 (first available)

// Use the fd
file_t *file = fd_get(&fd_table, fd);
vfs_read(file, buf, sizeof(buf));

// Close the fd
fd_close(&fd_table, fd);
```

## Implementation Details

### Path Resolution

The VFS uses longest prefix matching to find the appropriate mount point:

1. For path `/tmp/foo/bar.txt`, it searches all mount points
2. Finds `/tmp` as the longest matching prefix
3. Strips the mount point prefix to get relative path `foo/bar.txt`
4. Calls the filesystem-specific lookup with the relative path

### Reference Counting

Both `file_t` and `vnode_t` use reference counting:

- `file_t->ref_count`: Tracks open file descriptors
- `vnode_t->ref_count`: Tracks active vnodes
- When ref_count reaches 0, the structure is freed

### Backward Compatibility

The implementation maintains backward compatibility with existing code:

- `vfs_open()` first tries the new VFS lookup, then falls back to direct `tarfs_lookup()`
- Old-style `tarfs_entry` pointers are still supported in `file_t`
- Existing tests continue to work

### Memory Management

- **TARFS**: No dynamic allocation for file data (points into embedded TAR blob)
- **TMPFS**: Dynamic allocation for inodes and file data
  - File buffers grow automatically on write
  - Buffers are freed when files are deleted
  - All inodes are freed on unmount

### Limitations

Current implementation limits:

- **TMPFS**: Max 128 files per mount (`TMPFS_MAX_FILES`)
- **TMPFS**: Max 1MB per file (`TMPFS_MAX_FILESIZE`)
- **TMPFS**: Max 256 character filenames (`TMPFS_MAX_FILENAME`)
- **FD Table**: Max 16 file descriptors per process (`MAX_FDS`)
- **TARFS**: Max 64 entries (`TARFS_MAX_ENTRIES`)

These can be adjusted in the respective header files.

## Testing

The test suite (`fs_test.c`) includes:

1. **tarfs_test**: Tests TAR archive reading
2. **vfs_test**: Tests VFS file operations
3. **fd_test**: Tests file descriptor management
4. **mount_test**: Tests mount point resolution
5. **tmpfs_test**: Tests tmpfs create, read, write, mkdir, unlink operations

Run tests by enabling `RUN_TESTS` in your build configuration.

## Future Enhancements

Potential improvements:

1. **Directory Listing**: Add `readdir()` operation
2. **File Metadata**: Add timestamps, ownership, permissions
3. **Symbolic Links**: Support symlinks in both filesystems
4. **Path Normalization**: Handle `.` and `..` in paths
5. **File Locking**: Add locking mechanisms for concurrent access
6. **Extended Attributes**: Support xattrs
7. **More Filesystem Types**: Add devfs, procfs, sysfs
8. **Caching**: Add inode and dentry caches for performance
9. **Seek Operations**: Add `vfs_seek()` for explicit positioning
10. **Truncate**: Add `vfs_truncate()` for file size changes

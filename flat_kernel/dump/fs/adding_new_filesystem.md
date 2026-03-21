# Guide: Adding a New Filesystem Type

This guide shows how to add a new filesystem type to the VFS layer, using tmpfs as a reference example.

## Step 1: Create Header File

Create `kernel/include/fs/yourfs.h`:

```c
#pragma once
#include "kernel/types.h"
#include "fs/vfs.h"

// Define filesystem-specific structures
typedef struct yourfs_inode {
    uint64_t ino;
    uint32_t mode;
    uint32_t size;
    // ... your filesystem-specific fields
} yourfs_inode_t;

typedef struct yourfs_mount_data {
    // Per-mount data
    yourfs_inode_t *root;
    // ... other mount-specific data
} yourfs_mount_data_t;

// Function declarations
void yourfs_init(void);
int yourfs_mount(mount_t *mnt, const char *source);
int yourfs_unmount(mount_t *mnt);
vnode_t *yourfs_lookup(mount_t *mnt, const char *path);
int yourfs_read(vnode_t *vnode, void *buf, size_t offset, size_t len);
int yourfs_write(vnode_t *vnode, const void *buf, size_t offset, size_t len);
int yourfs_create(mount_t *mnt, const char *path, uint32_t mode);
int yourfs_unlink(mount_t *mnt, const char *path);
int yourfs_mkdir(mount_t *mnt, const char *path, uint32_t mode);

fs_type_t *yourfs_get_type(void);
```

## Step 2: Implement Filesystem Operations

Create `kernel/fs/yourfs.c`:

```c
#include "fs/yourfs.h"
#include "fs/vfs.h"
#include "kernel/printk.h"
#include "mm/kmalloc.h"
#include "lib/string.h"

// -----------------------------------------------------------------------------
// Mount Operation
// -----------------------------------------------------------------------------

int yourfs_mount(mount_t *mnt, const char *source) {
    if (!mnt) return -1;

    // Allocate mount-specific data
    yourfs_mount_data_t *mnt_data = kzalloc(sizeof(yourfs_mount_data_t));
    if (!mnt_data) {
        printk("yourfs: failed to allocate mount data\n");
        return -1;
    }

    // Initialize your filesystem
    // Create root directory, parse source data, etc.
    mnt_data->root = /* ... create root ... */;

    mnt->fs_data = mnt_data;

    printk("yourfs: mounted at '%s'\n", mnt->mountpoint);
    return 0;
}

// -----------------------------------------------------------------------------
// Unmount Operation
// -----------------------------------------------------------------------------

int yourfs_unmount(mount_t *mnt) {
    if (!mnt || !mnt->fs_data) return -1;

    yourfs_mount_data_t *mnt_data = (yourfs_mount_data_t *)mnt->fs_data;
    
    // Clean up all filesystem data
    // Free inodes, buffers, etc.
    
    kfree(mnt_data);
    mnt->fs_data = NULL;

    printk("yourfs: unmounted\n");
    return 0;
}

// -----------------------------------------------------------------------------
// Lookup Operation
// -----------------------------------------------------------------------------

vnode_t *yourfs_lookup(mount_t *mnt, const char *path) {
    if (!mnt || !mnt->fs_data || !path) return NULL;

    yourfs_mount_data_t *mnt_data = (yourfs_mount_data_t *)mnt->fs_data;
    
    // Find the inode for this path
    yourfs_inode_t *inode = /* ... search your filesystem ... */;
    
    if (!inode) return NULL;

    // Allocate a vnode
    vnode_t *vnode = kzalloc(sizeof(vnode_t));
    if (!vnode) return NULL;

    vnode->ino = inode->ino;
    vnode->mode = inode->mode;
    vnode->size = inode->size;
    vnode->ref_count = 0;
    vnode->mount = mnt;
    vnode->fs_data = inode;
    vnode->fs_type = mnt->fs_type;

    return vnode;
}

// -----------------------------------------------------------------------------
// Read Operation
// -----------------------------------------------------------------------------

int yourfs_read(vnode_t *vnode, void *buf, size_t offset, size_t len) {
    if (!vnode || !buf) return -1;

    yourfs_inode_t *inode = (yourfs_inode_t *)vnode->fs_data;
    if (!inode) return -1;

    // Implement your read logic
    // Copy data from inode to buf
    
    return (int)bytes_read;
}

// -----------------------------------------------------------------------------
// Write Operation
// -----------------------------------------------------------------------------

int yourfs_write(vnode_t *vnode, const void *buf, size_t offset, size_t len) {
    if (!vnode || !buf) return -1;

    yourfs_inode_t *inode = (yourfs_inode_t *)vnode->fs_data;
    if (!inode) return -1;

    // Implement your write logic
    // Copy data from buf to inode
    
    return (int)bytes_written;
}

// -----------------------------------------------------------------------------
// Create Operation
// -----------------------------------------------------------------------------

int yourfs_create(mount_t *mnt, const char *path, uint32_t mode) {
    if (!mnt || !mnt->fs_data || !path) return -1;

    yourfs_mount_data_t *mnt_data = (yourfs_mount_data_t *)mnt->fs_data;
    
    // Create a new file inode
    // Add it to the parent directory
    
    return 0;
}

// -----------------------------------------------------------------------------
// Unlink Operation
// -----------------------------------------------------------------------------

int yourfs_unlink(mount_t *mnt, const char *path) {
    if (!mnt || !mnt->fs_data || !path) return -1;

    yourfs_mount_data_t *mnt_data = (yourfs_mount_data_t *)mnt->fs_data;
    
    // Find and remove the inode
    // Free associated data
    
    return 0;
}

// -----------------------------------------------------------------------------
// Mkdir Operation
// -----------------------------------------------------------------------------

int yourfs_mkdir(mount_t *mnt, const char *path, uint32_t mode) {
    if (!mnt || !mnt->fs_data || !path) return -1;

    yourfs_mount_data_t *mnt_data = (yourfs_mount_data_t *)mnt->fs_data;
    
    // Create a new directory inode
    // Add it to the parent directory
    
    return 0;
}

// -----------------------------------------------------------------------------
// Filesystem Type Registration
// -----------------------------------------------------------------------------

static fs_type_t yourfs_type = {
    .name = "yourfs",
    .ops = {
        .mount = yourfs_mount,
        .unmount = yourfs_unmount,
        .lookup = yourfs_lookup,
        .read = yourfs_read,
        .write = yourfs_write,
        .create = yourfs_create,
        .unlink = yourfs_unlink,
        .mkdir = yourfs_mkdir,
    },
    .next = NULL
};

void yourfs_init(void) {
    vfs_register_fs(&yourfs_type);
    printk("yourfs: registered\n");
}

fs_type_t *yourfs_get_type(void) {
    return &yourfs_type;
}
```

## Step 3: Add to VFS Header (if needed)

If you need a new file type enum value, add it to `kernel/include/fs/vfs.h`:

```c
typedef enum {
    FILE_TARFS,
    FILE_TMPFS,
    FILE_YOURFS,  // Add your type here
    FILE_STDIN,
    FILE_STDOUT
} file_type_t;
```

## Step 4: Update VFS Operations (if needed)

If your filesystem needs special handling in `vfs_read()` or `vfs_write()`, add a case:

```c
// In kernel/fs/vfs.c

int vfs_read(file_t *f, void *buf, size_t len) {
    if (!f || !buf) return -1;

    switch (f->type) {
        // ... existing cases ...
        
        case FILE_YOURFS: {
            if (f->use_vnode && f->vnode) {
                vnode_t *vnode = f->vnode;
                if (vnode->mount && vnode->mount->fs_type->ops.read) {
                    int n = vnode->mount->fs_type->ops.read(vnode, buf, f->offset, len);
                    if (n > 0)
                        f->offset += (size_t)n;
                    return n;
                }
            }
            return -1;
        }
        
        // ... rest of cases ...
    }
}
```

## Step 5: Initialize and Mount

In `kernel/core/kernel.c`:

```c
#include "fs/yourfs.h"

void boot(uint64_t hart_id, uint64_t dtb) {
    // ... other initialization ...
    
    // Initialize filesystems
    vfs_init();
    tarfs_init();
    tmpfs_init();
    yourfs_init();  // Add your filesystem
    
    // Register with VFS
    vfs_register_fs(tarfs_get_type());
    vfs_register_fs(yourfs_get_type());  // Register your filesystem
    
    // Mount filesystems
    vfs_mount(NULL, "/", "tarfs");
    vfs_mount(NULL, "/tmp", "tmpfs");
    vfs_mount(NULL, "/your", "yourfs");  // Mount your filesystem
    
    // ... rest of boot ...
}
```

## Step 6: Add Tests

In `kernel/tests/fs_test.c`:

```c
void yourfs_test(void) {
    printk("=== yourfs tests ===\n");

    // Test your filesystem operations
    vfs_create("/your/test.txt", 0644);
    
    file_t *f = vfs_open("/your/test.txt");
    if (f) {
        vfs_write(f, "test data", 9);
        vfs_close(f);
        printk("PASS: wrote to yourfs\n");
    }
    
    // ... more tests ...
}

void fs_test_run(void) {
    // ... existing tests ...
    yourfs_test();
}
```

## Required Operations

### Mandatory Operations

These operations should be implemented for a functional filesystem:

1. **mount**: Initialize the filesystem instance
2. **unmount**: Clean up the filesystem instance
3. **lookup**: Find a file/directory by path and return a vnode

### Optional Operations

Implement based on your filesystem's capabilities:

- **read**: Read file data (required for readable files)
- **write**: Write file data (required for writable files)
- **create**: Create new files (required for writable filesystems)
- **unlink**: Delete files (required for writable filesystems)
- **mkdir**: Create directories (required for hierarchical filesystems)

### Read-Only Filesystem

For read-only filesystems (like tarfs):

```c
static fs_type_t readonly_fs_type = {
    .name = "readonly",
    .ops = {
        .mount = readonly_mount,
        .unmount = readonly_unmount,
        .lookup = readonly_lookup,
        .read = readonly_read,
        .write = NULL,     // Not supported
        .create = NULL,    // Not supported
        .unlink = NULL,    // Not supported
        .mkdir = NULL,     // Not supported
    },
    .next = NULL
};
```

## Common Patterns

### Pattern 1: Simple In-Memory Filesystem (like tmpfs)

- Store all data in kernel heap
- Use linked lists or trees for directory structure
- Allocate buffers on demand

### Pattern 2: Block-Based Filesystem

- Read/write data in fixed-size blocks
- Cache blocks in memory
- Implement block allocation bitmap

### Pattern 3: Network Filesystem

- Store mount data with network connection info
- Implement operations as RPC calls
- Handle caching and consistency

### Pattern 4: Device Filesystem (devfs)

- Create virtual files representing devices
- Redirect read/write to device drivers
- No actual data storage

## Example: Simple DevFS

```c
// kernel/include/fs/devfs.h
#pragma once
#include "fs/vfs.h"

typedef struct devfs_device {
    char name[64];
    int (*read)(void *buf, size_t len);
    int (*write)(const void *buf, size_t len);
    struct devfs_device *next;
} devfs_device_t;

void devfs_init(void);
int devfs_register_device(const char *name, 
                          int (*read)(void*, size_t),
                          int (*write)(const void*, size_t));

// kernel/fs/devfs.c
#include "fs/devfs.h"

static devfs_device_t *devices = NULL;

int devfs_mount(mount_t *mnt, const char *source) {
    // No per-mount data needed
    mnt->fs_data = NULL;
    return 0;
}

vnode_t *devfs_lookup(mount_t *mnt, const char *path) {
    // Find device by name
    devfs_device_t *dev = devices;
    while (dev) {
        if (strcmp(dev->name, path) == 0) {
            vnode_t *vnode = kzalloc(sizeof(vnode_t));
            vnode->fs_data = dev;
            vnode->mount = mnt;
            vnode->fs_type = mnt->fs_type;
            return vnode;
        }
        dev = dev->next;
    }
    return NULL;
}

int devfs_read(vnode_t *vnode, void *buf, size_t offset, size_t len) {
    devfs_device_t *dev = (devfs_device_t *)vnode->fs_data;
    if (dev && dev->read) {
        return dev->read(buf, len);
    }
    return -1;
}

int devfs_write(vnode_t *vnode, const void *buf, size_t offset, size_t len) {
    devfs_device_t *dev = (devfs_device_t *)vnode->fs_data;
    if (dev && dev->write) {
        return dev->write(buf, len);
    }
    return -1;
}

static fs_type_t devfs_type = {
    .name = "devfs",
    .ops = {
        .mount = devfs_mount,
        .unmount = NULL,
        .lookup = devfs_lookup,
        .read = devfs_read,
        .write = devfs_write,
        .create = NULL,
        .unlink = NULL,
        .mkdir = NULL,
    },
    .next = NULL
};

void devfs_init(void) {
    vfs_register_fs(&devfs_type);
    printk("devfs: registered\n");
}

int devfs_register_device(const char *name,
                          int (*read)(void*, size_t),
                          int (*write)(const void*, size_t)) {
    devfs_device_t *dev = kmalloc(sizeof(devfs_device_t));
    if (!dev) return -1;
    
    strncpy(dev->name, name, sizeof(dev->name) - 1);
    dev->read = read;
    dev->write = write;
    dev->next = devices;
    devices = dev;
    
    return 0;
}

// Usage:
// devfs_init();
// vfs_mount(NULL, "/dev", "devfs");
// devfs_register_device("console", console_read, console_write);
// file_t *f = vfs_open("/dev/console");
```

## Tips and Best Practices

### 1. Memory Management

- Always use `kzalloc()` to zero-initialize structures
- Free all allocated memory in unmount operation
- Use reference counting for shared resources
- Check for allocation failures

### 2. Path Handling

- Paths passed to filesystem operations are relative to the mount point
- Handle empty paths (should refer to root of filesystem)
- Strip leading slashes if needed
- Validate path lengths

### 3. Error Handling

- Return -1 on errors
- Return 0 or positive values on success
- Return NULL for pointer functions on error
- Print debug messages with `printk()` for troubleshooting

### 4. Thread Safety

- Current implementation is single-threaded
- For multi-threaded kernels, add locking:
  - Per-mount locks
  - Per-inode locks
  - Global VFS lock for mount list

### 5. Performance

- Cache frequently accessed inodes
- Use efficient data structures (hash tables, B-trees)
- Minimize memory allocations
- Consider read-ahead for sequential access

### 6. Testing

- Test all operations thoroughly
- Test error conditions (full filesystem, invalid paths)
- Test edge cases (empty files, large files, deep directories)
- Test concurrent access if applicable

## Filesystem Operation Checklist

When implementing a new filesystem, ensure:

- [ ] Mount operation initializes all necessary data structures
- [ ] Unmount operation frees all allocated memory
- [ ] Lookup returns NULL for non-existent files
- [ ] Read handles offset and length correctly
- [ ] Read returns 0 at EOF
- [ ] Write handles buffer expansion if needed
- [ ] Create checks if file already exists
- [ ] Unlink removes file and frees data
- [ ] Mkdir creates directory structure
- [ ] All operations validate input parameters
- [ ] All operations handle NULL pointers gracefully
- [ ] Error messages are clear and helpful
- [ ] Reference counting is correct

## Integration Checklist

- [ ] Header file created in `kernel/include/fs/`
- [ ] Implementation file created in `kernel/fs/`
- [ ] Filesystem type structure defined with all operations
- [ ] `yourfs_init()` function registers with VFS
- [ ] `yourfs_get_type()` function returns filesystem type
- [ ] Added to kernel initialization in `kernel.c`
- [ ] Tests added to `kernel/tests/fs_test.c`
- [ ] Documentation updated

## Debugging New Filesystems

### Enable Verbose Logging

Add printk statements at key points:

```c
int yourfs_lookup(mount_t *mnt, const char *path) {
    printk("yourfs_lookup: path='%s'\n", path);
    // ... implementation ...
    printk("yourfs_lookup: found inode %llu\n", inode->ino);
    return vnode;
}
```

### Check Mount Points

```c
mount_t *mnt = vfs_find_mount("/your/path");
if (mnt) {
    printk("Mount: %s, FS: %s\n", mnt->mountpoint, mnt->fs_type->name);
} else {
    printk("No mount found for path\n");
}
```

### Verify Registration

```c
void yourfs_init(void) {
    int ret = vfs_register_fs(&yourfs_type);
    if (ret == 0) {
        printk("yourfs: successfully registered\n");
    } else {
        printk("yourfs: registration failed\n");
    }
}
```

### Test Incrementally

1. Start with just mount/unmount
2. Add lookup (read-only)
3. Add read operation
4. Add write operation
5. Add create/unlink
6. Add mkdir

Test each step before moving to the next.

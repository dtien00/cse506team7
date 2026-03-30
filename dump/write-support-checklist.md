# Write Support Implementation Checklist

## Quick Reference: What's Missing

### The Core Problem
**sys_open() doesn't create files when O_CREAT is specified** - it only opens existing files.

## Implementation Checklist

### ✅ Already Working (No Changes Needed)
- [x] tmpfs backend (write, create, unlink, mkdir all work)
- [x] VFS routing layer (vfs_create, vfs_write, vfs_unlink, vfs_mkdir)
- [x] sys_write() syscall
- [x] sys_read() syscall
- [x] Basic libc wrappers (read, write, open, close)

### 🔧 Phase 1: File Creation (CRITICAL - Do This First)

#### Kernel Changes
- [ ] **kernel/include/fs/vfs.h**
  - [ ] Add `int flags;` field to `file_t` structure
  - [ ] Change `vfs_open()` signature: `file_t *vfs_open(const char *path, int flags);`
  - [ ] Change `vfs_lseek()` return type: `off_t vfs_lseek(file_t *f, off_t offset, int whence);`

- [ ] **kernel/fs/vfs.c**
  - [ ] Update `vfs_open()` to accept flags parameter
  - [ ] Store flags in `f->flags = flags;`
  - [ ] Fix `vfs_lseek()` to use `f->vnode->size` instead of `f->entry->size`
  - [ ] Fix `vfs_lseek()` return type to `off_t`

- [ ] **kernel/trap/syscall.c**
  - [ ] Add O_* flag definitions at top (or include fcntl.h)
  - [ ] Fix `sys_open()`:
    - [ ] Call `vfs_open(path, flags)` with flags
    - [ ] If file not found and O_CREAT set, call `vfs_create()` then retry `vfs_open()`
    - [ ] Handle O_TRUNC (truncate file to zero)
    - [ ] Handle O_APPEND (set offset to end)

**After Phase 1**: File creation with `open("/tmp/file.txt", O_CREAT | O_WRONLY)` will work!

### 🔧 Phase 2: File Seeking

#### Kernel Changes
- [ ] **kernel/include/trap/syscall.h**
  - [ ] Add `#define SYS_lseek 17`
  - [ ] Add declaration: `off_t sys_lseek(int fd, off_t offset, int whence);`

- [ ] **kernel/trap/syscall.c**
  - [ ] Implement `sys_lseek()` function
  - [ ] Add `case SYS_lseek:` to `syscall_handler()` switch

#### Libc Changes
- [ ] **libc/fileio.c**
  - [ ] Replace `lseek()` stub with actual syscall wrapper
  - [ ] Add `#define SYS_lseek 17` at top

**After Phase 2**: Seeking in files with `lseek()` will work!

### 🔧 Phase 3: File Deletion

#### Kernel Changes
- [ ] **kernel/include/trap/syscall.h**
  - [ ] Add `#define SYS_unlink 18`
  - [ ] Add declaration: `int sys_unlink(const char *path);`

- [ ] **kernel/trap/syscall.c**
  - [ ] Implement `sys_unlink()` function
  - [ ] Add `case SYS_unlink:` to `syscall_handler()` switch

#### Libc Changes
- [ ] **libc/fileio.c**
  - [ ] Replace `unlink()` stub with actual syscall wrapper
  - [ ] Add `#define SYS_unlink 18` at top

**After Phase 3**: Deleting files with `unlink()` will work!

### 🔧 Phase 4: Directory Creation

#### Kernel Changes
- [ ] **kernel/include/trap/syscall.h**
  - [ ] Add `#define SYS_mkdir 19`
  - [ ] Add declaration: `int sys_mkdir(const char *path, mode_t mode);`

- [ ] **kernel/trap/syscall.c**
  - [ ] Implement `sys_mkdir()` function
  - [ ] Add `case SYS_mkdir:` to `syscall_handler()` switch

#### Libc Changes
- [ ] **libc/fileio.c** (or create `libc/directory.c`)
  - [ ] Implement `mkdir()` syscall wrapper
  - [ ] Add `#define SYS_mkdir 19` at top

**After Phase 4**: Creating directories with `mkdir()` will work!

### 🔧 Phase 5: Standard I/O Streams (Optional)

#### Libc Changes
- [ ] **libc/include/stdio.h**
  - [ ] Add `FILE` structure definition
  - [ ] Add declarations for fopen, fclose, fread, fwrite, etc.
  - [ ] Add `extern FILE *stdin, *stdout, *stderr;`

- [ ] **libc/stdio.c** (NEW FILE)
  - [ ] Implement `FILE` structure with buffering
  - [ ] Implement `fopen()` - parse mode string, call open()
  - [ ] Implement `fclose()` - flush and close
  - [ ] Implement `fread()` - buffered read
  - [ ] Implement `fwrite()` - buffered write
  - [ ] Implement `fflush()` - flush write buffer
  - [ ] Implement `fseek()`, `ftell()`, `rewind()`
  - [ ] Implement `fgetc()`, `fputc()`, `fgets()`, `fputs()`
  - [ ] Implement `feof()`, `ferror()`, `clearerr()`

**After Phase 5**: Standard C programs using FILE* will work!

## Quick Start: Minimum to Get Writes Working

If you only want to get basic writes working ASAP, do these 3 things:

### 1. Add flags to file_t
```c
// In kernel/include/fs/vfs.h
typedef struct file {
    // ... existing fields ...
    int flags;  // ADD THIS
} file_t;
```

### 2. Fix sys_open() to create files
```c
// In kernel/trap/syscall.c
int sys_open(const char *path, int flags) {
    // ... existing setup ...
    
    file_t *f = vfs_open(path, flags);  // Pass flags
    
    // ADD THIS BLOCK:
    if (!f && (flags & 0x0040)) {  // O_CREAT = 0x0040
        if (vfs_create(path, 0644) == 0) {
            f = vfs_open(path, flags);
        }
    }
    
    // ... rest of function ...
}
```

### 3. Update vfs_open() signature
```c
// In kernel/fs/vfs.c
file_t *vfs_open(const char *path, int flags) {  // Add flags param
    // ... existing code ...
    f->flags = flags;  // Store flags
    // ... rest of function ...
}
```

**That's it!** With just these 3 changes, you can create and write files:
```c
int fd = open("/tmp/myfile.txt", O_CREAT | O_WRONLY);
write(fd, "Hello!", 6);
close(fd);
```

## Syscall Number Assignments

Available numbers (17-99):
- **17**: SYS_lseek
- **18**: SYS_unlink  
- **19**: SYS_mkdir
- **20**: SYS_rmdir (future)
- **21**: SYS_stat (future)
- **22**: SYS_fstat (future)
- **23**: SYS_getdents (future)

## What About Persistent Storage?

tmpfs is **non-persistent** (lost on reboot). For true persistence, you need:

1. **Block Device Driver**
   - IDE/SATA/NVMe driver
   - Or virtio-blk for QEMU
   - Read/write 512-byte sectors

2. **Persistent Filesystem**
   - ext2 (simple, well-documented)
   - FAT32 (simpler but limited)
   - Custom filesystem

3. **Block Cache**
   - Cache frequently accessed blocks
   - Write-back or write-through policy
   - Dirty block tracking

4. **Sync/Flush Mechanism**
   - Periodic sync to disk
   - Flush on unmount
   - Journal for crash recovery (advanced)

**Estimated effort for persistent storage**: 1000-2000 lines, several weeks of work.

For now, tmpfs is sufficient for:
- Testing write operations
- Shell scripts
- Temporary files
- Build systems
- Most OS functionality

You can add persistent storage later as an enhancement.

## Common Pitfalls

1. **Forgetting to include fcntl.h in kernel code**
   - Kernel needs O_CREAT, O_WRONLY, etc. definitions
   - Either include or redefine in syscall.c

2. **Not handling user-space pointers**
   - Remember to set/clear SSTATUS_SUM bit when accessing user pointers
   - Already done correctly in your sys_open

3. **Not flushing FILE* buffers**
   - Always flush before closing or seeking
   - Implement fflush() properly

4. **Mixing file_t->entry and file_t->vnode**
   - Old code used `entry` for tarfs
   - New code should use `vnode` for both tarfs and tmpfs
   - Consider removing `entry` field entirely

5. **Not checking write permissions**
   - Should verify file was opened with O_WRONLY or O_RDWR before writing
   - Check `f->flags` in sys_write

## Next Steps

1. Start with Phase 1 (file creation) - this is the critical missing piece
2. Add Phase 2 (seeking) - needed for many programs
3. Add Phase 3 (deletion) - needed for cleanup
4. Add Phase 4 (mkdir) - needed for directory management
5. Consider Phase 5 (stdio.c) - nice to have for standard C programs

After Phase 1-4, you'll have a fully functional writable filesystem (albeit non-persistent).

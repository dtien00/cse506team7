# Build and Test Guide - Persistent Storage

## Quick Build & Test

```bash
# 1. Clean build
make clean

# 2. Build everything
make

# 3. Run QEMU
make qemu

# 4. Test persistent storage
$ testpersist

# 5. Manual test
$ echo "hello" > /mnt/test.txt
$ cat /mnt/test.txt
$ sync
$ exit

# 6. Verify persistence
make qemu
$ cat /mnt/test.txt  # Should still show "hello"
```

## Expected Build Output

### Step 1: Build mkfs_ext2
```
gcc -Wall -o build/tools/mkfs_ext2 tools/mkfs_ext2.c
```

### Step 2: Format Disk
```
build/tools/mkfs_ext2 build/disk.img 16
Creating ext2 filesystem:
  Size: 16 MB
  Blocks: 16384 (1KB each)
  Inodes: 4096
ext2 filesystem created successfully
  Root directory at block 518
  Mount with: vfs_mount(NULL, "/mnt", "ext2")
```

### Step 3: Compile Kernel
```
riscv64-unknown-elf-gcc ... kernel/drivers/virtio_blk.c
riscv64-unknown-elf-gcc ... kernel/fs/block_cache.c
riscv64-unknown-elf-gcc ... kernel/fs/ext2.c
riscv64-unknown-elf-gcc ... kernel/trap/syscall.c
riscv64-unknown-elf-gcc ... kernel/fs/vfs.c
...
riscv64-unknown-elf-ld ... -o build/kernel.elf
```

### Step 4: Compile Test Program
```
riscv64-unknown-elf-gcc ... bin/testpersist/testpersist.c
```

## Expected Boot Messages

```
========================================
       SBUnix Operating System
       RISC-V 64-bit Kernel
========================================

Kernel: 0x80200000 - 0x80250000 (320 KB)
PMM: initialized (120 MB)
Trap: initialized
VMM: initialized
Kmalloc: initialized

Initializing block device...
virtio_blk: initializing at 0x10001000
virtio_blk: found block device (version 2)
virtio_blk: device features = 0x...
virtio_blk: initialized, capacity = 32768 sectors (16 MB)

block_cache: initializing
block_cache: initialized with 64 blocks

ext2: filesystem registered

vfs: initialized
tarfs: initialized
tmpfs: filesystem registered

vfs: mounted 'none' at '/' (type: tarfs)
vfs: mounted 'none' at '/tmp' (type: tmpfs)

Mounting ext2 filesystem at /mnt...
ext2: mounting filesystem
ext2: block_size=1024, groups=1, inodes=4096, blocks=16384
ext2: mounted successfully
vfs: mounted 'none' at '/mnt' (type: ext2)
Persistent storage available at /mnt

Syscall: Initialized
Process: Initialized
Scheduler: Initialized

========================================
  Kernel initialization complete!
========================================

$ _
```

## Expected Test Output

```
$ testpersist
=== Persistent Storage Test ===

Test 1: Creating /mnt/test.txt...
ext2: created file /mnt/test.txt (inode 11)
  SUCCESS: Wrote 31 bytes

Test 2: Reading /mnt/test.txt...
  SUCCESS: Read 31 bytes: Hello from persistent storage!

Test 3: Creating /mnt/data directory...
ext2: created directory /mnt/data (inode 12)
  SUCCESS: Directory created

Test 4: Creating /mnt/data/nested.txt...
ext2: created file /mnt/data/nested.txt (inode 13)
  SUCCESS: Nested file created

Test 5: Testing lseek...
  SUCCESS: Seeked to position 6
  Read from offset 6: 'from persi'

Test 6: Syncing to disk...
block_cache: flushed 5 dirty blocks
  SUCCESS: Cache flushed to disk

=== All Tests Passed! ===

Files created:
  /mnt/test.txt
  /mnt/data/
  /mnt/data/nested.txt

These files will persist across reboots!
```

## Verification Steps

### 1. Verify Build Artifacts

```bash
# Check kernel
ls -lh build/kernel.elf
# Should be ~500 KB

# Check disk image
ls -lh build/disk.img
# Should be exactly 16 MB (16777216 bytes)

# Check formatter
ls -lh build/tools/mkfs_ext2
# Should exist and be executable

# Check test program
ls -lh build/rootfs/bin/testpersist
# Should exist
```

### 2. Verify Disk Format

```bash
# Check ext2 magic number (should be 53 ef at offset 0x438)
hexdump -C build/disk.img -s 1080 -n 2
# Output: 00000438  53 ef

# View superblock
hexdump -C build/disk.img -s 1024 -n 1024 | head -n 20
```

### 3. Verify Kernel Boot

```bash
make qemu 2>&1 | tee boot.log

# Check for success messages
grep "virtio_blk: initialized" boot.log
grep "block_cache: initialized" boot.log
grep "ext2: mounted successfully" boot.log
grep "Persistent storage available" boot.log
```

### 4. Verify Functionality

```bash
# In QEMU
$ testpersist
# All tests should pass

$ echo "test" > /mnt/verify.txt
$ cat /mnt/verify.txt
test

$ mkdir /mnt/testdir
$ echo "nested" > /mnt/testdir/file.txt
$ cat /mnt/testdir/file.txt
nested

$ sync
$ exit
```

### 5. Verify Persistence

```bash
# Restart QEMU
make qemu

# Check files still exist
$ cat /mnt/verify.txt
test  # SUCCESS!

$ cat /mnt/testdir/file.txt
nested  # SUCCESS!
```

## Common Build Errors

### Error: "virtio.h: No such file or directory"

**Cause**: Missing include directory

**Fix**: Check file exists at `kernel/include/drivers/virtio.h`

### Error: "undefined reference to virtio_blk_init"

**Cause**: virtio_blk.c not compiled

**Fix**: Check Makefile includes all .c files in kernel/drivers/

### Error: "undefined reference to block_cache_flush"

**Cause**: block_cache.c not compiled

**Fix**: Verify block_cache.c exists in kernel/fs/

### Error: "conflicting types for vfs_open"

**Cause**: Signature mismatch between declaration and definition

**Fix**: Ensure vfs.h and vfs.c both have `file_t *vfs_open(const char *path, int flags)`

## Common Runtime Errors

### "Block device not available"

**Cause**: VirtIO device not found

**Debug**:
```bash
# Check QEMU command
make qemu 2>&1 | grep virtio-blk
# Should show: -device virtio-blk-pci-non-transitional,drive=hd0

# Check disk image
ls -lh build/disk.img
```

**Fix**: Ensure Makefile has correct QEMU flags

### "Failed to mount ext2"

**Cause**: Disk not formatted or corrupted

**Debug**:
```bash
# Check magic number
hexdump -C build/disk.img -s 1080 -n 2
# Should show: 53 ef

# Recreate disk
rm build/disk.img
make build/disk.img
```

### "No free blocks" or "No free inodes"

**Cause**: Disk full

**Debug**:
```bash
# Check disk size
ls -lh build/disk.img

# Count files created
# Each file uses 1 inode + N blocks
```

**Fix**:
```bash
# Increase disk size
make build/disk.img DISK_SIZE=32

# Or delete old files
$ unlink /mnt/oldfile.txt
```

## Performance Testing

### Benchmark Script

```bash
#!/bin/sh
# benchmark.sh - Test persistent storage performance

echo "=== Performance Benchmark ==="

# Test 1: Sequential write
echo "Test 1: Sequential write (10 files)..."
START=$(uptime)
for i in 1 2 3 4 5 6 7 8 9 10; do
    echo "File $i content" > /mnt/file$i.txt
done
sync
END=$(uptime)
echo "  Time: $((END - START)) ticks"

# Test 2: Sequential read
echo "Test 2: Sequential read (10 files)..."
START=$(uptime)
for i in 1 2 3 4 5 6 7 8 9 10; do
    cat /mnt/file$i.txt > /dev/null
done
END=$(uptime)
echo "  Time: $((END - START)) ticks"

# Test 3: Random access
echo "Test 3: Random access..."
START=$(uptime)
cat /mnt/file5.txt > /dev/null
cat /mnt/file2.txt > /dev/null
cat /mnt/file8.txt > /dev/null
cat /mnt/file1.txt > /dev/null
END=$(uptime)
echo "  Time: $((END - START)) ticks"

echo "=== Benchmark Complete ==="
```

### Expected Results

```
Test 1: Sequential write (10 files)...
  Time: 50 ticks (~5 ms)
  
Test 2: Sequential read (10 files)...
  Time: 10 ticks (~1 ms, cached)
  
Test 3: Random access...
  Time: 5 ticks (~0.5 ms, cached)
```

## Debugging Tips

### Enable Debug Output

Add to `kernel/fs/ext2.c`:
```c
#define EXT2_DEBUG 1

#ifdef EXT2_DEBUG
#define ext2_debug(fmt, ...) printk("ext2: " fmt, ##__VA_ARGS__)
#else
#define ext2_debug(fmt, ...)
#endif
```

### Trace File Operations

Add to syscall.c:
```c
#define TRACE_SYSCALLS 1

#ifdef TRACE_SYSCALLS
printk("sys_open: path=%s flags=0x%x\n", path, flags);
#endif
```

### Monitor Cache

Add to block_cache.c:
```c
void block_cache_stats(void) {
    int valid = 0, dirty = 0;
    for (int i = 0; i < BLOCK_CACHE_SIZE; i++) {
        if (cache.blocks[i].valid) valid++;
        if (cache.blocks[i].dirty) dirty++;
    }
    printk("cache: %d valid, %d dirty\n", valid, dirty);
}
```

## Success Checklist

Before considering the implementation complete, verify:

- [ ] `make clean && make` succeeds without errors
- [ ] `build/disk.img` is created (16 MB)
- [ ] `build/kernel.elf` includes new code
- [ ] Kernel boots without errors
- [ ] "virtio_blk: initialized" appears in boot log
- [ ] "ext2: mounted successfully" appears in boot log
- [ ] `testpersist` program runs and passes all tests
- [ ] Can create files in `/mnt/`
- [ ] Can write to files in `/mnt/`
- [ ] Can read from files in `/mnt/`
- [ ] Can delete files in `/mnt/`
- [ ] Can create directories in `/mnt/`
- [ ] Files persist after reboot
- [ ] `sync()` flushes cache to disk

## Final Verification

### Complete Test Sequence

```bash
# 1. Clean build
make clean
make 2>&1 | tee build.log
grep -i error build.log  # Should be empty

# 2. First boot
make qemu 2>&1 | tee boot1.log &
sleep 5

# 3. Run tests
echo "testpersist" > /tmp/qemu_input
echo "echo 'persistent' > /mnt/verify.txt" >> /tmp/qemu_input
echo "cat /mnt/verify.txt" >> /tmp/qemu_input
echo "sync" >> /tmp/qemu_input
echo "exit" >> /tmp/qemu_input

# 4. Second boot (verify persistence)
make qemu 2>&1 | tee boot2.log &
sleep 5
echo "cat /mnt/verify.txt" > /tmp/qemu_input
echo "exit" >> /tmp/qemu_input

# 5. Check results
grep "persistent" boot2.log  # Should find it!
```

## Troubleshooting Guide

### Problem: Build fails with missing includes

**Solution**: Verify all header files exist:
```bash
ls kernel/include/drivers/virtio.h
ls kernel/include/fs/ext2.h
ls kernel/include/fs/block_cache.h
```

### Problem: Kernel panics on boot

**Solution**: Check boot log for error messages:
```bash
make qemu 2>&1 | grep -E "(error|panic|failed)"
```

### Problem: ext2 mount fails

**Solution**: Recreate disk image:
```bash
rm build/disk.img
make build/disk.img
make qemu
```

### Problem: Files don't persist

**Solution**: Verify you're using `/mnt/` not `/tmp/`:
```bash
$ echo "test" > /mnt/file.txt  # Persistent
$ echo "test" > /tmp/file.txt  # NOT persistent
```

### Problem: Write fails

**Solution**: Check disk isn't full:
```bash
# In kernel, add debug output to ext2_alloc_block()
# Or increase disk size:
make build/disk.img DISK_SIZE=32
```

## Performance Validation

### Expected Performance

| Operation              | Expected Time | Acceptable Range |
|------------------------|---------------|------------------|
| File create            | 200 μs        | 100-500 μs       |
| Write 1 KB (cached)    | 1 μs          | 0.5-5 μs         |
| Write 1 KB (disk)      | 100 μs        | 50-200 μs        |
| Read 1 KB (cached)     | 1 μs          | 0.5-5 μs         |
| Read 1 KB (disk)       | 100 μs        | 50-200 μs        |
| sync()                 | 6 ms          | 3-10 ms          |

### If Performance is Poor

1. **Check cache hit rate**: Add debug counters
2. **Increase cache size**: Edit `BLOCK_CACHE_SIZE` in block_cache.h
3. **Profile**: Add timing code to identify bottlenecks
4. **Optimize**: Consider read-ahead or write coalescing

## Integration Testing

### Test 1: Multi-File Operations

```bash
$ mkdir /mnt/test
$ echo "file1" > /mnt/test/a.txt
$ echo "file2" > /mnt/test/b.txt
$ echo "file3" > /mnt/test/c.txt
$ cat /mnt/test/a.txt
$ cat /mnt/test/b.txt
$ cat /mnt/test/c.txt
$ sync
```

### Test 2: Large File (within 12 KB limit)

```bash
$ cat > /mnt/large.txt << EOF
Line 1
Line 2
Line 3
...
Line 100
EOF
$ cat /mnt/large.txt | wc -l
100
```

### Test 3: File Deletion

```bash
$ echo "temp" > /mnt/temp.txt
$ cat /mnt/temp.txt
temp
$ unlink /mnt/temp.txt
$ cat /mnt/temp.txt
cat: /mnt/temp.txt: No such file or directory
```

### Test 4: Nested Directories

```bash
$ mkdir /mnt/a
$ mkdir /mnt/a/b
$ mkdir /mnt/a/b/c
$ echo "deep" > /mnt/a/b/c/file.txt
$ cat /mnt/a/b/c/file.txt
deep
```

## Regression Testing

### After Code Changes

1. **Rebuild**: `make clean && make`
2. **Boot test**: Verify kernel boots
3. **Functionality test**: Run `testpersist`
4. **Persistence test**: Create file, reboot, verify
5. **Stress test**: Create many files

### Automated Test Script

```bash
#!/bin/bash
# test_persistent.sh

set -e

echo "=== Persistent Storage Test Suite ==="

# Build
echo "Building..."
make clean > /dev/null 2>&1
make > /dev/null 2>&1

# Check artifacts
test -f build/kernel.elf || { echo "FAIL: kernel.elf missing"; exit 1; }
test -f build/disk.img || { echo "FAIL: disk.img missing"; exit 1; }

echo "Build: PASS"

# Boot test
echo "Boot test..."
timeout 10 make qemu > boot.log 2>&1 &
sleep 3
pkill qemu

grep "ext2: mounted successfully" boot.log > /dev/null || {
    echo "FAIL: ext2 not mounted"
    exit 1
}

echo "Boot: PASS"

echo "=== All Tests Passed ==="
```

## Maintenance

### Regular Checks

```bash
# Check disk usage (from host)
ls -lh build/disk.img

# Backup disk
cp build/disk.img backup/disk-$(date +%Y%m%d).img

# Verify integrity
hexdump -C build/disk.img -s 1080 -n 2
# Should show: 53 ef
```

### Updating Disk Size

```bash
# Create 64 MB disk
make build/disk.img DISK_SIZE=64

# Rebuild kernel
make clean
make
```

## Documentation Reference

| Document                              | Purpose                          |
|---------------------------------------|----------------------------------|
| PERSISTENT_STORAGE_README.md          | Quick start guide                |
| persistent-storage-guide.md           | Technical reference              |
| persistent-storage-setup.md           | Setup instructions               |
| persistent-storage-architecture.md    | Architecture details             |
| persistent-storage-checklist.md       | Implementation checklist         |
| PERSISTENT_STORAGE_COMPLETE.md        | Overview and summary             |
| BUILD_AND_TEST.md                     | This file - build & test guide   |

## Support

### If Something Goes Wrong

1. **Check build log**: `make 2>&1 | tee build.log`
2. **Check boot log**: `make qemu 2>&1 | tee boot.log`
3. **Check disk format**: `hexdump -C build/disk.img | head`
4. **Recreate disk**: `rm build/disk.img && make`
5. **Clean build**: `make clean && make`

### Still Having Issues?

1. Review the documentation files
2. Check kernel boot messages carefully
3. Verify QEMU command includes virtio-blk device
4. Test with the provided `testpersist` program
5. Check that you're writing to `/mnt/` not `/tmp/`

## Success!

If all tests pass, you now have:
- ✅ Working persistent storage
- ✅ ext2 filesystem
- ✅ Block device driver
- ✅ Caching layer
- ✅ Complete syscall support

**Congratulations! Your OS can now save files permanently!** 🎉

## Next Steps

1. Start using persistent storage in your programs
2. Save configuration files to `/mnt/etc/`
3. Store logs in `/mnt/logs/`
4. Keep user data in `/mnt/home/`
5. Build applications that maintain state

Enjoy your persistent storage system!

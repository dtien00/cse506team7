# Persistent Storage - Quick Start

## Build and Run (3 commands)

```bash
make clean && make
make qemu
$ testpersist
```

## Use Persistent Storage (3 steps)

```bash
# 1. Write to /mnt/
$ echo "my data" > /mnt/file.txt

# 2. Flush to disk
$ sync

# 3. Verify after reboot
$ exit
make qemu
$ cat /mnt/file.txt  # Still there! ✅
```

## Key Points

- ✅ Write to `/mnt/` for persistent files
- ❌ Don't use `/tmp/` (lost on reboot)
- ✅ Call `sync()` before shutdown
- ✅ Files survive reboots

## API

```c
// Create file
int fd = open("/mnt/file.txt", O_CREAT | O_WRONLY, 0644);
write(fd, "data", 4);
close(fd);

// Read file
fd = open("/mnt/file.txt", O_RDONLY);
read(fd, buf, 100);
close(fd);

// Delete file
unlink("/mnt/file.txt");

// Create directory
mkdir("/mnt/dir", 0755);

// Flush to disk
sync();
```

## Filesystems

- `/` = tarfs (read-only boot files)
- `/tmp` = tmpfs (temporary, fast, non-persistent)
- `/mnt` = ext2 (**persistent storage**) ← Use this!

## Documentation

- **[README_PERSISTENT_STORAGE.md](README_PERSISTENT_STORAGE.md)** - Full quick start
- **[PERSISTENT_STORAGE_INDEX.md](PERSISTENT_STORAGE_INDEX.md)** - All docs
- **[BUILD_AND_TEST.md](BUILD_AND_TEST.md)** - Troubleshooting

## That's It!

```bash
make clean && make && make qemu
```

**Your OS now has persistent storage!** 🎉

# Tuneable Constants Reference

This file documents all the "knobs" you can turn to fix capacity/timeout issues.

---

## 1. Disk Image Size

**File:** `Makefile:39`
```makefile
DISK_SIZE ?= 16   # megabytes
```
Passed to `tools/mkfs.c` which calls `build/tools/mkfs <image> <size_mb>`.
Increasing this gives ext2 more blocks and inodes.

**Derived quantities in `tools/mkfs.c:144-147`:**
```c
uint32_t blocks = (mb * 1024 * 1024) / EXT2_BLOCK_SIZE;   // blocks = size_mb * 1024
uint32_t inodes = blocks / 4;                               // 1 inode per 4KB
if (inodes < 100) inodes = 100;
```
So `DISK_SIZE=64` → 65536 blocks, 16384 inodes.

**Errors this fixes:**
- `ext2: no free inodes available`
- `ext2: parent directory not found` (caused by failed earlier alloc that ran out of space)

---

## 2. Block Cache Size

**File:** `kernel/include/fs/block_cache.h:5`
```c
#define BLOCK_CACHE_SIZE 64   // Cache 64 blocks (64KB)
```
Number of 1KB blocks held in RAM before eviction. When all 64 slots are dirty and
a write-back fails (virtio timeout), `find_free_cache_block()` returns NULL and every
subsequent read/write also fails.

**Error this fixes:**
- `block_cache: no free cache blocks`
- `block_cache: evicting dirty block N` (symptom, not the root cause)

Raising to 256 or 512 reduces eviction pressure but does NOT fix the underlying
virtio timeout — it just delays it. Fix the timeout first.

---

## 3. VirtIO Spin-poll Timeout

**File:** `kernel/drivers/virtio_blk.c:239`
```c
int timeout = 10000000;
while (blk_dev.last_used_idx == blk_dev.used->idx && timeout-- > 0) {}
```
This appears in both `virtio_blk_rw()` (line 239) and `virtio_blk_flush()` (line 306).
On slow QEMU instances the device finishes after the counter expires.

**Error this fixes:**
- `virtio_blk: request timeout`

Raise to `100000000` (10×) or more. This is a spin-poll so it is CPU-bound; there
is no real cost on QEMU other than boot latency.

---

## 4. VirtIO Ring Size

**File:** `kernel/include/drivers/virtio.h:71`
```c
#define VIRTIO_RING_SIZE 8   // Number of queue entries
```
Depth of the virtqueue. Increasing it lets the driver queue more requests before
stalling. Not directly related to the timeout errors but relevant if you later
enable async I/O.

---

## Summary — Quick Fix Table

| Symptom | Knob | Current | Suggested |
|---------|------|---------|-----------|
| `virtio_blk: request timeout` | `timeout` in `virtio_blk.c:239,306` | 10 000 000 | 100 000 000 |
| `block_cache: no free cache blocks` | `BLOCK_CACHE_SIZE` in `block_cache.h:5` | 64 | 256 |
| `ext2: no free inodes available` | `DISK_SIZE` in `Makefile:39` | 16 MB | 64 MB |
| `ext2: parent directory not found` | (same — inodes/blocks ran out) | — | increase disk size |

**Root cause chain in your log:**
1. virtio timeout → write-back fails → dirty block stays in cache
2. Cache fills with unflushable dirty blocks → `no free cache blocks`
3. ext2 can't allocate new blocks/inodes → mkdir/create fail
4. tarfs population fails → `/bin`, `/etc`, etc. never exist

Fix (1) first. Everything else cascades from it.

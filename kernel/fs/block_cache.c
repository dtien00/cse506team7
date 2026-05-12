#include "kernel/types.h"
#include "fs/block_cache.h"
#include "drivers/virtio.h"
#include "kernel/printk.h"
#include "mm/kmalloc.h"
#include "lib/string.h"
#include "arch/riscv.h"
#include "trap/trap.h"

/* Save SIE bit and disable interrupts; returns old sstatus for restore. */
static inline uint64_t intr_lock(void) {
    uint64_t old = read_csr(sstatus);
    interrupts_off();
    return old;
}

/* Restore SIE from saved sstatus (re-enables only if caller had them on). */
static inline void intr_unlock(uint64_t saved) {
    if (saved & SSTATUS_SIE)
        interrupts_on();
}


static block_cache_t cache;
static int cache_initialized = 0;
static void (*race_hook)(uint32_t) = NULL;

void block_cache_debug_set_race_hook(void (*fn)(uint32_t block_num)) {
    race_hook = fn;
}

// Initialize block cache
void block_cache_init(void) {
    printk("block_cache: initializing\n");

    memset(&cache, 0, sizeof(block_cache_t));

    // Allocate data buffers for each cache block
    for (int i = 0; i < BLOCK_CACHE_SIZE; i++) {
        cache.blocks[i].data = (uint8_t *)kmalloc(BLOCK_SIZE);
        if (!cache.blocks[i].data) {
            printk("block_cache: failed to allocate block %d\n", i);
            // Free previously allocated blocks
            for (int j = 0; j < i; j++) {
                kfree(cache.blocks[j].data);
            }
            return;
        }
        cache.blocks[i].valid = 0;
        cache.blocks[i].dirty = 0;
        cache.blocks[i].in_flight = 0;
        cache.blocks[i].block_num = 0;
        cache.blocks[i].last_access = 0;
    }

    cache.access_counter = 0;
    cache_initialized = 1;

    printk("block_cache: initialized with %d blocks\n", BLOCK_CACHE_SIZE);
}

// Find a cache block for the given block number
static cached_block_t *find_cache_block(uint32_t block_num) {
    for (int i = 0; i < BLOCK_CACHE_SIZE; i++) {
        // Skip in_flight slots: the slot is being evicted (its old data is being
        // written to disk).  Treating it as a miss forces concurrent readers and
        // writers to a fresh slot, preventing them from overwriting cb->data
        // while virtio_blk_write is still DMA-ing the old version to disk.
        if (cache.blocks[i].valid && !cache.blocks[i].in_flight &&
            cache.blocks[i].block_num == block_num) {
            return &cache.blocks[i];
        }
    }
    return NULL;
}

// Find an empty or LRU slot. If the LRU slot is dirty, write it back first
// (with interrupts enabled so the virtio IRQ can fire).
// Returns the slot with valid=0, dirty=0, in_flight=1 — caller owns it.
// Must be called with interrupts ENABLED. Caller must clear in_flight when done.
static cached_block_t *find_free_cache_block(void) {
    // First, try to find an invalid (empty) block that isn't in-flight
    for (int i = 0; i < BLOCK_CACHE_SIZE; i++) {
        if (!cache.blocks[i].valid && !cache.blocks[i].in_flight) {
            cache.blocks[i].in_flight = 1;
            return &cache.blocks[i];
        }
    }

    // All blocks are valid (or in-flight); find LRU non-in-flight slot
    cached_block_t *lru = NULL;
    for (int i = 0; i < BLOCK_CACHE_SIZE; i++) {
        if (cache.blocks[i].in_flight)
            continue;
        if (!lru || cache.blocks[i].last_access < lru->last_access)
            lru = &cache.blocks[i];
    }

    if (!lru) {
        printk("block_cache: all slots in-flight\n");
        return NULL;
    }

    lru->in_flight = 1;

    if (lru->dirty) {
        printk("block_cache: evicting dirty block %u\n", lru->block_num);
        uint64_t sector = lru->block_num * 2;
        // Interrupts must be ON before calling virtio (IRQ-driven sleep).
        // Callers of find_free_cache_block must ensure interrupts are enabled.
        if (virtio_blk_write(sector, lru->data, 2) != 0) {
            printk("block_cache: failed to write back block %u\n", lru->block_num);
            lru->in_flight = 0;
            return NULL;
        }
        lru->dirty = 0;
    }

    lru->valid = 0;
    return lru;
}

// Read a block from cache or disk
int block_cache_read(uint32_t block_num, void *buf) {
    if (!cache_initialized) {
        printk("block_cache: not initialized\n");
        return -1;
    }

    uint64_t s = intr_lock();

    cached_block_t *cb = find_cache_block(block_num);
    if (cb) {
        cb->last_access = ++cache.access_counter;
        memcpy(buf, cb->data, BLOCK_SIZE);
        intr_unlock(s);
        return 0;
    }

    // Must release interrupt lock before find_free_cache_block: it may
    // write back a dirty block via virtio (IRQ-driven sleep requires IRQs on).
    intr_unlock(s);

    // find_free_cache_block returns slot with in_flight=1 to prevent Race 2
    // (two concurrent misses grabbing the same LRU slot).
    cb = find_free_cache_block();
    if (!cb) {
        printk("block_cache: no free cache blocks\n");
        return -1;
    }

    // Fire test hook here: interrupts ON, lock dropped, slot reserved with
    // in_flight=1.  A concurrent caller would insert block_num right here.
    if (race_hook) race_hook(block_num);

    // Recheck: another thread may have loaded block_num during our unlock window.
    s = intr_lock();
    cached_block_t *existing = find_cache_block(block_num);
    if (existing) {
        // Race 1 resolved: someone else populated it; release our reserved slot.
        cb->in_flight = 0;
        existing->last_access = ++cache.access_counter;
        memcpy(buf, existing->data, BLOCK_SIZE);
        intr_unlock(s);
        return 0;
    }
    // Claim the slot under lock before dropping again for I/O.
    cb->block_num = block_num;
    cb->valid = 0;
    cb->dirty = 0;
    intr_unlock(s);

    // Read from disk (1 block = 2 sectors for 1KB blocks); IRQ must be enabled.
    uint64_t sector = block_num * 2;
    if (virtio_blk_read(sector, cb->data, 2) != 0) {
        printk("block_cache: failed to read block %u\n", block_num);
        cb->in_flight = 0;
        return -1;
    }

    // Post-I/O recheck: a concurrent block_cache_write may have populated
    // block_num into a fresh slot while we slept in virtio_blk_read.  If so,
    // their data is newer (they wrote it after we started reading the old disk
    // version); use their slot and discard ours to avoid two valid slots
    // diverging for the same block number.
    s = intr_lock();
    existing = find_cache_block(block_num);
    if (existing) {
        cb->in_flight = 0;   // free our slot (valid still 0)
        existing->last_access = ++cache.access_counter;
        memcpy(buf, existing->data, BLOCK_SIZE);
        intr_unlock(s);
        return 0;
    }
    cb->valid = 1;
    cb->in_flight = 0;
    cb->last_access = ++cache.access_counter;
    memcpy(buf, cb->data, BLOCK_SIZE);
    intr_unlock(s);
    return 0;
}

// Write a block to cache (write-back on eviction or explicit flush)
int block_cache_write(uint32_t block_num, const void *buf) {
    if (!cache_initialized) {
        printk("block_cache: not initialized\n");
        return -1;
    }

    uint64_t s = intr_lock();

    cached_block_t *cb = find_cache_block(block_num);

    if (!cb) {
        // find_free_cache_block may write back a dirty block with interrupts on.
        // Release lock first so virtio_blk_write can sleep on IRQ.
        intr_unlock(s);
        // find_free_cache_block returns slot with in_flight=1 to prevent Race 2.
        cb = find_free_cache_block();
        if (!cb) {
            printk("block_cache: no free cache blocks\n");
            return -1;
        }
        // Fire test hook here: same window as read path.
        if (race_hook) race_hook(block_num);

        // Recheck: another thread may have loaded block_num during our unlock window.
        s = intr_lock();
        cached_block_t *existing = find_cache_block(block_num);
        if (existing) {
            // Race 1 resolved: use existing slot, release reserved one.
            cb->in_flight = 0;
            cb = existing;
            // existing is not in_flight (find_cache_block skips in_flight slots)
        } else {
            cb->block_num = block_num;
            cb->valid = 0;   // keep invalid until data is written below
        }
    }

    // Write data into slot while holding the lock, then mark valid + clear
    // in_flight atomically so no concurrent caller sees valid=1 with stale data.
    memcpy(cb->data, buf, BLOCK_SIZE);
    cb->valid = 1;
    cb->in_flight = 0;
    cb->dirty = 1;
    cb->last_access = ++cache.access_counter;

    intr_unlock(s);
    return 0;
}

int block_cache_flush_block(uint32_t block_num) {
    if (!cache_initialized) return -1;

    uint64_t s = intr_lock();
    cached_block_t *cb = find_cache_block(block_num);
    if (!cb || !cb->dirty) {
        intr_unlock(s);
        return 0;
    }

    // Snapshot the data into a local buffer while holding the lock.
    // This prevents a concurrent find_free_cache_block from evicting the slot
    // (reusing cb->data for a different block) mid-DMA, which would write the
    // wrong block's data to disk and corrupt cb->dirty for the wrong block.
    uint8_t *snap = (uint8_t *)kmalloc(BLOCK_SIZE);
    if (!snap) {
        intr_unlock(s);
        return -1;
    }
    memcpy(snap, cb->data, BLOCK_SIZE);

    // Clear dirty optimistically before dropping the lock.  Any concurrent
    // block_cache_write that fires after this point sets dirty=1 again and
    // will be picked up by the next flush — so we never lose a write.
    cb->dirty = 0;
    intr_unlock(s);

    uint64_t sector = block_num * 2;
    if (virtio_blk_write(sector, snap, 2) != 0) {
        printk("block_cache: failed to flush block %d\n", block_num);
        // Re-mark dirty so the next flush retries.  Re-check that the slot
        // still belongs to block_num (it could have been evicted and reused).
        s = intr_lock();
        cb = find_cache_block(block_num);
        if (cb)
            cb->dirty = 1;
        intr_unlock(s);
        kfree(snap);
        return -1;
    }

    kfree(snap);
    return 0;
}

/* Test-only: count valid slots claiming block_num. >1 ⇒ duplicate-slot bug. */
int block_cache_debug_count_block(uint32_t block_num) {
    if (!cache_initialized) return -1;
    int n = 0;
    uint64_t s = intr_lock();
    for (int i = 0; i < BLOCK_CACHE_SIZE; i++) {
        if (cache.blocks[i].valid && cache.blocks[i].block_num == block_num)
            n++;
    }
    intr_unlock(s);
    return n;
}

/* Test-only: reserve an empty slot for block_num as if a concurrent caller
 * had populated it during another caller's unlocked virtio window. */
int block_cache_debug_reserve_slot(uint32_t block_num) {
    if (!cache_initialized) return -1;
    uint64_t s = intr_lock();
    for (int i = 0; i < BLOCK_CACHE_SIZE; i++) {
        if (!cache.blocks[i].valid) {
            cache.blocks[i].block_num = block_num;
            cache.blocks[i].valid = 1;
            cache.blocks[i].dirty = 0;
            cache.blocks[i].last_access = ++cache.access_counter;
            intr_unlock(s);
            return 0;
        }
    }
    intr_unlock(s);
    return -1;
}

int block_cache_flush(void) {
    if (!cache_initialized) return -1;

    int flushed = 0;
    for (int i = 0; i < BLOCK_CACHE_SIZE; i++) {
        if (cache.blocks[i].valid && cache.blocks[i].dirty) {
            if (block_cache_flush_block(cache.blocks[i].block_num) == 0)
                flushed++;
        }
    }

    // printk("block_cache: flushed %d dirty blocks\n", flushed);
    return 0;
}

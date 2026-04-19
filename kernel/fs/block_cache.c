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
        if (cache.blocks[i].valid && cache.blocks[i].block_num == block_num) {
            return &cache.blocks[i];
        }
    }
    return NULL;
}

// Find an empty or least recently used cache block
static cached_block_t *find_free_cache_block(void) {
    // First, try to find an invalid (empty) block
    for (int i = 0; i < BLOCK_CACHE_SIZE; i++) {
        if (!cache.blocks[i].valid) {
            return &cache.blocks[i];
        }
    }
    
    // All blocks are valid, find LRU (least recently used)
    cached_block_t *lru = &cache.blocks[0];
    for (int i = 1; i < BLOCK_CACHE_SIZE; i++) {
        if (cache.blocks[i].last_access < lru->last_access) {
            lru = &cache.blocks[i];
        }
    }
    
    if (lru->dirty) {
        printk("block_cache: evicting dirty block %u\n", lru->block_num);
        uint64_t sector = lru->block_num * 2;
        if (virtio_blk_write(sector, lru->data, 2) != 0) {
            printk("block_cache: failed to write back block %u\n", lru->block_num);
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

    cb = find_free_cache_block();
    if (!cb) {
        printk("block_cache: no free cache blocks\n");
        intr_unlock(s);
        return -1;
    }

    // Read from disk (1 block = 2 sectors for 1KB blocks)
    uint64_t sector = block_num * 2;
    if (virtio_blk_read(sector, cb->data, 2) != 0) {
        printk("block_cache: failed to read block %u\n", block_num);
        intr_unlock(s);
        return -1;
    }

    cb->block_num = block_num;
    cb->valid = 1;
    cb->dirty = 0;
    cb->last_access = ++cache.access_counter;

    memcpy(buf, cb->data, BLOCK_SIZE);
    intr_unlock(s);
    return 0;
}

// Write a block to cache (write-back)
int block_cache_write(uint32_t block_num, const void *buf) {
    if (!cache_initialized) {
        printk("block_cache: not initialized\n");
        return -1;
    }

    uint64_t s = intr_lock();

    cached_block_t *cb = find_cache_block(block_num);

    if (!cb) {
        cb = find_free_cache_block();
        if (!cb) {
            printk("block_cache: no free cache blocks\n");
            intr_unlock(s);
            return -1;
        }
        cb->block_num = block_num;
        cb->valid = 1;
    }

    memcpy(cb->data, buf, BLOCK_SIZE);
    cb->dirty = 1;
    cb->last_access = ++cache.access_counter;

    intr_unlock(s);
    return 0;
}

// Flush a specific block to disk (caller must hold intr lock or call externally)
static int flush_block_locked(uint32_t block_num) {
    cached_block_t *cb = find_cache_block(block_num);
    if (!cb || !cb->dirty)
        return 0;

    uint64_t sector = block_num * 2;
    if (virtio_blk_write(sector, cb->data, 2) != 0) {
        printk("block_cache: failed to flush block %d\n", block_num);
        return -1;
    }
    cb->dirty = 0;
    return 0;
}

int block_cache_flush_block(uint32_t block_num) {
    if (!cache_initialized) return -1;
    uint64_t s = intr_lock();
    int ret = flush_block_locked(block_num);
    intr_unlock(s);
    return ret;
}

int block_cache_flush(void) {
    if (!cache_initialized) return -1;

    uint64_t s = intr_lock();
    int flushed = 0;
    for (int i = 0; i < BLOCK_CACHE_SIZE; i++) {
        if (cache.blocks[i].valid && cache.blocks[i].dirty) {
            if (flush_block_locked(cache.blocks[i].block_num) == 0)
                flushed++;
        }
    }
    intr_unlock(s);

    printk("block_cache: flushed %d dirty blocks\n", flushed);
    return 0;
}

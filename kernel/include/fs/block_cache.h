#pragma once
#include "kernel/types.h"

#define BLOCK_SIZE 1024
#define BLOCK_CACHE_SIZE 256  // Cache 256 blocks (256KB)

// Cached block structure
typedef struct cached_block {
    uint32_t block_num;         // Block number
    uint8_t *data;              // Block data (BLOCK_SIZE bytes)
    int dirty;                  // 1 if modified, 0 if clean
    int valid;                  // 1 if contains valid data
    int in_flight;              // 1 while I/O in progress (lock dropped); skip for eviction/free
    uint64_t last_access;       // For LRU replacement
    struct cached_block *next;  // For hash table chaining
} cached_block_t;

// Block cache state
typedef struct block_cache {
    cached_block_t blocks[BLOCK_CACHE_SIZE];
    uint64_t access_counter;    // Incremented on each access
} block_cache_t;

// Function declarations
void block_cache_init(void);
int block_cache_read(uint32_t block_num, void *buf);
int block_cache_write(uint32_t block_num, const void *buf);
int block_cache_flush(void);
int block_cache_flush_block(uint32_t block_num);

/* Test-only introspection: count valid slots claiming block_num.
 * Should be 0 or 1; >1 indicates duplicate-slot race bug. */
int block_cache_debug_count_block(uint32_t block_num);
/* Test-only: simulate the unlocked-window race by reserving a free slot
 * for block_num with valid=1 (mimics what a concurrent caller would do
 * after the first caller dropped intr_lock).  Returns 0 on success. */
int block_cache_debug_reserve_slot(uint32_t block_num);
/* Test-only: register a hook called with interrupts ON, between the
 * unlock-for-virtio and the re-lock/recheck in block_cache_read/write.
 * The hook receives the block_num being fetched.  Set to NULL to disable.
 * This is the precise window where the real race occurs. */
void block_cache_debug_set_race_hook(void (*fn)(uint32_t block_num));

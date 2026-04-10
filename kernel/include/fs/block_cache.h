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

#include "stdlib.h"
#include "sys/mman.h"
#include "string.h"

typedef struct block {
    size_t size;
    struct block* next;
    int free;
    int is_large;
} block_t;

#define BLOCK_SIZE sizeof(block_t)
#define MMAP_THRESHOLD 4096
#define ARENA_SIZE (64 * 1024)

static block_t* head = NULL;
static void* arena_start = NULL;
static void* arena_end = NULL;
static void* arena_current = NULL;

static block_t* find_free_block(block_t** last, size_t size) {
    block_t* current = head;
    while (current && !(current->free && current->size >= size && !current->is_large)) {
        *last = current;
        current = current->next;
    }
    return current;
}

static block_t* request_space(block_t* last, size_t size) {
    block_t* block;
    
    if (size >= MMAP_THRESHOLD) {
        size_t map_size = ((size + BLOCK_SIZE + 4095) / 4096) * 4096;
        block = mmap(NULL, map_size, PROT_READ | PROT_WRITE, 
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (block == MAP_FAILED) {
            return NULL;
        }
        block->size = size;
        block->next = NULL;
        block->free = 0;
        block->is_large = 1;
    } else {
        if (arena_start == NULL || 
            (char*)arena_current + size + BLOCK_SIZE > (char*)arena_end) {
            
            void* new_arena = mmap(NULL, ARENA_SIZE, PROT_READ | PROT_WRITE,
                                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (new_arena == MAP_FAILED) {
                return NULL;
            }
            
            arena_start = new_arena;
            arena_end = (char*)new_arena + ARENA_SIZE;
            arena_current = new_arena;
        }
        
        block = (block_t*)arena_current;
        arena_current = (char*)arena_current + size + BLOCK_SIZE;
        
        block->size = size;
        block->next = NULL;
        block->free = 0;
        block->is_large = 0;
    }
    
    if (last) {
        last->next = block;
    }
    
    return block;
}

void* malloc(size_t size) {
    if (size == 0) {
        return NULL;
    }
    
    block_t* block;
    if (!head) {
        block = request_space(NULL, size);
        if (!block) {
            return NULL;
        }
        head = block;
    } else {
        block_t* last = head;
        block = find_free_block(&last, size);
        if (!block) {
            block = request_space(last, size);
            if (!block) {
                return NULL;
            }
        } else {
            block->free = 0;
        }
    }
    
    return (block + 1);
}

void free(void* ptr) {
    if (!ptr) {
        return;
    }
    
    block_t* block = (block_t*)ptr - 1;
    
    if (block->is_large) {
        size_t map_size = ((block->size + BLOCK_SIZE + 4095) / 4096) * 4096;
        munmap(block, map_size);
    } else {
        block->free = 1;
    }
}

void* calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    void* ptr = malloc(total);
    if (ptr) {
        memset(ptr, 0, total);
    }
    return ptr;
}

void* realloc(void* ptr, size_t size) {
    if (!ptr) {
        return malloc(size);
    }
    
    if (size == 0) {
        free(ptr);
        return NULL;
    }
    
    block_t* block = (block_t*)ptr - 1;
    if (block->size >= size) {
        return ptr;
    }
    
    void* new_ptr = malloc(size);
    if (!new_ptr) {
        return NULL;
    }
    
    memcpy(new_ptr, ptr, block->size);
    free(ptr);
    return new_ptr;
}

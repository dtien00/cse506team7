#include "mm/kmalloc.h"
#include "mm/pmm.h"
#include "arch/riscv.h"
#include "kernel/printk.h"

// Simple first-fit heap allocator
// Each block has a header with size and free flag

typedef struct block_header {
  size_t size;              // Size of the block (excluding header)
  int free;                 // 1 if free, 0 if allocated
  struct block_header *next;
} block_header_t;

#define BLOCK_HEADER_SIZE sizeof(block_header_t)
#define ALIGN(size) (((size) + 7) & ~7)  // 8-byte alignment

static block_header_t *heap_start = NULL;
static size_t total_allocated = 0;
static size_t total_freed = 0;

void kmalloc_init(void) {
  // Allocate initial heap (16 pages = 64KB)
  size_t initial_pages = 16;
  void *heap = pmm_alloc_pages(initial_pages);
  
  if (heap == NULL) {
    printk("kmalloc: Failed to allocate initial heap\n");
    return;
  }
  
  heap_start = (block_header_t *)heap;
  heap_start->size = initial_pages * PGSIZE - BLOCK_HEADER_SIZE;
  heap_start->free = 1;
  heap_start->next = NULL;
  
  printk("kmalloc: Initialized with %lu KB heap\n", 
         (initial_pages * PGSIZE) / 1024);
}

void *kmalloc(size_t size) {
  if (size == 0) {
    return NULL;
  }
  
  if (heap_start == NULL) {
    printk("kmalloc: Heap not initialized\n");
    return NULL;
  }
  
  // Align size
  size = ALIGN(size);
  
  // Find first free block that fits (first-fit)
  block_header_t *current = heap_start;
  block_header_t *prev = NULL;
  
  while (current != NULL) {
    if (current->free && current->size >= size) {
      // Found a suitable block
      
      // Split block if it's large enough
      if (current->size >= size + BLOCK_HEADER_SIZE + 64) {
        block_header_t *new_block = (block_header_t *)((char *)current + BLOCK_HEADER_SIZE + size);
        new_block->size = current->size - size - BLOCK_HEADER_SIZE;
        new_block->free = 1;
        new_block->next = current->next;
        
        current->size = size;
        current->next = new_block;
      }
      
      current->free = 0;
      total_allocated += current->size;
      
      return (void *)((char *)current + BLOCK_HEADER_SIZE);
    }
    
    prev = current;
    current = current->next;
  }
  
  // No suitable block found, allocate more memory
  size_t pages_needed = (size + BLOCK_HEADER_SIZE + PGSIZE - 1) / PGSIZE;
  void *new_mem = pmm_alloc_pages(pages_needed);
  
  if (new_mem == NULL) {
    printk("kmalloc: Out of memory (requested %lu bytes)\n", size);
    return NULL;
  }
  
  // Create new block
  block_header_t *new_block = (block_header_t *)new_mem;
  new_block->size = pages_needed * PGSIZE - BLOCK_HEADER_SIZE;
  new_block->free = 0;
  new_block->next = NULL;
  
  // Link to end of list
  if (prev != NULL) {
    prev->next = new_block;
  }
  
  total_allocated += size;
  
  // Split if necessary
  if (new_block->size >= size + BLOCK_HEADER_SIZE + 64) {
    block_header_t *split_block = (block_header_t *)((char *)new_block + BLOCK_HEADER_SIZE + size);
    split_block->size = new_block->size - size - BLOCK_HEADER_SIZE;
    split_block->free = 1;
    split_block->next = new_block->next;
    
    new_block->size = size;
    new_block->next = split_block;
  }
  
  return (void *)((char *)new_block + BLOCK_HEADER_SIZE);
}

void *kzalloc(size_t size) {
  void *ptr = kmalloc(size);
  if (ptr != NULL) {
    // Zero out the memory
    char *p = (char *)ptr;
    for (size_t i = 0; i < size; i++) {
      p[i] = 0;
    }
  }
  return ptr;
}

void kfree(void *ptr) {
  if (ptr == NULL) {
    return;
  }
  
  // Get block header
  block_header_t *block = (block_header_t *)((char *)ptr - BLOCK_HEADER_SIZE);
  
  if (block->free) {
    printk("kmalloc: Double free detected at %p\n", ptr);
    return;
  }
  
  block->free = 1;
  total_freed += block->size;
  
  // Coalesce with next block if it's free
  if (block->next != NULL && block->next->free) {
    block->size += BLOCK_HEADER_SIZE + block->next->size;
    block->next = block->next->next;
  }
  
  // Coalesce with previous block if it's free
  block_header_t *current = heap_start;
  while (current != NULL && current->next != block) {
    current = current->next;
  }
  
  if (current != NULL && current->free) {
    current->size += BLOCK_HEADER_SIZE + block->size;
    current->next = block->next;
  }
}

void *krealloc(void *ptr, size_t size) {
  if (ptr == NULL) {
    return kmalloc(size);
  }
  
  if (size == 0) {
    kfree(ptr);
    return NULL;
  }
  
  // Get current block
  block_header_t *block = (block_header_t *)((char *)ptr - BLOCK_HEADER_SIZE);
  
  if (block->size >= size) {
    // Current block is large enough
    return ptr;
  }
  
  // Allocate new block and copy data
  void *new_ptr = kmalloc(size);
  if (new_ptr == NULL) {
    return NULL;
  }
  
  // Copy old data
  char *src = (char *)ptr;
  char *dst = (char *)new_ptr;
  for (size_t i = 0; i < block->size && i < size; i++) {
    dst[i] = src[i];
  }
  
  kfree(ptr);
  return new_ptr;
}

void kmalloc_stats(void) {
  printk("kmalloc statistics:\n");
  printk("  Total allocated: %lu bytes\n", total_allocated);
  printk("  Total freed: %lu bytes\n", total_freed);
  printk("  Currently used: %lu bytes\n", total_allocated - total_freed);
  
  // Walk the heap and count free/used blocks
  size_t free_blocks = 0;
  size_t used_blocks = 0;
  size_t free_bytes = 0;
  size_t used_bytes = 0;
  
  block_header_t *current = heap_start;
  while (current != NULL) {
    if (current->free) {
      free_blocks++;
      free_bytes += current->size;
    } else {
      used_blocks++;
      used_bytes += current->size;
    }
    current = current->next;
  }
  
  printk("  Free blocks: %lu (%lu bytes)\n", free_blocks, free_bytes);
  printk("  Used blocks: %lu (%lu bytes)\n", used_blocks, used_bytes);
}

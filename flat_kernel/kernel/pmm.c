#include "pmm.h"
#include "riscv.h"
#include "printk.h"

// Physical Memory Manager using bitmap allocation
// Each bit represents one 4KB page

static uint8_t *bitmap = NULL;
static uintptr_t mem_start_addr = 0;
static size_t total_pages = 0;
static size_t free_pages = 0;

// Helper: set a bit in the bitmap
static inline void bitmap_set(size_t bit) {
  bitmap[bit / 8] |= (1 << (bit % 8));
}

// Helper: clear a bit in the bitmap
static inline void bitmap_clear(size_t bit) {
  bitmap[bit / 8] &= ~(1 << (bit % 8));
}

// Helper: test if a bit is set
static inline int bitmap_test(size_t bit) {
  return (bitmap[bit / 8] & (1 << (bit % 8))) != 0;
}

void pmm_init(uintptr_t mem_start, size_t mem_size) {
  // Round up mem_start to page boundary
  mem_start_addr = PGROUNDUP(mem_start);
  
  // Calculate number of pages
  total_pages = mem_size / PGSIZE;
  
  // Bitmap size in bytes (1 bit per page)
  size_t bitmap_size = (total_pages + 7) / 8;
  
  // Place bitmap at the start of managed memory
  bitmap = (uint8_t *)mem_start_addr;
  
  // Mark bitmap pages as used
  size_t bitmap_pages = PGROUNDUP(bitmap_size) / PGSIZE;
  for (size_t i = 0; i < bitmap_pages; i++) {
    bitmap_set(i);
  }
  
  // Mark all other pages as free
  for (size_t i = bitmap_pages; i < total_pages; i++) {
    bitmap_clear(i);
  }
  
  free_pages = total_pages - bitmap_pages;
  
  printk("PMM: Initialized %d MB (%d pages, %d free)\n",
         mem_size / (1024 * 1024), total_pages, free_pages);
  printk("PMM: Bitmap at %p (%d bytes, %d pages)\n",
         mem_start_addr, bitmap_size, bitmap_pages);
}

void *pmm_alloc_page(void) {
  if (free_pages == 0) {
    return NULL;
  }
  
  // Find first free page
  for (size_t i = 0; i < total_pages; i++) {
    if (!bitmap_test(i)) {
      bitmap_set(i);
      free_pages--;
      return (void *)(mem_start_addr + i * PGSIZE);
    }
  }
  
  return NULL;
}

void pmm_free_page(void *page) {
  if (page == NULL) {
    return;
  }
  
  uintptr_t addr = (uintptr_t)page;
  
  // Validate address
  if (addr < mem_start_addr || addr >= mem_start_addr + total_pages * PGSIZE) {
    printk("PMM: Invalid free address %p\n", addr);
    return;
  }
  
  // Check alignment
  if (addr % PGSIZE != 0) {
    printk("PMM: Unaligned free address %p\n", addr);
    return;
  }
  
  size_t page_idx = (addr - mem_start_addr) / PGSIZE;
  
  // Check if already free
  if (!bitmap_test(page_idx)) {
    printk("PMM: Double free at %p\n", addr);
    return;
  }
  
  bitmap_clear(page_idx);
  free_pages++;
}

void *pmm_alloc_pages(size_t n) {
  if (n == 0 || free_pages < n) {
    return NULL;
  }
  
  // Find n contiguous free pages
  size_t start = 0;
  size_t count = 0;
  
  for (size_t i = 0; i < total_pages; i++) {
    if (!bitmap_test(i)) {
      if (count == 0) {
        start = i;
      }
      count++;
      if (count == n) {
        // Found n contiguous pages, allocate them
        for (size_t j = start; j < start + n; j++) {
          bitmap_set(j);
        }
        free_pages -= n;
        return (void *)(mem_start_addr + start * PGSIZE);
      }
    } else {
      count = 0;
    }
  }
  
  return NULL;
}

void pmm_free_pages(void *page, size_t n) {
  if (page == NULL || n == 0) {
    return;
  }
  
  uintptr_t addr = (uintptr_t)page;
  
  // Validate address
  if (addr < mem_start_addr || addr >= mem_start_addr + total_pages * PGSIZE) {
    printk("PMM: Invalid free address %p\n", addr);
    return;
  }
  
  // Check alignment
  if (addr % PGSIZE != 0) {
    printk("PMM: Unaligned free address %p\n", addr);
    return;
  }
  
  size_t page_idx = (addr - mem_start_addr) / PGSIZE;
  
  // Free all pages
  for (size_t i = 0; i < n; i++) {
    if (page_idx + i >= total_pages) {
      printk("PMM: Free range exceeds memory\n");
      return;
    }
    
    if (!bitmap_test(page_idx + i)) {
      printk("PMM: Double free in range at %p\n", addr + i * PGSIZE);
      continue;
    }
    
    bitmap_clear(page_idx + i);
    free_pages++;
  }
}

size_t pmm_free_memory(void) {
  return free_pages * PGSIZE;
}

size_t pmm_total_memory(void) {
  return total_pages * PGSIZE;
}

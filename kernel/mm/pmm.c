#include "mm/pmm.h"
#include "arch/riscv.h"
#include "kernel/printk.h"

// Physical Memory Manager using bitmap allocation
// Each bit represents one 4KB page

static uint8_t *bitmap = NULL;
static uint16_t *refcounts;
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

  // Bitmap: 1 bit per page, placed at start of managed memory
  size_t bitmap_size  = (total_pages + 7) / 8;
  size_t bitmap_pages = PGROUNDUP(bitmap_size) / PGSIZE;
  bitmap = (uint8_t *)mem_start_addr;

  // Refcount array: uint16_t per page, placed immediately after bitmap
  size_t refcount_size  = total_pages * sizeof(uint16_t);
  size_t refcount_pages = PGROUNDUP(refcount_size) / PGSIZE;
  refcounts = (uint16_t *)(mem_start_addr + bitmap_pages * PGSIZE);

  size_t overhead_pages = bitmap_pages + refcount_pages;

  // Mark overhead pages as used, rest as free
  for (size_t i = 0; i < overhead_pages; i++)
    bitmap_set(i);
  for (size_t i = overhead_pages; i < total_pages; i++)
    bitmap_clear(i);

  // Zero all refcounts
  for (size_t i = 0; i < total_pages; i++)
    refcounts[i] = 0;

  free_pages = total_pages - overhead_pages;

  printk("PMM: Initialized %d MB (%d pages, %d free)\n",
         mem_size / (1024 * 1024), total_pages, free_pages);
  printk("PMM: Bitmap at %p (%d bytes), refcounts at %p (%d bytes)\n",
         bitmap, bitmap_size, refcounts, refcount_size);
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
      refcounts[i] = 1;
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

  // Decrement refcount; only reclaim when the last reference drops
  if (--refcounts[page_idx] == 0) {
    bitmap_clear(page_idx);
    free_pages++;
  }
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
          refcounts[j] = 1;
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

    if (--refcounts[page_idx + i] == 0) {
      bitmap_clear(page_idx + i);
      free_pages++;
    }
  }
}

size_t pmm_free_memory(void) {
  return free_pages * PGSIZE;
}

size_t pmm_total_memory(void) {
  return total_pages * PGSIZE;
}

void pmm_incref(void *page) {
  if (!page) return;
  uintptr_t addr = (uintptr_t)page;
  if (addr < mem_start_addr || addr >= mem_start_addr + total_pages * PGSIZE) return;
  size_t idx = (addr - mem_start_addr) / PGSIZE;
  refcounts[idx]++;
}

void pmm_decref(void *page) {
  // pmm_free_page already does decref-and-conditionally-free
  pmm_free_page(page);
}

uint16_t pmm_refcount(void *page) {
  if (!page) return 0;
  uintptr_t addr = (uintptr_t)page;
  if (addr < mem_start_addr || addr >= mem_start_addr + total_pages * PGSIZE) return 0;
  size_t idx = (addr - mem_start_addr) / PGSIZE;
  return refcounts[idx];
}

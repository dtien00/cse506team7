#pragma once

#include "kernel/types.h"

// Physical Memory Manager (PMM)
// Manages physical memory pages (4KB each) using a bitmap allocator

void  pmm_init(uintptr_t mem_start, size_t mem_size);
void *pmm_alloc_page(void);      // Allocate one 4KB page, returns physical address or NULL
void  pmm_free_page(void *page); // Free a 4KB page
void *pmm_alloc_pages(size_t n); // Allocate n contiguous pages
void  pmm_free_pages(void *page, size_t n); // Free n contiguous pages
size_t pmm_free_memory(void);    // Get amount of free memory in bytes
size_t pmm_total_memory(void);   // Get total managed memory in bytes

#pragma once
#include "types.h"

// Virtual Memory Manager (VMM)
// Manages Sv39 page tables for RISC-V

typedef uint64_t *pagetable_t;  // Pointer to page table (512 PTEs)

// Initialize VMM and set up kernel page table
void vmm_init(void);

// Create a new empty page table
pagetable_t vmm_create_pagetable(void);

// Free a page table and all its pages
void vmm_free_pagetable(pagetable_t pagetable);

// Map a virtual address to a physical address
// Returns 0 on success, -1 on failure
int vmm_map(pagetable_t pagetable, uintptr_t va, uintptr_t pa, uint64_t perm);

// Unmap a virtual address
void vmm_unmap(pagetable_t pagetable, uintptr_t va);

// Walk the page table to find PTE for a virtual address
// If alloc is true, allocate missing page tables
uint64_t *vmm_walk(pagetable_t pagetable, uintptr_t va, int alloc);

// Translate virtual address to physical address
// Returns 0 if not mapped
uintptr_t vmm_translate(pagetable_t pagetable, uintptr_t va);

// Get the kernel page table
pagetable_t vmm_kernel_pagetable(void);

// Switch to a page table
void vmm_switch(pagetable_t pagetable);

// Map a range of pages
int vmm_map_range(pagetable_t pagetable, uintptr_t va, uintptr_t pa, size_t size, uint64_t perm);

// Unmap a range of pages
void vmm_unmap_range(pagetable_t pagetable, uintptr_t va, size_t size);

// Copy pagetable
pagetable_t vmm_copy_pagetable(pagetable_t parent)
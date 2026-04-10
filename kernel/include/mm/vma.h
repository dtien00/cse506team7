#pragma once

#include "kernel/types.h"

// Virtual Memory Area (VMA) - tracks memory regions in a process's address space
// Each VMA represents a contiguous region of virtual memory with specific properties

// VMA types
#define VMA_TYPE_ANON    0x01  // Anonymous memory (heap, stack)
#define VMA_TYPE_FILE    0x02  // File-backed mapping
#define VMA_TYPE_STACK   0x04  // Stack region
#define VMA_TYPE_HEAP    0x08  // Heap region (brk)

// VMA flags (match mmap flags)
#define VMA_PROT_READ    0x01  // Pages may be read
#define VMA_PROT_WRITE   0x02  // Pages may be written
#define VMA_PROT_EXEC    0x04  // Pages may be executed
#define VMA_SHARED       0x10  // Share changes
#define VMA_PRIVATE      0x20  // Changes are private
#define VMA_FIXED        0x40  // Fixed address mapping
#define VMA_GROWSDOWN    0x80  // Stack-like segment (grows down)

struct vm_area {
    uintptr_t start;           // Start virtual address (inclusive)
    uintptr_t end;             // End virtual address (exclusive)
    uint32_t prot;             // Protection flags (VMA_PROT_*)
    uint32_t flags;            // VMA flags (VMA_*)
    uint32_t type;             // VMA type (VMA_TYPE_*)

    // File-backed mapping info (for future use)
    void *file;                // File descriptor or inode
    uint64_t offset;           // Offset in file

    struct vm_area *next;      // Next VMA in list
};

// VMA management functions

// Initialize VMA for a process (called during process creation)
void vma_init(struct vm_area **vma_list);

// Find VMA containing a virtual address
struct vm_area *vma_find(struct vm_area *vma_list, uintptr_t addr);

// Find VMA that overlaps with [start, end)
struct vm_area *vma_find_overlap(struct vm_area *vma_list, uintptr_t start, uintptr_t end);

// Insert a new VMA into the list (sorted by start address)
// Returns 0 on success, -1 on failure (overlap detected)
int vma_insert(struct vm_area **vma_list, struct vm_area *new_vma);

// Remove a VMA from the list
void vma_remove(struct vm_area **vma_list, struct vm_area *vma);

// Create a new VMA
struct vm_area *vma_create(uintptr_t start, uintptr_t end, uint32_t prot, uint32_t flags, uint32_t type);

// Free a VMA
void vma_free(struct vm_area *vma);

// Free all VMAs in a list
void vma_free_all(struct vm_area *vma_list);

// Find a free region in the address space
// Returns start address of free region, or 0 if not found
uintptr_t vma_find_free_region(struct vm_area *vma_list, size_t size, uintptr_t hint, uintptr_t min_addr, uintptr_t max_addr);

// Split a VMA at a given address
// Returns 0 on success, -1 on failure
int vma_split(struct vm_area **vma_list, struct vm_area *vma, uintptr_t addr);

// Merge adjacent VMAs with compatible properties
void vma_merge(struct vm_area **vma_list);

// Copy all VMAs from one list to another (for fork)
int vma_copy_all(struct vm_area **dest_list, struct vm_area *src_list);

// Debug: print all VMAs
void vma_dump(struct vm_area *vma_list);

// Find a GROWSDOWN VMA whose current start is strictly above addr.
// Returns the VMA if addr is below the current stack bottom, NULL otherwise.
// The caller is responsible for enforcing maximum stack size limits.
struct vm_area *vma_find_growsdown(struct vm_area *vma_list, uintptr_t addr);
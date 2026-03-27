# VMM Integration Fix for Memory Allocation

## Problem Identified

The original implementation of `sys_mmap()`, `sys_munmap()`, `sys_brk()`, and `sys_sbrk()` was **bypassing the Virtual Memory Manager (VMM)** entirely:

1. **Direct physical memory allocation**: `sys_mmap()` was calling `pmm_alloc_page()` and returning physical addresses directly to userspace
2. **No virtual address space management**: No virtual-to-physical mapping was created
3. **No memory protection**: Page table entries with proper permissions (PTE_U, PTE_R, PTE_W) were not being set
4. **Security vulnerability**: Userspace could potentially access physical memory directly

## What Was Wrong

### Original `sys_mmap()` (BROKEN)
```c
void* mem = (void*)0;
for (size_t i = 0; i < pages; i++) {
    void* page = pmm_alloc_page();  // Allocates physical page
    if (i == 0) {
        mem = page;  // Returns PHYSICAL address to userspace!
    }
}
return mem;  // Physical address leaked to userspace
```

### Original `sys_brk()` / `sys_sbrk()` (BROKEN)
```c
// These were calling mmap() which doesn't exist in kernel space
program_break = mmap(NULL, 4096, ...);  // WRONG!
```

## The Fix

### Key Changes

1. **Added VMM integration**: Now properly uses `vmm_map()` to create virtual-to-physical mappings
2. **Virtual address allocation**: Allocates virtual addresses in user space (0x10000000 - 0x40000000)
3. **Per-process tracking**: Uses `current->sz` to track each process's heap size
4. **Proper permissions**: Sets PTE_U (user accessible) along with PTE_R/PTE_W based on protection flags
5. **Cleanup on failure**: Properly unmaps and frees pages if allocation fails partway through

### New `sys_mmap()` Flow

1. Get current process and its page table
2. Allocate virtual address in user heap region (or use MAP_FIXED address)
3. For each page:
   - Allocate physical page via PMM
   - Zero the physical page
   - Map virtual address → physical address in process's page table with proper permissions
4. Update process's `sz` field to track heap growth
5. Return **virtual address** to userspace

### New `sys_brk()` Flow

1. Get current process and its page table
2. Initialize heap start at USER_HEAP_START if needed
3. If growing heap:
   - Allocate physical pages
   - Map them to virtual addresses in the heap region
   - Update process's `sz`
4. If shrinking heap:
   - Unmap pages from page table
   - Free physical pages back to PMM

### New `sys_sbrk()` Flow

- Simple wrapper around `sys_brk()` that:
  - Returns old break value
  - Calls `sys_brk()` with new break = old break + increment

## Memory Layout

```
User Virtual Address Space:
0x00000000 - 0x10000000: [Reserved/Code/Data/Stack]
0x10000000 - 0x40000000: User Heap (managed by mmap/brk/sbrk)
0x40000000 - MAXVA:      [Reserved/Kernel]
```

## Benefits

1. **Address space isolation**: Each process has its own virtual address space
2. **Memory protection**: Page table permissions prevent unauthorized access
3. **Security**: Physical addresses are never exposed to userspace
4. **Proper VM semantics**: Follows standard Unix memory management model
5. **Copy-on-write ready**: Foundation for future COW implementation

## Testing Recommendations

1. Test `malloc()` in userspace programs
2. Verify that different processes get different virtual addresses for same allocations
3. Test large allocations (> 4KB) that use mmap path in libc
4. Test small allocations (< 4KB) that use arena path in libc
5. Verify page faults occur when accessing unmapped memory
6. Test `brk()` and `sbrk()` directly

## Notes

- The `current` global variable must be properly set when syscalls are invoked
- Each process must have its page table initialized before calling these syscalls
- The kernel still uses identity mapping, so it can access physical addresses directly when zeroing pages

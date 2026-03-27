# libc Additions Summary

## Problem
The original libc was missing critical functionality:
1. `stdlib.h` only had `exit()` - missing malloc, free, string conversion, NULL
2. Missing `sys/time.h` - POSIX requires time structures in sys/ subdirectory
3. No memory allocation mechanism (sbrk not implemented)

## Solution: Modern mmap-based Approach

Instead of implementing traditional `sbrk`, we use **mmap** for memory allocation (modern standard).

---

## Files Added/Modified

### Headers

#### 1. `libc/include/stdlib.h` (UPDATED)
Added complete stdlib declarations:
- **Memory allocation**: `malloc()`, `calloc()`, `realloc()`, `free()`
- **String conversion**: `atoi()`, `atol()`, `atoll()`, `strtol()`, `strtoul()`, `strtoll()`, `strtoull()`
- **Process control**: `_Exit()`, `abort()`, `atexit()`
- **Random numbers**: `rand()`, `srand()`
- **Math**: `abs()`, `labs()`, `llabs()`, `div()`, `ldiv()`, `lldiv()`
- **Environment**: `getenv()`, `setenv()`, `unsetenv()`
- **Search/sort**: `bsearch()`, `qsort()`
- **Constants**: `EXIT_SUCCESS`, `EXIT_FAILURE`, `RAND_MAX`, `NULL` (via stddef.h)

#### 2. `libc/include/sys/time.h` (NEW - POSIX REQUIRED)
POSIX programs expect time structures in `sys/time.h`:
- `struct timeval`, `struct timezone`, `struct itimerval`
- `gettimeofday()`, `settimeofday()`, `getitimer()`, `setitimer()`

#### 3. `libc/include/sys/select.h` (NEW - OPTIONAL)
For programs using select():
- `fd_set` type and macros (`FD_SET`, `FD_CLR`, `FD_ISSET`, `FD_ZERO`)
- `select()` function declaration

#### 4. `libc/include/unistd.h` (UPDATED)
Added:
- `void* sbrk(intptr_t increment)` - for legacy compatibility
- `int brk(void* addr)` - for legacy compatibility

---

### Implementation Files

#### 1. `libc/malloc.c` (NEW)
**Modern mmap-based allocator** with two strategies:
- **Small allocations** (<4KB): Uses arena allocation from 64KB mmap'd regions
- **Large allocations** (≥4KB): Direct mmap for each allocation
- Implements: `malloc()`, `free()`, `calloc()`, `realloc()`
- Features:
  - Free list for reusing small blocks
  - Automatic arena expansion
  - Large blocks are unmapped on free

#### 2. `libc/stdlib.c` (NEW)
Complete stdlib implementations:
- **String conversion**: Full strtol/strtoul family with base detection (hex, octal, decimal)
- **Math functions**: abs, labs, llabs, div, ldiv, lldiv
- **Random number generator**: Linear congruential generator (LCG)
- **Environment**: Stub implementations (return NULL/-1)
- **atexit**: Supports up to 32 exit handlers
- **qsort**: Quicksort implementation
- **bsearch**: Binary search implementation

#### 3. `libc/syscall.S` (NEW)
Assembly wrappers for system calls:
- `mmap` (syscall #14)
- `munmap` (syscall #15)
- `brk` (syscall #16)
- `sbrk` (syscall #10)

---

### Kernel Changes

#### 1. `kernel/include/trap/syscall.h` (UPDATED)
Added syscall numbers:
- `SYS_mmap` = 14
- `SYS_munmap` = 15
- `SYS_brk` = 16

Added function declarations:
- `void *sys_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)`
- `int sys_munmap(void *addr, size_t length)`
- `int sys_brk(void *addr)`

#### 2. `kernel/trap/syscall.c` (UPDATED)
Added syscall handlers:
- **`sys_mmap()`**: Allocates pages using `pmm_alloc_page()`, zeros them, returns pointer
  - Currently supports `MAP_ANONYMOUS` only
  - Rounds up to page boundaries
  - Returns `MAP_FAILED` on error
  
- **`sys_munmap()`**: Frees pages using `pmm_free_page()`
  - Handles multiple pages
  
- **`sys_brk()`**: Sets program break (simple implementation)
  
- **`sys_sbrk()`**: Increments program break using mmap
  - Maintains program break pointer
  - Uses mmap internally for allocation

---

## How It Works

### Memory Allocation Flow

1. **User program calls `malloc(1024)`**
   - libc malloc.c checks size
   - Size < 4KB → allocate from arena
   - If arena full/empty → call `mmap()` syscall to get 64KB arena
   - Return pointer to user

2. **User program calls `malloc(8192)`**
   - libc malloc.c checks size
   - Size ≥ 4KB → call `mmap()` syscall directly
   - Mark as "large allocation"
   - Return pointer to user

3. **User program calls `free(ptr)`**
   - libc malloc.c checks if large allocation
   - If large → call `munmap()` syscall
   - If small → mark block as free for reuse

### Syscall Flow

```
User Space (libc)          Kernel Space
─────────────────          ────────────
malloc()
  └─> mmap() in syscall.S
        └─> ecall with a7=14
              └─> syscall_handler()
                    └─> sys_mmap()
                          └─> pmm_alloc_page()
                          └─> zero memory
                          └─> return pointer
```

---

## Why mmap Instead of sbrk?

1. **Modern standard**: Linux and most Unix systems use mmap for malloc
2. **Better performance**: Can allocate/free anywhere in address space
3. **Simpler**: No need to track contiguous heap region
4. **More flexible**: Can specify permissions, flags, file backing
5. **Already defined**: You have `sys/mman.h` header ready

The `sbrk()` implementation is provided for compatibility but uses mmap internally.

---

## Testing

To test the new libc:

```c
#include <stdlib.h>
#include <stdio.h>

int main() {
    // Test malloc
    char* str = malloc(100);
    if (str) {
        printf("malloc succeeded\n");
        free(str);
    }
    
    // Test string conversion
    int num = atoi("42");
    printf("atoi(\"42\") = %d\n", num);
    
    // Test calloc
    int* arr = calloc(10, sizeof(int));
    if (arr) {
        printf("calloc succeeded\n");
        free(arr);
    }
    
    return EXIT_SUCCESS;
}
```

---

## Next Steps (If Needed)

If you want to improve the implementation:

1. **Process-aware mmap**: Track mappings per process (requires process structure)
2. **Virtual memory integration**: Map into process page tables instead of physical memory
3. **File-backed mmap**: Support mapping files (requires VFS)
4. **Memory protection**: Implement PROT_READ/WRITE/EXEC permissions
5. **Shared memory**: Implement MAP_SHARED flag

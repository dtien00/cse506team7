# Files Created for SBUnix Project

## Summary

This document lists all files created to establish the OS kernel foundation.

## Kernel Headers (kernel/include/)

### 1. types.h
- **Lines:** ~20
- **Purpose:** Basic type definitions
- **Contents:**
  - Integer types (uint8_t through uint64_t)
  - Size types (size_t, ssize_t)
  - Pointer types (uintptr_t, intptr_t)
  - Boolean type
  - NULL definition

### 2. riscv.h
- **Lines:** ~200
- **Purpose:** RISC-V architecture definitions
- **Contents:**
  - CSR register definitions (sstatus, sie, sip, etc.)
  - Inline functions to read/write CSRs
  - Page table entry flags (PTE_V, PTE_R, PTE_W, etc.)
  - Memory layout constants
  - Page size and alignment macros
  - SATP register manipulation
  - Interrupt enable/disable functions

### 3. physical_memory_mapper.h
- **Lines:** ~15
- **Purpose:** Physical memory manager interface
- **Contents:**
  - pmm_init() - Initialize PMM
  - pmm_alloc_page() - Allocate single page
  - pmm_free_page() - Free single page
  - pmm_alloc_pages() - Allocate multiple pages
  - pmm_free_pages() - Free multiple pages
  - pmm_free_memory() - Query free memory
  - pmm_total_memory() - Query total memory

### 4. vmm.h
- **Lines:** ~30
- **Purpose:** Virtual memory manager interface
- **Contents:**
  - pagetable_t type definition
  - vmm_init() - Initialize VMM
  - vmm_create_pagetable() - Create page table
  - vmm_free_pagetable() - Free page table
  - vmm_map() - Map single page
  - vmm_unmap() - Unmap single page
  - vmm_map_range() - Map multiple pages
  - vmm_unmap_range() - Unmap multiple pages
  - vmm_walk() - Walk page table
  - vmm_translate() - Virtual to physical translation
  - vmm_switch() - Switch page table
  - vmm_kernel_pagetable() - Get kernel page table

### 5. kmalloc.h
- **Lines:** ~15
- **Purpose:** Kernel heap allocator interface
- **Contents:**
  - kmalloc_init() - Initialize heap
  - kmalloc() - Allocate memory
  - kzalloc() - Allocate and zero memory
  - krealloc() - Reallocate memory
  - kfree() - Free memory
  - kmalloc_stats() - Print statistics

### 6. trap.h
- **Lines:** ~40
- **Purpose:** Interrupt and trap handling interface
- **Contents:**
  - trapframe_t structure (32 registers)
  - trap_init() - Initialize trap handling
  - trap_handler() - C trap handler
  - timer_handler() - Timer interrupt handler

### 7. process.h
- **Lines:** ~50
- **Purpose:** Process management interface
- **Contents:**
  - proc_state_t enum (UNUSED, EMBRYO, RUNNABLE, etc.)
  - process_t structure (PCB)
  - proc_init() - Initialize process subsystem
  - proc_alloc() - Allocate process
  - proc_free() - Free process
  - proc_current() - Get current process
  - proc_set_current() - Set current process
  - proc_find() - Find process by PID
  - proc_dump() - Print process table

### 8. syscall.h
- **Lines:** ~30
- **Purpose:** System call interface
- **Contents:**
  - System call numbers (SYS_exit, SYS_fork, etc.)
  - syscall_init() - Initialize syscalls
  - syscall_handler() - Syscall dispatcher
  - Individual syscall function prototypes

## Kernel Implementation (kernel/)

### 1. kernel.c
- **Lines:** ~70
- **Purpose:** Main kernel entry point
- **Contents:**
  - boot() function
  - Initialization sequence:
    - PMM initialization
    - VMM initialization
    - kmalloc initialization
    - Trap initialization
    - Process initialization
    - Syscall initialization
  - Memory allocation tests
  - Idle loop

### 2. pmm.c
- **Lines:** ~180
- **Purpose:** Physical memory manager implementation
- **Contents:**
  - Bitmap-based allocator
  - bitmap_set/clear/test helper functions
  - pmm_init() - Set up bitmap and free pages
  - pmm_alloc_page() - Find and allocate free page
  - pmm_free_page() - Free page with validation
  - pmm_alloc_pages() - Allocate contiguous pages
  - pmm_free_pages() - Free multiple pages
  - Memory statistics functions
  - Error checking (double free, invalid address, etc.)

### 3. vmm.c
- **Lines:** ~200
- **Purpose:** Virtual memory manager implementation
- **Contents:**
  - vmm_walk() - Walk 3-level page table
  - vmm_map() - Map virtual to physical page
  - vmm_unmap() - Unmap page
  - vmm_map_range() - Map multiple pages
  - vmm_unmap_range() - Unmap multiple pages
  - vmm_translate() - VA to PA translation
  - vmm_create_pagetable() - Allocate page table
  - vmm_free_pagetable() - Recursively free page tables
  - vmm_init() - Set up kernel page table
  - Identity mapping for kernel memory
  - Device memory mapping

### 4. kmalloc.c
- **Lines:** ~200
- **Purpose:** Kernel heap allocator implementation
- **Contents:**
  - First-fit allocation algorithm
  - Block header structure
  - kmalloc_init() - Allocate initial heap
  - kmalloc() - Find free block or allocate new
  - kzalloc() - Allocate and zero
  - kfree() - Free block with coalescing
  - krealloc() - Reallocate block
  - kmalloc_stats() - Print heap statistics
  - Block splitting for efficiency
  - Adjacent block coalescing

### 5. trap.c
- **Lines:** ~150
- **Purpose:** Trap and interrupt handler
- **Contents:**
  - trap_init() - Set stvec and enable interrupts
  - trap_handler() - Main C handler
  - handle_exception() - Exception dispatcher
  - handle_interrupt() - Interrupt dispatcher
  - timer_handler() - Timer interrupt handler
  - Detailed exception reporting
  - Register dump on panic
  - System call integration

### 6. trap_entry.S
- **Lines:** ~80
- **Purpose:** Trap entry/exit assembly
- **Contents:**
  - trap_entry label
  - Save all 32 registers to stack
  - Call trap_handler with trapframe pointer
  - Restore all 32 registers
  - Return with sret instruction

### 7. process.c
- **Lines:** ~120
- **Purpose:** Process management implementation
- **Contents:**
  - Process table (array of 64 processes)
  - proc_init() - Initialize process table
  - proc_alloc() - Find free slot and initialize
  - proc_free() - Free process and its resources
  - proc_current() - Get current process pointer
  - proc_set_current() - Set current process
  - proc_find() - Linear search by PID
  - proc_dump() - Print process table for debugging

### 8. syscall.c
- **Lines:** ~150
- **Purpose:** System call dispatcher and implementations
- **Contents:**
  - syscall_init() - Initialize syscall subsystem
  - syscall_handler() - Dispatch based on syscall number
  - Extract arguments from trapframe (a0-a5)
  - Set return value in trapframe (a0)
  - Stub implementations for all syscalls:
    - sys_exit() - Mark process as zombie
    - sys_fork() - Not implemented (stub)
    - sys_read() - Not implemented (stub)
    - sys_write() - Basic console output
    - sys_open() - Not implemented (stub)
    - sys_close() - Not implemented (stub)
    - sys_wait() - Not implemented (stub)
    - sys_exec() - Not implemented (stub)
    - sys_getpid() - Return current process PID
    - sys_sbrk() - Not implemented (stub)
    - sys_sleep() - Not implemented (stub)
    - sys_uptime() - Not implemented (stub)
    - sys_kill() - Not implemented (stub)

## Documentation (dump/)

### 1. PROJECT_PLAN.md
- **Lines:** ~400
- **Purpose:** Overall project architecture and planning
- **Contents:**
  - Project overview
  - Architecture components breakdown
  - Team distribution (4 people)
  - Development phases (8 weeks)
  - Critical dependencies
  - Key files to create
  - Testing strategy
  - Common pitfalls
  - Resources and success metrics

### 2. TEAM_TASKS.md
- **Lines:** ~600
- **Purpose:** Detailed task breakdown for each team member
- **Contents:**
  - Person 1: Memory Management Lead
    - Tasks, files, testing checklist
    - PMM, VMM, kmalloc implementation
  - Person 2: Interrupt & Process Management Lead
    - Tasks, files, testing checklist
    - Trap handling, scheduler, context switching
  - Person 3: System Calls & User Space Lead
    - Tasks, files, testing checklist
    - fork, exec, exit, wait, sbrk, ELF loading
  - Person 4: File System & I/O Lead
    - Tasks, files, testing checklist
    - VFS, TARFS, UART, file descriptors
  - Integration milestones
  - Communication strategy
  - Git workflow
  - Testing strategy
  - Debugging tips per role
  - Code templates for each person

### 3. GETTING_STARTED.md
- **Lines:** ~400
- **Purpose:** Quick start guide for development
- **Contents:**
  - Build and run instructions
  - Project structure explanation
  - Development workflow
  - Common tasks (adding files, debugging)
  - Boot process explanation
  - Memory layout
  - Key concepts (PMM, VMM, kmalloc, etc.)
  - Troubleshooting guide
  - Testing patterns
  - Code style guidelines

### 4. QUICK_REFERENCE.md
- **Lines:** ~500
- **Purpose:** Complete API reference
- **Contents:**
  - Memory management API
  - Process management API
  - Trap handling API
  - System call API
  - RISC-V CSR operations
  - Utility macros
  - Common patterns and examples
  - Assembly snippets
  - GDB commands
  - Common errors and solutions
  - Performance tips
  - Testing checklist

### 5. SUMMARY.md
- **Lines:** ~400
- **Purpose:** Implementation summary
- **Contents:**
  - What has been created
  - Files created (this list)
  - What works (fully implemented)
  - What's partially implemented
  - What needs implementation
  - Code quality notes
  - Testing recommendations
  - Performance characteristics
  - Memory usage
  - Next steps for team

### 6. ARCHITECTURE.md
- **Lines:** ~500
- **Purpose:** Visual architecture diagrams
- **Contents:**
  - System overview diagram
  - Component dependencies
  - Memory management architecture
  - Page table structure (Sv39)
  - Process structure
  - Trap/interrupt flow
  - System call flow
  - Scheduler architecture
  - File system architecture
  - Boot sequence
  - Team work distribution
  - Data flow examples

### 7. FILES_CREATED.md
- **Lines:** ~300 (this file)
- **Purpose:** Comprehensive list of all files
- **Contents:**
  - List of all headers
  - List of all implementations
  - List of all documentation
  - Line counts and purposes

## Statistics

### Code Statistics

| Category | Files | Lines | Purpose |
|----------|-------|-------|---------|
| Headers | 8 | ~400 | Interfaces and definitions |
| Implementation | 8 | ~1,150 | Core kernel functionality |
| Assembly | 1 | ~80 | Trap entry/exit |
| Documentation | 7 | ~3,100 | Planning and reference |
| **Total** | **24** | **~4,730** | **Complete OS foundation** |

### Breakdown by Component

| Component | Files | Lines | Status |
|-----------|-------|-------|--------|
| Memory Management | 6 | ~600 | ✅ Complete |
| Process Management | 2 | ~170 | ✅ Complete (stubs for scheduler) |
| Trap Handling | 3 | ~270 | ✅ Complete |
| System Calls | 2 | ~180 | 🚧 Infrastructure complete, implementations needed |
| Documentation | 7 | ~3,100 | ✅ Complete |
| Main Kernel | 1 | ~70 | ✅ Complete |

### Breakdown by Team Member

| Person | Component | Files to Work On | Lines Ready | Status |
|--------|-----------|------------------|-------------|--------|
| Person 1 | Memory | 6 files | ~600 | ✅ Ready for testing |
| Person 2 | Process/Trap | 5 files | ~440 | 🚧 Need scheduler |
| Person 3 | Syscalls | 2 files | ~180 | 🚧 Need implementations |
| Person 4 | Filesystem | 0 files | 0 | ⏳ Need to create |

## File Dependencies

### Dependency Graph

```
types.h
  └─► riscv.h
      └─► physical_memory_mapper.h
          └─► vmm.h
              └─► kmalloc.h
                  └─► trap.h
                      └─► process.h
                          └─► syscall.h
                              └─► kernel.c
```

### Include Relationships

- **kernel.c** includes: printk.h, physical_memory_mapper.h, vmm.h, kmalloc.h, trap.h, process.h, syscall.h, riscv.h
- **pmm.c** includes: physical_memory_mapper.h, riscv.h, printk.h
- **vmm.c** includes: vmm.h, physical_memory_mapper.h, riscv.h, printk.h
- **kmalloc.c** includes: kmalloc.h, physical_memory_mapper.h, riscv.h, printk.h
- **trap.c** includes: trap.h, riscv.h, printk.h, syscall.h
- **process.c** includes: process.h, kmalloc.h, printk.h
- **syscall.c** includes: syscall.h, process.h, printk.h

## Files NOT Created (For Team to Implement)

### Person 2: Process Management
- [ ] `kernel/scheduler.c` - Round-robin scheduler
- [ ] `kernel/context_switch.S` - Context switching assembly
- [ ] `kernel/include/scheduler.h` - Scheduler interface

### Person 3: System Calls & User Space
- [ ] `kernel/syscall_proc.c` - fork, exec, exit, wait
- [ ] `kernel/syscall_mem.c` - sbrk implementation
- [ ] `kernel/elf.c` - ELF file loader
- [ ] `kernel/include/elf.h` - ELF structures

### Person 4: File System & I/O
- [ ] `kernel/vfs.c` - Virtual file system
- [ ] `kernel/include/vfs.h` - VFS interface
- [ ] `kernel/tarfs.c` - TARFS driver
- [ ] `kernel/uart.c` - UART driver
- [ ] `kernel/include/uart.h` - UART interface
- [ ] `kernel/syscall_io.c` - I/O syscalls (read, write, open, close)

## How to Use These Files

### For Development

1. **Read documentation first:**
   - Start with `GETTING_STARTED.md`
   - Read `PROJECT_PLAN.md` for overview
   - Check `TEAM_TASKS.md` for your role
   - Use `QUICK_REFERENCE.md` while coding

2. **Build and test:**
   ```bash
   make clean
   make
   make qemu
   ```

3. **Add new files:**
   - Create in appropriate directory
   - Follow naming conventions
   - Add header guards
   - Include in relevant headers
   - Makefile will automatically pick up new files

4. **Debug:**
   - Use `printk()` for output
   - Check `QUICK_REFERENCE.md` for GDB commands
   - Read `ARCHITECTURE.md` for system understanding

### For Integration

1. **Before merging:**
   - Ensure code compiles
   - Test your component
   - Update documentation if needed
   - Review with team

2. **During merge:**
   - Resolve conflicts carefully
   - Test integrated system
   - Update `SUMMARY.md` if needed

## Version Information

- **Created:** March 2026
- **Target:** CSE506 Operating Systems Project
- **Language:** C (kernel), Assembly (boot/trap)
- **Architecture:** RISC-V 64-bit (Sv39)
- **Build System:** Make
- **Emulator:** QEMU
- **Team Size:** 4 people
- **Estimated Completion:** 6-8 weeks

## License

This code is created for educational purposes for the CSE506 course.

## Acknowledgments

- Inspired by xv6-riscv (MIT)
- RISC-V specifications
- QEMU documentation

---

**End of Files Created List**

# SBUnix OS Project Plan

## Project Overview
Building a RISC-V 64-bit operating system kernel with essential features for a team of 4 developers.

## Architecture Components

### 1. Memory Management (Priority: HIGH)
- **Physical Memory Manager (PMM)**
  - Track free/used physical pages (4KB each)
  - Bitmap or free-list based allocator
  - Functions: `pmm_init()`, `pmm_alloc_page()`, `pmm_free_page()`

- **Virtual Memory Manager (VMM)**
  - RISC-V Sv39 page table management
  - Map/unmap virtual to physical addresses
  - Kernel and user address space separation
  - Functions: `vmm_init()`, `vmm_map()`, `vmm_unmap()`, `vmm_create_address_space()`

- **Kernel Heap (kmalloc)**
  - Dynamic memory allocation for kernel
  - Simple first-fit or buddy allocator
  - Functions: `kmalloc_init()`, `kmalloc()`, `kfree()`

### 2. Interrupt & Trap Handling (Priority: HIGH)
- **Trap Vector Setup**
  - Configure RISC-V stvec register
  - Trap entry/exit assembly code
  - Save/restore context

- **Interrupt Handlers**
  - Timer interrupts
  - External interrupts (UART, disk, etc.)
  - Exception handlers (page faults, illegal instructions)

### 3. Process Management (Priority: MEDIUM)
- **Process Control Block (PCB)**
  - Process state (running, ready, blocked)
  - Register context
  - Memory mappings
  - File descriptors

- **Scheduler**
  - Round-robin or priority-based
  - Context switching
  - Process creation/termination

### 4. System Calls (Priority: MEDIUM)
- **System Call Interface**
  - RISC-V ecall instruction handling
  - System call number dispatch
  - Parameter passing conventions

- **Basic System Calls**
  - `fork()`, `exec()`, `exit()`, `wait()`
  - `read()`, `write()`, `open()`, `close()`
  - `sbrk()` for user heap

### 5. File System (Priority: MEDIUM)
- **TARFS (Already present)**
  - Read-only initial filesystem
  
- **VFS Layer**
  - Virtual file system abstraction
  - File descriptor table
  - Path resolution

### 6. Device Drivers (Priority: LOW)
- **UART Driver**
  - Console input/output
  - Interrupt-driven or polling

- **VirtIO Block Device**
  - Disk I/O operations
  - Buffer cache

## Team Distribution (4 People)

### Person 1: Memory Management Lead
- Physical memory manager implementation
- Virtual memory manager (page tables)
- Kernel heap allocator (kmalloc)
- **Estimated time:** 2-3 weeks

### Person 2: Interrupt & Process Management Lead
- Trap/interrupt handling infrastructure
- Context switching
- Process control blocks
- Basic scheduler
- **Estimated time:** 2-3 weeks

### Person 3: System Calls & User Space Lead
- System call infrastructure
- User/kernel mode transitions
- Implement core syscalls (fork, exec, exit, wait)
- User heap (sbrk)
- **Estimated time:** 2-3 weeks

### Person 4: File System & I/O Lead
- VFS layer
- File descriptor management
- TARFS integration
- Basic UART driver
- I/O system calls (read, write, open, close)
- **Estimated time:** 2-3 weeks

## Development Phases

### Phase 1: Foundation (Week 1-2)
- [ ] Physical memory manager
- [ ] Basic virtual memory (identity mapping for kernel)
- [ ] Kernel heap allocator
- [ ] Trap handling infrastructure
- [ ] Basic printk working

### Phase 2: Process Support (Week 3-4)
- [ ] Process structures (PCB)
- [ ] Context switching
- [ ] Basic scheduler
- [ ] System call infrastructure
- [ ] First process creation

### Phase 3: User Programs (Week 5-6)
- [ ] Fork/exec/exit/wait implementation
- [ ] User memory management
- [ ] ELF loading
- [ ] Shell execution

### Phase 4: I/O & Polish (Week 7-8)
- [ ] File system integration
- [ ] I/O system calls
- [ ] Device drivers
- [ ] Bug fixes and testing

## Critical Dependencies

```
PMM → VMM → kmalloc
     ↓
  Trap Handling → Process Management → Scheduler
                         ↓
                   System Calls → User Programs
                         ↓
                   File System → I/O
```

## Key Files Status

### Memory Management ✅ COMPLETE
- ✅ `kernel/include/mm/pmm.h` - Physical memory manager interface
- ✅ `kernel/mm/pmm.c` - Physical memory manager implementation
- ✅ `kernel/include/mm/vmm.h` - Virtual memory manager interface
- ✅ `kernel/mm/vmm.c` - Virtual memory manager implementation
- ✅ `kernel/include/mm/kmalloc.h` - Kernel heap interface
- ✅ `kernel/mm/kmalloc.c` - Kernel heap implementation

### Interrupts & Traps ✅ COMPLETE
- ✅ `kernel/include/trap/trap.h` - Trap handling interface
- ✅ `kernel/trap/trap.c` - Trap handler implementation
- ✅ `kernel/arch/riscv/trap_entry.S` - Assembly trap entry/exit
- ✅ `kernel/include/arch/riscv.h` - RISC-V CSR definitions

### Process Management 🚧 PARTIAL
- ✅ `kernel/include/process/process.h` - Process structures
- ✅ `kernel/process/process.c` - Process management
- ⏳ `kernel/process/scheduler.c` - Scheduler implementation (TODO)
- ⏳ `kernel/arch/riscv/context_switch.S` - Context switching assembly (TODO)

### System Calls 🚧 PARTIAL
- ✅ `kernel/include/trap/syscall.h` - System call numbers
- ✅ `kernel/trap/syscall.c` - System call dispatcher (with stubs)
- ⏳ `kernel/trap/syscall_proc.c` - Process syscalls (TODO)
- ⏳ `kernel/trap/syscall_mem.c` - Memory syscalls (TODO)
- ⏳ `kernel/trap/syscall_io.c` - I/O syscalls (TODO)
- ⏳ `kernel/core/elf.c` - ELF loader (TODO)
- ⏳ `kernel/include/kernel/elf.h` - ELF structures (TODO)

### File System ⏳ TODO
- ⏳ `kernel/include/fs/vfs.h` - VFS interface (TODO)
- ⏳ `kernel/fs/vfs.c` - VFS implementation (TODO)
- ⏳ `kernel/fs/tarfs.c` - TARFS driver (TODO)
- ⏳ `kernel/include/drivers/uart.h` - UART interface (TODO)
- ⏳ `kernel/drivers/uart.c` - UART driver (TODO)

## Testing Strategy
1. Unit test each component in isolation
2. Integration testing between components
3. User program testing (echo, shell, etc.)
4. Stress testing (many processes, memory pressure)

## Common Pitfalls to Avoid
1. **Memory alignment issues** - RISC-V requires proper alignment
2. **Page table bugs** - Triple-check page table entries
3. **Race conditions** - Be careful with shared data structures
4. **Stack overflow** - Ensure adequate stack space
5. **Interrupt handling** - Save/restore ALL registers properly
6. **User/kernel boundaries** - Validate all user pointers

## Resources Needed
- RISC-V Privileged Specification
- RISC-V Unprivileged Specification
- xv6-riscv source code (reference)
- QEMU documentation
- VirtIO specification

## Success Metrics
- [ ] Kernel boots and prints messages
- [ ] Memory allocation works correctly
- [ ] Can handle interrupts and exceptions
- [ ] Can create and schedule processes
- [ ] User programs can make system calls
- [ ] Shell runs and executes commands
- [ ] File system operations work

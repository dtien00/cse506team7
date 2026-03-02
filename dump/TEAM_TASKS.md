# Team Task Distribution for SBUnix OS Project

## Team Structure (4 People)

This document breaks down the OS project into 4 parallel work streams that can be developed relatively independently, with clear interfaces between components.

---

## Person 1: Memory Management Lead

### Primary Responsibilities
- Physical Memory Manager (PMM)
- Virtual Memory Manager (VMM)  
- Kernel Heap Allocator (kmalloc)

### Files to Work On
- ✅ `kernel/include/physical_memory_mapper.h` (already created)
- ✅ `kernel/pmm.c` (already created)
- ✅ `kernel/include/vmm.h` (already created)
- ✅ `kernel/vmm.c` (already created)
- ✅ `kernel/include/kmalloc.h` (already created)
- ✅ `kernel/kmalloc.c` (already created)

### Tasks
1. **Week 1-2: Physical Memory Manager**
   - ✅ Implement bitmap-based page allocator
   - ✅ Test single page allocation/free
   - ✅ Test multi-page allocation
   - Test edge cases (out of memory, double free, etc.)

2. **Week 2-3: Virtual Memory Manager**
   - ✅ Implement Sv39 page table walking
   - ✅ Implement page mapping/unmapping
   - ✅ Set up kernel identity mapping
   - Test page table operations
   - Handle page faults properly

3. **Week 3-4: Kernel Heap**
   - ✅ Implement first-fit allocator
   - ✅ Implement coalescing for freed blocks
   - Test various allocation patterns
   - Add debugging/statistics features

### Testing Checklist
- [ ] Can allocate and free single pages
- [ ] Can allocate contiguous pages
- [ ] Detects double-free errors
- [ ] Page tables correctly translate addresses
- [ ] kmalloc/kfree work for various sizes
- [ ] Memory doesn't leak over time

### Dependencies
- None initially (foundational component)
- Other teams will depend on your work

---

## Person 2: Interrupt & Process Management Lead

### Primary Responsibilities
- Trap/Interrupt handling
- Process structures and management
- Context switching
- Basic scheduler

### Files to Work On
- ✅ `kernel/include/trap.h` (already created)
- ✅ `kernel/trap.c` (already created)
- ✅ `kernel/trap_entry.S` (already created)
- ✅ `kernel/include/process.h` (already created)
- ✅ `kernel/process.c` (already created)
- `kernel/scheduler.c` (TODO)
- `kernel/context_switch.S` (TODO)

### Tasks
1. **Week 1-2: Trap Handling**
   - ✅ Set up trap vector (stvec)
   - ✅ Implement trap entry/exit in assembly
   - ✅ Handle exceptions (page faults, illegal instructions)
   - Test exception handling
   - Handle timer interrupts

2. **Week 2-3: Process Management**
   - ✅ Define process control block (PCB)
   - ✅ Implement process allocation/deallocation
   - Implement context switching in assembly
   - Test switching between kernel contexts

3. **Week 3-4: Scheduler**
   - Implement round-robin scheduler
   - Implement yield() and sleep()
   - Test multiple processes running
   - Handle process termination

### Testing Checklist
- [ ] Traps save/restore all registers correctly
- [ ] Can handle page faults without crashing
- [ ] Timer interrupts fire regularly
- [ ] Can create multiple processes
- [ ] Context switching preserves state
- [ ] Scheduler fairly distributes CPU time

### Dependencies
- Depends on Person 1 for memory allocation
- Person 3 will depend on your process structures

### Code to Create

**kernel/scheduler.c:**
```c
#include "process.h"
#include "riscv.h"

void scheduler(void) {
  // Round-robin scheduler
  // Find next RUNNABLE process and switch to it
}

void yield(void) {
  // Voluntarily give up CPU
}
```

**kernel/context_switch.S:**
```asm
# Save current context and switch to new process
.global context_switch
context_switch:
  # Save callee-saved registers
  # Switch stack pointer
  # Restore callee-saved registers
  ret
```

---

## Person 3: System Calls & User Space Lead

### Primary Responsibilities
- System call infrastructure
- User/kernel mode transitions
- Core system calls (fork, exec, exit, wait)
- User memory management (sbrk)

### Files to Work On
- ✅ `kernel/include/syscall.h` (already created)
- ✅ `kernel/syscall.c` (already created - stubs)
- `kernel/syscall_proc.c` (TODO - fork, exec, exit, wait)
- `kernel/syscall_mem.c` (TODO - sbrk)
- `kernel/elf.c` (TODO - ELF loader)
- `kernel/include/elf.h` (TODO)

### Tasks
1. **Week 1-2: System Call Infrastructure**
   - ✅ Define system call numbers
   - ✅ Implement syscall dispatcher
   - Test basic syscalls (getpid, exit)
   - Handle invalid syscall numbers

2. **Week 2-3: Process System Calls**
   - Implement fork() - duplicate process
   - Implement exec() - load new program
   - Implement exit() - terminate process
   - Implement wait() - wait for child
   - Test process lifecycle

3. **Week 3-4: Memory & ELF Loading**
   - Implement sbrk() for user heap
   - Implement ELF file parser
   - Load user programs from TARFS
   - Test running user programs

### Testing Checklist
- [ ] System calls transition to kernel correctly
- [ ] fork() creates exact copy of process
- [ ] exec() loads and runs new program
- [ ] Parent can wait() for child
- [ ] User programs can allocate memory with sbrk()
- [ ] Can run echo and other binaries

### Dependencies
- Depends on Person 2 for process management
- Depends on Person 4 for file system access

### Code to Create

**kernel/syscall_proc.c:**
```c
#include "syscall.h"
#include "process.h"
#include "vmm.h"

pid_t sys_fork(void) {
  // 1. Allocate new process
  // 2. Copy page table
  // 3. Copy trapframe
  // 4. Return 0 to child, child PID to parent
}

int sys_exec(const char *path, char **argv) {
  // 1. Load ELF file from filesystem
  // 2. Create new page table
  // 3. Map program segments
  // 4. Set up stack with arguments
  // 5. Return to user mode at entry point
}
```

**kernel/include/elf.h:**
```c
#pragma once

typedef struct {
  uint32_t magic;
  uint8_t  class;
  uint8_t  data;
  // ... ELF header fields
} elf_header_t;

int elf_load(const char *path, pagetable_t pt, uint64_t *entry);
```

---

## Person 4: File System & I/O Lead

### Primary Responsibilities
- Virtual File System (VFS) layer
- TARFS integration
- File descriptor management
- I/O system calls (read, write, open, close)
- Basic UART driver

### Files to Work On
- `kernel/include/vfs.h` (TODO)
- `kernel/vfs.c` (TODO)
- `kernel/tarfs.c` (TODO)
- `kernel/include/uart.h` (TODO)
- `kernel/uart.c` (TODO)
- `kernel/syscall_io.c` (TODO)

### Tasks
1. **Week 1-2: UART Driver**
   - Implement UART initialization
   - Implement character read/write
   - Test console I/O
   - Make printk use UART

2. **Week 2-3: File System**
   - Design VFS interface
   - Implement TARFS driver
   - Parse tar archive format
   - Test reading files from TARFS

3. **Week 3-4: I/O System Calls**
   - Implement file descriptor table
   - Implement open/close/read/write
   - Connect to TARFS
   - Test file operations from user space

### Testing Checklist
- [ ] UART driver sends/receives characters
- [ ] Can list files in TARFS
- [ ] Can read file contents from TARFS
- [ ] File descriptors work correctly
- [ ] User programs can open and read files
- [ ] stdin/stdout/stderr work

### Dependencies
- Depends on Person 2 for process structures (file descriptor table)
- Person 3 will depend on your file system for exec()

### Code to Create

**kernel/include/vfs.h:**
```c
#pragma once

typedef struct file {
  int type;      // File, directory, device
  int ref;       // Reference count
  int readable;
  int writable;
  void *inode;   // Underlying inode
  uint64_t off;  // Current offset
} file_t;

int vfs_open(const char *path, int flags);
int vfs_close(int fd);
ssize_t vfs_read(int fd, void *buf, size_t count);
ssize_t vfs_write(int fd, const void *buf, size_t count);
```

**kernel/tarfs.c:**
```c
#include "vfs.h"

// External symbols from linker
extern char _tarfs_start[];
extern char _tarfs_end[];

typedef struct tar_header {
  char name[100];
  char mode[8];
  char uid[8];
  char gid[8];
  char size[12];
  char mtime[12];
  char checksum[8];
  char type;
  // ...
} tar_header_t;

void tarfs_init(void) {
  // Parse tar archive
}

int tarfs_lookup(const char *path, tar_header_t **out) {
  // Find file in tar archive
}
```

**kernel/include/uart.h:**
```c
#pragma once

void uart_init(void);
void uart_putc(char c);
char uart_getc(void);
int uart_can_read(void);
```

---

## Integration Milestones

### Milestone 1: Basic Kernel (Week 2)
- **Who:** Person 1 + Person 2
- **Goal:** Kernel boots, memory works, can handle interrupts
- **Test:** Kernel allocates memory and handles timer interrupts

### Milestone 2: Process Support (Week 4)
- **Who:** Person 2 + Person 3
- **Goal:** Can create processes and switch between them
- **Test:** Multiple kernel threads running concurrently

### Milestone 3: User Programs (Week 6)
- **Who:** Person 3 + Person 4
- **Goal:** Can load and run user programs
- **Test:** Echo program runs successfully

### Milestone 4: Full System (Week 8)
- **Who:** All 4 people
- **Goal:** Shell runs, can execute commands, file I/O works
- **Test:** Interactive shell with multiple programs

---

## Communication & Coordination

### Daily Standup (15 minutes)
- What did you complete yesterday?
- What are you working on today?
- Any blockers?

### Weekly Integration (2 hours)
- Merge everyone's code
- Run integration tests
- Fix conflicts and bugs
- Plan next week

### Code Review Process
1. Create feature branch
2. Implement feature
3. Test locally
4. Submit for review
5. Address feedback
6. Merge to main

### Shared Interfaces (DO NOT CHANGE WITHOUT DISCUSSION)
- `physical_memory_mapper.h` - PMM interface
- `vmm.h` - VMM interface
- `process.h` - Process structures
- `syscall.h` - System call numbers
- `vfs.h` - File system interface

---

## Git Workflow

### Branch Strategy
```
main (stable, always compiles)
├── person1-memory
├── person2-process
├── person3-syscall
└── person4-filesystem
```

### Commit Messages
```
[component] Brief description

Detailed explanation of what changed and why.

Tested: What tests were run
```

Example:
```
[pmm] Implement multi-page allocation

Added pmm_alloc_pages() and pmm_free_pages() functions
to allocate contiguous physical memory regions.

Tested: Allocated 10 pages, verified contiguous addresses
```

---

## Testing Strategy

### Unit Tests
Each person writes tests for their components:
```c
void test_pmm(void) {
  void *p1 = pmm_alloc_page();
  void *p2 = pmm_alloc_page();
  assert(p1 != NULL && p2 != NULL);
  assert(p1 != p2);
  pmm_free_page(p1);
  pmm_free_page(p2);
  printk("PMM test passed\n");
}
```

### Integration Tests
Test interactions between components:
```c
void test_process_memory(void) {
  process_t *p = proc_alloc();
  p->pagetable = vmm_create_pagetable();
  vmm_map(p->pagetable, 0x1000, pmm_alloc_page(), PTE_R | PTE_W);
  // Test that process can access mapped memory
}
```

### System Tests
End-to-end tests:
- Boot kernel
- Run echo program
- Run shell
- Execute multiple commands

---

## Debugging Tips

### For Memory Issues (Person 1)
- Print physical addresses before/after allocation
- Check bitmap state
- Verify page table entries with `vmm_translate()`
- Use QEMU's memory inspection: `info mem`

### For Process Issues (Person 2)
- Print process state before/after context switch
- Verify all registers are saved/restored
- Check stack pointer values
- Use `proc_dump()` to see process table

### For System Call Issues (Person 3)
- Print syscall number and arguments
- Verify user pointers are valid
- Check return values
- Test with simple syscalls first (getpid)

### For File System Issues (Person 4)
- Print tar header contents
- Verify file sizes and offsets
- Check file descriptor table
- Test with known files first

---

## Resources

### RISC-V Documentation
- RISC-V Privileged Specification
- RISC-V Unprivileged Specification
- RISC-V Calling Convention

### Reference Implementations
- xv6-riscv (MIT)
- Linux kernel (RISC-V port)
- FreeRTOS (RISC-V port)

### Tools
- QEMU RISC-V emulator
- GDB for RISC-V
- riscv64-unknown-elf-gcc toolchain

### Useful QEMU Commands
```bash
# Run with debugging
make qemu

# Run with GDB
qemu-system-riscv64 ... -s -S
riscv64-unknown-elf-gdb build/kernel.elf
(gdb) target remote :1234
(gdb) break boot
(gdb) continue

# Inspect memory
(gdb) x/10x 0x80200000

# Inspect registers
(gdb) info registers
```

---

## Current Status

### ✅ Completed
- Project structure and build system
- Core header files (types.h, riscv.h)
- Physical memory manager (PMM)
- Virtual memory manager (VMM)
- Kernel heap allocator (kmalloc)
- Trap/interrupt infrastructure
- Process structures
- System call infrastructure (stubs)
- Project planning and documentation

### 🚧 In Progress
- Testing memory management components
- Implementing scheduler
- Implementing system calls
- File system integration

### ⏳ TODO
- Context switching
- Fork/exec implementation
- ELF loader
- TARFS driver
- UART driver
- File descriptor management
- User program testing
- Shell integration

---

## Questions?

If you're stuck or need clarification:
1. Check this document first
2. Look at the reference implementations (xv6)
3. Ask in team chat
4. Discuss in weekly meeting

Remember: **It's okay to ask for help!** OS development is complex, and we're all learning together.

Good luck! 🚀

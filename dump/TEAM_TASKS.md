# Team Task Distribution for SBUnix OS Project

## Team Structure (4 People)

This document breaks down the OS project into 4 **PARALLEL** work streams. All team members can start working immediately without waiting for others.

## 🎯 Current Status Overview

### ✅ FOUNDATION COMPLETE
- **Memory Management:** PMM, VMM, kmalloc (6 files, ~600 lines)
- **Trap Infrastructure:** Trap handling, trap_entry.S (3 files, ~270 lines)
- **Process Structures:** PCB, process table (2 files, ~170 lines)
- **Syscall Framework:** Dispatcher and stubs (2 files, ~180 lines)
- **Core Types:** types.h, riscv.h, printk.h (3 files, ~250 lines)

### 🚀 PARALLEL WORK DISTRIBUTION

All foundation code is complete. **All 4 team members can now work in parallel** on their respective components without blocking each other.

| Person | Component | Estimated Work | Can Start Now? |
|--------|-----------|----------------|----------------|
| **Person 1** | Scheduler & Context Switching | ~400 lines | ✅ YES - No dependencies |
| **Person 2** | Fork/Exec/Wait & ELF Loader | ~500 lines | ✅ YES - No dependencies |
| **Person 3** | TARFS & VFS & File Descriptors | ~600 lines | ✅ YES - No dependencies |
| **Person 4** | UART & I/O Syscalls & sbrk | ~400 lines | ✅ YES - No dependencies |

**Key Point:** Everyone has independent work that doesn't block on others. Integration happens at the end of Week 1-2.

---

## Person 1: Scheduler & Context Switching Lead

### Primary Responsibilities
- Round-robin scheduler implementation
- Context switching (assembly)
- Process lifecycle management
- Timer-based preemption

### Files to Create
- [ ] `kernel/include/process/scheduler.h` - Scheduler interface
- [ ] `kernel/process/scheduler.c` - Round-robin scheduler
- [ ] `kernel/arch/riscv/context_switch.S` - Context switching assembly
- [ ] `kernel/process/proc_lifecycle.c` - Process creation/termination helpers

### Tasks
1. **Context Switching (Priority: HIGH)**
   - [ ] Implement context_switch() in assembly
   - [ ] Save callee-saved registers (s0-s11, sp, ra)
   - [ ] Switch stack pointers between processes
   - [ ] Test switching between 2 kernel threads
   - [ ] Verify register preservation

2. **Scheduler (Priority: HIGH)**
   - [ ] Implement round-robin scheduler
   - [ ] Maintain ready queue of RUNNABLE processes
   - [ ] Implement yield() for voluntary CPU release
   - [ ] Implement sleep() for blocking processes
   - [ ] Handle timer interrupts for preemption
   - [ ] Test with multiple processes

3. **Process Lifecycle (Priority: MEDIUM)**
   - [ ] Enhance proc_alloc() with proper initialization
   - [ ] Implement process cleanup on exit
   - [ ] Handle orphaned processes
   - [ ] Implement process state transitions

### Testing Checklist
- [ ] Can create 2+ processes and switch between them
- [ ] Context switching preserves all registers
- [ ] Scheduler fairly distributes CPU time
- [ ] Timer interrupts trigger rescheduling
- [ ] yield() works correctly
- [ ] Process termination cleans up resources

### Dependencies
- ✅ Memory management (COMPLETE - ready to use)
- ✅ Process structures (COMPLETE - ready to use)
- ✅ Trap handling (COMPLETE - ready to use)
- 🔄 Will integrate with Person 2 for system calls

---

## Person 2: Process System Calls Lead (fork/exec/wait)

### Primary Responsibilities
- Fork system call (process duplication)
- Exec system call (program loading)
- Exit/Wait system calls (process lifecycle)
- ELF file loader

### Files to Create
- [ ] `kernel/trap/syscall_proc.c` - Process syscalls (fork, exec, exit, wait)
- [ ] `kernel/include/kernel/elf.h` - ELF structures and definitions
- [ ] `kernel/core/elf.c` - ELF file parser and loader
- [ ] `kernel/process/proc_copy.c` - Helper for copying process state (for fork)

### Tasks
1. **Fork System Call (Priority: HIGH)**
   - [ ] Implement sys_fork() in syscall_proc.c
   - [ ] Allocate new process with proc_alloc()
   - [ ] Copy parent's page table (use vmm_copy_pagetable)
   - [ ] Copy parent's trapframe
   - [ ] Set up parent-child relationship
   - [ ] Return 0 to child, child PID to parent
   - [ ] Test fork creates exact copy

2. **Exec System Call (Priority: HIGH)**
   - [ ] Implement sys_exec() in syscall_proc.c
   - [ ] Parse ELF file format (elf.c)
   - [ ] Create new page table for process
   - [ ] Load program segments into memory
   - [ ] Set up user stack with arguments
   - [ ] Set instruction pointer to entry point
   - [ ] Test loading and running user programs

3. **Exit/Wait System Calls (Priority: MEDIUM)**
   - [ ] Enhance sys_exit() to clean up resources
   - [ ] Mark process as ZOMBIE
   - [ ] Wake up parent process
   - [ ] Implement sys_wait() to collect child status
   - [ ] Clean up zombie processes
   - [ ] Handle orphaned processes

4. **ELF Loader (Priority: HIGH)**
   - [ ] Parse ELF header and program headers
   - [ ] Validate ELF file format
   - [ ] Load segments at correct virtual addresses
   - [ ] Handle BSS section (zero-initialized data)
   - [ ] Support different segment permissions

### Testing Checklist
- [ ] fork() creates exact copy of process
- [ ] Child and parent have separate memory
- [ ] exec() loads ELF file correctly
- [ ] exec() sets up stack with arguments
- [ ] Can run simple user programs (echo)
- [ ] exit() cleans up process resources
- [ ] wait() returns child exit status
- [ ] Orphaned processes handled correctly

### Dependencies
- ✅ Memory management (COMPLETE - ready to use)
- ✅ Process structures (COMPLETE - ready to use)
- ✅ Syscall infrastructure (COMPLETE - ready to use)
- ⚠️ Depends on Person 4 for TARFS (to load ELF files)
- 🔄 Will integrate with Person 1 for scheduler

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

## Person 3: File System & TARFS Lead

### Primary Responsibilities
- TARFS driver (read-only tar filesystem)
- Virtual File System (VFS) layer
- File descriptor management
- File-related system calls (open, close)

### Files to Create
- [ ] `kernel/include/fs/vfs.h` - VFS interface and file_t structure
- [ ] `kernel/fs/vfs.c` - VFS implementation
- [ ] `kernel/include/fs/tarfs.h` - TARFS interface
- [ ] `kernel/fs/tarfs.c` - TARFS driver implementation
- [ ] `kernel/fs/file_descriptor.c` - File descriptor table management
- [ ] `kernel/trap/syscall_fs.c` - File syscalls (open, close, lseek)

### Tasks
1. **TARFS Driver (Priority: HIGH)**
   - [ ] Parse tar archive format (tar_header_t)
   - [ ] Implement tarfs_init() - scan and index tar archive
   - [ ] Implement tarfs_lookup() - find file by path
   - [ ] Implement tarfs_read() - read file contents
   - [ ] Implement tarfs_stat() - get file metadata
   - [ ] Use external symbols (_tarfs_start, _tarfs_end)
   - [ ] Test reading known files from tar

2. **VFS Layer (Priority: HIGH)**
   - [ ] Design file_t structure (type, offset, inode)
   - [ ] Implement vfs_open() - open file by path
   - [ ] Implement vfs_close() - close file
   - [ ] Implement vfs_read() - read from file
   - [ ] Implement vfs_write() - write to file (stub for now)
   - [ ] Connect VFS to TARFS backend

3. **File Descriptor Management (Priority: MEDIUM)**
   - [ ] Add fd_table to process_t structure
   - [ ] Implement fd_alloc() - allocate new fd
   - [ ] Implement fd_get() - get file from fd
   - [ ] Implement fd_close() - close fd
   - [ ] Set up stdin/stdout/stderr (fd 0, 1, 2)

4. **File System Calls (Priority: MEDIUM)**
   - [ ] Implement sys_open() - open file, return fd
   - [ ] Implement sys_close() - close fd
   - [ ] Implement sys_lseek() - seek in file (optional)
   - [ ] Test opening and closing files

### Testing Checklist
- [ ] TARFS can parse tar archive
- [ ] Can list all files in TARFS
- [ ] Can read file contents from TARFS
- [ ] File descriptors allocated correctly
- [ ] open() returns valid fd
- [ ] close() frees fd
- [ ] Multiple processes have separate fd tables
- [ ] Can open same file multiple times

### Dependencies
- ✅ Memory management (COMPLETE - ready to use)
- ✅ Process structures (COMPLETE - ready to use)
- ✅ Syscall infrastructure (COMPLETE - ready to use)
- 🔄 Person 2 will depend on your TARFS for exec()
- 🔄 Person 4 will depend on your VFS for I/O operations

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

## Person 4: I/O & Device Drivers Lead

### Primary Responsibilities
- UART driver (console I/O)
- I/O system calls (read, write)
- User memory management (sbrk)
- Additional device drivers (optional)

### Files to Create
- [ ] `kernel/include/drivers/uart.h` - UART driver interface
- [ ] `kernel/drivers/uart.c` - UART driver implementation
- [ ] `kernel/trap/syscall_io.c` - I/O syscalls (read, write)
- [ ] `kernel/trap/syscall_mem.c` - Memory syscalls (sbrk)
- [ ] `kernel/drivers/console.c` - Console abstraction (optional)

### Tasks
1. **UART Driver (Priority: HIGH)**
   - [ ] Create uart.h and uart.c in kernel/drivers/
   - [ ] Implement uart_init() - Initialize UART hardware (16550 compatible)
   - [ ] Implement uart_putc() - Send character
   - [ ] Implement uart_getc() - Receive character  
   - [ ] Implement uart_can_read() - Check if data available
   - [ ] Handle UART interrupts (optional, can start with polling)
   - [ ] Test console I/O
   - [ ] Integrate with printk if needed

2. **I/O System Calls (Priority: HIGH)**
   - [ ] Implement sys_read() in syscall_io.c
   - [ ] Implement sys_write() in syscall_io.c
   - [ ] Handle fd 0 (stdin) - read from UART
   - [ ] Handle fd 1/2 (stdout/stderr) - write to UART
   - [ ] Handle regular file fds - delegate to VFS (Person 3)
   - [ ] Validate user pointers (copy_from_user, copy_to_user)
   - [ ] Test reading from stdin and writing to stdout

3. **User Memory Management (Priority: MEDIUM)**
   - [ ] Implement sys_sbrk() in syscall_mem.c
   - [ ] Track process heap pointer (brk)
   - [ ] Allocate pages for heap growth
   - [ ] Map new pages into user address space
   - [ ] Handle heap shrinking (optional)
   - [ ] Test user programs allocating memory

4. **Additional Features (Priority: LOW)**
   - [ ] Implement console buffering
   - [ ] Add line editing support (backspace, etc.)
   - [ ] Implement sys_ioctl() for device control (optional)
   - [ ] Add VirtIO block device driver (optional)

### Testing Checklist
- [ ] UART driver sends/receives characters
- [ ] Can read from stdin (keyboard input)
- [ ] Can write to stdout/stderr (console output)
- [ ] User programs can read() and write()
- [ ] sbrk() grows user heap correctly
- [ ] User programs can allocate memory
- [ ] Console I/O works from user space

### Dependencies
- ✅ Memory management (COMPLETE - ready to use)
- ✅ Process structures (COMPLETE - ready to use)
- ✅ Syscall infrastructure (COMPLETE - ready to use)
- 🔄 Will integrate with Person 3 for file-based I/O
- 🔄 Will integrate with Person 1 for process scheduling

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

### Milestone 1: Foundation ✅ COMPLETE
- **Status:** ✅ DONE
- **What's Ready:**
  - ✅ Memory management (PMM, VMM, kmalloc)
  - ✅ Trap handling infrastructure
  - ✅ Process structures and management
  - ✅ System call dispatcher framework
- **Result:** All 4 people can now work in parallel

### Milestone 2: Core Components (Week 1-2) 🚀 ALL PARALLEL
**Everyone works independently on their component:**

- **Person 1:** Scheduler + context switching
- **Person 2:** Fork/exec/wait + ELF loader
- **Person 3:** TARFS + VFS + file descriptors
- **Person 4:** UART driver + read/write syscalls + sbrk

**Integration Point:** End of Week 2
- Merge all branches
- Test that components compile together
- Resolve any interface conflicts

### Milestone 3: Process Execution (Week 3) 🔄 INTEGRATION
- **Who:** Person 1 + Person 2 + Person 3
- **Goal:** Can create, schedule, and run user programs
- **Requirements:**
  - ✅ Scheduler working (Person 1)
  - ✅ Fork/exec working (Person 2)
  - ✅ TARFS working (Person 3)
- **Test:** Echo program runs successfully
- **Integration Tasks:**
  - Person 2's exec() uses Person 3's TARFS to load programs
  - Person 1's scheduler switches between user processes
  - Test full process lifecycle: fork → exec → run → exit

### Milestone 4: I/O & Shell (Week 4) 🔄 FINAL INTEGRATION
- **Who:** All 4 people
- **Goal:** Shell runs, can execute commands, file I/O works
- **Requirements:**
  - ✅ All previous milestones
  - ✅ UART driver (Person 4)
  - ✅ read/write syscalls (Person 4)
  - ✅ File descriptors (Person 3)
- **Test:** Interactive shell with multiple programs
- **Integration Tasks:**
  - Person 4's read/write use Person 3's VFS
  - stdin/stdout/stderr work through UART
  - Shell can execute multiple commands
  - File I/O works for user programs

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
- `pmm.h` - PMM interface
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

## Current Status Summary

### ✅ Completed (Ready to Use)
- ✅ Project structure and build system
- ✅ Core header files (types.h, riscv.h, printk.h)
- ✅ Physical memory manager (PMM) - bitmap allocator
- ✅ Virtual memory manager (VMM) - Sv39 page tables
- ✅ Kernel heap allocator (kmalloc) - first-fit with coalescing
- ✅ Trap/interrupt infrastructure - trap_entry.S, trap.c
- ✅ Process structures - process.h, process.c
- ✅ System call infrastructure - syscall.h, syscall.c (dispatcher + stubs)
- ✅ Project planning and documentation

### 🚀 Ready for Parallel Development (All Can Start NOW)

**Person 1:** Scheduler & Context Switching
- ⏳ scheduler.c, context_switch.S
- ⏳ ~400 lines to write
- ✅ No blockers - can start immediately

**Person 2:** Fork/Exec/Wait & ELF Loader
- ⏳ syscall_proc.c, elf.c
- ⏳ ~500 lines to write
- ✅ No blockers - can start immediately (will integrate with Person 3's TARFS later)

**Person 3:** TARFS & VFS & File Descriptors
- ⏳ tarfs.c, vfs.c, file_descriptor.c, syscall_fs.c
- ⏳ ~600 lines to write
- ✅ No blockers - can start immediately

**Person 4:** UART & I/O Syscalls & sbrk
- ⏳ uart.c, syscall_io.c, syscall_mem.c
- ⏳ ~400 lines to write
- ✅ No blockers - can start immediately

### Integration Strategy
**Week 1-2:** Everyone works independently on their components
**Week 2:** Integration meeting - merge branches, resolve conflicts
**Week 3:** Test integrated system - processes running, programs loading
**Week 4:** Polish and testing - shell, multiple programs, stress tests

---

## Questions?

If you're stuck or need clarification:
1. Check this document first
2. Look at the reference implementations (xv6)
3. Ask in team chat
4. Discuss in weekly meeting

Remember: **It's okay to ask for help!** OS development is complex, and we're all learning together.

Good luck! 🚀

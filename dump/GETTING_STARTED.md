# Getting Started with SBUnix Development

## Quick Start

### 1. Build the Kernel
```bash
cd sbunix-skeleton
make clean
make
```

This will:
- Compile all kernel C files
- Assemble all kernel assembly files
- Link everything into `build/kernel.elf`
- Create a tar filesystem with user programs

### 2. Run in QEMU
```bash
make qemu
```

You should see:
```
========================================
       SBUnix Operating System
       RISC-V 64-bit Kernel
========================================

Kernel: 0x80200000 - 0x80210000 (64 KB)
PMM: Initialized 120 MB (30720 pages, 30704 free)
...
Kernel initialization complete!
```

Press `Ctrl+A` then `X` to exit QEMU.

## Project Structure

```
sbunix-skeleton/
├── kernel/              # Kernel source code
│   ├── include/         # Kernel headers
│   │   ├── types.h      # Basic types
│   │   ├── riscv.h      # RISC-V CSR definitions
│   │   ├── physical_memory_mapper.h  # PMM interface
│   │   ├── vmm.h        # VMM interface
│   │   ├── kmalloc.h    # Kernel heap
│   │   ├── trap.h       # Interrupt handling
│   │   ├── process.h    # Process management
│   │   └── syscall.h    # System calls
│   ├── kernel.c         # Main kernel entry
│   ├── pmm.c            # Physical memory manager
│   ├── vmm.c            # Virtual memory manager
│   ├── kmalloc.c        # Kernel heap allocator
│   ├── trap.c           # Trap handler
│   ├── trap_entry.S     # Trap entry/exit assembly
│   ├── process.c        # Process management
│   ├── syscall.c        # System call dispatcher
│   ├── start.S          # Boot assembly
│   └── kernel.ld        # Linker script
├── libc/                # C library for user programs
│   ├── include/
│   └── *.c
├── bin/                 # User programs
│   └── echo/
├── build/               # Build output
├── dump/                # Documentation
└── Makefile
```

## Development Workflow

### 1. Create a Feature Branch
```bash
git checkout -b your-name-feature-name
```

### 2. Make Changes
Edit the relevant files for your component.

### 3. Build and Test
```bash
make clean
make
make qemu
```

### 4. Commit Your Changes
```bash
git add .
git commit -m "[component] Brief description

Detailed explanation

Tested: What you tested"
```

### 5. Push and Create Pull Request
```bash
git push origin your-name-feature-name
```

Then create a pull request on your Git hosting platform.

## Common Tasks

### Adding a New Kernel File

1. Create the file in `kernel/` (e.g., `kernel/scheduler.c`)
2. Create header in `kernel/include/` (e.g., `kernel/include/scheduler.h`)
3. Include the header where needed
4. Run `make` - the Makefile automatically picks up new files

### Adding Debug Output

Use `printk()` anywhere in kernel code:
```c
#include "printk.h"

void my_function(void) {
  printk("Debug: value = %d\n", some_value);
}
```

### Testing Memory Allocation

```c
void test_memory(void) {
  void *ptr = kmalloc(100);
  if (ptr == NULL) {
    printk("Allocation failed!\n");
    return;
  }
  
  // Use the memory
  char *str = (char *)ptr;
  str[0] = 'H';
  str[1] = 'i';
  str[2] = '\0';
  
  printk("String: %s\n", str);
  
  kfree(ptr);
}
```

### Debugging with GDB

Terminal 1:
```bash
make qemu-gdb
```

Terminal 2:
```bash
riscv64-unknown-elf-gdb build/kernel.elf
(gdb) target remote :1234
(gdb) break boot
(gdb) continue
(gdb) step
(gdb) print some_variable
(gdb) x/10x 0x80200000  # Examine memory
```

## Understanding the Boot Process

1. **QEMU loads kernel** at `0x80200000`
2. **start.S** sets up stack and calls `boot()`
3. **boot()** in kernel.c:
   - Initializes PMM (physical memory)
   - Initializes VMM (virtual memory/paging)
   - Initializes kmalloc (kernel heap)
   - Initializes trap handling
   - Initializes process management
   - Initializes system calls
4. **Kernel enters idle loop** waiting for interrupts

## Memory Layout

```
Physical Memory:
0x80000000 - 0x80200000  OpenSBI firmware
0x80200000 - 0x80xxxxxx  Kernel code/data/bss
0x80xxxxxx - 0x88000000  Free memory (managed by PMM)

Virtual Memory (after paging enabled):
0x80000000 - 0x88000000  Kernel (identity mapped)
0x10000000 - 0x10010000  Devices (UART, etc.)
```

## Key Concepts

### Physical Memory Manager (PMM)
- Manages physical RAM as 4KB pages
- Uses bitmap to track free/used pages
- Functions: `pmm_alloc_page()`, `pmm_free_page()`

### Virtual Memory Manager (VMM)
- Manages Sv39 page tables (3-level)
- Maps virtual addresses to physical addresses
- Functions: `vmm_map()`, `vmm_unmap()`, `vmm_walk()`

### Kernel Heap (kmalloc)
- Dynamic memory allocation for kernel
- Built on top of PMM
- Functions: `kmalloc()`, `kfree()`, `kzalloc()`

### Trap Handling
- Handles interrupts and exceptions
- Saves/restores register state
- Dispatches to appropriate handler

### Process Management
- Process Control Block (PCB) stores process state
- Scheduler decides which process runs
- Context switch saves/restores CPU state

### System Calls
- User programs request kernel services
- `ecall` instruction traps to kernel
- Kernel validates and performs operation

## Troubleshooting

### Kernel Doesn't Boot
- Check for compilation errors: `make clean && make`
- Verify linker script is correct
- Check that `start.S` is being assembled

### Kernel Crashes Immediately
- Look at the exception message
- Use GDB to see where it crashed
- Check for null pointer dereferences
- Verify memory addresses are valid

### Memory Allocation Fails
- Check if PMM was initialized
- Verify there's free memory: `pmm_free_memory()`
- Look for memory leaks (allocations without frees)

### Page Fault
- Check if virtual address is mapped
- Verify page table entries have correct permissions
- Use `vmm_translate()` to check mappings

### Build Errors
```bash
# Clean and rebuild
make clean
make

# Check for missing headers
ls kernel/include/

# Verify toolchain is installed
riscv64-unknown-elf-gcc --version
```

## Useful Make Targets

```bash
make              # Build kernel
make clean        # Remove build directory
make qemu         # Run in QEMU
make qemu-gdb     # Run in QEMU with GDB server
```

## Code Style

### Naming Conventions
- Functions: `lowercase_with_underscores()`
- Types: `lowercase_with_underscores_t`
- Macros/Constants: `UPPERCASE_WITH_UNDERSCORES`
- Global variables: `lowercase_with_underscores`

### Example
```c
#define MAX_PROCS 64

typedef struct process {
  int pid;
  int state;
} process_t;

static process_t proc_table[MAX_PROCS];

process_t *proc_alloc(void) {
  // Implementation
}
```

### Comments
```c
// Single line comment for brief explanation

/*
 * Multi-line comment for detailed explanation
 * of complex logic or algorithms
 */
```

## Testing Your Code

### Unit Test Pattern
```c
void test_my_feature(void) {
  printk("Testing my_feature...\n");
  
  // Setup
  void *ptr = setup_test();
  
  // Test
  int result = my_feature(ptr);
  
  // Verify
  if (result == expected_value) {
    printk("  PASS: my_feature works\n");
  } else {
    printk("  FAIL: expected %d, got %d\n", expected_value, result);
  }
  
  // Cleanup
  cleanup_test(ptr);
}
```

Call from `boot()`:
```c
void boot(void) {
  // ... initialization ...
  
  #ifdef RUN_TESTS
  test_my_feature();
  #endif
  
  // ... rest of boot ...
}
```

## Resources

### Documentation
- See `dump/PROJECT_PLAN.md` for overall architecture
- See `dump/TEAM_TASKS.md` for task distribution
- RISC-V specs: https://riscv.org/technical/specifications/

### Reference Code
- xv6-riscv: https://github.com/mit-pdos/xv6-riscv
- Linux RISC-V: https://github.com/torvalds/linux/tree/master/arch/riscv

### Tools
- QEMU documentation: https://www.qemu.org/docs/master/
- GDB manual: https://sourceware.org/gdb/documentation/

## Getting Help

1. **Check documentation** in `dump/` directory
2. **Read the code** - start from `kernel/kernel.c`
3. **Look at xv6** for reference implementation
4. **Ask the team** in chat or meetings
5. **Debug with printk** - add debug output liberally

## Next Steps

Based on your role (see `TEAM_TASKS.md`):

**Person 1 (Memory):** Test PMM/VMM thoroughly, add edge case handling

**Person 2 (Process):** Implement scheduler and context switching

**Person 3 (Syscalls):** Implement fork/exec/exit/wait

**Person 4 (Filesystem):** Implement TARFS and file I/O

Good luck! 🚀

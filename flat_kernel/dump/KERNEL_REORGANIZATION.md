# Kernel Directory Reorganization

## Summary

The kernel directory has been reorganized from a flat structure into a logical, hierarchical layout that groups related functionality together.

## New Directory Structure

```
kernel/
kernel/
├── arch/
│   └── riscv/
│       ├── start.S
│       ├── trap_entry.S
│       └── sbi.c
│
├── drivers/ 
│   └── console.c
|
├── core/
│   ├── kernel.c                 # Main kernel initialization
│   └── printk.c                 # Core kernel printing
│
├── mm/                          # Memory Management
│   ├── pmm.c                    # Physical memory manager
│   ├── vmm.c                    # Virtual memory manager
│   └── kmalloc.c                # Kernel heap allocator
│
├── process/
│   └── process.c                # Process management
│
├── trap/
│   ├── trap.c                   # Trap handling
│   └── syscall.c                # System call handling
│
├── experiments/                 # Experimental language implementations
│   ├── kernel.rs                # Rust experiments
│   ├── kernel.zig               # Zig experiments
│   ├── printk.rs
│   └── printk.zig
│
├── include/
│   ├── arch/
│   │   └── riscv.h              # RISC-V specific definitions
│   │   └── sbi.h
│   ├── kernel/
│   │   ├── types.h              # Basic type definitions
│   │   └── printk.h             # Kernel printing interface
│   |   ├── console.h
│   ├── mm/
│   │   ├── pmm.h
│   │   ├── vmm.h
│   │   └── kmalloc.h
│   ├── process/
│   │   └── process.h
│   └── trap/
│       ├── trap.h
│       └── syscall.h
│
└── kernel.ld                    # Linker script (stays at root)
```

## Changes Made

### 1. Source File Organization
- **Architecture-specific code** → `arch/riscv/`
  - `start.S`, `trap_entry.S`
  
- **Core kernel code** → `core/`
  - `kernel.c`, `printk.c`
  
- **Memory management** → `mm/`
  - `pmm.c`, `vmm.c`, `kmalloc.c`
  
- **Process management** → `process/`
  - `process.c`
  
- **Trap and syscall handling** → `trap/`
  - `trap.c`, `syscall.c`
  
- **Experimental code** → `experiments/`
  - `*.rs`, `*.zig` files

### 2. Header File Organization
Headers now mirror the source structure:
- `include/arch/` - Architecture-specific headers
- `include/kernel/` - Core kernel headers
- `include/mm/` - Memory management headers
- `include/process/` - Process management headers
- `include/trap/` - Trap and syscall headers

### 3. Include Path Updates
All source files now use hierarchical include paths:
```c
// Old style
#include "printk.h"
#include "vmm.h"

// New style
#include "kernel/printk.h"
#include "mm/vmm.h"
```

### 4. Makefile Updates
- Updated to search for source files in new subdirectories
- Changed include path to use unified `-Ikernel/include`
- Updated experimental language file paths

## Benefits

1. **Logical Grouping**: Related functionality is grouped together
2. **Scalability**: Easy to add new subsystems (e.g., `fs/`, `drivers/`)
3. **Architecture Separation**: RISC-V specific code is isolated
4. **Clear Dependencies**: Include paths show module relationships
5. **Cleaner Root**: Kernel root is no longer cluttered with many files
6. **Professional Structure**: Follows standard OS kernel organization

## Build Instructions

The build process remains the same:
```bash
make                    # Build kernel
make qemu              # Run in QEMU
make clean             # Clean build artifacts
```

The Makefile has been updated to automatically find source files in the new structure.

## Migration Notes

- All include paths have been updated to use the new hierarchical structure
- The Makefile now uses `-Ikernel/include` as a unified include directory
- Experimental Rust and Zig files have been moved to `experiments/` directory
- No functional changes to the code - only organizational improvements

## Future Expansion

The new structure makes it easy to add:
- `kernel/fs/` - Filesystem implementation
- `kernel/drivers/` - Device drivers
- `kernel/net/` - Network stack
- `kernel/ipc/` - Inter-process communication
- Additional architectures under `kernel/arch/`

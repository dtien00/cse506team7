```
kernel/
kernel/
├── arch/
│   └── riscv/
│       ├── start.S
│       ├── trap_entry.S
│       └── sbi.c
|
├── core/
│   ├── kernel.c                 # Main kernel initialization
│   └── printk.c                 # Core kernel printing
│
├── drivers/ 
│   └── console.c
│
├── fs/
│   ├── file.c
│   ├── tarfs.c 
│   └── vfs.c   
├── include/
│   ├── arch/
│   │   ├── riscv.h              # RISC-V specific definitions
│   │   └── sbi.h
│   ├── fs/
│   │   ├── file.h 
│   │   ├── tarfs.h 
│   │   └── vfs.h   
│   ├── kernel/
│   │   ├── types.h              # Basic type definitions
│   │   ├── printk.h             # Kernel printing interface
│   |   └── console.h
│   ├── lib/
│   │   └── stdarg.h
│   ├── mm/
│   │   ├── pmm.h
│   │   ├── vmm.h
│   │   └── kmalloc.h
│   ├── process/
│   │   └── process.h
│   └── trap/
│       ├── trap.h
│       └── syscall.h
|
├── experiments/                 # Experimental language implementations
│   ├── kernel.rs                # Rust experiments
│   ├── kernel.zig               # Zig experiments
│   ├── printk.rs
│   └── printk.zig
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
└── kernel.ld                    # Linker script (stays at root)
```
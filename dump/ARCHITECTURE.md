# SBUnix Architecture Diagram

## System Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                        User Space                                │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐       │
│  │  Shell   │  │   Echo   │  │   Cat    │  │  Other   │       │
│  │          │  │          │  │          │  │  Programs│       │
│  └────┬─────┘  └────┬─────┘  └────┬─────┘  └────┬─────┘       │
│       │             │             │             │               │
│       └─────────────┴─────────────┴─────────────┘               │
│                         │                                        │
│                    System Calls (ecall)                          │
└─────────────────────────┼──────────────────────────────────────┘
                          │
┌─────────────────────────┼──────────────────────────────────────┐
│                    Kernel Space                                  │
│                         │                                        │
│  ┌──────────────────────▼───────────────────────────────────┐  │
│  │              System Call Interface                        │  │
│  │  fork│exec│exit│wait│read│write│open│close│sbrk│...     │  │
│  └──┬────┬────┬────┬────┬────┬────┬────┬────┬────┬─────────┘  │
│     │    │    │    │    │    │    │    │    │                  │
│  ┌──▼────▼────▼────▼────┴────┴────┴────┴────┴─────────────┐   │
│  │           Process Management Layer                       │   │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐             │   │
│  │  │ Process  │  │Scheduler │  │ Context  │             │   │
│  │  │  Table   │  │          │  │ Switch   │             │   │
│  │  └──────────┘  └──────────┘  └──────────┘             │   │
│  └───────────────────┬──────────────────────────────────────┘  │
│                      │                                          │
│  ┌───────────────────▼──────────────────────────────────────┐  │
│  │              Memory Management Layer                      │  │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐              │  │
│  │  │   VMM    │  │   PMM    │  │ kmalloc  │              │  │
│  │  │(Paging)  │  │ (Pages)  │  │  (Heap)  │              │  │
│  │  └──────────┘  └──────────┘  └──────────┘              │  │
│  └───────────────────┬──────────────────────────────────────┘  │
│                      │                                          │
│  ┌───────────────────▼──────────────────────────────────────┐  │
│  │              File System Layer                            │  │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐              │  │
│  │  │   VFS    │  │  TARFS   │  │   FD     │              │  │
│  │  │          │  │          │  │  Table   │              │  │
│  │  └──────────┘  └──────────┘  └──────────┘              │  │
│  └───────────────────┬──────────────────────────────────────┘  │
│                      │                                          │
│  ┌───────────────────▼──────────────────────────────────────┐  │
│  │           Interrupt/Trap Handling Layer                   │  │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐              │  │
│  │  │  Trap    │  │Exception │  │ Interrupt│              │  │
│  │  │  Entry   │  │ Handler  │  │ Handler  │              │  │
│  │  └──────────┘  └──────────┘  └──────────┘              │  │
│  └───────────────────┬──────────────────────────────────────┘  │
│                      │                                          │
└──────────────────────┼──────────────────────────────────────────┘
                       │
┌──────────────────────▼──────────────────────────────────────────┐
│                    Hardware Layer                                │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐       │
│  │   CPU    │  │  Memory  │  │   UART   │  │  Timer   │       │
│  │ (RISC-V) │  │  (RAM)   │  │          │  │          │       │
│  └──────────┘  └──────────┘  └──────────┘  └──────────┘       │
└─────────────────────────────────────────────────────────────────┘
```

## Component Dependencies

```
                    ┌──────────┐
                    │  Boot    │
                    └────┬─────┘
                         │
        ┌────────────────┼────────────────┐
        │                │                │
        ▼                ▼                ▼
    ┌───────┐       ┌────────┐      ┌────────┐
    │  PMM  │◄──────│  VMM   │◄─────│kmalloc │
    └───┬───┘       └───┬────┘      └────────┘
        │               │
        │               │
        ▼               ▼
    ┌───────────────────────┐
    │   Trap Handling       │
    └───────┬───────────────┘
            │
            ▼
    ┌───────────────────────┐
    │  Process Management   │
    └───────┬───────────────┘
            │
            ▼
    ┌───────────────────────┐
    │   System Calls        │
    └───────┬───────────────┘
            │
            ▼
    ┌───────────────────────┐
    │   File System         │
    └───────────────────────┘
```

## Memory Management Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    Virtual Memory (VMM)                      │
│                                                              │
│  User Space:                                                │
│  ┌──────────────────────────────────────────────────────┐  │
│  │ 0x00000000 - 0x00001000  [Unmapped]                  │  │
│  │ 0x00001000 - 0x........  [User Code/Data]            │  │
│  │ 0x........  - 0x........  [User Heap (sbrk)]         │  │
│  │ 0x........  - 0x........  [User Stack]               │  │
│  │ 0xFFFFFFFF - 0xFFFFFFFF  [Trapframe]                 │  │
│  └──────────────────────────────────────────────────────┘  │
│                                                              │
│  Kernel Space:                                              │
│  ┌──────────────────────────────────────────────────────┐  │
│  │ 0x80000000 - 0x80200000  [OpenSBI]                   │  │
│  │ 0x80200000 - 0x........  [Kernel Code/Data/BSS]      │  │
│  │ 0x........  - 0x........  [Kernel Heap (kmalloc)]    │  │
│  │ 0x........  - 0x88000000  [Free Memory]              │  │
│  └──────────────────────────────────────────────────────┘  │
│                                                              │
│  Device Space:                                              │
│  ┌──────────────────────────────────────────────────────┐  │
│  │ 0x10000000 - 0x10010000  [UART, Devices]             │  │
│  └──────────────────────────────────────────────────────┘  │
└──────────────────┬───────────────────────────────────────────┘
                   │
                   │ Maps to
                   ▼
┌─────────────────────────────────────────────────────────────┐
│                  Physical Memory (PMM)                       │
│                                                              │
│  ┌──────────────────────────────────────────────────────┐  │
│  │ 0x80000000 - 0x80200000  [OpenSBI]                   │  │
│  │ 0x80200000 - 0x........  [Kernel]                    │  │
│  │ 0x........  - 0x........  [PMM Bitmap]               │  │
│  │ 0x........  - 0x........  [Kernel Heap]              │  │
│  │ 0x........  - 0x88000000  [Free Pages (4KB each)]    │  │
│  └──────────────────────────────────────────────────────┘  │
│                                                              │
│  Managed as 4KB pages:                                      │
│  ┌───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┐       │
│  │ U │ U │ U │ F │ F │ F │ U │ F │ F │ U │...│ F │       │
│  └───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┘       │
│   U = Used, F = Free                                        │
└─────────────────────────────────────────────────────────────┘
```

## Page Table Structure (Sv39)

```
Virtual Address (39 bits):
┌─────────┬─────────┬─────────┬──────────────┐
│ VPN[2]  │ VPN[1]  │ VPN[0]  │   Offset     │
│ 9 bits  │ 9 bits  │ 9 bits  │   12 bits    │
└─────────┴─────────┴─────────┴──────────────┘

Page Table Walk:
                    ┌──────────────┐
                    │  SATP (CSR)  │
                    └──────┬───────┘
                           │
                           ▼
                    ┌──────────────┐
                    │  Level 2 PT  │ ◄── 512 entries
                    │  (Root)      │
                    └──────┬───────┘
                           │ [VPN[2]]
                           ▼
                    ┌──────────────┐
                    │  Level 1 PT  │ ◄── 512 entries
                    └──────┬───────┘
                           │ [VPN[1]]
                           ▼
                    ┌──────────────┐
                    │  Level 0 PT  │ ◄── 512 entries
                    └──────┬───────┘
                           │ [VPN[0]]
                           ▼
                    ┌──────────────┐
                    │     PTE      │
                    │  (Physical)  │
                    └──────┬───────┘
                           │
                           ▼
                  Physical Address

Page Table Entry (PTE):
┌──────────────────────────────────────────┬─────────────┐
│           PPN (Physical Page Number)     │    Flags    │
│               44 bits                    │   10 bits   │
└──────────────────────────────────────────┴─────────────┘
                                            │││││││││││
                                            │││││││││└─ V (Valid)
                                            ││││││││└── R (Read)
                                            │││││││└─── W (Write)
                                            ││││││└──── X (Execute)
                                            │││││└───── U (User)
                                            ││││└────── G (Global)
                                            │││└─────── A (Accessed)
                                            ││└──────── D (Dirty)
                                            └┴───────── Reserved
```

## Process Structure

```
┌─────────────────────────────────────────────────────────────┐
│                     Process (PCB)                            │
├─────────────────────────────────────────────────────────────┤
│  pid: 1                                                      │
│  state: RUNNING                                              │
│  name: "shell"                                               │
├─────────────────────────────────────────────────────────────┤
│  Memory:                                                     │
│  ┌──────────────────────────────────────────────────────┐  │
│  │  pagetable ───────┐                                  │  │
│  │  sz: 65536        │                                  │  │
│  └───────────────────┼──────────────────────────────────┘  │
│                      │                                      │
│                      ▼                                      │
│              ┌───────────────┐                              │
│              │  Page Table   │                              │
│              │  (3 levels)   │                              │
│              └───────────────┘                              │
├─────────────────────────────────────────────────────────────┤
│  Context (for scheduling):                                  │
│  ┌──────────────────────────────────────────────────────┐  │
│  │  context_sp: 0x80240000                              │  │
│  │  context_ra: 0x80201234                              │  │
│  │  context_s0-s11: [saved registers]                   │  │
│  └──────────────────────────────────────────────────────┘  │
├─────────────────────────────────────────────────────────────┤
│  Trapframe (for user mode):                                │
│  ┌──────────────────────────────────────────────────────┐  │
│  │  All 32 registers (ra, sp, gp, tp, t0-t6, s0-s11,   │  │
│  │  a0-a7)                                              │  │
│  └──────────────────────────────────────────────────────┘  │
├─────────────────────────────────────────────────────────────┤
│  parent: NULL                                               │
│  exit_status: 0                                             │
└─────────────────────────────────────────────────────────────┘
```

## Trap/Interrupt Flow

```
User Mode                    Kernel Mode
─────────                    ───────────

[User Code]
    │
    │ ecall (system call)
    │ or exception
    │ or interrupt
    ▼
┌────────────┐
│  Hardware  │
│  - Save PC to sepc
│  - Save cause to scause
│  - Jump to stvec
└─────┬──────┘
      │
      ▼
┌─────────────────┐
│  trap_entry.S   │
│  - Save all 32  │
│    registers    │
│  - Call handler │
└─────┬───────────┘
      │
      ▼
┌─────────────────┐
│  trap_handler() │
│  - Check scause │
│  - Dispatch     │
└─────┬───────────┘
      │
      ├─── Interrupt? ──────┐
      │                     │
      │                     ▼
      │              ┌──────────────┐
      │              │  Timer       │
      │              │  External    │
      │              │  Software    │
      │              └──────────────┘
      │
      ├─── Exception? ──────┐
      │                     │
      │                     ▼
      │              ┌──────────────┐
      │              │  Page Fault  │
      │              │  Illegal Inst│
      │              │  Misaligned  │
      │              └──────────────┘
      │
      └─── System Call? ────┐
                            │
                            ▼
                     ┌──────────────┐
                     │syscall_handler
                     │  - Dispatch  │
                     │  - Execute   │
                     │  - Return    │
                     └──────┬───────┘
                            │
                            ▼
                     ┌──────────────┐
                     │  trap_entry.S│
                     │  - Restore   │
                     │    registers │
                     │  - sret      │
                     └──────┬───────┘
                            │
                            ▼
                     [Return to User]
```

## System Call Flow

```
User Program                 Kernel
────────────                 ──────

write(1, "Hi", 2)
    │
    │ Prepare arguments:
    │   a7 = SYS_write (4)
    │   a0 = fd (1)
    │   a1 = buf ("Hi")
    │   a2 = count (2)
    │
    ▼
┌─────────────┐
│   ecall     │ ─────────────────┐
└─────────────┘                  │
                                 │
                                 ▼
                          ┌──────────────┐
                          │ trap_entry.S │
                          │ Save state   │
                          └──────┬───────┘
                                 │
                                 ▼
                          ┌──────────────┐
                          │trap_handler()│
                          │ Check scause │
                          │ = ECALL_U    │
                          └──────┬───────┘
                                 │
                                 ▼
                          ┌──────────────┐
                          │syscall_handler
                          │ Read a7 (4)  │
                          │ = SYS_write  │
                          └──────┬───────┘
                                 │
                                 ▼
                          ┌──────────────┐
                          │ sys_write()  │
                          │ fd=1, buf,   │
                          │ count=2      │
                          │ -> printk()  │
                          └──────┬───────┘
                                 │
                                 │ Return value
                                 │ in a0 (2)
                                 │
                                 ▼
                          ┌──────────────┐
                          │ trap_entry.S │
                          │ Restore state│
                          │ sret         │
                          └──────┬───────┘
                                 │
                                 │
┌─────────────┐                  │
│ return 2    │ ◄────────────────┘
└─────────────┘
```

## Scheduler Architecture (To Be Implemented)

```
┌─────────────────────────────────────────────────────────────┐
│                    Process Table                             │
│  ┌─────────┐  ┌─────────┐  ┌─────────┐  ┌─────────┐       │
│  │ PID: 1  │  │ PID: 2  │  │ PID: 3  │  │ PID: 4  │       │
│  │ RUNNING │  │RUNNABLE │  │SLEEPING │  │ ZOMBIE  │       │
│  └────┬────┘  └────┬────┘  └────┬────┘  └────┬────┘       │
│       │            │            │            │              │
└───────┼────────────┼────────────┼────────────┼──────────────┘
        │            │            │            │
        │            │            │            │
        ▼            │            │            │
   ┌────────┐       │            │            │
   │Current │       │            │            │
   │Process │       │            │            │
   └────────┘       │            │            │
                    │            │            │
        ┌───────────┴────────────┘            │
        │                                     │
        ▼                                     │
┌───────────────┐                             │
│   Scheduler   │                             │
│  (Round Robin)│                             │
└───────┬───────┘                             │
        │                                     │
        │ Pick next RUNNABLE                  │
        │                                     │
        ▼                                     │
┌───────────────┐                             │
│Context Switch │                             │
│  - Save old   │                             │
│  - Load new   │                             │
└───────────────┘                             │
                                              │
                                              ▼
                                        ┌──────────┐
                                        │  Reaper  │
                                        │  Clean   │
                                        │  Zombies │
                                        └──────────┘
```

## File System Architecture (To Be Implemented)

```
┌─────────────────────────────────────────────────────────────┐
│                    User Program                              │
│  open("/bin/echo", O_RDONLY)                                │
└─────────────────────┬───────────────────────────────────────┘
                      │ System Call
                      ▼
┌─────────────────────────────────────────────────────────────┐
│                    VFS Layer                                 │
│  ┌──────────────────────────────────────────────────────┐  │
│  │  vfs_open(path, flags)                               │  │
│  │  - Parse path                                        │  │
│  │  - Lookup file                                       │  │
│  │  - Allocate FD                                       │  │
│  └──────────────────┬───────────────────────────────────┘  │
└─────────────────────┼───────────────────────────────────────┘
                      │
                      ▼
┌─────────────────────────────────────────────────────────────┐
│                  TARFS Driver                                │
│  ┌──────────────────────────────────────────────────────┐  │
│  │  tarfs_lookup(path)                                  │  │
│  │  - Walk tar archive                                  │  │
│  │  - Find matching entry                               │  │
│  │  - Return file info                                  │  │
│  └──────────────────┬───────────────────────────────────┘  │
└─────────────────────┼───────────────────────────────────────┘
                      │
                      ▼
┌─────────────────────────────────────────────────────────────┐
│                  TAR Archive                                 │
│  ┌──────────┬──────────┬──────────┬──────────┐            │
│  │ Header 1 │  Data 1  │ Header 2 │  Data 2  │ ...        │
│  │ /bin/echo│  [code]  │ /bin/cat │  [code]  │            │
│  └──────────┴──────────┴──────────┴──────────┘            │
└─────────────────────────────────────────────────────────────┘
```

## Boot Sequence

```
Time ──────────────────────────────────────────────────────────►

1. QEMU starts
   └─► Loads OpenSBI at 0x80000000
       └─► OpenSBI initializes hardware
           └─► Loads kernel at 0x80200000
               └─► Jumps to _start

2. _start (start.S)
   └─► Set up stack pointer
       └─► Call boot()

3. boot() (kernel.c)
   └─► Print banner
       └─► Initialize PMM
           └─► Initialize VMM
               └─► Enable paging
                   └─► Initialize kmalloc
                       └─► Initialize trap handling
                           └─► Initialize process management
                               └─► Initialize system calls
                                   └─► Run tests
                                       └─► Enable interrupts
                                           └─► Enter idle loop

4. Idle Loop
   └─► Wait for interrupt (wfi)
       └─► Handle interrupt
           └─► Schedule next process
               └─► Repeat
```

## Team Work Distribution

```
┌─────────────────────────────────────────────────────────────┐
│                      Person 1                                │
│                  Memory Management                           │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐                  │
│  │   PMM    │  │   VMM    │  │ kmalloc  │                  │
│  └──────────┘  └──────────┘  └──────────┘                  │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│                      Person 2                                │
│            Interrupt & Process Management                    │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐                  │
│  │   Trap   │  │ Process  │  │Scheduler │                  │
│  └──────────┘  └──────────┘  └──────────┘                  │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│                      Person 3                                │
│              System Calls & User Space                       │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐                  │
│  │   fork   │  │   exec   │  │   sbrk   │                  │
│  └──────────┘  └──────────┘  └──────────┘                  │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│                      Person 4                                │
│                File System & I/O                             │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐                  │
│  │   VFS    │  │  TARFS   │  │   UART   │                  │
│  └──────────┘  └──────────┘  └──────────┘                  │
└─────────────────────────────────────────────────────────────┘
```

## Data Flow Examples

### Example 1: Memory Allocation

```
User: malloc(100)
    │
    ▼
libc: sbrk(100) ──────────────┐
                              │ System Call
                              ▼
                       syscall_handler()
                              │
                              ▼
                         sys_sbrk(100)
                              │
                              ▼
                       pmm_alloc_page() ◄─── Get physical page
                              │
                              ▼
                       vmm_map() ◄────────── Map to user space
                              │
                              ▼
                       Return new address
                              │
                              ▼
User: ptr = 0x10000 ◄─────────┘
```

### Example 2: Process Creation

```
User: fork()
    │
    ▼
System Call ──────────────────┐
                              │
                              ▼
                         sys_fork()
                              │
                              ├─► proc_alloc() ◄──── Allocate PCB
                              │
                              ├─► vmm_create_pagetable() ◄─ New page table
                              │
                              ├─► Copy parent's memory
                              │       │
                              │       ├─► For each page:
                              │       │   ├─► pmm_alloc_page()
                              │       │   ├─► Copy data
                              │       │   └─► vmm_map()
                              │       │
                              ├─► Copy trapframe
                              │
                              ├─► Set child's return value = 0
                              │
                              ├─► Set parent's return value = child PID
                              │
                              └─► Add child to scheduler
                                      │
                                      ▼
                              Both processes runnable
```

### Example 3: File Read

```
User: read(fd, buf, 100)
    │
    ▼
System Call ──────────────────┐
                              │
                              ▼
                         sys_read(fd, buf, 100)
                              │
                              ├─► Validate fd
                              │
                              ├─► Get file from FD table
                              │
                              ▼
                         vfs_read(file, buf, 100)
                              │
                              ├─► Check permissions
                              │
                              ▼
                         tarfs_read(file, buf, 100)
                              │
                              ├─► Find file in tar
                              │
                              ├─► Copy data to buffer
                              │
                              └─► Return bytes read
                                      │
                                      ▼
User: n = 100 ◄───────────────────────┘
```

## Summary

This architecture provides:

1. **Clean layering** - Each layer has clear responsibilities
2. **Modularity** - Components can be developed independently
3. **Extensibility** - Easy to add new features
4. **Testability** - Each component can be tested separately
5. **Team parallelism** - 4 people can work simultaneously

The foundation (memory management, trap handling, process structures) is complete. Your team can now build the higher-level features (scheduler, syscalls, filesystem) on this solid base.

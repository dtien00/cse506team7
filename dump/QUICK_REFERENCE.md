# SBUnix Quick Reference Guide

## Memory Management API

### Physical Memory Manager (PMM)
```c
#include "pmm.h"

// Initialize PMM
void pmm_init(uintptr_t mem_start, size_t mem_size);

// Allocate/free single page (4KB)
void *pmm_alloc_page(void);      // Returns physical address
void pmm_free_page(void *page);

// Allocate/free multiple pages
void *pmm_alloc_pages(size_t n);
void pmm_free_pages(void *page, size_t n);

// Query memory
size_t pmm_free_memory(void);    // Free bytes
size_t pmm_total_memory(void);   // Total bytes
```

### Virtual Memory Manager (VMM)
```c
#include "vmm.h"

// Initialize VMM (sets up kernel page table)
void vmm_init(void);

// Create/destroy page tables
pagetable_t vmm_create_pagetable(void);
void vmm_free_pagetable(pagetable_t pt);

// Map/unmap pages
int vmm_map(pagetable_t pt, uintptr_t va, uintptr_t pa, uint64_t perm);
void vmm_unmap(pagetable_t pt, uintptr_t va);

// Map/unmap ranges
int vmm_map_range(pagetable_t pt, uintptr_t va, uintptr_t pa, size_t size, uint64_t perm);
void vmm_unmap_range(pagetable_t pt, uintptr_t va, size_t size);

// Utilities
uintptr_t vmm_translate(pagetable_t pt, uintptr_t va);
void vmm_switch(pagetable_t pt);
pagetable_t vmm_kernel_pagetable(void);
```

### Page Permissions (from riscv.h)
```c
#define PTE_V  (1UL << 0)   // Valid
#define PTE_R  (1UL << 1)   // Readable
#define PTE_W  (1UL << 2)   // Writable
#define PTE_X  (1UL << 3)   // Executable
#define PTE_U  (1UL << 4)   // User accessible

// Common combinations
PTE_R | PTE_W           // Read-write data
PTE_R | PTE_X           // Read-execute code
PTE_R | PTE_W | PTE_U   // User read-write
```

### Kernel Heap (kmalloc)
```c
#include "kmalloc.h"

// Initialize heap
void kmalloc_init(void);

// Allocate/free
void *kmalloc(size_t size);      // Allocate
void *kzalloc(size_t size);      // Allocate and zero
void *krealloc(void *ptr, size_t size);
void kfree(void *ptr);

// Debug
void kmalloc_stats(void);
```

## Process Management API

### Process Operations
```c
#include "process.h"

// Initialize process subsystem
void proc_init(void);

// Allocate/free processes
process_t *proc_alloc(void);
void proc_free(process_t *p);

// Current process
process_t *proc_current(void);
void proc_set_current(process_t *p);

// Find process
process_t *proc_find(pid_t pid);

// Debug
void proc_dump(void);
```

### Process States
```c
typedef enum {
  PROC_UNUSED = 0,
  PROC_EMBRYO,     // Being created
  PROC_RUNNABLE,   // Ready to run
  PROC_RUNNING,    // Currently running
  PROC_SLEEPING,   // Waiting for event
  PROC_ZOMBIE      // Exited but not yet cleaned up
} proc_state_t;
```

### Process Structure
```c
typedef struct process {
  pid_t pid;
  proc_state_t state;
  char name[PROC_NAME_LEN];
  pagetable_t pagetable;
  uint64_t sz;
  uint64_t context_sp;
  uint64_t context_ra;
  uint64_t context_s[12];
  trapframe_t *trapframe;
  struct process *parent;
  int exit_status;
} process_t;
```

## Interrupt/Trap Handling API

### Trap Operations
```c
#include "trap.h"

// Initialize trap handling
void trap_init(void);

// Trap handler (called from assembly)
void trap_handler(trapframe_t *tf);

// Timer handler
void timer_handler(void);
```

### Trapframe Structure
```c
typedef struct trapframe {
  uint64_t ra;   // Return address
  uint64_t sp;   // Stack pointer
  uint64_t gp;   // Global pointer
  uint64_t tp;   // Thread pointer
  uint64_t t0-t6;  // Temporary registers
  uint64_t s0-s11; // Saved registers
  uint64_t a0-a7;  // Argument registers
} trapframe_t;
```

## System Call API

### System Call Numbers
```c
#include "syscall.h"

#define SYS_exit     1
#define SYS_fork     2
#define SYS_read     3
#define SYS_write    4
#define SYS_open     5
#define SYS_close    6
#define SYS_wait     7
#define SYS_exec     8
#define SYS_getpid   9
#define SYS_sbrk     10
#define SYS_sleep    11
#define SYS_uptime   12
#define SYS_kill     13
```

### System Call Functions
```c
void syscall_init(void);
void syscall_handler(trapframe_t *tf);

// Individual syscalls (implement these)
void sys_exit(int status);
pid_t sys_fork(void);
ssize_t sys_read(int fd, void *buf, size_t count);
ssize_t sys_write(int fd, const void *buf, size_t count);
// ... etc
```

## RISC-V CSR Operations

### Reading CSRs
```c
#include "riscv.h"

uint64_t sstatus = r_sstatus();
uint64_t sie = r_sie();
uint64_t sip = r_sip();
uint64_t stvec = r_stvec();
uint64_t scause = r_scause();
uint64_t stval = r_stval();
uint64_t sepc = r_sepc();
uint64_t satp = r_satp();
uint64_t sp = r_sp();
uint64_t tp = r_tp();
```

### Writing CSRs
```c
w_sstatus(value);
w_sie(value);
w_sip(value);
w_stvec(value);
w_sepc(value);
w_satp(value);
w_tp(value);
```

### Interrupt Control
```c
intr_on();              // Enable interrupts
intr_off();             // Disable interrupts
int enabled = intr_get(); // Check if enabled
```

### SSTATUS Bits
```c
#define SSTATUS_SIE  (1UL << 1)   // Supervisor Interrupt Enable
#define SSTATUS_SPIE (1UL << 5)   // Previous Interrupt Enable
#define SSTATUS_SPP  (1UL << 8)   // Previous Privilege
#define SSTATUS_SUM  (1UL << 18)  // Supervisor User Memory
```

### Interrupt Enable (SIE)
```c
#define SIE_SEIE (1UL << 9)   // External interrupt
#define SIE_STIE (1UL << 5)   // Timer interrupt
#define SIE_SSIE (1UL << 1)   // Software interrupt
```

### Exception Codes (SCAUSE)
```c
#define SCAUSE_INST_MISALIGNED    0
#define SCAUSE_INST_ACCESS_FAULT  1
#define SCAUSE_ILLEGAL_INST       2
#define SCAUSE_BREAKPOINT         3
#define SCAUSE_LOAD_MISALIGNED    4
#define SCAUSE_LOAD_ACCESS_FAULT  5
#define SCAUSE_STORE_MISALIGNED   6
#define SCAUSE_STORE_ACCESS_FAULT 7
#define SCAUSE_ECALL_FROM_U       8
#define SCAUSE_INST_PAGE_FAULT    12
#define SCAUSE_LOAD_PAGE_FAULT    13
#define SCAUSE_STORE_PAGE_FAULT   15
```

## Utility Macros

### Memory Alignment
```c
#define PGSIZE 4096
#define PGSHIFT 12
#define PGROUNDUP(sz)   (((sz) + PGSIZE - 1) & ~(PGSIZE - 1))
#define PGROUNDDOWN(a)  (((a)) & ~(PGSIZE - 1))
```

### Page Table Manipulation
```c
#define PA_TO_PTE(pa) (((uint64_t)(pa) >> 12) << 10)
#define PTE_TO_PA(pte) (((uint64_t)(pte) >> 10) << 12)
#define PTE_FLAGS(pte) ((pte) & 0x3FF)
```

### Address Translation
```c
#define PXMASK 0x1FF  // 9 bits
#define PXSHIFT(level) (PGSHIFT + (9 * (level)))
#define PX(level, va) ((((uint64_t)(va)) >> PXSHIFT(level)) & PXMASK)
```

### SATP Register
```c
#define SATP_MODE_SV39 (8UL << 60)
#define MAKE_SATP(pagetable) (SATP_MODE_SV39 | ((uint64_t)(pagetable) >> 12))
```

## Memory Layout Constants

```c
#define KERNBASE     0x80000000UL
#define PHYSTOP      0x88000000UL  // 128 MB
#define MAXVA        (1UL << (9 + 9 + 9 + 12 - 1))

#define USTACK_SIZE  (4 * PGSIZE)
#define TRAMPOLINE   (MAXVA - PGSIZE)
#define TRAPFRAME    (TRAMPOLINE - PGSIZE)
```

## Debug Output

### printk
```c
#include "printk.h"

printk("Hello, world!\n");
printk("Value: %d\n", 42);
printk("Hex: 0x%lx\n", 0x80200000);
printk("String: %s\n", "test");
printk("Pointer: %p\n", ptr);
```

### Format Specifiers
- `%d` - signed integer
- `%u` - unsigned integer
- `%ld` - signed long
- `%lu` - unsigned long
- `%x` - hexadecimal (lowercase)
- `%lx` - long hexadecimal
- `%p` - pointer
- `%s` - string
- `%c` - character

## Common Patterns

### Allocating a Page
```c
void *page = pmm_alloc_page();
if (page == NULL) {
  printk("Out of memory!\n");
  return -1;
}

// Use the page
// ...

pmm_free_page(page);
```

### Mapping a Page
```c
pagetable_t pt = vmm_create_pagetable();
void *phys_page = pmm_alloc_page();

if (vmm_map(pt, 0x1000, (uintptr_t)phys_page, PTE_R | PTE_W | PTE_U) != 0) {
  printk("Failed to map page\n");
  pmm_free_page(phys_page);
  vmm_free_pagetable(pt);
  return -1;
}

// Use the mapping
// ...

vmm_unmap(pt, 0x1000);
pmm_free_page(phys_page);
vmm_free_pagetable(pt);
```

### Creating a Process
```c
process_t *p = proc_alloc();
if (p == NULL) {
  printk("No free process slots\n");
  return NULL;
}

// Set up process
p->pagetable = vmm_create_pagetable();
p->trapframe = kzalloc(sizeof(trapframe_t));

// Copy name
const char *name = "myproc";
for (int i = 0; i < PROC_NAME_LEN - 1 && name[i]; i++) {
  p->name[i] = name[i];
}
p->name[PROC_NAME_LEN - 1] = '\0';

p->state = PROC_RUNNABLE;
```

### Handling a System Call
```c
void syscall_handler(trapframe_t *tf) {
  uint64_t syscall_num = tf->a7;
  uint64_t arg0 = tf->a0;
  uint64_t arg1 = tf->a1;
  
  uint64_t ret = 0;
  
  switch (syscall_num) {
    case SYS_write:
      ret = sys_write((int)arg0, (void *)arg1, (size_t)tf->a2);
      break;
    // ... other syscalls
  }
  
  tf->a0 = ret;  // Return value
}
```

### Walking a Page Table
```c
uint64_t *pte = vmm_walk(pagetable, va, 0);
if (pte == NULL || (*pte & PTE_V) == 0) {
  printk("Page not mapped\n");
  return;
}

uintptr_t pa = PTE_TO_PA(*pte);
uint64_t flags = PTE_FLAGS(*pte);

printk("VA 0x%lx -> PA 0x%lx (flags: 0x%lx)\n", va, pa, flags);
```

## Assembly Snippets

### Trap Entry
```asm
.global trap_entry
trap_entry:
  # Save all registers to stack
  addi sp, sp, -256
  sd ra, 0(sp)
  sd sp, 8(sp)
  # ... save other registers
  
  # Call C handler
  mv a0, sp
  call trap_handler
  
  # Restore registers
  ld ra, 0(sp)
  # ... restore other registers
  addi sp, sp, 256
  
  sret
```

### Context Switch
```asm
.global context_switch
context_switch:
  # a0 = old context, a1 = new context
  
  # Save old context
  sd ra, 0(a0)
  sd sp, 8(a0)
  sd s0, 16(a0)
  # ... save s1-s11
  
  # Restore new context
  ld ra, 0(a1)
  ld sp, 8(a1)
  ld s0, 16(a1)
  # ... restore s1-s11
  
  ret
```

## GDB Commands

```bash
# Connect to QEMU
target remote :1234

# Breakpoints
break boot
break trap_handler
break *0x80200000

# Execution
continue
step
next
finish

# Inspection
info registers
print variable_name
x/10x 0x80200000    # Examine memory (hex)
x/10i 0x80200000    # Examine instructions
backtrace

# Watchpoints
watch variable_name

# Display
display variable_name
```

## Common Errors and Solutions

### "Page fault at 0x..."
- Check if address is mapped: `vmm_translate()`
- Verify page table is set up
- Check page permissions (R/W/X/U bits)

### "Out of memory"
- Check `pmm_free_memory()`
- Look for memory leaks (allocations without frees)
- Increase initial heap size in `pmm_init()`

### "Double free detected"
- Track all `kfree()` calls
- Make sure pointer is not freed twice
- Set pointer to NULL after freeing

### "Illegal instruction"
- Check if code is mapped as executable (PTE_X)
- Verify instruction encoding
- Check if PC is valid

### "Load/Store access fault"
- Check if address is valid
- Verify memory is mapped
- Check alignment (RISC-V requires aligned access)

## Performance Tips

### Memory Allocation
- Use `kzalloc()` instead of `kmalloc()` + manual zeroing
- Allocate large blocks instead of many small ones
- Free memory as soon as possible

### Page Tables
- Reuse page tables when possible
- Don't create unnecessary mappings
- Use large pages if supported (future)

### Context Switching
- Minimize saved state
- Only save/restore necessary registers
- Keep scheduler simple

## Testing Checklist

### Memory Management
- [ ] Single page allocation/free
- [ ] Multiple page allocation/free
- [ ] Out of memory handling
- [ ] Double free detection
- [ ] Page table creation/destruction
- [ ] Virtual to physical translation
- [ ] kmalloc/kfree various sizes

### Process Management
- [ ] Process allocation/deallocation
- [ ] Process state transitions
- [ ] Context switching
- [ ] Scheduler fairness

### System Calls
- [ ] Syscall number dispatch
- [ ] Argument passing
- [ ] Return value handling
- [ ] Invalid syscall handling

### Interrupts
- [ ] Timer interrupts
- [ ] Exception handling
- [ ] Register save/restore

## Useful Links

- RISC-V Specs: https://riscv.org/technical/specifications/
- xv6 Book: https://pdos.csail.mit.edu/6.828/2021/xv6/book-riscv-rev2.pdf
- QEMU Docs: https://www.qemu.org/docs/master/
- GDB Manual: https://sourceware.org/gdb/documentation/

## Quick Command Reference

```bash
# Build
make clean && make

# Run
make qemu

# Debug
make qemu-gdb
riscv64-unknown-elf-gdb build/kernel.elf

# Clean
make clean

# Exit QEMU
Ctrl+A, then X
```

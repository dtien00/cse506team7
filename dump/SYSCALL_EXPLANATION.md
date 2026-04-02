# System Call Mechanism Explained

## What is a System Call?

A **system call** is the interface between user programs and the kernel. It allows user-space programs to request kernel services (like reading files, allocating memory, creating processes) in a **safe and controlled** way.

---

## Why Assembly (.S files)?

### The Problem: Privilege Levels

Your RISC-V CPU has different **privilege modes**:

```
┌─────────────────────────────────────┐
│  User Mode (U-mode)                 │  ← User programs run here
│  - Cannot access kernel memory      │     (least privileged)
│  - Cannot execute privileged instr  │
│  - Ring 3 (x86 terminology)         │
└─────────────────────────────────────┘
              ↕ ecall
┌─────────────────────────────────────┐
│  Supervisor Mode (S-mode)           │  ← Kernel runs here
│  - Can access all memory            │     (privileged)
│  - Can configure page tables        │
│  - Ring 0 (x86 terminology)         │
└─────────────────────────────────────┘
```

**The Challenge**: User programs need kernel services but can't directly call kernel functions (different privilege level, different address space).

**The Solution**: Use a special CPU instruction called `ecall` (environment call) that:
1. Switches from User mode → Supervisor mode
2. Saves current state
3. Jumps to kernel's trap handler
4. Kernel processes the request
5. Returns to user mode with `sret`

### Why Not C?

**You CANNOT write `ecall` in C** because:
1. `ecall` is a **CPU instruction**, not a C function
2. C has no way to express "switch privilege modes"
3. C compiler doesn't know the RISC-V calling convention for syscalls

---

## How Your Syscall Works: Step-by-Step

### Example: `malloc(1024)` calling `mmap()`

Let's trace what happens when user code calls `malloc(1024)`:

```c
// User program (user.c)
#include <stdlib.h>

int main() {
    char* buffer = malloc(1024);  // ← START HERE
    return 0;
}
```

---

### Step 1: User Space - malloc() in C

```c
// libc/malloc.c
void* malloc(size_t size) {
    // ... malloc logic ...
    void* mem = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    // ↑ This calls the mmap function
}
```

---

### Step 2: User Space - mmap() Wrapper in Assembly

```assembly
# libc/syscall.S
.global mmap
mmap:
    li a7, 14        # Load syscall number 14 into register a7
    ecall            # ← MAGIC HAPPENS HERE
    ret              # Return to caller
```

**What happens here:**

1. **Arguments already in registers** (RISC-V calling convention):
   - `a0` = addr (NULL = 0)
   - `a1` = length (4096)
   - `a2` = prot (PROT_READ | PROT_WRITE)
   - `a3` = flags (MAP_PRIVATE | MAP_ANONYMOUS)
   - `a4` = fd (-1)
   - `a5` = offset (0)

2. **`li a7, 14`**: Load immediate value 14 into register `a7`
   - `a7` is the **syscall number register** (RISC-V convention)
   - 14 = SYS_mmap

3. **`ecall`**: Environment call instruction
   - CPU switches from **U-mode → S-mode**
   - CPU saves current PC (program counter) to `sepc` CSR
   - CPU jumps to address in `stvec` CSR (trap vector)
   - CPU sets `scause` CSR to 8 (ECALL_FROM_U)

4. **`ret`**: Return instruction (executed AFTER kernel returns)

---

### Step 3: Kernel Space - Trap Entry in Assembly

```assembly
# kernel/arch/riscv/trap_entry.S
.global trap_entry
trap_entry:
    addi sp, sp, -280      # Allocate stack space for trap_frame
    
    # Save ALL registers to stack
    sd ra, 0(sp)           # Save return address
    sd sp, 8(sp)           # Save stack pointer
    sd gp, 16(sp)          # Save global pointer
    sd tp, 24(sp)          # Save thread pointer
    sd a0, 32(sp)          # Save argument registers
    sd a1, 40(sp)
    sd a2, 48(sp)
    sd a3, 56(sp)
    sd a4, 64(sp)
    sd a5, 72(sp)
    sd a6, 80(sp)
    sd a7, 88(sp)          # Save syscall number!
    # ... save all other registers ...
    
    csrr t0, sepc          # Read exception PC
    sd   t0, 248(sp)       # Save it
    csrr t0, scause        # Read cause register
    sd   t0, 264(sp)       # Save it
    
    mv   a0, sp            # Pass trap_frame pointer as argument
    call trap_handler      # Call C function
    
    # Restore ALL registers from stack
    ld ra, 0(sp)
    ld a0, 32(sp)          # ← a0 contains return value!
    # ... restore all other registers ...
    
    addi sp, sp, 280       # Deallocate stack space
    sret                   # Return to user mode
```

**Why assembly here?**
- Need to save/restore ALL 31 registers precisely
- Need to read/write CSRs (Control and Status Registers)
- Need to use `sret` instruction (supervisor return)
- C cannot express these low-level operations

---

### Step 4: Kernel Space - Trap Handler in C

```c
// kernel/trap/trap.c
void trap_handler(trap_frame *frame) {
    if (frame->scause & SCAUSE_INTERRUPT) {
        handle_interrupt(frame);
    } else {
        handle_exception(frame);  // ← Goes here for ecall
    }
}

void handle_exception(trap_frame *frame) {
    switch (frame->scause) {
        case SCAUSE_ECALL_FROM_U:  // scause = 8
            syscall_handler(frame);  // ← Dispatch to syscall handler
            write_csr(sepc, frame->sepc + 4);  // Skip past ecall instruction
            return;
        // ... other exceptions ...
    }
}
```

---

### Step 5: Kernel Space - Syscall Dispatcher in C

```c
// kernel/trap/syscall.c
void syscall_handler(trap_frame *frame) {
    const uint64_t syscall_num = frame->a7;  // Get syscall number
    
    // Get arguments from registers
    const uint64_t arg0 = frame->a0;  // addr
    const uint64_t arg1 = frame->a1;  // length
    const uint64_t arg2 = frame->a2;  // prot
    const uint64_t arg3 = frame->a3;  // flags
    const uint64_t arg4 = frame->a4;  // fd
    const uint64_t arg5 = frame->a5;  // offset
    
    uint64_t ret = 0;
    
    switch (syscall_num) {
        case SYS_mmap:  // 14
            ret = (uint64_t) sys_mmap((void *) arg0, (size_t) arg1, 
                                      (int) arg2, (int) arg3, 
                                      (int) arg4, (off_t) arg5);
            break;
        // ... other syscalls ...
    }
    
    frame->a0 = ret;  // Put return value in a0 register
}
```

---

### Step 6: Kernel Space - Actual Implementation in C

```c
// kernel/trap/syscall.c
void *sys_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
    if (length == 0) {
        return MAP_FAILED;
    }
    
    size_t pages = (length + 4095) / 4096;
    
    void* mem = NULL;
    for (size_t i = 0; i < pages; i++) {
        void* page = pmm_alloc_page();  // Allocate physical page
        if (page == NULL) {
            // Cleanup on failure
            return MAP_FAILED;
        }
        if (i == 0) mem = page;
        
        // Zero the page
        for (int j = 0; j < 4096; j++) {
            ((char*)page)[j] = 0;
        }
    }
    
    return mem;  // ← This goes into frame->a0
}
```

---

### Step 7: Return to User Space

1. `sys_mmap()` returns pointer
2. `syscall_handler()` puts return value in `frame->a0`
3. `trap_entry.S` restores all registers (including `a0` with return value)
4. `sret` instruction:
   - Switches S-mode → U-mode
   - Jumps back to user code (at `sepc + 4`, past the `ecall`)
5. User code continues with return value in `a0`

```assembly
# Back in libc/syscall.S
mmap:
    li a7, 14
    ecall            # ← Returned from here
    ret              # ← Return to malloc() with value in a0
```

---

## The Complete Flow Diagram

```
User Program (user.c)
    │
    │ malloc(1024)
    ↓
libc malloc.c (C)
    │
    │ mmap(NULL, 4096, ...)
    ↓
libc/syscall.S (Assembly)
    │
    │ li a7, 14        ← Load syscall number
    │ ecall            ← PRIVILEGE SWITCH (U-mode → S-mode)
    ↓
kernel/arch/riscv/trap_entry.S (Assembly)
    │
    │ Save all registers to trap_frame
    │ call trap_handler
    ↓
kernel/trap/trap.c (C)
    │
    │ trap_handler()
    │ handle_exception()
    ↓
kernel/trap/syscall.c (C)
    │
    │ syscall_handler() - dispatch based on a7
    │ sys_mmap() - actual implementation
    │ frame->a0 = return_value
    ↓
kernel/arch/riscv/trap_entry.S (Assembly)
    │
    │ Restore all registers (including a0 with return value)
    │ sret              ← PRIVILEGE SWITCH (S-mode → U-mode)
    ↓
libc/syscall.S (Assembly)
    │
    │ ret               ← Return to malloc with value in a0
    ↓
libc malloc.c (C)
    │
    │ return mem;
    ↓
User Program (user.c)
    │
    │ buffer = <pointer>
    ↓
```

---

## Why Assembly is Required

### 1. **The `ecall` Instruction**

```assembly
ecall
```

This is a **CPU instruction** that:
- Cannot be expressed in C
- Triggers a trap/exception
- Switches privilege modes
- Is the ONLY way to enter the kernel from user space

**You cannot write this in C:**
```c
// This doesn't exist!
__builtin_ecall();  // ❌ No such thing
```

### 2. **Register Conventions**

RISC-V syscall convention:
- `a7` = syscall number
- `a0-a5` = arguments
- `a0` = return value

Assembly ensures these registers are set correctly:

```assembly
mmap:
    li a7, 14    # Must use a7 for syscall number
    ecall        # CPU expects a7 to contain syscall number
    ret          # Return value is already in a0
```

**In C, you can't control which register is used:**
```c
// C compiler decides which registers to use
// You have NO control over a7
int syscall_num = 14;  // Might go in t0, t1, or stack!
```

### 3. **No Function Call Overhead**

Assembly wrapper is minimal:
```assembly
mmap:
    li a7, 14    # 1 instruction
    ecall        # 1 instruction
    ret          # 1 instruction
```

If you tried to do this in C, you'd need inline assembly anyway:
```c
void* mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset) {
    register long a7 asm("a7") = 14;
    register void* a0 asm("a0") = addr;
    register size_t a1 asm("a1") = length;
    // ... more register variables ...
    
    asm volatile("ecall"
        : "+r"(a0)  // output
        : "r"(a7), "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"(a5)  // inputs
        : "memory");  // clobbers
    
    return a0;
}
```

This is **ugly, error-prone, and harder to read** than the assembly version!

---

## The RISC-V Calling Convention

### Standard Function Calls (C)

When you call a C function:
```c
int result = add(5, 10);
```

The compiler automatically:
- Puts `5` in register `a0`
- Puts `10` in register `a1`
- Calls the function
- Reads return value from `a0`

### System Calls

When you make a syscall:
```assembly
mmap:
    li a7, 14    # Tell kernel WHICH syscall
    ecall        # Make the call
    ret
```

**Same calling convention**, but:
- Arguments are already in `a0-a5` (from C caller)
- We just add `a7` = syscall number
- `ecall` does the privilege switch
- Return value comes back in `a0`

---

## Your Syscall Flow in Detail

### 1. User Space Setup (libc/syscall.S)

```assembly
.global mmap
mmap:
    li a7, 14    # a7 = SYS_mmap (syscall number)
    ecall        # Trap to kernel
    ret          # Return to caller
```

**Register state before `ecall`:**
```
a0 = addr (first argument from C caller)
a1 = length (second argument)
a2 = prot (third argument)
a3 = flags (fourth argument)
a4 = fd (fifth argument)
a5 = offset (sixth argument)
a7 = 14 (syscall number) ← WE SET THIS
```

---

### 2. CPU Executes `ecall`

**Hardware does this automatically:**

```
1. Save current privilege mode
2. Set privilege mode to S-mode (Supervisor)
3. Save current PC to sepc CSR
4. Set PC to stvec CSR (trap vector)
5. Set scause CSR to 8 (ECALL_FROM_U)
6. Disable interrupts
```

**CSR = Control and Status Register** (special CPU registers):
- `sepc`: Supervisor Exception Program Counter (where to return)
- `stvec`: Supervisor Trap Vector (where to jump)
- `scause`: Supervisor Cause (why we trapped)
- `stval`: Supervisor Trap Value (additional info)

---

### 3. Kernel Entry (kernel/arch/riscv/trap_entry.S)

```assembly
trap_entry:
    addi sp, sp, -280    # Allocate 280 bytes on stack
    
    # Save ALL 31 registers (except x0 which is always 0)
    sd ra, 0(sp)
    sd sp, 8(sp)
    sd gp, 16(sp)
    sd tp, 24(sp)
    sd a0, 32(sp)        # Save arguments
    sd a1, 40(sp)
    sd a2, 48(sp)
    sd a3, 56(sp)
    sd a4, 64(sp)
    sd a5, 72(sp)
    sd a6, 80(sp)
    sd a7, 88(sp)        # Save syscall number!
    # ... save s0-s11, t0-t6 ...
    
    csrr t0, sepc        # Read exception PC
    sd   t0, 248(sp)     # Save it to trap_frame
    csrr t0, scause      # Read cause
    sd   t0, 264(sp)     # Save it
    
    mv   a0, sp          # Pass trap_frame* as first argument
    call trap_handler    # Call C function!
    
    # Restore ALL registers
    ld a0, 32(sp)        # ← a0 now has return value!
    # ... restore all others ...
    
    addi sp, sp, 280     # Deallocate stack
    sret                 # Return to user mode
```

**Why save all registers?**
- Kernel code will use registers
- Must restore user's registers exactly
- User program expects registers unchanged (except a0 = return value)

**The `trap_frame` structure:**
```c
typedef struct {
    uint64_t ra;    // offset 0
    uint64_t sp;    // offset 8
    uint64_t gp;    // offset 16
    uint64_t tp;    // offset 24
    uint64_t a0;    // offset 32  ← Arguments and return value
    uint64_t a1;    // offset 40
    uint64_t a2;    // offset 48
    uint64_t a3;    // offset 56
    uint64_t a4;    // offset 64
    uint64_t a5;    // offset 72
    uint64_t a6;    // offset 80
    uint64_t a7;    // offset 88  ← Syscall number
    // ... all other registers ...
    uint64_t sepc;  // offset 248
    uint64_t scause;// offset 264
} trap_frame;
```

---

### 4. Kernel C Code (kernel/trap/trap.c)

```c
void trap_handler(trap_frame *frame) {
    if (frame->scause & SCAUSE_INTERRUPT) {
        handle_interrupt(frame);  // Timer, external interrupts
    } else {
        handle_exception(frame);  // Syscalls, page faults, etc.
    }
}

void handle_exception(trap_frame *frame) {
    switch (frame->scause) {
        case SCAUSE_ECALL_FROM_U:  // 8
            syscall_handler(frame);
            write_csr(sepc, frame->sepc + 4);  // Skip past ecall
            return;
        // ... other exceptions ...
    }
}
```

**Why `sepc + 4`?**
- `ecall` is a 4-byte instruction
- When we return, we want to execute the NEXT instruction (`ret`)
- If we don't increment, we'll execute `ecall` again (infinite loop!)

---

### 5. Kernel Syscall Dispatcher (kernel/trap/syscall.c)

```c
void syscall_handler(trap_frame *frame) {
    const uint64_t syscall_num = frame->a7;  // Read a7
    
    // Read arguments from saved registers
    const uint64_t arg0 = frame->a0;
    const uint64_t arg1 = frame->a1;
    const uint64_t arg2 = frame->a2;
    const uint64_t arg3 = frame->a3;
    const uint64_t arg4 = frame->a4;
    const uint64_t arg5 = frame->a5;
    
    uint64_t ret = 0;
    
    switch (syscall_num) {
        case SYS_mmap:  // 14
            ret = (uint64_t) sys_mmap(
                (void *) arg0,      // addr
                (size_t) arg1,      // length
                (int) arg2,         // prot
                (int) arg3,         // flags
                (int) arg4,         // fd
                (off_t) arg5        // offset
            );
            break;
        // ... other syscalls ...
    }
    
    frame->a0 = ret;  // Write return value to a0!
}
```

---

### 6. Kernel Syscall Implementation (kernel/trap/syscall.c)

```c
void *sys_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
    if (length == 0) {
        return MAP_FAILED;
    }
    
    // Calculate pages needed
    size_t pages = (length + 4095) / 4096;
    
    void* mem = NULL;
    for (size_t i = 0; i < pages; i++) {
        void* page = pmm_alloc_page();  // Allocate from physical memory
        if (page == NULL) {
            return MAP_FAILED;
        }
        if (i == 0) mem = page;
        
        // Zero the page
        for (int j = 0; j < 4096; j++) {
            ((char*)page)[j] = 0;
        }
    }
    
    return mem;  // Return pointer to allocated memory
}
```

---

### 7. Return Path

The return happens in **reverse**:

1. `sys_mmap()` returns pointer → stored in local variable `ret`
2. `syscall_handler()` writes `ret` to `frame->a0`
3. `trap_handler()` returns
4. `trap_entry.S` restores registers (including `a0` with return value)
5. `sret` instruction:
   - Switches S-mode → U-mode
   - Jumps to `sepc` (which was incremented to skip past `ecall`)
6. User code continues at `ret` instruction in `syscall.S`
7. `ret` returns to `malloc()` with pointer in `a0`
8. `malloc()` returns to user program

---

## Why Each Layer Exists

### Layer 1: User Program (C)
```c
char* buffer = malloc(1024);
```
**Purpose**: High-level API that user understands

---

### Layer 2: libc malloc (C)
```c
void* mem = mmap(NULL, 4096, ...);
```
**Purpose**: Implement malloc logic, decide when to call mmap

---

### Layer 3: libc syscall wrapper (Assembly)
```assembly
mmap:
    li a7, 14
    ecall
    ret
```
**Purpose**: 
- Set syscall number in `a7`
- Execute `ecall` instruction (cannot do in C)
- Minimal overhead (3 instructions)

---

### Layer 4: Kernel trap entry (Assembly)
```assembly
trap_entry:
    # Save all registers
    call trap_handler
    # Restore all registers
    sret
```
**Purpose**:
- Save/restore ALL registers (C can't do this reliably)
- Use `sret` instruction (C can't do this)
- Create trap_frame structure for C code

---

### Layer 5: Kernel trap handler (C)
```c
void trap_handler(trap_frame *frame) {
    if (ecall) syscall_handler(frame);
}
```
**Purpose**: Distinguish between interrupts, syscalls, page faults, etc.

---

### Layer 6: Kernel syscall dispatcher (C)
```c
void syscall_handler(trap_frame *frame) {
    switch (frame->a7) {
        case SYS_mmap: ret = sys_mmap(...);
    }
    frame->a0 = ret;
}
```
**Purpose**: Route to correct syscall implementation based on number

---

### Layer 7: Kernel syscall implementation (C)
```c
void *sys_mmap(...) {
    // Actual memory allocation logic
    return mem;
}
```
**Purpose**: Do the actual work

---

## Key Insights

### 1. **Assembly is ONLY for privilege switching**
- User space: 3-line assembly wrapper (set a7, ecall, ret)
- Kernel space: Assembly for save/restore registers and sret
- Everything else is C!

### 2. **Registers are the communication channel**
- `a0-a5`: Arguments (user → kernel)
- `a7`: Syscall number (user → kernel)
- `a0`: Return value (kernel → user)

### 3. **The trap_frame is the bridge**
- Assembly saves registers to memory (trap_frame)
- C code reads/writes trap_frame
- Assembly restores registers from trap_frame

### 4. **Two privilege switches per syscall**
- `ecall`: U-mode → S-mode (enter kernel)
- `sret`: S-mode → U-mode (exit kernel)

---

## Alternative: Inline Assembly in C

You COULD write the syscall wrapper in C with inline assembly:

```c
void* mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset) {
    register long a7 asm("a7") = 14;
    register void* a0 asm("a0") = addr;
    register size_t a1 asm("a1") = length;
    register int a2 asm("a2") = prot;
    register int a3 asm("a3") = flags;
    register int a4 asm("a4") = fd;
    register off_t a5 asm("a5") = offset;
    
    asm volatile(
        "ecall"
        : "+r"(a0)
        : "r"(a7), "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"(a5)
        : "memory"
    );
    
    return a0;
}
```

**But this is:**
- Much longer and harder to read
- More error-prone
- Still requires assembly knowledge
- Less maintainable

**The pure assembly version is cleaner:**
```assembly
mmap:
    li a7, 14
    ecall
    ret
```

---

## Security: Why Privilege Modes Matter

### Without Privilege Modes (Dangerous!)

```c
// If user could directly call kernel functions:
void* evil_program() {
    // Read any memory!
    char* password = (char*)0x80001000;
    
    // Modify kernel data!
    kernel_pagetable[0] = 0;
    
    // Crash the system!
    while(1);
}
```

### With Privilege Modes (Safe!)

```c
void* safe_program() {
    // User can only request services via syscalls
    void* mem = mmap(...);  // Kernel validates and allocates
    
    // User cannot access kernel memory
    char* password = (char*)0x80001000;  // PAGE FAULT!
    
    // User cannot modify kernel data
    kernel_pagetable[0] = 0;  // PAGE FAULT!
}
```

The `ecall` instruction is the **only controlled gateway** into the kernel.

---

## Summary

**Why .S files for syscalls?**

1. **`ecall` is a CPU instruction** - cannot be written in C
2. **Register control** - must set `a7` precisely
3. **Minimal overhead** - 3 instructions per syscall
4. **Clean separation** - Assembly for privilege switch, C for logic
5. **Standard practice** - All Unix-like systems do this

**The pattern:**
```
User C code → libc C wrapper → libc ASM stub (ecall) → 
Kernel ASM entry (save regs) → Kernel C handler → Kernel C implementation →
Kernel ASM exit (restore regs, sret) → libc ASM stub (ret) → User C code
```

Assembly is used ONLY where absolutely necessary (privilege switching and register management). Everything else is nice, readable C code!

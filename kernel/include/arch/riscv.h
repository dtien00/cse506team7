#pragma once

#include "kernel/types.h"

// Supervisor Status Register (sstatus)
#define SSTATUS_SIE (1UL << 1) // Supervisor Interrupt Enable
#define SSTATUS_SPIE (1UL << 5) // Previous Interrupt Enable
#define SSTATUS_SPP (1UL << 8) // Previous Privilege (0=User, 1=Supervisor)
#define SSTATUS_SUM (1UL << 18) // Permit Supervisor User Memory access

// the bit positions in SIE to enable/disable certain interrupt types
#define SIE_SOFTWARE 1 // from syscalls
#define SIE_TIMER 5 // from timer events
#define SIE_EXTERNAL 9 // from external sources - PLIC

// Supervisor Interrupt Pending (sip)
#define SIP_SOFTWARE (1UL << 1) // Software interrupt pending
#define SIP_TIMER (1UL << 5) // Timer interrupt pending
#define SIP_EXTERNAL (1UL << 9) // External interrupt pending

// Supervisor Trap Cause (scause)
#define SCAUSE_INTERRUPT (1UL << 63)

// Interrupt codes (with SCAUSE_INTERRUPT bit set)
// same bit positions as bit positions in SIP / SIE
#define SCAUSE_S_SOFTWARE_INT 1
#define SCAUSE_S_TIMER_INT 5
#define SCAUSE_S_EXTERNAL_INT 9

// Exception codes
#define SCAUSE_INST_MISALIGNED 0
#define SCAUSE_INST_ACCESS_FAULT 1
#define SCAUSE_ILLEGAL_INST 2
#define SCAUSE_BREAKPOINT 3
#define SCAUSE_LOAD_MISALIGNED 4
#define SCAUSE_LOAD_ACCESS_FAULT 5
#define SCAUSE_STORE_MISALIGNED 6
#define SCAUSE_STORE_ACCESS_FAULT 7
#define SCAUSE_ECALL_FROM_U 8
#define SCAUSE_ECALL_FROM_S 9
#define SCAUSE_INST_PAGE_FAULT 12
#define SCAUSE_LOAD_PAGE_FAULT 13
#define SCAUSE_STORE_PAGE_FAULT 15

// Page table entry (PTE) flags
#define PTE_V (1UL << 0) // Valid
#define PTE_R (1UL << 1) // Readable
#define PTE_W (1UL << 2) // Writable
#define PTE_X (1UL << 3) // Executable
#define PTE_U (1UL << 4) // User accessible
#define PTE_G (1UL << 5) // Global
#define PTE_A (1UL << 6) // Accessed
#define PTE_D (1UL << 7) // Dirty

// Page table entry manipulation
#define PTE_PPN_SHIFT 10
#define PA_TO_PTE(pa) (((uint64_t) (pa) >> 12) << PTE_PPN_SHIFT)
#define PTE_TO_PA(pte) (((uint64_t) (pte) >> PTE_PPN_SHIFT) << 12)
#define PTE_FLAGS(pte) ((pte) & 0x3FF)

// Sv39 virtual address breakdown
#define PGSIZE 4096
#define PGSHIFT 12
#define PGROUNDUP(sz) (((sz) + PGSIZE - 1) & ~(PGSIZE - 1))
#define PGROUNDDOWN(a) (((a)) & ~(PGSIZE - 1))

// Sv39 uses 3 levels of page tables (512 entries each)
#define PXMASK 0x1FF // 9 bits
#define PXSHIFT(level) (PGSHIFT + (9 * (level)))
#define PX(level, va) ((((uint64_t) (va)) >> PXSHIFT(level)) & PXMASK)

// SATP register (Supervisor Address Translation and Protection)
#define SATP_MODE_SV39 (8UL << 60)
#define SATP_PPN(pa) (((uint64_t) (pa) >> 12))
#define MAKE_SATP(pagetable) (SATP_MODE_SV39 | SATP_PPN(pagetable))

// Memory layout
#define KERNBASE 0x80000000UL
#define PHYSTOP 0x88000000UL // 128 MB of RAM
#define MAXVA (1UL << (9 + 9 + 9 + 12 - 1)) // Sv39 max VA

// User memory layout
#define USTACK_SIZE (4 * PGSIZE)
#define TRAMPOLINE (MAXVA - PGSIZE)
#define TRAPFRAME (TRAMPOLINE - PGSIZE)

// CSR read/write inline functions
static inline uint64_t r_satp(void) {
    uint64_t x;
    asm volatile("csrr %0, satp" : "=r"(x));
    return x;
}

static inline void w_satp(uint64_t x) {
    asm volatile("csrw satp, %0" : : "r"(x));
    asm volatile("sfence.vma zero, zero"); // Flush TLB
}

static inline uint64_t r_sp(void) {
    uint64_t x;
    asm volatile("mv %0, sp" : "=r"(x));
    return x;
}

static inline void w_tp(uint64_t x) { asm volatile("mv tp, %0" : : "r"(x)); }

static inline uint64_t r_tp(void) {
    uint64_t x;
    asm volatile("mv %0, tp" : "=r"(x));
    return x;
}

// Control and Status Registers (CSRs)

// Read a CSR into a uint64_t
#define read_csr(csr)                                                                                                  \
    ({                                                                                                                 \
        uint64_t _r;                                                                                                   \
        asm volatile("csrr %0, " #csr : "=r"(_r));                                                                     \
        _r;                                                                                                            \
    })

// Write a value to a CSR
#define write_csr(csr, val)                                                                                            \
    do {                                                                                                               \
        uint64_t __v = (uint64_t) (val);                                                                               \
        asm volatile("csrw " #csr ", %0" ::"r"(__v));                                                                  \
    } while (0)

// Atomically swap a CSR, returning the old value
#define swap_csr(csr, val)                                                                                             \
    ({                                                                                                                 \
        uint64_t __v = (uint64_t) (val);                                                                               \
        uint64_t __old;                                                                                                \
        asm volatile("csrrw %0, " #csr ", %1" : "=r"(__old) : "r"(__v));                                               \
        __old;                                                                                                         \
    })

// Set bits in a CSR (CSR |= mask), returns old value
#define set_csr_bit(csr, mask)                                                                                         \
    ({                                                                                                                 \
        uint64_t __v = (uint64_t) (mask);                                                                              \
        uint64_t __old;                                                                                                \
        asm volatile("csrrs %0, " #csr ", %1" : "=r"(__old) : "r"(__v));                                               \
        __old;                                                                                                         \
    })

// Clear bits in a CSR (CSR &= ~mask), returns old value
#define clear_csr_bit(csr, mask)                                                                                       \
    ({                                                                                                                 \
        uint64_t __v = (uint64_t) (mask);                                                                              \
        uint64_t __old;                                                                                                \
        asm volatile("csrrc %0, " #csr ", %1" : "=r"(__old) : "r"(__v));                                               \
        __old;                                                                                                         \
    })

// General Purpose Registers (GPRs)

// Read any GPR by ABI name — e.g. read_reg(a0), read_reg(ra)
// Do NOT use this for sp; use r_sp() instead.
#define read_reg(reg)                                                                                                  \
    ({                                                                                                                 \
        uint64_t _r;                                                                                                   \
        asm volatile("mv %0, " #reg : "=r"(_r));                                                                       \
        _r;                                                                                                            \
    })

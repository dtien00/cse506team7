#pragma once

#include <defs.h>

// Control and Status Registers (CSRs)

// Read a CSR into a uint64_t
#define read_csr(csr)                                                                                                  \
    ({                                                                                                                 \
        uint64_t __r;                                                                                                \
        asm volatile("csrr %0, " #csr : "=r"(__r));                                                                  \
        __r;                                                                                                         \
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
        uint64_t __r;                                                                                                \
        asm volatile("mv %0, " #reg : "=r"(__r));                                                                    \
        __r;                                                                                                         \
    })

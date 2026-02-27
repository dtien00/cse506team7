#pragma once

#include <defs.h>

// macros to help read control and status registers (CSRs)

#define read_csr(csr)                                                                                                  \
    ({                                                                                                                 \
        uint64_t _tmp;                                                                                                \
        asm volatile("csrr %0, " #csr : "=r"(_tmp));                                                                  \
        _tmp;                                                                                                         \
    })

#define write_csr(csr, val)                                                                                            \
    do {                                                                                                               \
        uint64_t _v = (uint64_t) (val);                                                                               \
        asm volatile("csrw " #csr ", %0" ::"r"(_v));                                                                  \
    } while (0)

#define swap_csr(csr, val)                                                                                             \
    ({                                                                                                                 \
        uint64_t _v = (uint64_t) (val);                                                                               \
        uint64_t _old;                                                                                                \
        asm volatile("csrrw %0, " #csr ", %1" : "=r"(_old) : "r"(_v));                                               \
        _old;                                                                                                         \
    })

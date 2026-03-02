#pragma once

#include "types.h"

// RISC-V Control and Status Registers (CSRs)

// Supervisor Status Register (sstatus)
#define SSTATUS_SIE  (1UL << 1)   // Supervisor Interrupt Enable
#define SSTATUS_SPIE (1UL << 5)   // Previous Interrupt Enable
#define SSTATUS_SPP  (1UL << 8)   // Previous Privilege (0=User, 1=Supervisor)
#define SSTATUS_SUM  (1UL << 18)  // Permit Supervisor User Memory access

// Supervisor Interrupt Enable (sie)
#define SIE_SEIE (1UL << 9)   // External interrupt enable
#define SIE_STIE (1UL << 5)   // Timer interrupt enable
#define SIE_SSIE (1UL << 1)   // Software interrupt enable

// Supervisor Interrupt Pending (sip)
#define SIP_SEIP (1UL << 9)   // External interrupt pending
#define SIP_STIP (1UL << 5)   // Timer interrupt pending
#define SIP_SSIP (1UL << 1)   // Software interrupt pending

// Supervisor Trap Cause (scause)
#define SCAUSE_INTERRUPT (1UL << 63)

// Exception codes
#define SCAUSE_INST_MISALIGNED    0
#define SCAUSE_INST_ACCESS_FAULT  1
#define SCAUSE_ILLEGAL_INST       2
#define SCAUSE_BREAKPOINT         3
#define SCAUSE_LOAD_MISALIGNED    4
#define SCAUSE_LOAD_ACCESS_FAULT  5
#define SCAUSE_STORE_MISALIGNED   6
#define SCAUSE_STORE_ACCESS_FAULT 7
#define SCAUSE_ECALL_FROM_U       8
#define SCAUSE_ECALL_FROM_S       9
#define SCAUSE_INST_PAGE_FAULT    12
#define SCAUSE_LOAD_PAGE_FAULT    13
#define SCAUSE_STORE_PAGE_FAULT   15

// Interrupt codes (with SCAUSE_INTERRUPT bit set)
#define SCAUSE_S_SOFTWARE_INT     1
#define SCAUSE_S_TIMER_INT        5
#define SCAUSE_S_EXTERNAL_INT     9

// Page table entry (PTE) flags
#define PTE_V     (1UL << 0)   // Valid
#define PTE_R     (1UL << 1)   // Readable
#define PTE_W     (1UL << 2)   // Writable
#define PTE_X     (1UL << 3)   // Executable
#define PTE_U     (1UL << 4)   // User accessible
#define PTE_G     (1UL << 5)   // Global
#define PTE_A     (1UL << 6)   // Accessed
#define PTE_D     (1UL << 7)   // Dirty

// Page table entry manipulation
#define PTE_PPN_SHIFT 10
#define PA_TO_PTE(pa) (((uint64_t)(pa) >> 12) << PTE_PPN_SHIFT)
#define PTE_TO_PA(pte) (((uint64_t)(pte) >> PTE_PPN_SHIFT) << 12)
#define PTE_FLAGS(pte) ((pte) & 0x3FF)

// Sv39 virtual address breakdown
#define PGSIZE 4096
#define PGSHIFT 12
#define PGROUNDUP(sz)   (((sz) + PGSIZE - 1) & ~(PGSIZE - 1))
#define PGROUNDDOWN(a)  (((a)) & ~(PGSIZE - 1))

// Sv39 uses 3 levels of page tables (512 entries each)
#define PXMASK 0x1FF  // 9 bits
#define PXSHIFT(level) (PGSHIFT + (9 * (level)))
#define PX(level, va) ((((uint64_t)(va)) >> PXSHIFT(level)) & PXMASK)

// SATP register (Supervisor Address Translation and Protection)
#define SATP_MODE_SV39 (8UL << 60)
#define SATP_PPN(pa) (((uint64_t)(pa) >> 12))
#define MAKE_SATP(pagetable) (SATP_MODE_SV39 | SATP_PPN(pagetable))

// Memory layout
#define KERNBASE     0x80000000UL
#define PHYSTOP      0x88000000UL  // 128 MB of RAM
#define MAXVA        (1UL << (9 + 9 + 9 + 12 - 1))  // Sv39 max VA

// User memory layout
#define USTACK_SIZE  (4 * PGSIZE)
#define TRAMPOLINE   (MAXVA - PGSIZE)
#define TRAPFRAME    (TRAMPOLINE - PGSIZE)

// CSR read/write inline functions
static inline uint64_t r_sstatus(void) {
  uint64_t x;
  asm volatile("csrr %0, sstatus" : "=r"(x));
  return x;
}

static inline void w_sstatus(uint64_t x) {
  asm volatile("csrw sstatus, %0" : : "r"(x));
}

static inline uint64_t r_sie(void) {
  uint64_t x;
  asm volatile("csrr %0, sie" : "=r"(x));
  return x;
}

static inline void w_sie(uint64_t x) {
  asm volatile("csrw sie, %0" : : "r"(x));
}

static inline uint64_t r_sip(void) {
  uint64_t x;
  asm volatile("csrr %0, sip" : "=r"(x));
  return x;
}

static inline void w_sip(uint64_t x) {
  asm volatile("csrw sip, %0" : : "r"(x));
}

static inline uint64_t r_stvec(void) {
  uint64_t x;
  asm volatile("csrr %0, stvec" : "=r"(x));
  return x;
}

static inline void w_stvec(uint64_t x) {
  asm volatile("csrw stvec, %0" : : "r"(x));
}

static inline uint64_t r_scause(void) {
  uint64_t x;
  asm volatile("csrr %0, scause" : "=r"(x));
  return x;
}

static inline uint64_t r_stval(void) {
  uint64_t x;
  asm volatile("csrr %0, stval" : "=r"(x));
  return x;
}

static inline uint64_t r_sepc(void) {
  uint64_t x;
  asm volatile("csrr %0, sepc" : "=r"(x));
  return x;
}

static inline void w_sepc(uint64_t x) {
  asm volatile("csrw sepc, %0" : : "r"(x));
}

static inline uint64_t r_satp(void) {
  uint64_t x;
  asm volatile("csrr %0, satp" : "=r"(x));
  return x;
}

static inline void w_satp(uint64_t x) {
  asm volatile("csrw satp, %0" : : "r"(x));
  asm volatile("sfence.vma zero, zero");  // Flush TLB
}

static inline uint64_t r_sp(void) {
  uint64_t x;
  asm volatile("mv %0, sp" : "=r"(x));
  return x;
}

static inline void w_tp(uint64_t x) {
  asm volatile("mv tp, %0" : : "r"(x));
}

static inline uint64_t r_tp(void) {
  uint64_t x;
  asm volatile("mv %0, tp" : "=r"(x));
  return x;
}

// Enable/disable interrupts
static inline void intr_on(void) {
  w_sstatus(r_sstatus() | SSTATUS_SIE);
}

static inline void intr_off(void) {
  w_sstatus(r_sstatus() & ~SSTATUS_SIE);
}

static inline int intr_get(void) {
  return (r_sstatus() & SSTATUS_SIE) != 0;
}

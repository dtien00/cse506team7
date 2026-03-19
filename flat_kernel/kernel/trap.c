#include "trap.h"
#include "riscv.h"
#include "sbi.h"
#include "printk.h"
#include "process.h"
#include "pmm.h"
#include "vmm.h"

#define TIMER_INTERVAL 1000000ULL // ~0.1 second at 10 MHz clock

// trap entry point code written in trap.S
extern void trap_entry(void);

void trap_init(void) {

    // Install trap entry point
    write_csr(stvec, (uint64_t) trap_entry);
    printk("Trap entry point (stvec) set to: %p\n", read_csr(stvec));
    sbi_set_timer(read_csr(time) + TIMER_INTERVAL);
    // Enable interrupts
    interrupts_enable(SIE_SOFTWARE);
    interrupts_enable(SIE_TIMER);
    interrupts_enable(SIE_EXTERNAL);
}

static void handle_interrupt(trap_frame *frame) {
    uint64_t interrupt_code = frame->scause & ~SCAUSE_INTERRUPT;

    switch (interrupt_code) {
        case SCAUSE_S_SOFTWARE_INT:
            printk("Software interrupt\n");
            clear_csr_bit(sip, SIP_SOFTWARE);
            break;

        case SCAUSE_S_TIMER_INT:
            timer_handler();
            break;

        case SCAUSE_S_EXTERNAL_INT:
            printk("External interrupt\n");
            break;

        default:
            printk("Unknown interrupt: %d\n", interrupt_code);
            break;
    }
}

// -----------------------------------------------------------------------------
// handle_page_fault
//
// Called for INST/LOAD/STORE page faults. Attempts to resolve the fault by
// allocating and mapping a fresh page at the faulting address.
//
// Resolution criteria (demand paging within managed physical memory):
//   - stval (faulting VA) must be non-zero and page-aligned-truncatable
//   - The VA must fall within KERNBASE..PHYSTOP (identity-mapped region)
//
// TODO: Once processes are further along, swap vmm_kernel_pagetable() for
//       current->pagetable so user-space faults are handled per-process.
//
// Returns 1 if the fault was resolved (caller should return to retry sepc).
// Returns 0 if the fault could not be resolved (caller should panic).
// -----------------------------------------------------------------------------
static int handle_page_fault(trap_frame *frame) {
    uintptr_t fault_va = frame->stval;

    // A zero stval usually means a null pointer dereference — never map page 0.
    if (fault_va == 0) return 0;

    // Round down to page boundary.
    uintptr_t page_va = PGROUNDDOWN(fault_va);

    // Only service faults in the kernel-managed physical memory window.
    // Faults outside this range (device MMIO, user stacks, bad pointers) panic.
    if (page_va < KERNBASE || page_va >= PHYSTOP) {
        printk("page fault: VA %p is outside managed range [%p, %p)\n",
               fault_va, KERNBASE, PHYSTOP);
        return 0;
    }

    // Determine which page table to map into.
    // For now this is always the kernel page table; swap in current->pagetable
    // here once per-process page tables are active.
    pagetable_t pt = vmm_kernel_pagetable();
    if (!pt) {
        printk("page fault: no active page table\n");
        return 0;
    }

    // Allocate a fresh physical page.
    void *phys = pmm_alloc_page();
    if (!phys) {
        printk("page fault: out of physical memory for VA %p\n", fault_va);
        return 0;
    }

    // Zero the page before mapping (avoids leaking stale kernel data).
    uint8_t *p = (uint8_t *)phys;
    for (size_t i = 0; i < PGSIZE; i++) p[i] = 0;

    // Map with R+W. Add PTE_X only for instruction faults so the kernel
    // doesn't hand out executable pages for data accesses.
    uint64_t perm = PTE_R | PTE_W;
    if (frame->scause == SCAUSE_INST_PAGE_FAULT) perm |= PTE_X;

    if (vmm_map(pt, page_va, (uintptr_t)phys, perm) != 0) {
        printk("page fault: vmm_map failed for VA %p -> PA %p\n", page_va, phys);
        pmm_free_page(phys);
        return 0;
    }

    printk("page fault: resolved VA %p -> PA %p (scause=%d)\n",
           fault_va, phys, frame->scause);
    return 1;  // Resolved — caller will return to retry the faulting instruction
}

void handle_exception(trap_frame *frame) {
    printk("\nEXCEPTION: scause=0x%p stval=0x%p sepc=0x%p\n",
           frame->scause, frame->stval, frame->sepc);

    switch (frame->scause) {
        case SCAUSE_INST_MISALIGNED:    printk("  Instruction address misaligned\n"); break;
        case SCAUSE_INST_ACCESS_FAULT:  printk("  Instruction access fault\n");       break;
        case SCAUSE_ILLEGAL_INST:       printk("  Illegal instruction\n");             break;
        case SCAUSE_BREAKPOINT:         printk("  Breakpoint\n");                      break;
        case SCAUSE_LOAD_MISALIGNED:    printk("  Load address misaligned\n");         break;
        case SCAUSE_LOAD_ACCESS_FAULT:  printk("  Load access fault\n");              break;
        case SCAUSE_STORE_MISALIGNED:   printk("  Store address misaligned\n");        break;
        case SCAUSE_STORE_ACCESS_FAULT: printk("  Store access fault\n");             break;
        case SCAUSE_ECALL_FROM_U:
            write_csr(sepc, frame->sepc + 4);
            return;
        case SCAUSE_ECALL_FROM_S:       printk("  System call from supervisor mode\n"); break;
        case SCAUSE_INST_PAGE_FAULT:
        case SCAUSE_LOAD_PAGE_FAULT:
        case SCAUSE_STORE_PAGE_FAULT:
            if (handle_page_fault(frame)) return;
            printk("  Page fault (unresolvable) at VA %p\n", frame->stval);
            break;
        default:                        printk("  Unknown exception\n");               break;
    }

    printk("\nRegister dump:\n");
    printk("  ra=0x%p sp=0x%p gp=0x%p tp=0x%p\n", frame->ra, frame->sp, frame->gp, frame->tp);
    printk("  t0=0x%p t1=0x%p t2=0x%p\n", frame->t0, frame->t1, frame->t2);
    printk("  s0=0x%p s1=0x%p\n", frame->s0, frame->s1);
    printk("  a0=0x%p a1=0x%p a2=0x%p a3=0x%p\n", frame->a0, frame->a1, frame->a2, frame->a3);
    printk("  a4=0x%p a5=0x%p a6=0x%p a7=0x%p\n", frame->a4, frame->a5, frame->a6, frame->a7);

    printk("\nKernel panic - halting\n");
    while (1) asm volatile("wfi");
}

void trap_handler(trap_frame *frame) {
    if (frame->scause & SCAUSE_INTERRUPT) {
        handle_interrupt(frame);
    } else {
        handle_exception(frame);
    }
}

// Timer interrupt handler
static uint64_t timer_ticks = 0;

void timer_handler(void) {
    timer_ticks++;
    sbi_set_timer(read_csr(time) + TIMER_INTERVAL);
    proc_yield();
    printk("Timer: %d ticks\n", timer_ticks * TIMER_INTERVAL);
}

// Set/clear the SIE (supervisor interrupt enable) bit in sstatus
void interrupts_on(void) { set_csr_bit(sstatus, SSTATUS_SIE); }
void interrupts_off(void) { clear_csr_bit(sstatus, SSTATUS_SIE); }

// Set/clear individual interrupt-type bits in the SIE CSR
void interrupts_enable(uint64_t bit) { set_csr_bit(sie, 1ULL << bit); }
void interrupts_disable(uint64_t bit) { clear_csr_bit(sie, 1ULL << bit); }

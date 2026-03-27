#include "trap/trap.h"
#include "arch/riscv.h"
#include "arch/sbi.h"
#include "kernel/printk.h"
#include "process/process.h"
#include "trap/syscall.h"
#include "mm/vmm.h"
#include "mm/pmm.h"
#include "mm/vma.h"
#include "string.h"   // memset

#define TIMER_INTERVAL 1000000ULL // ~0.1 second at 10 MHz clock

// trap entry point code written in trap.S
extern void trap_entry(void);

void trap_init(void) {

    // Install trap entry point
    write_csr(sscratch, 0);
    write_csr(stvec, (uint64_t) trap_entry);
    printk("Trap entry point (stvec) set to: %p\n", read_csr(stvec));
    sbi_set_timer(read_csr(time) + TIMER_INTERVAL);
    // Enable interrupts
    interrupts_enable(SIE_SOFTWARE);
    interrupts_enable(SIE_TIMER);
    interrupts_enable(SIE_EXTERNAL);
}

static void handle_interrupt(trap_frame *frame) {
    uint64_t interrupt_code = frame->scause & ~SCAUSE_INTERRUPT; // calculate exception code by removing interrupt bit

    switch (interrupt_code) {
        case SCAUSE_S_SOFTWARE_INT:
            printk("Software interrupt\n");
            // Clear software interrupt
            clear_csr_bit(sip, SIP_SOFTWARE);
            break;

        case SCAUSE_S_TIMER_INT:
            timer_handler();
            break;

        case SCAUSE_S_EXTERNAL_INT:
            printk("External interrupt\n");
            // TODO: Handle external interrupts (UART, disk, etc.)
            break;

        default:
            printk("Unknown interrupt: %d\n", interrupt_code);
            break;
    }
}

void handle_exception(trap_frame *frame) {
    // Handle U-mode syscalls silently — they are normal, not errors.
    if (frame->scause == SCAUSE_ECALL_FROM_U) {
        syscall_handler(frame);
        frame->sepc += 4;
        return;
    }

    printk("\nEXCEPTION: scause=0x%p stval=0x%p sepc=0x%p\n", frame->scause, frame->stval, frame->sepc);

    switch (frame->scause) {
        case SCAUSE_INST_MISALIGNED:
            printk("  Instruction address misaligned\n");
            break;
        case SCAUSE_INST_ACCESS_FAULT:
            printk("  Instruction access fault\n");
            break;
        case SCAUSE_ILLEGAL_INST:
            printk("  Illegal instruction\n");
            break;
        case SCAUSE_BREAKPOINT:
            printk("  Breakpoint\n");
            break;
        case SCAUSE_LOAD_MISALIGNED:
            printk("  Load address misaligned\n");
            break;
        case SCAUSE_LOAD_ACCESS_FAULT:
            printk("  Load access fault\n");
            break;
        case SCAUSE_STORE_MISALIGNED:
            printk("  Store address misaligned\n");
            break;
        case SCAUSE_STORE_ACCESS_FAULT:
            printk("  Store access fault\n");
            break;
        case SCAUSE_ECALL_FROM_U:
            // Handled above; unreachable.
        case SCAUSE_ECALL_FROM_S:
            printk("  System call from supervisor mode\n");
            break;
        case SCAUSE_INST_PAGE_FAULT:
        case SCAUSE_LOAD_PAGE_FAULT:
        case SCAUSE_STORE_PAGE_FAULT:
            page_fault_handler(frame);
            break;
        default:
            printk("  Unknown exception\n");
            break;
    }

    // Print register dump
    printk("\nRegister dump:\n");
    printk("  ra=0x%p sp=0x%p gp=0x%p tp=0x%p\n", frame->ra, frame->sp, frame->gp, frame->tp);
    printk("  t0=0x%p t1=0x%p t2=0x%p\n", frame->t0, frame->t1, frame->t2);
    printk("  s0=0x%p s1=0x%p\n", frame->s0, frame->s1);
    printk("  a0=0x%p a1=0x%p a2=0x%p a3=0x%p\n", frame->a0, frame->a1, frame->a2, frame->a3);
    printk("  a4=0x%p a5=0x%p a6=0x%p a7=0x%p\n", frame->a4, frame->a5, frame->a6, frame->a7);

    // Halt
    printk("\nKernel panic - halting\n");
    while (1) {
        asm volatile("wfi");
    }
}

void trap_handler(trap_frame *frame) {
    // printk("TRAP: Register info scause=%p sip=%p sie=%p\n", (void *) frame->scause, (void *) read_csr(sip),
    //        (void *) read_csr(sie));
    if (frame->scause & SCAUSE_INTERRUPT) {
        // Interrupt
        handle_interrupt(frame);
    } else {
        // Exception
        handle_exception(frame);
    }
}


// ************************************ 
//    Processes, Page Faults, And friends or something
// ************************************ 
//  if we have multiple of these big handler blocks, consider splitting into separate files like trap_syscall.c, trap_pagefault.c, etc.

/* ── prot bits needed for each fault type ─────────────────────────────────
 *
 * We need the faulting VMA to allow the access that caused the fault.
 * Instruction faults require VMA_PROT_EXEC, load/store require READ/WRITE.
 */
static uint32_t required_prot(uint64_t scause) {
    switch (scause) {
        case SCAUSE_INST_PAGE_FAULT:  return VMA_PROT_EXEC;
        case SCAUSE_LOAD_PAGE_FAULT:  return VMA_PROT_READ;
        case SCAUSE_STORE_PAGE_FAULT: return VMA_PROT_WRITE;
        default:                      return 0;
    }
}


/* ── PTE permission bits from VMA protection flags ────────────────────────
 *
 * Map VMA_PROT_* → RISC-V PTE_* so we can call vmm_map() with the right
 * permissions when we demand-allocate a new page.
 */
static uint64_t vma_prot_to_pte(uint32_t prot) {
    uint64_t pte = PTE_V | PTE_U;          // always valid + user-accessible
    if (prot & VMA_PROT_READ)  pte |= PTE_R;
    if (prot & VMA_PROT_WRITE) pte |= PTE_W;
    if (prot & VMA_PROT_EXEC)  pte |= PTE_X;
    return pte;
}

/* ── kill current process on unrecoverable fault ─────────────────────────*/
static void kill_current(trap_frame *frame, const char *reason) {
    printk("SEGFAULT: pid=%d %s sepc=0x%p stval=0x%p\n",
           current ? current->pid : -1, reason,
           frame->sepc, frame->stval);

    if (current) {
        proc_exit(1);   // does not return — switches to scheduler
    }

    /* No current process (fault in kernel context) — panic. */
    printk("Kernel page fault — halting\n");
    while (1) asm volatile("wfi");
}


/* ── main page fault handler ─────────────────────────────────────────────
 *
 * Called from handle_exception() for the three page-fault scause codes.
 *
 * Policy:
 *   1. Must have a current user-mode process.
 *   2. stval (faulting address) must fall inside a VMA with appropriate prot.
 *   3. If the page is already mapped, this is a protection violation → kill.
 *   4. Otherwise, demand-allocate a zeroed physical page and map it.
 */
void page_fault_handler(trap_frame *frame) {
    uintptr_t faulting_va = frame->stval;
    uint64_t  scause      = frame->scause;

    /* ── 1. Must have a user-mode process ──────────────────────────────── */
    if (!current) {
        kill_current(frame, "page fault with no current process");
        return; /* unreachable if kill_current panics */
    }

    /* ── 2. Look up the VMA containing the faulting address ────────────── */
    struct vm_area *vma = vma_find(current->vma_list, faulting_va);
    if (!vma) {
        kill_current(frame, "address not in any VMA");
        return;
    }

    /* ── 3. Check VMA protection matches the fault type ─────────────────── */
    uint32_t need = required_prot(scause);
    if (!(vma->prot & need)) {
        kill_current(frame, "VMA protection violation");
        return;
    }

    /* ── 4. If already mapped, this is a real protection fault ──────────── */
    uintptr_t page_va = PGROUNDDOWN(faulting_va);
    uintptr_t existing_pa = vmm_translate(current->pagetable, page_va);
    if (existing_pa != 0) {
        /*
         * Page is mapped but we faulted — this can happen on a write to a
         * read-only page (future CoW). For now, treat as a protection fault.
         */
        kill_current(frame, "protection fault on already-mapped page");
        return;
    }

    /* ── 5. Demand-allocate a zeroed physical page ───────────────────────── */
    void *phys = pmm_alloc_page();
    if (!phys) {
        kill_current(frame, "out of physical memory");
        return;
    }
    memset(phys, 0, PGSIZE);

    /* ── 6. Map the new page into the current page table ─────────────────── */
    uint64_t pte_perm = vma_prot_to_pte(vma->prot);
    if (vmm_map(current->pagetable, page_va, (uintptr_t)phys, pte_perm) != 0) {
        pmm_free_page(phys);
        kill_current(frame, "vmm_map failed during demand paging");
        return;
    }

    /*
     * TLB shootdown: vmm_map (via w_satp / sfence.vma) already flushes the
     * local TLB, so the retry of the faulting instruction will succeed.
     */
}

// ************************************ 
// END:    Processes, Page Faults, And friends or something
// ************************************ 


// Timer interrupt handler
static uint64_t timer_ticks = 0;

void timer_handler(void) {
    timer_ticks++;
    printk("Timer: %d ticks\n", timer_ticks * TIMER_INTERVAL);
    sbi_set_timer(read_csr(time) + TIMER_INTERVAL);
    proc_yield();
}

// Set/clear the SIE (supervisor interrupt enable) bit in sstatus
void interrupts_on(void) { set_csr_bit(sstatus, SSTATUS_SIE); }
void interrupts_off(void) { clear_csr_bit(sstatus, SSTATUS_SIE); }

// Set/clear individual interrupt-type bits in the SIE CSR
void interrupts_enable(uint64_t bit) { set_csr_bit(sie, 1ULL << bit); }
void interrupts_disable(uint64_t bit) { clear_csr_bit(sie, 1ULL << bit); }
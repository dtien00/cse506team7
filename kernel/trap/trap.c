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
#include "drivers/plic.h"
#include "drivers/uart.h"

#define TIMER_INTERVAL 1000000ULL // ~0.1 second at 10 MHz clock

// trap entry point code written in trap.S
extern void trap_entry(void);

static const char *proc_state_name(proc_state_t s) {
    switch (s) {
        case PROC_UNUSED:  return "UNUSED";
        case PROC_CREATED: return "CREATED";
        case PROC_READY:   return "READY";
        case PROC_RUNNING: return "RUNNING";
        case PROC_BLOCKED: return "BLOCKED";
        case PROC_ZOMBIE:  return "ZOMBIE";
        case PROC_STOPPED: return "STOPPED";
        default:           return "?";
    }
}

static const char *scause_name(uint64_t scause) {
    uint64_t code = scause & ~SCAUSE_INTERRUPT;
    if (scause & SCAUSE_INTERRUPT) {
        switch (code) {
            case SCAUSE_S_SOFTWARE_INT: return "S-mode software interrupt";
            case SCAUSE_S_TIMER_INT:    return "S-mode timer interrupt";
            case SCAUSE_S_EXTERNAL_INT: return "S-mode external interrupt";
            default:                    return "unknown interrupt";
        }
    }

    switch (code) {
        case SCAUSE_INST_MISALIGNED:  return "instruction address misaligned";
        case SCAUSE_INST_ACCESS_FAULT:return "instruction access fault";
        case SCAUSE_ILLEGAL_INST:     return "illegal instruction";
        case SCAUSE_BREAKPOINT:       return "breakpoint";
        case SCAUSE_LOAD_MISALIGNED:  return "load address misaligned";
        case SCAUSE_LOAD_ACCESS_FAULT:return "load access fault";
        case SCAUSE_STORE_MISALIGNED: return "store address misaligned";
        case SCAUSE_STORE_ACCESS_FAULT:return "store access fault";
        case SCAUSE_ECALL_FROM_U:     return "ecall from user";
        case SCAUSE_ECALL_FROM_S:     return "ecall from supervisor";
        case SCAUSE_INST_PAGE_FAULT:  return "instruction page fault";
        case SCAUSE_LOAD_PAGE_FAULT:  return "load page fault";
        case SCAUSE_STORE_PAGE_FAULT: return "store page fault";
        default:                      return "unknown exception";
    }
}

static void print_pte_flags(uint64_t pte) {
    printk("flags=%c%c%c%c%c%c%c%c",
           (pte & PTE_V) ? 'V' : '-',
           (pte & PTE_R) ? 'R' : '-',
           (pte & PTE_W) ? 'W' : '-',
           (pte & PTE_X) ? 'X' : '-',
           (pte & PTE_U) ? 'U' : '-',
           (pte & PTE_G) ? 'G' : '-',
           (pte & PTE_A) ? 'A' : '-',
           (pte & PTE_D) ? 'D' : '-');
}

static void dump_va_mapping(const char *tag, pagetable_t pt, uintptr_t va) {
    if (!pt) {
        printk("  %s: no pagetable\n", tag);
        return;
    }

    uint64_t *pte = vmm_walk(pt, va, 0);
    if (!pte || !(*pte & PTE_V)) {
        printk("  %s: va=0x%p unmapped\n", tag, va);
        return;
    }

    uintptr_t pa = PTE_TO_PA(*pte) | (va & (PGSIZE - 1));
    printk("  %s: va=0x%p -> pa=0x%p pte=0x%p ", tag, va, pa, *pte);
    print_pte_flags(*pte);
    printk("\n");
}

static void dump_exception_diagnostics(trap_frame *frame) {
    uint64_t live_satp = r_satp();
    uint64_t live_sie = read_csr(sie);
    uint64_t live_sip = read_csr(sip);
    uint64_t live_sscratch = read_csr(sscratch);

    printk("  Cause: %s\n", scause_name(frame->scause));
    printk("  Priv: %s-mode  SSTATUS[spp=%d sie=%d spie=%d sum=%d]\n",
           (frame->sstatus & SSTATUS_SPP) ? "S" : "U",
           (frame->sstatus & SSTATUS_SPP) ? 1 : 0,
           (frame->sstatus & SSTATUS_SIE) ? 1 : 0,
           (frame->sstatus & SSTATUS_SPIE) ? 1 : 0,
           (frame->sstatus & SSTATUS_SUM) ? 1 : 0);
    printk("  CSRs: satp=0x%p sie=0x%p sip=0x%p sscratch=0x%p\n",
           live_satp, live_sie, live_sip, live_sscratch);

    if (current) {
        printk("  Current: pid=%d name=%s state=%s pagetable=0x%p sz=0x%p trapframe=0x%p\n",
               current->pid, current->name, proc_state_name(current->state),
               (uintptr_t)current->pagetable, current->sz, (uintptr_t)current->trapframe);
        dump_va_mapping("current.pt stval", current->pagetable, frame->stval);
        dump_va_mapping("current.pt sepc", current->pagetable, frame->sepc);
    } else {
        printk("  Current: (none)\n");
    }

    dump_va_mapping("kernel.pt stval", vmm_kernel_pagetable(), frame->stval);
    dump_va_mapping("kernel.pt sepc", vmm_kernel_pagetable(), frame->sepc);
}

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
            // printk("External interrupt\n");
            uint32_t irq = plic_claim();
            if (irq == PLIC_UART_IRQ) {
                // handle the interrupt - adds to the driver's char buffer which a waiting read() can query later
                uart_handle_interrupt();
            }
            // complete the interrupt
            if (irq != 0) {
                plic_complete(irq);
            }
            break;

        default:
            printk("Unknown interrupt: %d\n", interrupt_code);
            break;
    }
}

void page_fault_handler(trap_frame *);

void handle_exception(trap_frame *frame) {
    // Handle U-mode syscalls silently — they are normal, not errors.
    if (frame->scause == SCAUSE_ECALL_FROM_U) {
        uint64_t old_sepc = frame->sepc;
        syscall_handler(frame);
        // Most syscalls resume at the instruction after ecall.
        // exec() may rewrite sepc to a new entry point; in that case do not bump.
        if (frame->sepc == old_sepc)
            frame->sepc += 4;
        return;
    }

    // Handle page faults before printing diagnostics: COW faults are expected
    // and handled silently by page_fault_handler.  On success it returns here;
    // on fatal fault it calls kill_current() which never returns.
    if (frame->scause == SCAUSE_INST_PAGE_FAULT ||
        frame->scause == SCAUSE_LOAD_PAGE_FAULT  ||
        frame->scause == SCAUSE_STORE_PAGE_FAULT) {
        page_fault_handler(frame);
        return;
    }

    printk("\nEXCEPTION: scause=0x%p stval=0x%p sepc=0x%p\n", frame->scause, frame->stval, frame->sepc);
    dump_exception_diagnostics(frame);

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

    // If returning to U-mode (SPP=0 in saved sstatus), restore the current
    // process's page table.  exec() may have switched SATP to a new image
    // while the parent's page table was still active; without this switch the
    // parent's sret would execute under the child's (now freed) page table.
    if (current && !(frame->sstatus & SSTATUS_SPP)) {
        // Deliver pending signals before every return to user mode.
        // Catches processes signaled while running in tight loops (no syscalls).
        uint32_t pending_stop = current->pending_signals & KERNEL_STOP_SIGS;
        if (pending_stop) {
            current->pending_signals &= ~pending_stop;
            proc_stop(current);  // yields; resumes after SIGCONT
        }
        uint32_t unmasked = current->pending_signals & ~current->signal_mask;
        if (unmasked & KERNEL_FATAL_SIGS) {
            current->pending_signals = 0;
            proc_exit(-1);       // never returns
        }

        // Ensure sret reenables S-mode interrupts after returning to user.
        frame->sstatus |= SSTATUS_SPIE;
        vmm_switch(current->pagetable);
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
    // If the fault came from S-mode, panic.
    if (frame->sstatus & SSTATUS_SPP) {
        printk("KERNEL PAGE FAULT: scause=0x%p stval=0x%p sepc=0x%p current=%d\n",
               frame->scause, frame->stval, frame->sepc,
               current ? current->pid : -1);
        printk("  Reason: %s\n", reason);
        dump_exception_diagnostics(frame);
        while (1) asm volatile("wfi");
    }

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
 * This function is invoked BEFORE any EXCEPTION diagnostic is printed, so
 * faults handled successfully here produce no kernel output — in particular,
 * COW store faults triggered by normal fork() usage are silent.
 *
 * Policy:
 *   1. Must have a current user-mode process.
 *   2. COW fast path (store fault on a PTE_COW page): promote to a private
 *      writable copy (or re-enable write in-place if refcount == 1).
 *      No VMA lookup needed — legitimacy was established at original map time.
 *      This also covers heap/mmap pages that have no VMA entry.
 *   3. stval (faulting address) must fall inside a VMA with appropriate prot.
 *   4. If the page is already mapped but not COW, this is a real protection
 *      violation → kill the process.
 *   5. Otherwise, demand-allocate a zeroed physical page and map it.
 */
void page_fault_handler(trap_frame *frame) {
    uintptr_t faulting_va = frame->stval;
    uint64_t  scause      = frame->scause;

    /* ── 1. Must have a user-mode process ──────────────────────────────── */
    if (!current) {
        kill_current(frame, "page fault with no current process");
        return;
    }

    uintptr_t page_va    = PGROUNDDOWN(faulting_va);
    uintptr_t existing_pa = vmm_translate(current->pagetable, page_va);

    /* ── 2. COW fast path: handle before VMA lookup ─────────────────────
     *
     * A page marked PTE_COW was previously writable — its legitimacy was
     * established at the time it was first mapped.  No VMA check needed.
     * This also covers heap/mmap regions that have no VMA entry.
     */
    if (existing_pa != 0 && scause == SCAUSE_STORE_PAGE_FAULT) {
        uint64_t *pte_ptr = vmm_walk(current->pagetable, page_va, 0);
        if (pte_ptr && (*pte_ptr & PTE_COW)) {
            void *old_phys = (void *)existing_pa;
            if (pmm_refcount(old_phys) == 1) {
                /* Last reference — re-enable write in place */
                *pte_ptr = (*pte_ptr & ~PTE_COW) | PTE_W;
            } else {
                /* Shared — allocate a private copy */
                void *new_phys = pmm_alloc_page();
                if (!new_phys) {
                    kill_current(frame, "COW: out of physical memory");
                    return;
                }
                memcpy(new_phys, old_phys, PGSIZE);
                pmm_decref(old_phys);
                /* PTE_FLAGS masks to bits [9:0] — do not OR in old PA bits */
                *pte_ptr = PA_TO_PTE((uintptr_t)new_phys)
                         | ((PTE_FLAGS(*pte_ptr) & ~(PTE_COW | PTE_W)) | PTE_W);
            }
            asm volatile("sfence.vma %0, zero" :: "r"(page_va));
            return;   /* retry the faulting instruction */
        }
    }

    /* ── 3. Look up the VMA for demand paging and protection checks ─────── */
    struct vm_area *vma = vma_find(current->vma_list, faulting_va);
    if (!vma) {
        /* ── 3a. GROWSDOWN: transparent stack expansion ──────────────────
         *
         * The faulting address is not in any existing VMA.  Check whether
         * it falls just below a VMA_GROWSDOWN region (the user stack) and
         * within the allowed maximum growth range (USTACK_SIZE from top).
         * If so, extend the stack VMA downward and map a fresh page; the
         * faulting instruction will be retried automatically by hardware.
         */
        struct vm_area *stack_vma =
            vma_find_growsdown(current->vma_list, faulting_va);
        if (stack_vma) {
            /* Enforce maximum stack size: the stack top (stack_vma->end) is
             * the anchor.  The floor is end - USTACK_SIZE.  Faults below the
             * floor are a genuine stack overflow — kill the process. */
            uintptr_t stack_floor = (stack_vma->end > USTACK_SIZE)
                                    ? stack_vma->end - USTACK_SIZE : 0;
            if (faulting_va < stack_floor) {
                kill_current(frame, "stack overflow: exceeded maximum stack size");
                return;
            }

            uintptr_t new_page   = PGROUNDDOWN(faulting_va);
            stack_vma->start     = new_page;   /* extend VMA downward */

            void *phys = pmm_alloc_page();
            if (!phys) {
                kill_current(frame, "stack growth: out of physical memory");
                return;
            }
            memset(phys, 0, PGSIZE);
            uint64_t pte_perm = vma_prot_to_pte(stack_vma->prot);
            if (vmm_map(current->pagetable, new_page,
                        (uintptr_t)phys, pte_perm) != 0) {
                pmm_free_page(phys);
                kill_current(frame, "stack growth: vmm_map failed");
                return;
            }
            return;   /* retry the faulting instruction */
        }

        /* No GROWSDOWN VMA matched — genuine bad access */
        printk("  Faulting VA=0x%p  sp=0x%p  sepc=0x%p\n",
               faulting_va, frame->sp, frame->sepc);
        vma_dump(current->vma_list);
        kill_current(frame, "address not in any VMA");
        return;
    }

    /* ── 4. Check VMA protection matches the fault type ─────────────────── */
    uint32_t need = required_prot(scause);
    if (!(vma->prot & need)) {
        kill_current(frame, "VMA protection violation");
        return;
    }

    /* ── 5. Already mapped but not COW — real protection fault ──────────── */
    if (existing_pa != 0) {
        kill_current(frame, "protection fault on already-mapped page");
        return;
    }

    /* ── 6. Demand-allocate a zeroed physical page ───────────────────────── */
    void *phys = pmm_alloc_page();
    if (!phys) {
        kill_current(frame, "out of physical memory");
        return;
    }
    memset(phys, 0, PGSIZE);

    /* ── 7. Map the new page into the current page table ─────────────────── */
    uint64_t pte_perm = vma_prot_to_pte(vma->prot);
    if (vmm_map(current->pagetable, page_va, (uintptr_t)phys, pte_perm) != 0) {
        pmm_free_page(phys);
        kill_current(frame, "vmm_map failed during demand paging");
        return;
    }
}

// ************************************ 
// END:    Processes, Page Faults, And friends or something
// ************************************ 


// Timer interrupt handler
uint64_t timer_ticks = 0;

void timer_handler(void) {
    timer_ticks++;
    sbi_set_timer(read_csr(time) + TIMER_INTERVAL);

    // Wake any processes whose timed sleep deadline has expired.
    for (int i = 0; i < MAX_PROCS; i++) {
        struct proc *p = &proctab[i];
        if (p->state == PROC_BLOCKED && p->wake_tick != 0 && timer_ticks >= p->wake_tick) {
            p->wake_tick = 0;
            proc_wakeup(p);  // p slept on itself (chan == p)
        }
    }

    // Only preempt when we're actually in a running process context.
    if (current != NULL && current->state == PROC_RUNNING) {
        current->utime_ticks++;  // charge every tick regardless of preemptibility
        if (current->time_slice > 0) {
            current->total_time++;
            if (current->total_time >= current->time_slice) {
                if (current->mlfq_level < MLFQ_LEVELS - 1)
                    current->mlfq_level++;
                current->total_time = 0;
                proc_yield();
                return;
            }
        }
        // time_slice == 0 means non-preemptible; do NOT yield here.
    }

    // Starvation prevention: every MLFQ_BOOST_INTERVAL ticks, move all BLOCKED and lower-queue READY processes back to level 0.
    if (timer_ticks % MLFQ_BOOST_INTERVAL == 0) {
        for (int i = 0; i < MAX_PROCS; i++) {
            if (proctab[i].state == PROC_BLOCKED ||
                proctab[i].state == PROC_READY)
                proctab[i].mlfq_level = 0;
        }
    }
}

// Set/clear the SIE (supervisor interrupt enable) bit in sstatus
void interrupts_on(void) { set_csr_bit(sstatus, SSTATUS_SIE); }
void interrupts_off(void) { clear_csr_bit(sstatus, SSTATUS_SIE); }

// Set/clear individual interrupt-type bits in the SIE CSR
void interrupts_enable(uint64_t bit) { set_csr_bit(sie, 1ULL << bit); }
void interrupts_disable(uint64_t bit) { clear_csr_bit(sie, 1ULL << bit); }
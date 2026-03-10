#include "trap/trap.h"
#include "arch/riscv.h"
#include "arch/sbi.h"
#include "kernel/printk.h"
#include "process/process.h"

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
            // Handle system call
            // syscall_handler(frame);
            // Advance PC past ecall instruction
            write_csr(sepc, frame->sepc + 4);
            return; // Don't panic, return normally
        case SCAUSE_ECALL_FROM_S:
            printk("  System call from supervisor mode\n");
            break;
        case SCAUSE_INST_PAGE_FAULT:
            printk("  Instruction page fault\n");
            break;
        case SCAUSE_LOAD_PAGE_FAULT:
            printk("  Load page fault\n");
            break;
        case SCAUSE_STORE_PAGE_FAULT:
            printk("  Store page fault\n");
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
    printk("TRAP: Register info scause=%p sip=%p sie=%p\n", (void *) frame->scause, (void *) read_csr(sip),
           (void *) read_csr(sie));
    if (frame->scause & SCAUSE_INTERRUPT) {
        // Interrupt
        handle_interrupt(frame);
    } else {
        // Exception
        handle_exception(frame);
    }
}

// Timer interrupt handler
static uint64_t timer_ticks = 0;

void timer_handler(void) {
    timer_ticks++;
    sbi_set_timer(read_csr(time) + TIMER_INTERVAL);
    proc_yield();
}

// Set/clear the SIE (supervisor interrupt enable) bit in sstatus
void interrupts_on(void) { set_csr_bit(sstatus, SSTATUS_SIE); }
void interrupts_off(void) { clear_csr_bit(sstatus, SSTATUS_SIE); }

// Set/clear individual interrupt-type bits in the SIE CSR
void interrupts_enable(uint64_t bit) { set_csr_bit(sie, 1ULL << bit); }
void interrupts_disable(uint64_t bit) { clear_csr_bit(sie, 1ULL << bit); }

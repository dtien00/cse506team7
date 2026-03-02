#include "trap/trap.h"
#include "arch/riscv.h"
#include "kernel/printk.h"
#include "trap/syscall.h"

// Forward declaration of trap entry point (defined in trap_entry.S)
extern void trap_entry(void);

void trap_init(void) {
  // Set trap vector to trap_entry
  w_stvec((uint64_t)trap_entry);
  
  // Enable interrupts
  w_sie(r_sie() | SIE_SEIE | SIE_STIE | SIE_SSIE);
  
  printk("Trap: Initialized (stvec=0x%lx)\n", r_stvec());
}

static void handle_exception(trapframe_t *tf, uint64_t scause, uint64_t stval, uint64_t sepc) {
  printk("\nEXCEPTION: scause=0x%lx stval=0x%lx sepc=0x%lx\n", scause, stval, sepc);
  
  switch (scause) {
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
      syscall_handler(tf);
      // Advance PC past ecall instruction
      w_sepc(sepc + 4);
      return;  // Don't panic, return normally
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
  printk("  ra=0x%016lx sp=0x%016lx gp=0x%016lx tp=0x%016lx\n", 
         tf->ra, tf->sp, tf->gp, tf->tp);
  printk("  t0=0x%016lx t1=0x%016lx t2=0x%016lx\n",
         tf->t0, tf->t1, tf->t2);
  printk("  s0=0x%016lx s1=0x%016lx\n",
         tf->s0, tf->s1);
  printk("  a0=0x%016lx a1=0x%016lx a2=0x%016lx a3=0x%016lx\n",
         tf->a0, tf->a1, tf->a2, tf->a3);
  printk("  a4=0x%016lx a5=0x%016lx a6=0x%016lx a7=0x%016lx\n",
         tf->a4, tf->a5, tf->a6, tf->a7);
  
  // Halt
  printk("\nKernel panic - halting\n");
  while (1) {
    asm volatile("wfi");
  }
}

static void handle_interrupt(trapframe_t *tf, uint64_t scause) {
  uint64_t interrupt_code = scause & ~SCAUSE_INTERRUPT;
  
  switch (interrupt_code) {
    case SCAUSE_S_SOFTWARE_INT:
      printk("Software interrupt\n");
      // Clear software interrupt
      w_sip(r_sip() & ~SIP_SSIP);
      break;
      
    case SCAUSE_S_TIMER_INT:
      timer_handler();
      break;
      
    case SCAUSE_S_EXTERNAL_INT:
      printk("External interrupt\n");
      // TODO: Handle external interrupts (UART, disk, etc.)
      break;
      
    default:
      printk("Unknown interrupt: %lu\n", interrupt_code);
      break;
  }
}

void trap_handler(trapframe_t *tf) {
  uint64_t scause = r_scause();
  uint64_t stval = r_stval();
  uint64_t sepc = r_sepc();
  
  if (scause & SCAUSE_INTERRUPT) {
    // Interrupt
    handle_interrupt(tf, scause);
  } else {
    // Exception
    handle_exception(tf, scause, stval, sepc);
  }
}

// Timer interrupt handler
static uint64_t timer_ticks = 0;

void timer_handler(void) {
  timer_ticks++;
  
  // Print message every ~10 seconds (assuming 10MHz timer)
  if (timer_ticks % 100000000 == 0) {
    printk("Timer: %lu ticks\n", timer_ticks);
  }
  
  // Clear timer interrupt by setting next timer
  // Note: On RISC-V, timer is cleared by writing to mtimecmp
  // In supervisor mode, we need to make an SBI call
  // For now, just acknowledge it
  w_sip(r_sip() & ~SIP_STIP);
}

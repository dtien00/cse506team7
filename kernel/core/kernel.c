#include "arch/riscv.h"
#include "kernel/printk.h"
#include "trap/trap.h"
#include "process/process.h"

// External symbols from linker script
extern char _kernel_end[];

void boot(uint64_t hart_id, uint64_t dtb) {
    printk("\n");
    printk("========================================\n");
    printk("       SBUnix Operating System\n");
    printk("       RISC-V 64-bit Kernel\n");
    printk("========================================\n");
    printk("\n");

    uintptr_t kernel_start = 0x80200000;
    uintptr_t kernel_end = (uintptr_t)_kernel_end;
    size_t kernel_size = kernel_end - kernel_start;

    printk("Kernel: %p - %p (%d KB)\n",
           kernel_start, kernel_end, kernel_size / 1024);

    // TODO: Initialize physical memory manager
    // TODO: Initialize virtual memory manager
    // TODO: Initialize kernel heap

    // Initialize scheduler
    proc_init();

    // Initialize trap handling
    trap_init();

    // Enable interrupts
    interrupts_on();
    printk("Turned on global interrupts. Now waiting for interrupts...\n");

    // Create additional kernel threads
    proc_create("task_a");
    proc_create("task_b");

    // Enter scheduler (never returns while there are runnable processes)
    schedule();

    printk("End of SBUNIX DEBUGGING\n");
    while (1)
        asm volatile("wfi");
}

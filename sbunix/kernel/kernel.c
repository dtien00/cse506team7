#include <printk.h>
#include <trap/trap.h>

// External symbols from linker script
extern char _kernel_end[];

// sbi passed in the hart_id and dtb address in a0 and a1 registers
// which also double as function arguments - call boot will pass those values as arguments to the boot function
// as long as boot is the very first method that runs
void boot(uint64_t hart_id, uint64_t dtb) {
    printk("\n");
    printk("========================================\n");
    printk("       SBUnix Operating System\n");
    printk("       RISC-V 64-bit Kernel\n");
    printk("========================================\n");
    printk("\n");

    // Calculate kernel size
    uintptr_t kernel_start = 0x80200000;
    uintptr_t kernel_end = (uintptr_t) _kernel_end;
    size_t kernel_size = kernel_end - kernel_start;

    printk("Kernel: %p - %p (%d KB)\n", kernel_start, kernel_end, kernel_size / 1024);

    // Initialize trap handling
    trap_init();

    // after configuring everything we allow interrupts
    interrupts_on();
    printk("Turned on global interrupts. Now waiting for interrupts...\n");
    // wait for interrupts
    while (1)
        asm volatile("wfi");
}

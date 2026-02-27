#include <printk.h>
#include <riscv.h>
#include <trap.h>

void boot(void) {
    printk("Booting SBUnix\n");

    // Install trap entry point
    write_csr(stvec, (uint64_t) trap_entry);
    printk("Trap entry point (stvec) set to: %p\n", read_csr(stvec));
    while (1)
        asm volatile("wfi"); // wait for interrupts
}

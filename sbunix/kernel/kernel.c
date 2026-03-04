#include <printk.h>
#include <riscv.h>
#include <sbi.h>
#include <trap.h>

// sbi passed in the hart_id and dtb address in a0 and a1 registers
// which also double as function arguments - call boot will pass those values as arguments to the boot function
// as long as boot is the very first method that runs
void boot(uint64_t hart_id, uint64_t dtb) {
    printk("Booting SBUnix\n");

    printk("satp: %p\n", read_csr(satp)); // page table address
    printk("hart id: %ld  dtb: %p\n", (long)hart_id, dtb);

    // Install trap entry point
    write_csr(stvec, (uint64_t) trap_entry);
    printk("Trap entry point (stvec) set to: %p\n", read_csr(stvec));

    // after configuring everything we allow interrupts
    interrupts_on();
    printk("Turned on global interrupts. Now waiting for interrupts...\n");
    // wait for interrupts
    while (1)
        asm volatile("wfi");
}

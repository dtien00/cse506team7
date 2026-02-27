#include <defs.h>
#include <printk.h>
#include <riscv.h>

void trap_handler() {
    uint64_t scause = read_csr(scause); // cause of the interrupt
    uint64_t sip = read_csr(sip); // info on pending interrupts
    uint64_t sie = read_csr(sie); // info on enabled interrupts
    char *type;
    if (scause >> 63)
        type = "INTERRUPT";
    else
        type = "EXCEPTION";
    printk("TRAP: %s\nRegister info scause=%p sip=%p sie=%p\n", type, (void *) scause, (void *) sip, (void *) sie);

    // trap dealt with - clear sip register to tell the hardware we're done
    if (scause >> 63) {
        uint64_t code = scause & ~(1ULL << 63); // calculate exception code
        write_csr(sie, read_csr(sie) & ~(1ULL << code)); // clear the sip bit corresponding to the exception code
    }
}

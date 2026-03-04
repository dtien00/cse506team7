#include "trap.h"


#include <defs.h>
#include <printk.h>
#include <riscv.h>

void trap_handler(struct trap_frame *frame) {
    uint64_t scause = frame->scause; // cause of the interrupt
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
    } else {
        // advance past faulting instruction (2 bytes if compressed, 4 if normal)
        uint64_t inst_len = (*(uint16_t *) frame->sepc & 0x3) == 0x3 ? 4 : 2;
        frame->sepc += inst_len;
        // write updated sepc back so sret returns to correct place
        write_csr(sepc, frame->sepc);
    }
}

void interrupts_on(void) { write_csr(sstatus, read_csr(sstatus) | (1ULL << 1)); }

void interrupts_off(void) { write_csr(sstatus, read_csr(sstatus) & ~(1ULL << 1)); }

void interrupts_enable(uint64_t bit) { write_csr(sie, read_csr(sie) | (1ULL << bit)); }

void interrupts_disable(uint64_t bit) { write_csr(sie, read_csr(sie) & ~(1ULL << bit)); }

#include "drivers/plic.h"

void plic_init(void) {
    // One-time PLIC hardware setup — just set threshold to accept all IRQs
    PLIC_S_THRESHOLD = 0;
}

void plic_enable_irq(uint32_t irq) {
    PLIC_PRIORITY(irq) = 1; // priority > 0 to enable source
    PLIC_S_ENABLE |= (1 << irq); // enable in S-mode hart 0
}

uint32_t plic_claim(void) { return PLIC_S_CLAIM; }

void plic_complete(uint32_t irq) { PLIC_S_CLAIM = irq; }

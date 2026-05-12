#include "drivers/plic.h"

void plic_init(void) {
    // One-time PLIC hardware setup — just set threshold to accept all IRQs
    PLIC_S_THRESHOLD = 0;
}

void plic_enable_irq(uint32_t irq) {
    PLIC_PRIORITY(irq) = 1;
    volatile uint32_t *en = (volatile uint32_t *)(PLIC_BASE + 0x2080 + (irq / 32) * 4);
    *en |= (1U << (irq % 32));
}

uint32_t plic_claim(void) { return PLIC_S_CLAIM; }

void plic_complete(uint32_t irq) { PLIC_S_CLAIM = irq; }

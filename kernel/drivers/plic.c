#include "drivers/plic.h"
#include "kernel/printk.h"

void plic_init(void) {
    // One-time PLIC hardware setup — just set threshold to accept all IRQs
    PLIC_S_THRESHOLD = 0;
}

void plic_enable_irq(uint32_t irq) {
    PLIC_PRIORITY(irq) = 1;
    volatile uint32_t *en = (volatile uint32_t *)(PLIC_BASE + 0x2080 + (irq / 32) * 4);
    *en |= (1U << (irq % 32));
}

void plic_disable_irq(uint32_t irq) {
    volatile uint32_t *en = (volatile uint32_t *)(PLIC_BASE + 0x2080 + (irq / 32) * 4);
    *en &= ~(1U << (irq % 32));
}

uint32_t plic_claim(void) { return PLIC_S_CLAIM; }

void plic_complete(uint32_t irq) { PLIC_S_CLAIM = irq; }

void plic_dump_pending(void) {
    volatile uint32_t *pend = (volatile uint32_t *)(PLIC_BASE + 0x1000);
    volatile uint32_t *enab = (volatile uint32_t *)(PLIC_BASE + 0x2080);
    printk("plic: pending[0]=0x%lx pending[1]=0x%lx enable[0]=0x%lx enable[1]=0x%lx threshold=%u\n",
           (unsigned long)pend[0], (unsigned long)pend[1],
           (unsigned long)enab[0], (unsigned long)enab[1], PLIC_S_THRESHOLD);
}

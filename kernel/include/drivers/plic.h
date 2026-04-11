#pragma once
#include "kernel/types.h"

// QEMU virt PLIC base address
#define PLIC_BASE 0x0C000000UL

// UART is IRQ 10 on the QEMU virt machine
#define PLIC_UART_IRQ 10

// virtio-blk is IRQ 1 on the QEMU virt machine (first virtio device)
#define PLIC_VIRTIO_IRQ 1

// Register addresses (supervisor-mode, hart 0)
// Priority:  one uint32_t per IRQ source
#define PLIC_PRIORITY(irq) (*(volatile uint32_t *) (PLIC_BASE + 4 * (irq)))

// Enable bits: one bit per IRQ, supervisor-mode hart 0 starts at offset 0x2080
#define PLIC_S_ENABLE (*(volatile uint32_t *) (PLIC_BASE + 0x2080))

// Threshold: supervisor-mode hart 0, only IRQs with priority > threshold fire
#define PLIC_S_THRESHOLD (*(volatile uint32_t *) (PLIC_BASE + 0x201000))

// Claim/complete: read to claim the highest-priority pending IRQ,
//                 write the IRQ number back to signal completion
#define PLIC_S_CLAIM (*(volatile uint32_t *) (PLIC_BASE + 0x201004))

void plic_init(void);
void plic_enable_irq(uint32_t irq); // called by each driver for its own IRQ
uint32_t plic_claim(void);
void plic_complete(uint32_t irq);

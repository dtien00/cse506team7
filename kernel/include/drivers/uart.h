#pragma once
#include "kernel/types.h"

// QEMU virt UART (NS16550A) base address
#define UART_BASE 0x10000000UL

// Register offsets — each is 1 byte wide
#define UART_RBR 0 // Receive Buffer Register    (read,  DLAB=0)
#define UART_THR 0 // Transmit Holding Register  (write, DLAB=0)
#define UART_IER 1 // Interrupt Enable Register
#define UART_IIR 2 // Interrupt ID Register      (read)
#define UART_FCR 2 // FIFO Control Register      (write)
#define UART_LCR 3 // Line Control Register
#define UART_MCR 4 // Modem Control Register
#define UART_LSR 5 // Line Status Register
#define UART_DLL 0 // Divisor Latch Low          (DLAB=1)
#define UART_DLH 1 // Divisor Latch High         (DLAB=1)

// LSR bits
#define UART_LSR_DR (1 << 0) // Data Ready:           RX has a byte to read
#define UART_LSR_THRE (1 << 5) // TX Holding Reg Empty: safe to write next byte

// IER bits
#define UART_IER_RX (1 << 0) // Enable RX interrupt

// Functions used outside uart.c
void uart_putc_sync(char c); // blocking transmit — usable before scheduler
int  uart_read_raw(char *buf, uint64_t n); //blocking read from driver maintained buffer
void uart_handle_interrupt(void); // called from trap handler on external IRQ

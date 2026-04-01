#include "drivers/uart.h"
#include "drivers/plic.h"
#include "kernel/console.h"
#include "kernel/types.h"

static volatile uint8_t *uart_base = (volatile uint8_t *) UART_BASE;
#define UART_REG(r) (uart_base[(r)])

#define UART_RX_BUF_SIZE 256

static void uart_hw_init(void) {
    UART_REG(UART_IER) = 0x00; // disable interrupts during setup
    UART_REG(UART_LCR) = 0x80; // DLAB=1 to set baud divisor
    UART_REG(UART_DLL) = 0x03; // 38400 baud (divisor=3)
    UART_REG(UART_DLH) = 0x00;
    UART_REG(UART_LCR) = 0x03; // DLAB=0, 8N1
    UART_REG(UART_FCR) = 0x07; // enable + reset FIFOs
    UART_REG(UART_IER) = UART_IER_RX; // enable RX interrupt

    plic_enable_irq(PLIC_UART_IRQ); // wire UART IRQ 10 through the PLIC
}

// Blocking transmit — polls LSR until TX register is empty
void uart_putc_sync(char c) {
    while (!(UART_REG(UART_LSR) & UART_LSR_THRE))
        ;
    UART_REG(UART_THR) = (uint8_t) c;
}

// Read one byte directly from hardware — non-blocking, -1 if none ready
static int uart_getc(void) {
    if (UART_REG(UART_LSR) & UART_LSR_DR)
        return UART_REG(UART_RBR);
    return -1;
}

// Called from trap handler on external interrupt
void uart_handle_interrupt(void) {
    int c;
    while ((c = uart_getc()) >= 0)
        console_handle_input((char)c);  // just delivers upward, done
}

static void uart_write(uint64_t num_bytes, const char *data) {
    for (uint64_t i = 0; i < num_bytes; i++)
        uart_putc_sync(data[i]);
}

static console_driver_t uart_driver = {
        .name = "uart",
        .init = uart_hw_init, // called once when console_set_driver("uart") is invoked
        .write = uart_write,
        .next = NULL,
};

void console_uart_register(void) { console_register_driver(&uart_driver); }

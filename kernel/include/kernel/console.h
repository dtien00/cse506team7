#pragma once

#include "kernel/types.h"

typedef struct console_driver {
    const char *name;
    void (*init)(void); // called once on activation
    void (*write)(uint64_t num_bytes, const char *data);
    int (*read)(char *buf, uint64_t num_bytes); // returns bytes read, -1 on error
    struct console_driver *next; // linked list
} console_driver_t;

// Called by each backend's own file to add itself to the list
void console_register_driver(console_driver_t *drv);

// Switch active driver by name — calls drv->init() on first activation
// Returns 0 on success, -1 if name not found
int console_set_driver(const char *name);

// Returns name of currently active driver ("sbi", "uart", etc.) for debugging
const char *console_active_name(void);

void console_write(uint64_t num_bytes, const char *data);
int console_read(char *buf, uint64_t num_bytes);

// Called in kernel_init — registers all drivers, activates "sbi"
void console_init(void);

// per-driver registration methods
void console_sbi_register(void);
void console_uart_register(void);

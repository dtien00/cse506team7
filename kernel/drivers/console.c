#include "kernel/console.h"
#include "arch/sbi.h"
#include "lib/kstring.h"

static console_driver_t *drivers = NULL; // all registered drivers
static console_driver_t *active = NULL; // currently active one

void console_register_driver(console_driver_t *drv) {
    // add to the head of the linked list of drivers
    drv->next = drivers;
    drivers = drv;
}

int console_set_driver(const char *name) {
    for (console_driver_t *d = drivers; d; d = d->next) {
        if (strcmp(d->name, name) == 0) {
            active = d;
            if (d->init)
                d->init(); // initialize hardware on first switch
            return 0;
        }
    }
    return -1; // driver not found
}

const char *console_active_name(void) { return active ? active->name : "none"; }

static void (*input_handler)(char c) = NULL;

void console_set_input_handler(void (*handler)(char c)) {
    input_handler = handler;
}

void console_handle_input(char c) {
    if (input_handler)
        input_handler(c);
    // if nothing registered yet (early boot), byte is dropped —
    // same behavior as console_write() before console_init()
}

void console_write(uint64_t num_bytes, const char *data) {
    if (active && active->write)
        active->write(num_bytes, data);
    // if nothing is active yet, output is silently dropped —
    // this only happens before console_init() in the very early boot
}

void console_init(void) {
    // SBI is always safe — no MMIO, no mapping needed, always available
    // Register and activate it directly as the boot default
    console_sbi_register();
    console_set_driver("sbi");
}

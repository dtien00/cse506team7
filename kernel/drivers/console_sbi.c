#include "arch/sbi.h"
#include "kernel/console.h"

static void sbi_console_write(const uint64_t num_bytes, const char *data) {
    uint64_t offset = 0;
    while (offset < num_bytes) {
        const sbi_ret r = sbi_write(num_bytes - offset, // ← now correctly calls arch/sbi.h
                                    (uint8_t *) data + offset);
        if (r.error != 0)
            break;
        if (r.value <= 0)
            break;
        offset += r.value;
    }
}

static console_driver_t sbi_driver = {
        .name = "sbi",
        .init = NULL,
        .write = sbi_console_write,
        .next = NULL,
};

void console_sbi_register(void) { console_register_driver(&sbi_driver); }

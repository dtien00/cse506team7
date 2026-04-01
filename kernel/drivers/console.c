#include "kernel/console.h"
#include "arch/sbi.h"

void console_write(const uint64_t num_bytes, const char* data) {
    uint64_t offset = 0;
    // keep writing in case SBI is only able to partially write
    while (offset < num_bytes) {
        const sbi_ret r = sbi_write(num_bytes - offset, (uint8_t*)data + offset);
        if (r.error < 0) break;
        if (r.value <= 0) break;
        offset += r.value;
    }
}
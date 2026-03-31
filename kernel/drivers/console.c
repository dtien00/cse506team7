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

// Blocking read: busy-waits until at least 1 byte is available, then reads up
// to num_bytes. Returns the number of bytes actually read, or -1 on error.
int console_read(uint64_t num_bytes, char *data) {
    uint64_t offset = 0;
    while (offset < num_bytes) {
        sbi_ret r = sbi_read(num_bytes - offset, (uint8_t *)data + offset);
        if (r.error < 0) return -1;
        offset += r.value;
        // Return as soon as we have at least one byte
        if (offset > 0) break;
    }
    return (int)offset;
}
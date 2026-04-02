#include "arch/sbi.h"

#define SBI_EXT_DBCN 0x4442434E
#define SBI_EXT_DBCN_WRITE_BYTE 2
#define SBI_EXT_DBCN_WRITE 0
#define SBI_EXT_TIME 0x54494D45
#define SBI_EXT_TIME_SET 0

sbi_ret sbi_ecall(uint64_t eid, uint64_t fid, uint64_t arg0, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4,
                  uint64_t arg5) {
    register uint64_t a0 asm("a0") = arg0;
    register uint64_t a1 asm("a1") = arg1;
    register uint64_t a2 asm("a2") = arg2;
    register uint64_t a3 asm("a3") = arg3;
    register uint64_t a4 asm("a4") = arg4;
    register uint64_t a5 asm("a5") = arg5;
    register uint64_t a6 asm("a6") = fid;
    register uint64_t a7 asm("a7") = eid;

    asm volatile("ecall" : "+r"(a0), "+r"(a1) : "r"(a2), "r"(a3), "r"(a4), "r"(a5), "r"(a6), "r"(a7) : "memory");

    return (sbi_ret) {.error = (int64_t) a0, .value = a1};
}

// returns number of bytes written in sbi_ret.value
sbi_ret sbi_write(const uint64_t num_bytes, const uint8_t *data) {
    return sbi_ecall(SBI_EXT_DBCN, SBI_EXT_DBCN_WRITE, num_bytes, (uint64_t) data, 0, 0, 0, 0);
}

void sbi_set_timer(const uint64_t time_after) {
    (void) sbi_ecall(SBI_EXT_TIME, SBI_EXT_TIME_SET, time_after, 0, 0, 0, 0, 0);
}

// Legacy SBI console getchar (EID=0x02).
// Returns the received character (0-255), or -1 if no character is available.
// NOTE: EID 0x01 is sbi_console_putchar; EID 0x02 is sbi_console_getchar.
int64_t sbi_console_getchar(void) {
    sbi_ret r = sbi_ecall(0x02, 0, 0, 0, 0, 0, 0, 0);
    return r.error;
}
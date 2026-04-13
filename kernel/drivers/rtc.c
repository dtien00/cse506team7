#include "drivers/rtc.h"
#include "kernel/printk.h"

static volatile uint32_t *rtc_base = (volatile uint32_t *)RTC_BASE;

// Access a 32-bit RTC register by byte offset
#define RTC_REG(off)  (*(volatile uint32_t *)((uint8_t *)rtc_base + (off)))

void rtc_init(void) {
    printk("RTC: Goldfish RTC at 0x%lx\n", (unsigned long)RTC_BASE);
}

// Read current time as nanoseconds since Unix epoch.
// The Goldfish RTC latches TIME_HIGH when TIME_LOW is read, so TIME_LOW
// must be read first to get a consistent 64-bit value.
uint64_t rtc_read_ns(void) {
    uint32_t lo = RTC_REG(RTC_TIME_LOW);   // latch happens here
    uint32_t hi = RTC_REG(RTC_TIME_HIGH);  // reads latched value
    return ((uint64_t)hi << 32) | lo;
}

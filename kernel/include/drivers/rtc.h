#pragma once
#include "kernel/types.h"

// Goldfish RTC base address on QEMU virt machine
#define RTC_BASE      0x101000UL

// Register offsets (32-bit MMIO reads)
#define RTC_TIME_LOW  0x00  // read first — hardware latches TIME_HIGH on this read
#define RTC_TIME_HIGH 0x04  // read second — returns value latched by TIME_LOW read

void     rtc_init(void);
uint64_t rtc_read_ns(void);  // returns nanoseconds since Unix epoch

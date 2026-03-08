#pragma once

#include <types.h>

typedef struct {
    int64_t error;
    uint64_t value;
} sbi_ret;

// writes a fixed number of bytes to debug console
sbi_ret sbi_write(uint64_t, const uint8_t *);

// schedules a timer event with sbi
void sbi_set_timer(uint64_t);

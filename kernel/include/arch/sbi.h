#pragma once

#include "kernel/types.h"

typedef struct {
    int64_t error;
    uint64_t value;
} sbi_ret;

// writes a fixed number of bytes to debug console
sbi_ret sbi_write(uint64_t, const uint8_t *);

// reads up to num_bytes from debug console; returns bytes read in sbi_ret.value
sbi_ret sbi_read(uint64_t, uint8_t *);

// schedules a timer event with sbi
void sbi_set_timer(uint64_t);
#pragma once

#include <defs.h>

typedef struct {
    int64_t error;
    uint64_t value;
} sbi_ret;

sbi_ret sbi_write(uint64_t, const uint8_t*);
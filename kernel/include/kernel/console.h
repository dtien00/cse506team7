#pragma once

#include "kernel/types.h"

void console_write(uint64_t num_bytes, const char *data);
int  console_read(uint64_t num_bytes, char *data);
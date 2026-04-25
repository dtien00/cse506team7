#pragma once

#include "kernel/types.h"

// Copy len bytes from kernel src to user dst.
// Returns 0 on success, -EFAULT on bad/unmapped user pointer.
int copy_to_user(void *dst, const void *src, size_t len);

// Copy len bytes from user src to kernel dst.
// Returns 0 on success, -EFAULT on bad/unmapped user pointer.
int copy_from_user(void *dst, const void *src, size_t len);

// Copy a NUL-terminated string from user src into kernel dst (at most n bytes).
// Returns number of bytes copied (excluding NUL) on success, -EFAULT on fault.
ssize_t strncpy_from_user(char *dst, const char *src, size_t n);

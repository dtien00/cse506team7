#pragma once

#include "kernel/types.h"

// Kernel heap allocator
// Simple first-fit allocator for dynamic kernel memory

void  kmalloc_init(void);
void *kmalloc(size_t size);
void *kzalloc(size_t size);  // Allocate and zero
void *krealloc(void *ptr, size_t size);
void  kfree(void *ptr);
void   kmalloc_stats(void);       // Print allocator statistics
size_t kmalloc_used_bytes(void);  // Currently live bytes (alloc - freed)
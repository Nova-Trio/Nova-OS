#pragma once
#include <stdint.h>
#include <stddef.h>

#define KERNEL_HEAP_BASE 0xFFFF900000000000ULL

void heap_init(void);

void* kmalloc(size_t size);
void* kzalloc(size_t size);
void* krealloc(void* ptr, size_t new_size);
void kfree(void *ptr);

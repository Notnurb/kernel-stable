/*
 * heap.h - Kernel Heap Allocator
 *
 * Simple free-list heap allocator backed by the physical frame allocator.
 * Provides kmalloc/kfree for dynamic memory allocation in kernel space.
 *
 * The heap grows on demand by allocating physical frames and mapping
 * them into the virtual address space above _kernel_end.
 */
#ifndef HEAP_H
#define HEAP_H

#include "pmm.h"
#include <stdint.h>
#include <stddef.h>

/* Heap starts in higher-half, right after the initial 2MiB mapping.
 * This triggers on-demand page table allocation as the heap grows. */
#define HEAP_START 0xFFFFFFFF80200000ULL

/* Initialize the kernel heap. Returns 0 on success, -1 on failure. */
int heap_init(void);

/* Allocate `size` bytes from the kernel heap. Returns NULL on failure. */
void* kmalloc(size_t size);

/* Allocate `count` objects of `size` bytes, aligned to `align`. */
void* kmalloc_aligned(size_t size, size_t align);

/* Free previously allocated memory. */
void kfree(void* ptr);

/* Get heap stats */
uint64_t heap_used(void);
uint64_t heap_size(void);

#endif /* HEAP_H */

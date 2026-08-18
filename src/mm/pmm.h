/*
 * pmm.h - Physical Memory Manager (Frame Allocator)
 *
 * Bitmap-based allocator for 4KiB physical page frames.
 * Each bit represents one frame: 0 = free, 1 = allocated.
 *
 * Initialized from the Multiboot2 memory map to know which
 * physical addresses are usable RAM.
 */
#ifndef PMM_H
#define PMM_H

#include <stdint.h>
#include <stddef.h>

/* Frame size: 4 KiB */
#define PMM_FRAME_SIZE      4096
#define PMM_FRAME_SHIFT     12

/* Convert between frame indices and physical addresses */
#define PMM_FRAME_TO_ADDR(frame)    ((phys_addr_t)(frame) << PMM_FRAME_SHIFT)
#define PMM_ADDR_TO_FRAME(addr)     ((uint64_t)(addr) >> PMM_FRAME_SHIFT)

typedef uint64_t phys_addr_t;

/* Initialize the physical memory manager from Multiboot2 info */
void pmm_init(void* mbi);

/* Allocate a single physical frame. Returns physical address, or 0 on failure. */
phys_addr_t pmm_alloc_frame(void);

/* Allocate a contiguous block of `count` frames. Returns base address, or 0. */
phys_addr_t pmm_alloc_frames(size_t count);

/* Free a single physical frame. */
void pmm_free_frame(phys_addr_t addr);

/* Free a contiguous block of `count` frames. */
void pmm_free_frames(phys_addr_t addr, size_t count);

/* Return total usable memory in bytes */
uint64_t pmm_total_memory(void);

/* Return number of free frames */
uint64_t pmm_free_frames_count(void);

#endif /* PMM_H */

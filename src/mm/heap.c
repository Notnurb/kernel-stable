/*
 * heap.c - Kernel Heap Allocator
 *
 * Simple free-list allocator. Each free block has a header containing
 * its size and a pointer to the next free block. Allocated blocks have
 * a header with size and a magic value for validation.
 *
 * When the heap runs out, it extends by allocating physical frames
 * from the PMM and mapping them into virtual space via paging_map_page.
 *
 * Layout of each block (free or allocated):
 *   [uint64_t size]      - block size (including header, excluding this field)
 *   [uint64_t magic]     - 0xDEADBEEFCAFEBABE if allocated, 0 if free
 *   [uint64_t next_free] - (only if free) pointer to next free block
 *   [user data...]       - (only if allocated)
 */
#include "heap.h"
#include "pmm.h"
#include "paging.h"
#include "../kernel/prism.h"
#include "../drivers/serial.h"
#include <string.h>

#define HEAP_MAGIC  0xDEADBEEFCAFEBABEULL
#define BLOCK_HEADER_SIZE  (sizeof(uint64_t) * 2)
#define MIN_BLOCK_SIZE     (BLOCK_HEADER_SIZE + sizeof(uint64_t) * 2) /* min free block */

/* Heap state */
static uint64_t heap_start = 0;
static uint64_t heap_end   = 0;
static uint64_t heap_used_bytes = 0;

/* Free list head */
typedef struct free_block {
    uint64_t size;
    uint64_t magic;  /* 0 = free */
    struct free_block* next;
} free_block_t;

static free_block_t* free_list = NULL;

static inline uint64_t align_up(uint64_t val, uint64_t align) {
    return (val + align - 1) & ~(align - 1);
}

/*
 * Extend the heap by `pages` pages.
 * Allocates physical frames and maps them at heap_end.
 */
static int heap_extend(size_t pages) {
    for (size_t i = 0; i < pages; i++) {
        phys_addr_t frame = pmm_alloc_frame();
        if (!frame) return -1;

        int result = paging_map_page(heap_end, frame,
            PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
        if (result != 0) {
            pmm_free_frame(frame);
            return -1;
        }

        memset((void*)heap_end, 0, PMM_FRAME_SIZE);
        heap_end += PMM_FRAME_SIZE;
    }
    return 0;
}

/*
 * heap_init - Initialize the kernel heap.
 *
 * Starts at a fixed higher-half virtual address, allocates initial pages.
 * Returns 0 on success, -1 on failure.
 */
int heap_init(void) {
    kprint("Heap: Initializing...\n");

    heap_start = HEAP_START;
    heap_end   = HEAP_START;

    /* Map initial heap: 4 pages = 16 KiB */
    if (heap_extend(4) != 0) {
        kprint("Heap: ERROR - Cannot allocate initial pages!\n");
        return -1;
    }

    /* Set up the initial free block spanning the entire heap */
    free_list = (free_block_t*)heap_start;
    free_list->size = heap_end - heap_start - BLOCK_HEADER_SIZE;
    free_list->magic = 0; /* free */
    free_list->next = NULL;

    kprint("Heap: Ready (16 KiB initial)\n");
    return 0;
}

/*
 * split_block - Split a free block if it's large enough for two.
 */
static void split_block(free_block_t* block, size_t needed) {
    if (block->size >= needed + MIN_BLOCK_SIZE) {
        /* Create a new free block after the allocated portion */
        free_block_t* new_free = (free_block_t*)((uint8_t*)block
            + BLOCK_HEADER_SIZE + needed);
        new_free->size = block->size - needed - BLOCK_HEADER_SIZE;
        new_free->magic = 0;
        new_free->next = block->next;

        block->size = needed;
        block->next = new_free;
    }
}

/*
 * kmalloc - Allocate `size` bytes from the kernel heap.
 *
 * First-fit search through the free list.
 */
void* kmalloc(size_t size) {
    if (size == 0) return NULL;

    size = align_up(size, 8); /* 8-byte alignment */

    free_block_t* prev = NULL;
    free_block_t* curr = free_list;

    while (curr) {
        if (curr->magic == 0 && curr->size >= size) {
            /* Found a suitable free block */
            split_block(curr, size);

            /* Remove from free list */
            if (prev) {
                prev->next = curr->next;
            } else {
                free_list = curr->next;
            }

            /* Mark as allocated */
            curr->magic = HEAP_MAGIC;
            heap_used_bytes += size + BLOCK_HEADER_SIZE;

            return (void*)((uint8_t*)curr + BLOCK_HEADER_SIZE);
        }
        prev = curr;
        curr = curr->next;
    }

    /* No suitable block found — extend the heap */
    size_t pages_needed = (size + BLOCK_HEADER_SIZE + PMM_FRAME_SIZE - 1)
        / PMM_FRAME_SIZE;
    if (heap_extend(pages_needed) != 0) return NULL;

    /* Add the new memory as a free block at the start of the free list */
    {
        uint64_t new_region = heap_end - pages_needed * PMM_FRAME_SIZE;
        free_block_t* new_block = (free_block_t*)new_region;
        new_block->size = pages_needed * PMM_FRAME_SIZE - BLOCK_HEADER_SIZE;
        new_block->magic = 0;
        new_block->next = free_list;
        free_list = new_block;

        /* Now try to allocate again */
        return kmalloc(size);
    }
}

/*
 * kmalloc_aligned - Allocate `size` bytes with specific alignment.
 */
void* kmalloc_aligned(size_t size, size_t align) {
    /* Allocate extra space for alignment and header */
    void* raw = kmalloc(size + align + BLOCK_HEADER_SIZE);
    if (!raw) return NULL;

    uint64_t addr = (uint64_t)raw;
    uint64_t aligned = align_up(addr, align);

    /* If already aligned, return as-is */
    if (aligned == addr) return raw;

    /* Otherwise, return the aligned address.
     * Note: this wastes the space between raw and aligned.
     * A proper implementation would track the original pointer. */
    return (void*)aligned;
}

/*
 * kfree - Free previously allocated memory.
 *
 * Adds the block back to the free list and coalesces adjacent free blocks.
 */
void kfree(void* ptr) {
    if (!ptr) return;

    free_block_t* block = (free_block_t*)((uint8_t*)ptr - BLOCK_HEADER_SIZE);

    /* Validate */
    if (block->magic != HEAP_MAGIC) return;

    /* Mark as free */
    block->magic = 0;
    heap_used_bytes -= block->size + BLOCK_HEADER_SIZE;

    /* Insert into free list (sorted by address for coalescing) */
    free_block_t* prev = NULL;
    free_block_t* curr = free_list;

    /* Find insertion point (sorted by address) */
    while (curr && (uint64_t)curr < (uint64_t)block) {
        prev = curr;
        curr = curr->next;
    }

    /* Insert block */
    block->next = curr;
    if (prev) {
        prev->next = block;
    } else {
        free_list = block;
    }

    /* Coalesce with next block if adjacent */
    if (curr && (uint8_t*)block + BLOCK_HEADER_SIZE + block->size
        == (uint8_t*)curr)
    {
        block->size += BLOCK_HEADER_SIZE + curr->size;
        block->next = curr->next;
    }

    /* Coalesce with previous block if adjacent */
    if (prev && (uint8_t*)prev + BLOCK_HEADER_SIZE + prev->size
        == (uint8_t*)block)
    {
        prev->size += BLOCK_HEADER_SIZE + block->size;
        prev->next = block->next;
    }
}

uint64_t heap_used(void) { return heap_used_bytes; }
uint64_t heap_size(void) { return heap_end - heap_start; }

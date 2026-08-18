/*
 * pmm.c - Physical Memory Manager (Frame Allocator)
 *
 * Bitmap-based allocator. One bit per 4KiB frame.
 * Bitmap is placed right after the kernel in memory.
 * Frames used by kernel, bitmap, page tables, etc. are marked as used.
 */
#include "pmm.h"
#include "paging.h"
#include "multiboot2.h"
#include "../kernel/prism.h"
#include "../drivers/serial.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* Bitmap array: 1 bit per frame */
static uint32_t* bitmap = NULL;
static uint64_t total_frames = 0;
static uint64_t used_frames = 0;

/* Maximum number of frames we can track */
#define MAX_FRAMES (1024 * 1024) /* 1024 * 1024 frames = 4 GiB */

/* External symbols from linker.ld */
extern uint64_t _kernel_end;

static inline void bitmap_set(uint64_t frame) {
    bitmap[frame / 32] |= (1U << (frame % 32));
}

static inline void bitmap_clear(uint64_t frame) {
    bitmap[frame / 32] &= ~(1U << (frame % 32));
}

static inline int bitmap_test(uint64_t frame) {
    return (bitmap[frame / 32] >> (frame % 32)) & 1;
}

/*
 * Mark a region of physical memory as used in the bitmap.
 */
static void pmm_mark_region(phys_addr_t base, uint64_t size, int used) {
    uint64_t start_frame = PMM_ADDR_TO_FRAME(base);
    uint64_t end_frame = PMM_ADDR_TO_FRAME(base + size + PMM_FRAME_SIZE - 1);

    for (uint64_t f = start_frame; f < end_frame; f++) {
        if (f >= total_frames) break;
        if (used) {
            if (!bitmap_test(f)) {
                bitmap_set(f);
                used_frames++;
            }
        } else {
            if (bitmap_test(f)) {
                bitmap_clear(f);
                used_frames--;
            }
        }
    }
}

/*
 * pmm_init - Initialize the physical memory manager.
 *
 * Parses the Multiboot2 memory map to discover usable RAM,
 * places the bitmap after the kernel, and marks all non-usable
 * regions as allocated.
 */
void pmm_init(void* mbi_ptr) {
    struct multiboot2_info* mbi = (struct multiboot2_info*)mbi_ptr;

    /* Find the memory map tag */
    struct multiboot2_tag_mmap* mmap_tag =
        (struct multiboot2_tag_mmap*)multiboot2_find_tag(mbi, 6);
    if (!mmap_tag) {
        kprint("PMM: ERROR - No memory map from GRUB!\n");
        return;
    }

    /* First pass: find highest physical address to determine bitmap size */
    uint64_t max_addr = 0;
    struct multiboot2_mmap_entry* entry =
        (struct multiboot2_mmap_entry*)(
            (uint8_t*)mmap_tag + sizeof(struct multiboot2_tag_mmap)
        );
    uint32_t mmap_size = mmap_tag->size - sizeof(struct multiboot2_tag_mmap);
    uint32_t entry_count = mmap_size / mmap_tag->entry_size;

    for (uint32_t i = 0; i < entry_count; i++) {
        uint64_t end = entry[i].base_addr + entry[i].length;
        if (end > max_addr) max_addr = end;
    }

    total_frames = max_addr / PMM_FRAME_SIZE;
    if (total_frames > MAX_FRAMES) total_frames = MAX_FRAMES;

    /* Place bitmap right after the kernel image.
     * _kernel_end has a higher-half VMA; subtract to get physical address. */
    uint64_t bitmap_bytes = (total_frames + 31) / 32 * 4;
    uint64_t kernel_phys_end = (uint64_t)&_kernel_end - HIGHER_HALF_BASE;
    bitmap = (uint32_t*)kernel_phys_end;

    /* Mark bitmap region as used (we'll clear usable frames later) */
    pmm_mark_region((phys_addr_t)bitmap, bitmap_bytes, 1);

    serial_puts("[PMM] Bitmap at ");
    serial_puts("0x");
    /* Simple hex print */
    {
        char buf[17];
        uint64_t v = (uint64_t)bitmap;
        buf[16] = 0;
        for (int i = 15; i >= 0; i--) {
            buf[i] = "0123456789ABCDEF"[v & 0xF];
            v >>= 4;
        }
        serial_puts(buf);
    }
    serial_puts("\n");

    /* Second pass: mark usable frames as free, others as used */
    for (uint32_t i = 0; i < entry_count; i++) {
        uint64_t base = entry[i].base_addr;
        uint64_t len = entry[i].length;
        uint32_t type = entry[i].type;

        if (type == MULTIBOOT2_MEMORY_AVAILABLE) {
            /* Mark all frames in this region as free */
            uint64_t start_frame = PMM_ADDR_TO_FRAME(base);
            uint64_t end_frame = PMM_ADDR_TO_FRAME(base + len);
            for (uint64_t f = start_frame; f < end_frame; f++) {
                if (f >= total_frames) break;
                if (bitmap_test(f)) {
                    bitmap_clear(f);
                    used_frames--;
                }
            }
        } else {
            /* Mark non-usable regions as used */
            pmm_mark_region(base, len, 1);
        }
    }

    /* Mark the first 1 MiB as used (BIOS, VRAM, IVT, BDA, EBDA, etc.) */
    pmm_mark_region(0, 0x100000, 1);

    /* Mark the kernel itself as used (from 1MiB to end of image + bitmap) */
    pmm_mark_region(0x100000, kernel_phys_end - 0x100000, 1);

    /* Mark bitmap memory as used */
    pmm_mark_region((phys_addr_t)bitmap, bitmap_bytes, 1);

    /* Mark VGA text buffer as used */
    pmm_mark_region(0xB8000, 0x8000, 1);
}

/*
 * pmm_alloc_frame - Allocate a single physical frame.
 */
phys_addr_t pmm_alloc_frame(void) {
    for (uint64_t i = 0; i < total_frames; i++) {
        if (!bitmap_test(i)) {
            bitmap_set(i);
            used_frames++;
            return PMM_FRAME_TO_ADDR(i);
        }
    }
    return 0; /* Out of memory */
}

/*
 * pmm_alloc_frames - Allocate `count` contiguous physical frames.
 */
phys_addr_t pmm_alloc_frames(size_t count) {
    if (count == 0) return 0;
    if (count == 1) return pmm_alloc_frame();

    uint64_t run = 0;
    uint64_t run_start = 0;

    for (uint64_t i = 0; i < total_frames; i++) {
        if (!bitmap_test(i)) {
            if (run == 0) run_start = i;
            run++;
            if (run == count) {
                for (uint64_t f = run_start; f < run_start + count; f++) {
                    bitmap_set(f);
                    used_frames++;
                }
                return PMM_FRAME_TO_ADDR(run_start);
            }
        } else {
            run = 0;
        }
    }
    return 0; /* Not enough contiguous frames */
}

/*
 * pmm_free_frame - Free a single physical frame.
 */
void pmm_free_frame(phys_addr_t addr) {
    uint64_t frame = PMM_ADDR_TO_FRAME(addr);
    if (frame < total_frames && bitmap_test(frame)) {
        bitmap_clear(frame);
        used_frames--;
    }
}

/*
 * pmm_free_frames - Free a contiguous block of `count` frames.
 */
void pmm_free_frames(phys_addr_t addr, size_t count) {
    for (size_t i = 0; i < count; i++) {
        pmm_free_frame(addr + i * PMM_FRAME_SIZE);
    }
}

uint64_t pmm_total_memory(void) {
    return total_frames * PMM_FRAME_SIZE;
}

uint64_t pmm_free_frames_count(void) {
    return total_frames - used_frames;
}

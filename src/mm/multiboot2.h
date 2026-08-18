/*
 * multiboot2.h - Multiboot2 information structure parsing
 *
 * Provides structs and functions to parse the Multiboot2 info structure
 * passed by GRUB to the kernel. Key for discovering the physical memory map.
 *
 * Reference: https://www.gnu.org/software/grub/manual/multiboot2/multiboot.html
 */
#ifndef MULTIBOOT2_H
#define MULTIBOOT2_H

#include <stdint.h>
#include <stddef.h>

/* Multiboot2 info header */
struct multiboot2_info {
    uint32_t total_size;
    uint32_t reserved;
} __attribute__((packed));

/* Generic tag header */
struct multiboot2_tag {
    uint32_t type;
    uint32_t size;
} __attribute__((packed));

/* Tag type 6: Memory map */
struct multiboot2_tag_mmap {
    uint32_t type;          /* 6 */
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
    /* Followed by multiboot2_mmap_entry entries */
} __attribute__((packed));

/* Memory map entry */
struct multiboot2_mmap_entry {
    uint64_t base_addr;
    uint64_t length;
    uint32_t type;
    uint32_t reserved;
} __attribute__((packed));

/* Memory map entry types */
#define MULTIBOOT2_MEMORY_AVAILABLE      1
#define MULTIBOOT2_MEMORY_RESERVED       2
#define MULTIBOOT2_MEMORY_ACPI_RECLAIM   3
#define MULTIBOOT2_MEMORY_ACPI_NVS       4
#define MULTIBOOT2_MEMORY_BAD            5

/* Tag type 4: Basic memory info */
struct multiboot2_tag_basic_meminfo {
    uint32_t type;          /* 4 */
    uint32_t size;
    uint32_t mem_lower;     /* in KiB, excluding first 1MiB */
    uint32_t mem_upper;     /* in KiB, above 1MiB */
} __attribute__((packed));

/*
 * Find a tag by type in the Multiboot2 info structure.
 * Returns pointer to the tag, or NULL if not found.
 */
static inline struct multiboot2_tag* multiboot2_find_tag(
    struct multiboot2_info* mbi, uint32_t type
) {
    uint8_t* base = (uint8_t*)mbi;
    uint32_t offset = 8; /* skip header (total_size + reserved) */

    while (offset < mbi->total_size) {
        struct multiboot2_tag* tag = (struct multiboot2_tag*)(base + offset);
        if (tag->type == 0) break; /* end tag */
        if (tag->type == type) return tag;
        /* Tags are 8-byte aligned */
        offset += (tag->size + 7) & ~7;
    }
    return NULL;
}

#endif /* MULTIBOOT2_H */

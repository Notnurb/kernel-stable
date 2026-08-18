/*
 * paging.h - Virtual memory / paging management
 *
 * Sets up higher-half kernel mapping at runtime:
 *   Virtual 0xFFFFFFFF80000000 → Physical 0x100000
 *
 * Uses 4-level paging (PML4 → PDPT → PD → PT → 4KiB pages).
 * The kernel identity-maps during early boot (in boot.s),
 * then this module builds proper higher-half tables and switches CR3.
 */
#ifndef PAGING_H
#define PAGING_H

#include "pmm.h"
#include <stdint.h>

/* Higher-half virtual base address for the kernel */
#define HIGHER_HALF_BASE    0xFFFFFFFF80000000ULL

/* Page table entry flags */
#define PAGE_PRESENT    (1ULL << 0)
#define PAGE_WRITABLE   (1ULL << 1)
#define PAGE_USER       (1ULL << 2)
#define PAGE_WRITE_THROUGH (1ULL << 3)
#define PAGE_CACHE_DISABLE (1ULL << 4)
#define PAGE_ACCESSED   (1ULL << 5)
#define PAGE_DIRTY      (1ULL << 6)
#define PAGE_PS         (1ULL << 7) /* Page Size (2MiB or 1GiB) */
#define PAGE_GLOBAL     (1ULL << 8)
#define PAGE_NO_EXECUTE (1ULL << 63)

/* Page table structures (each entry is uint64_t) */
#define PAGE_TABLE_ENTRIES 512

typedef uint64_t pml4_entry_t;
typedef uint64_t pdpt_entry_t;
typedef uint64_t pd_entry_t;
typedef uint64_t pt_entry_t;

/* Set up higher-half page tables and switch CR3 */
void paging_init(void* mbi);

/* Create a new user address space. Returns the PML4 physical address, or 0.
 * Copies the kernel's higher-half mappings (PML4[511]) so kernel code,
 * heap, and ISR stubs remain accessible. User mappings (PML4[0-255]) are empty. */
uint64_t paging_create_user_space(void);

/* Map a virtual address to a physical address using 4KiB pages */
int paging_map_page(uint64_t virt, uint64_t phys, uint64_t flags);

/* Unmap a virtual address */
void paging_unmap_page(uint64_t virt);

/* Translate virtual to physical address. Returns 0 if not mapped. */
phys_addr_t paging_virt_to_phys(uint64_t virt);

/* Translate using an explicit PML4 physical base (used to walk a user
 * address space that may differ from the currently-loaded CR3). */
phys_addr_t paging_virt_to_phys_cr3(uint64_t cr3_pa, uint64_t virt);

/* Validate a user-supplied buffer: every page in [addr, addr+len) must be
 * present (and writable if `writable`) AND within canonical user space
 * (< USER_ADDR_LIMIT). Returns 1 if safe to access from the kernel, 0 if not.
 * Must be called while the target task's CR3 is loaded. */
int user_mem_check(uint64_t addr, uint64_t len, int writable);

/* Top of canonical user space (exclusive). Addresses >= this are kernel. */
#define USER_ADDR_LIMIT  0x0000800000000000ULL

/* Free a user address space created by paging_create_user_space().
 * Frees the PML4 and all user-only page tables / data frames. The kernel
 * shared region (PML4[511]) is intentionally NOT freed. */
void paging_free_user_space(uint64_t cr3_pa);

/* Physical address of the kernel PML4 (used when switching to a kernel task). */
uint64_t paging_kernel_cr3(void);

/* Translate a physical address to a kernel-accessible virtual address.
 * For addresses in the first 2MiB, returns pa + HIGHER_HALF_BASE.
 * For higher addresses, uses the scratch page (one page at a time). */
void* paging_phys_to_virt(phys_addr_t pa);

/* Read CR3 (current PML4 physical address) */
static inline uint64_t paging_read_cr3(void) {
    uint64_t cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

/* Write CR3 (triggers TLB flush) */
static inline void paging_write_cr3(uint64_t cr3) {
    asm volatile("mov %0, %%cr3" :: "r"(cr3) : "memory");
}

/* Invalidate a single TLB entry */
static inline void paging_invalidate_page(uint64_t addr) {
    asm volatile("invlpg (%0)" :: "r"(addr) : "memory");
}

#endif /* PAGING_H */

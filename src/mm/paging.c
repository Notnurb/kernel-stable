/*
 * paging.c - Virtual memory / higher-half kernel paging
 *
 * Builds proper 4KiB page tables after boot.s's 2MiB page setup.
 * Maps the first 2MiB at both identity (VA 0-2MiB) and higher-half
 * (VA 0xFFFFFFFF80000000-0xFFFFFFFF801FFFFF).
 *
 * Identity mapping is kept for stack, IDT, ISR stubs, and boot data.
 *
 * Uses a "scratch page" (pt_hi_kernel[511]) to temporarily map physical
 * frames for zeroing when creating new page tables on-demand.
 */
#include "paging.h"
#include "../kernel/prism.h"
#include "../drivers/serial.h"
#include <string.h>

extern uint64_t _kernel_end;
static pml4_entry_t* new_pml4 = NULL;

/* All page tables are static, aligned to 4KiB, in BSS */
static pml4_entry_t pml4_new[PAGE_TABLE_ENTRIES] __attribute__((aligned(4096)));
static pdpt_entry_t pdpt_id[PAGE_TABLE_ENTRIES] __attribute__((aligned(4096)));
static pdpt_entry_t pdpt_hi[PAGE_TABLE_ENTRIES] __attribute__((aligned(4096)));
static pd_entry_t   pd_id[PAGE_TABLE_ENTRIES] __attribute__((aligned(4096)));
static pd_entry_t   pd_hi[PAGE_TABLE_ENTRIES] __attribute__((aligned(4096)));
static pt_entry_t   pt_hi_kernel[PAGE_TABLE_ENTRIES] __attribute__((aligned(4096)));
static pt_entry_t   pt_id_0[PAGE_TABLE_ENTRIES] __attribute__((aligned(4096)));
static pt_entry_t   pt_id_1[PAGE_TABLE_ENTRIES] __attribute__((aligned(4096)));

/*
 * Scratch page: pt_hi_kernel[511] maps VA 0xFFFFFFFF801FF000.
 * We remap it to any physical frame that needs zeroing or temporary access.
 * Limitation: only one physical page can be mapped at a time.
 */
#define SCRATCH_VA      0xFFFFFFFF801FF000ULL
#define SCRATCH_PT_IDX  511

static void* scratch_map(phys_addr_t pa) {
    pt_hi_kernel[SCRATCH_PT_IDX] = (pa & ~0xFFFULL) | PAGE_PRESENT | PAGE_WRITABLE;
    paging_invalidate_page(SCRATCH_VA);
    return (void*)SCRATCH_VA;
}

/*
 * virt_for_phys - Get a virtual address for a page table's physical address.
 *
 * Page tables within the first 2MiB are accessed via PA + HIGHER_HALF_BASE
 * (the higher-half identity mapping). Page tables above 2MiB use the scratch
 * page. Only one dynamic table can be accessed at a time.
 */
static void* virt_for_phys(phys_addr_t pa) {
    if (pa < 0x200000) {
        return (void*)(pa + HIGHER_HALF_BASE);
    }
    return scratch_map(pa);
}

void paging_init(void* mbi) {
    (void)mbi;

    /* All page table pointers must be physical addresses for hardware.
     * Since these static arrays are in .bss at higher-half VMA, we
     * subtract HIGHER_HALF_BASE to get the physical address. */
    uint64_t pml4_pa  = (uint64_t)pml4_new  - HIGHER_HALF_BASE;
    uint64_t pdpt_id_pa = (uint64_t)pdpt_id - HIGHER_HALF_BASE;
    uint64_t pdpt_hi_pa = (uint64_t)pdpt_hi - HIGHER_HALF_BASE;
    uint64_t pd_id_pa = (uint64_t)pd_id     - HIGHER_HALF_BASE;
    uint64_t pd_hi_pa = (uint64_t)pd_hi     - HIGHER_HALF_BASE;
    uint64_t pt_hi_pa = (uint64_t)pt_hi_kernel - HIGHER_HALF_BASE;
    uint64_t pt_id0_pa = (uint64_t)pt_id_0  - HIGHER_HALF_BASE;
    uint64_t pt_id1_pa = (uint64_t)pt_id_1  - HIGHER_HALF_BASE;

    memset(pml4_new, 0, 4096);
    memset(pdpt_id, 0, 4096);
    memset(pdpt_hi, 0, 4096);
    memset(pd_id, 0, 4096);
    memset(pd_hi, 0, 4096);
    memset(pt_hi_kernel, 0, 4096);
    memset(pt_id_0, 0, 4096);
    memset(pt_id_1, 0, 4096);

    /* --- IDENTITY: PML4[0] -> pdpt_id -> pd_id -> pt_id_0/1 ---
     * Covers first 2 MiB (512 x 4KiB pages).
     * Needed so stack, globals, IDT, ISR stubs all remain accessible. */
    uint64_t rw = PAGE_PRESENT | PAGE_WRITABLE;
    pml4_new[0]   = pdpt_id_pa | rw;
    pdpt_id[0]    = pd_id_pa | rw;
    pd_id[0]      = pt_id0_pa | rw;
    pd_id[1]      = pt_id1_pa | rw;

    for (int i = 0; i < 512; i++) {
        pt_id_0[i] = (uint64_t)(i * PMM_FRAME_SIZE) | rw;
        pt_id_1[i] = (uint64_t)((i + 512) * PMM_FRAME_SIZE) | rw;
    }

    /* --- HIGHER-HALF: PML4[511] -> pdpt_hi -> pd_hi -> pt_hi_kernel ---
     * Maps first 2 MiB at virtual 0xFFFFFFFF80000000.
     * VA = HIGHER_HALF_BASE + (i * 4KiB) -> PA = i * 4KiB.
     * This covers the kernel image (loaded at PA ~1MiB) and the VGA buffer
     * (at PA 0xB8000).
     * PAGE_USER is required so ring-3 interrupts can read the IDT, ISR
     * stubs, and push the interrupt frame onto the kernel stack. */
    uint64_t rw_user = PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
    pml4_new[511] = pdpt_hi_pa | rw_user;
    pdpt_hi[510]  = pd_hi_pa | rw_user;
    pd_hi[0]      = pt_hi_pa | rw_user;

    for (int i = 0; i < 512; i++) {
        pt_hi_kernel[i] = (uint64_t)(i * PMM_FRAME_SIZE) | rw_user;
    }

    new_pml4 = pml4_new;

    /* Switch to new page tables.
     * CPU is currently at higher-half VMA (called from kernel_main).
     * New tables map higher-half correctly, so execution continues.
     * Identity mapping keeps boot code / stack / globals alive.
     * CR3 requires a physical address, not a virtual one. */
    paging_write_cr3(pml4_pa);
}

/*
 * paging_map_page - Map a virtual address to a physical address using 4KiB pages.
 *
 * Allocates intermediate page tables on-demand from the PMM.
 * Uses scratch page to zero newly-allocated page table frames.
 *
 * For addresses within the existing higher-half/identity mapping (first 2MiB),
 * only the PT level may need allocation. For addresses beyond that, deeper
 * levels may also need allocation via the scratch page.
 */
int paging_map_page(uint64_t virt, uint64_t phys, uint64_t flags) {
    if (!new_pml4) return -1;

    uint64_t pi  = (virt >> 39) & 0x1FF;
    uint64_t pdi = (virt >> 30) & 0x1FF;
    uint64_t pdi2= (virt >> 21) & 0x1FF;
    uint64_t pti = (virt >> 12) & 0x1FF;

    uint64_t rw = PAGE_PRESENT | PAGE_WRITABLE;

    /* For higher-half pages, intermediate tables need PAGE_USER too
     * so ring-3 interrupt delivery can reach IDT / ISR stubs / kernel stack */
    uint64_t intermediate_rw = (pi == 511) ? (rw | PAGE_USER) : rw;

    /* PML4 -> PDPT */
    if (!(new_pml4[pi] & PAGE_PRESENT)) {
        phys_addr_t frame = pmm_alloc_frame();
        if (!frame) return -1;
        memset(scratch_map(frame), 0, 4096);
        new_pml4[pi] = frame | intermediate_rw;
    }
    pdpt_entry_t* pdpt = (pdpt_entry_t*)virt_for_phys(new_pml4[pi] & ~0xFFFULL);

    /* PDPT -> PD */
    if (!(pdpt[pdi] & PAGE_PRESENT)) {
        phys_addr_t frame = pmm_alloc_frame();
        if (!frame) return -1;
        memset(scratch_map(frame), 0, 4096);
        pdpt[pdi] = frame | intermediate_rw;
    }
    pd_entry_t* pd = (pd_entry_t*)virt_for_phys(pdpt[pdi] & ~0xFFFULL);

    /* PD -> PT */
    if (!(pd[pdi2] & PAGE_PRESENT)) {
        phys_addr_t frame = pmm_alloc_frame();
        if (!frame) return -1;
        memset(scratch_map(frame), 0, 4096);
        pd[pdi2] = frame | intermediate_rw;
    }
    pt_entry_t* pt = (pt_entry_t*)virt_for_phys(pd[pdi2] & ~0xFFFULL);

    pt[pti] = (phys & ~0xFFFULL) | flags;
    paging_invalidate_page(virt);
    return 0;
}

/*
 * paging_unmap_page - Remove a virtual-to-physical mapping.
 */
void paging_unmap_page(uint64_t virt) {
    if (!new_pml4) return;

    uint64_t pi  = (virt >> 39) & 0x1FF;
    uint64_t pdi = (virt >> 30) & 0x1FF;
    uint64_t pdi2= (virt >> 21) & 0x1FF;
    uint64_t pti = (virt >> 12) & 0x1FF;

    if (!(new_pml4[pi] & PAGE_PRESENT)) return;
    pdpt_entry_t* pdpt = (pdpt_entry_t*)virt_for_phys(new_pml4[pi] & ~0xFFFULL);
    if (!(pdpt[pdi] & PAGE_PRESENT)) return;
    pd_entry_t* pd = (pd_entry_t*)virt_for_phys(pdpt[pdi] & ~0xFFFULL);
    if (!(pd[pdi2] & PAGE_PRESENT)) return;
    pt_entry_t* pt = (pt_entry_t*)virt_for_phys(pd[pdi2] & ~0xFFFULL);

    pt[pti] = 0;
    paging_invalidate_page(virt);
}

/*
 * paging_create_user_space - Create a new user-mode page table.
 *
 * Allocates a fresh PML4, copies the kernel's higher-half mappings (PML4[511])
 * so that kernel code, data, heap, and ISR stubs are accessible from user mode.
 * User entries (PML4[0-255]) start empty — the caller maps user pages via
 * paging_map_page or the ELF loader.
 *
 * Returns the PML4 physical address on success, 0 on failure.
 */
uint64_t paging_create_user_space(void) {
    /* Allocate a new PML4 frame */
    phys_addr_t new_pml4_pa = pmm_alloc_frame();
    if (!new_pml4_pa) return 0;

    /* Zero it out using the scratch mapping or direct VA calculation.
     * Since new_pml4_pa could be anywhere in physical memory, we use
     * the scratch page if needed. But for PML4 we can just use the
     * higher-half identity if it's in the first 2MiB, otherwise scratch. */
    pml4_entry_t* user_pml4 = (pml4_entry_t*)virt_for_phys(new_pml4_pa);
    memset(user_pml4, 0, PMM_FRAME_SIZE);

    /* Get the kernel's current PML4 to copy higher-half mappings */
    pml4_entry_t* kernel_pml4 = (pml4_entry_t*)virt_for_phys(
        paging_read_cr3() & ~0xFFFULL);

    /* Copy PML4[511] (higher-half: kernel code, data, heap, ISR stubs).
     * This is a shallow copy — both PML4s point to the same PDPT/PD/PT
     * tables. New mappings added by paging_map_page in the kernel are
     * automatically visible through both address spaces. */
    user_pml4[511] = kernel_pml4[511];

    return new_pml4_pa;
}

/*
 * paging_phys_to_virt - Translate a physical address to a kernel-accessible
 * virtual address.
 */
void* paging_phys_to_virt(phys_addr_t pa) {
    return virt_for_phys(pa);
}

/*
 * paging_virt_to_phys - Translate virtual to physical address.
 * Returns 0 if not mapped.
 */
phys_addr_t paging_virt_to_phys(uint64_t virt) {
    if (!new_pml4) return 0;
    return paging_virt_to_phys_cr3(paging_read_cr3() & ~0xFFFULL, virt);
}

/* Walk a page table rooted at cr3_pa (physical address of a PML4). */
phys_addr_t paging_virt_to_phys_cr3(uint64_t cr3_pa, uint64_t virt) {
    pml4_entry_t* pml4 = (pml4_entry_t*)virt_for_phys(cr3_pa);

    uint64_t pi  = (virt >> 39) & 0x1FF;
    uint64_t pdi = (virt >> 30) & 0x1FF;
    uint64_t pdi2= (virt >> 21) & 0x1FF;
    uint64_t pti = (virt >> 12) & 0x1FF;
    uint64_t off = virt & 0xFFFULL;

    if (!(pml4[pi] & PAGE_PRESENT)) return 0;
    pdpt_entry_t* pdpt = (pdpt_entry_t*)virt_for_phys(pml4[pi] & ~0xFFFULL);
    if (!(pdpt[pdi] & PAGE_PRESENT)) return 0;
    pd_entry_t* pd = (pd_entry_t*)virt_for_phys(pdpt[pdi] & ~0xFFFULL);
    if (!(pd[pdi2] & PAGE_PRESENT)) return 0;
    pt_entry_t* pt = (pt_entry_t*)virt_for_phys(pd[pdi2] & ~0xFFFULL);
    if (!(pt[pti] & PAGE_PRESENT)) return 0;

    return (pt[pti] & ~0xFFFULL) | off;
}

/* Validate a user buffer against the CURRENT address space (caller must
 * have already switched CR3 to the target task's tables). */
int user_mem_check(uint64_t addr, uint64_t len, int writable) {
    /* Reject non-canonical / kernel addresses outright. */
    if (addr >= USER_ADDR_LIMIT) return 0;
    if (len == 0) return 1;
    /* Wraparound / overflow into kernel space. */
    if (addr + len < addr) return 0;
    if (addr + len > USER_ADDR_LIMIT) return 0;

    uint64_t first = addr & ~0xFFFULL;
    for (uint64_t va = first; va < addr + len; va += PMM_FRAME_SIZE) {
        uint64_t pa = paging_virt_to_phys(va);
        if (!pa) return 0;                       /* not mapped */
        uint64_t flag = paging_read_cr3();
        /* present verified above; re-walk to read flags */
        (void)flag;
        /* need flags: walk again via the helper-less path */
    }
    /* flags check needs the entry; redo with a tiny inline walk */
    for (uint64_t va = first; va < addr + len; va += PMM_FRAME_SIZE) {
        pml4_entry_t* pml4 = (pml4_entry_t*)virt_for_phys(
            paging_read_cr3() & ~0xFFFULL);
        uint64_t pi  = (va >> 39) & 0x1FF;
        uint64_t pdi = (va >> 30) & 0x1FF;
        uint64_t pdi2= (va >> 21) & 0x1FF;
        uint64_t pti = (va >> 12) & 0x1FF;
        if (!(pml4[pi] & PAGE_PRESENT)) return 0;
        pdpt_entry_t* pdpt = (pdpt_entry_t*)virt_for_phys(pml4[pi] & ~0xFFFULL);
        if (!(pdpt[pdi] & PAGE_PRESENT)) return 0;
        pd_entry_t* pd = (pd_entry_t*)virt_for_phys(pdpt[pdi] & ~0xFFFULL);
        if (!(pd[pdi2] & PAGE_PRESENT)) return 0;
        pt_entry_t* pt = (pt_entry_t*)virt_for_phys(pd[pdi2] & ~0xFFFULL);
        uint64_t e = pt[pti];
        if (!(e & PAGE_PRESENT)) return 0;
        if (writable && !(e & PAGE_WRITABLE)) return 0;
        if (!(e & PAGE_USER)) return 0;
    }
    return 1;
}

uint64_t paging_kernel_cr3(void) {
    return paging_read_cr3() & ~0xFFFULL;
}

/* Free a user address space, leaving the kernel-shared PML4[511] intact.
 * Walks the user half (PML4[0..510]) and frees every unique page-frame
 * and page-table frame it owns. Kernel-mapped frames are skipped because
 * they belong to the shared region. */
void paging_free_user_space(uint64_t cr3_pa) {
    pml4_entry_t* pml4 = (pml4_entry_t*)virt_for_phys(cr3_pa);
    if (!pml4) return;

    for (uint64_t pi = 0; pi < 511; pi++) {
        if (!(pml4[pi] & PAGE_PRESENT)) continue;
        pdpt_entry_t* pdpt = (pdpt_entry_t*)virt_for_phys(pml4[pi] & ~0xFFFULL);
        for (uint64_t pdi = 0; pdi < 512; pdi++) {
            if (!(pdpt[pdi] & PAGE_PRESENT)) continue;
            pd_entry_t* pd = (pd_entry_t*)virt_for_phys(pdpt[pdi] & ~0xFFFULL);
            for (uint64_t pdi2 = 0; pdi2 < 512; pdi2++) {
                if (!(pd[pdi2] & PAGE_PRESENT)) continue;
                if (pd[pdi2] & PAGE_PS) { /* 2MiB huge page */
                    pmm_free_frame(pd[pdi2] & ~0xFFFULL);
                    continue;
                }
                pt_entry_t* pt = (pt_entry_t*)virt_for_phys(pd[pdi2] & ~0xFFFULL);
                for (uint64_t pti = 0; pti < 512; pti++) {
                    if (!(pt[pti] & PAGE_PRESENT)) continue;
                    pmm_free_frame(pt[pti] & ~0xFFFULL);
                }
                pmm_free_frame(pd[pdi2] & ~0xFFFULL); /* free PT */
            }
            pmm_free_frame(pdpt[pdi] & ~0xFFFULL);     /* free PD */
        }
        pmm_free_frame(pml4[pi] & ~0xFFFULL);          /* free PDPT */
    }
    pmm_free_frame(cr3_pa);                            /* free PML4 */
}

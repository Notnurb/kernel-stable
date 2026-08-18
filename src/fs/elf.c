/*
 * elf.c - ELF64 loader
 *
 * Parses an ELF64 executable and maps its PT_LOAD segments into a
 * user process's virtual address space. Uses PAGE_USER flag for
 * proper access control.
 */
#include "elf.h"
#include "../mm/paging.h"
#include "../mm/pmm.h"
#include "../kernel/log.h"
#include "../drivers/serial.h"
#include <string.h>

int elf_load(const uint8_t* data, uint64_t size, uint64_t pml4_pa,
             uint64_t* entry_out) {
    if (!data || size < sizeof(elf64_ehdr_t) || !entry_out) {
        serial_puts("[ELF] ERR: bad args\n");
        return -1;
    }

    const elf64_ehdr_t* ehdr = (const elf64_ehdr_t*)data;

    /* Validate ELF magic */
    if (ehdr->e_ident[0] != ELFMAG0 || ehdr->e_ident[1] != ELFMAG1 ||
        ehdr->e_ident[2] != ELFMAG2 || ehdr->e_ident[3] != ELFMAG3) {
        serial_puts("[ELF] ERR: bad magic\n");
        return -1;
    }
    if (ehdr->e_ident[4] != ELFCLASS64) {
        serial_puts("[ELF] ERR: not 64-bit\n");
        return -1;
    }
    if (ehdr->e_ident[5] != ELFDATA2LSB) {
        serial_puts("[ELF] ERR: not little-endian\n");
        return -1;
    }

    /* Debug: dump first 20 bytes */
    serial_puts("[ELF] bytes: ");
    for (int i = 0; i < 20; i++) {
        char buf[4];
        const char* hex = "0123456789ABCDEF";
        buf[0] = hex[data[i] >> 4];
        buf[1] = hex[data[i] & 0xF];
        buf[2] = ' ';
        buf[3] = 0;
        serial_puts(buf);
    }
    serial_puts("\n");

    if (ehdr->e_type != ET_EXEC) {
        serial_puts("[ELF] ERR: not executable\n");
        return -1;
    }
    if (ehdr->e_machine != EM_X86_64) {
        serial_puts("[ELF] ERR: not x86_64\n");
        return -1;
    }

    serial_puts("[ELF] header OK, entry=0x");
    serial_puthex64(ehdr->e_entry);
    serial_puts(" type=0x");
    serial_puthex64(ehdr->e_type);
    serial_puts(" machine=0x");
    serial_puthex64(ehdr->e_machine);
    serial_puts(" phnum=");
    {
        char buf[5] = "0000";
        buf[0] = '0' + (ehdr->e_phnum / 100);
        buf[1] = '0' + ((ehdr->e_phnum / 10) % 10);
        buf[2] = '0' + (ehdr->e_phnum % 10);
        serial_puts(buf);
    }
    serial_puts("\n");

    *entry_out = ehdr->e_entry;

    /* Map each PT_LOAD segment */
    uint16_t ph_count = ehdr->e_phnum;
    for (uint16_t i = 0; i < ph_count; i++) {
        uint64_t ph_off = ehdr->e_phoff + (uint64_t)i * ehdr->e_phentsize;
        if (ph_off + sizeof(elf64_phdr_t) > size) {
            serial_puts("[ELF] ERR: phdr past end\n");
            return -1;
        }

        const elf64_phdr_t* phdr = (const elf64_phdr_t*)(data + ph_off);
        if (phdr->p_type != PT_LOAD) {
            serial_puts("[ELF] skipping non-LOAD segment\n");
            continue;
        }

        serial_puts("[ELF] LOAD: vaddr=0x");
        serial_puthex64(phdr->p_vaddr);
        serial_puts(" filesz=0x");
        serial_puthex64(phdr->p_filesz);
        serial_puts(" memsz=0x");
        serial_puthex64(phdr->p_memsz);
        serial_puts("\n");

        /* Determine page flags */
        uint64_t flags = PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
        if (!(phdr->p_flags & PF_W)) {
            /* For read-only segments, we still need WRITABLE during load
             * to copy data. TODO: use copy-on-write for read-only mapping. */
            flags |= PAGE_WRITABLE;
        }

        /* Map each page of the segment */
        uint64_t vaddr_start = phdr->p_vaddr & ~0xFFFULL;
        uint64_t vaddr_end = (phdr->p_vaddr + phdr->p_memsz + 0xFFF) & ~0xFFFULL;

        for (uint64_t va = vaddr_start; va < vaddr_end; va += PMM_FRAME_SIZE) {
            phys_addr_t frame = pmm_alloc_frame();
            if (!frame) {
                serial_puts("[ELF] ERR: pmm_alloc_frame failed\n");
                return -1;
            }

            memset(paging_phys_to_virt(frame), 0, PMM_FRAME_SIZE);

            /* If this page has file content, copy it */
            if (va + PMM_FRAME_SIZE > phdr->p_vaddr &&
                va < phdr->p_vaddr + phdr->p_filesz) {
                uint64_t copy_start = (va > phdr->p_vaddr) ? va : phdr->p_vaddr;
                uint64_t copy_end = (va + PMM_FRAME_SIZE < phdr->p_vaddr + phdr->p_filesz)
                                    ? va + PMM_FRAME_SIZE
                                    : phdr->p_vaddr + phdr->p_filesz;
                uint64_t offset = copy_start - phdr->p_vaddr;
                uint64_t len = copy_end - copy_start;

                /* Clamp the copy to the actual bytes present in `data`.
                 * A malformed or over-long p_filesz must never read past
                 * the end of the ELF image — doing so would leave the page
                 * zeroed and the CPU would execute zeros / walk off into an
                 * unmapped address, triple-faulting the user process. */
                uint64_t file_off = phdr->p_offset + offset;
                uint64_t avail = (file_off < size) ? (size - file_off) : 0;
                if (len > 0 && avail > 0) {
                    uint64_t cpy = (len < avail) ? len : avail;
                    memcpy(paging_phys_to_virt(frame),
                           data + file_off, cpy);
                }
            }

            /* Map in user's page table using paging_phys_to_virt for safe access */
            uint64_t pi  = (va >> 39) & 0x1FF;
            uint64_t pdi = (va >> 30) & 0x1FF;
            uint64_t pdi2= (va >> 21) & 0x1FF;
            uint64_t pti = (va >> 12) & 0x1FF;

            pml4_entry_t* user_pml4 = (pml4_entry_t*)paging_phys_to_virt(pml4_pa);
            uint64_t rw = PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;

            /* PML4 -> PDPT */
            if (!(user_pml4[pi] & PAGE_PRESENT)) {
                phys_addr_t tbl = pmm_alloc_frame();
                if (!tbl) return -1;
                memset(paging_phys_to_virt(tbl), 0, PMM_FRAME_SIZE);
                user_pml4[pi] = tbl | rw;
            }
            pdpt_entry_t* pdpt = (pdpt_entry_t*)paging_phys_to_virt(user_pml4[pi] & ~0xFFFULL);

            /* PDPT -> PD */
            if (!(pdpt[pdi] & PAGE_PRESENT)) {
                phys_addr_t tbl = pmm_alloc_frame();
                if (!tbl) return -1;
                memset(paging_phys_to_virt(tbl), 0, PMM_FRAME_SIZE);
                pdpt[pdi] = tbl | rw;
            }
            pd_entry_t* pd = (pd_entry_t*)paging_phys_to_virt(pdpt[pdi] & ~0xFFFULL);

            /* PD -> PT */
            if (!(pd[pdi2] & PAGE_PRESENT)) {
                phys_addr_t tbl = pmm_alloc_frame();
                if (!tbl) return -1;
                memset(paging_phys_to_virt(tbl), 0, PMM_FRAME_SIZE);
                pd[pdi2] = tbl | rw;
            }
            pt_entry_t* pt = (pt_entry_t*)paging_phys_to_virt(pd[pdi2] & ~0xFFFULL);

            pt[pti] = (frame & ~0xFFFULL) | flags;
        }
    }

    serial_puts("[ELF] load OK\n");
    return 0;
}

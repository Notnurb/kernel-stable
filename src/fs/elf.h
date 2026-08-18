/*
 * elf.h - ELF64 binary format definitions
 *
 * Minimal definitions for loading ELF64 executables into user processes.
 */
#ifndef ELF_H
#define ELF_H

#include <stdint.h>

#define EI_NIDENT 16

/* ELF magic */
#define ELFMAG0  0x7F
#define ELFMAG1  'E'
#define ELFMAG2  'L'
#define ELFMAG3  'F'

/* ELF class */
#define ELFCLASS64  2

/* ELF data encoding */
#define ELFDATA2LSB 1  /* Little-endian */

/* ELF type */
#define ET_EXEC  2  /* Executable */

/* ELF machine */
#define EM_X86_64 62

/* Program header types */
#define PT_NULL    0
#define PT_LOAD    1
#define PT_INTERP  3
#define PT_NOTE    4
#define PT_PHDR    6

/* Program header flags */
#define PF_X  0x1  /* Execute */
#define PF_W  0x2  /* Write */
#define PF_R  0x4  /* Read */

/* ELF64 header */
typedef struct {
    unsigned char e_ident[EI_NIDENT];
    uint16_t      e_type;
    uint16_t      e_machine;
    uint32_t      e_version;
    uint64_t      e_entry;
    uint64_t      e_phoff;
    uint64_t      e_shoff;
    uint32_t      e_flags;
    uint16_t      e_ehsize;
    uint16_t      e_phentsize;
    uint16_t      e_phnum;
    uint16_t      e_shentsize;
    uint16_t      e_shnum;
    uint16_t      e_shstrndx;
} __attribute__((packed)) elf64_ehdr_t;

/* ELF64 program header */
typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} __attribute__((packed)) elf64_phdr_t;

/*
 * elf_load - Load an ELF64 executable into a user address space.
 *
 * Validates the ELF header, maps PT_LOAD segments with PAGE_USER,
 * and returns the entry point address.
 *
 * Parameters:
 *   data     - Pointer to the raw ELF data (must be in kernel-mapped memory)
 *   size     - Size of the ELF data in bytes
 *   pml4_pa  - Physical address of the target PML4
 *   entry_out - Receives the entry point virtual address
 *
 * Returns 0 on success, -1 on error.
 */
int elf_load(const uint8_t* data, uint64_t size, uint64_t pml4_pa,
             uint64_t* entry_out);

#endif /* ELF_H */

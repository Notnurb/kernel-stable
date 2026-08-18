/*
 * kernel.c - Prism Kernel main entry point
 */
#include "prism.h"
#include "log.h"
#include "../drivers/serial.h"
#include "../drivers/vga.h"
#include "../drivers/keyboard.h"
#include "../task/task.h"
#include "../mm/pmm.h"
#include "../mm/paging.h"
#include "../mm/heap.h"
#include "../syscall/syscall.h"
#include "../fs/elf.h"
#include "../cpu/gdt.h"
#include <string.h>

/* ---- Tiny ELF64 test binary: "Hello from ring 3!" with syscall ----
 *
 * Compiled hand-crafted code:
 *   mov $1, %eax       ; SYSCALL_WRITE
 *   mov $1, %edi       ; fd = 1 (stdout)
 *   lea msg(%rip), %rsi
 *   mov $21, %edx      ; len
 *   syscall
 *   mov $2, %eax       ; SYSCALL_GETPID
 *   syscall
 * loop:
 *   nop
 *   jmp loop
 * msg: .asciz "Hello from ring 3!\n"
 *
 * We embed it as raw bytes in a minimal ELF64 header.
 * Load address: 0x400000, entry: 0x400000.
 */
static const uint8_t test_elf[] = {
    /* ELF header (64 bytes) */
    0x7f, 'E','L','F',        /* e_ident[0..3]: magic */
    2,                         /* e_ident[4]: ELFCLASS64 */
    1,                         /* e_ident[5]: ELFDATA2LSB */
    1,                         /* e_ident[6]: EV_CURRENT */
    0, 0,0,0,0,0,0,0,0,       /* e_ident[7..15]: padding (9 bytes) */
    0x02, 0x00,                /* e_type: ET_EXEC */
    0x3E, 0x00,                /* e_machine: EM_X86_64 */
    0x01, 0x00, 0x00, 0x00,   /* e_version */
    0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00,  /* e_entry: 0x400000 */
    0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  /* e_phoff: 64 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  /* e_shoff: 0 */
    0x00, 0x00, 0x00, 0x00,   /* e_flags */
    0x40, 0x00,                /* e_ehsize: 64 */
    0x38, 0x00,                /* e_phentsize: 56 */
    0x01, 0x00,                /* e_phnum: 1 */
    0x00, 0x00,                /* e_shentsize: 0 */
    0x00, 0x00,                /* e_shnum: 0 */
    0x00, 0x00,                /* e_shstrndx: 0 */

    /* Program header: PT_LOAD (56 bytes) */
    0x01, 0x00, 0x00, 0x00,   /* p_type: PT_LOAD */
    0x07, 0x00, 0x00, 0x00,   /* p_flags: PF_R | PF_W | PF_X */
    0x78, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  /* p_offset: 120 (0x78) */
    0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00,  /* p_vaddr: 0x400000 */
    0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00,  /* p_paddr: 0x400000 */
    0x37, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  /* p_filesz: 55 (0x37) */
    0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  /* p_memsz: 4096 */
    0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  /* p_align: 4096 */

    /* Code (at file offset 120 = 0x78) */
    /* Simple loop: nop + jmp back. No syscalls yet. */
    0x90,                               /* nop */
    0xEB, 0xFD,                         /* jmp -3 (back to nop) */
    /* msg (dead code, just padding): */
    'H','e','l','l','o',' ','f','r','o','m',' ',
    'r','i','n','g',' ','3','!','\n','\0',
};

/* Demo kernel threads to prove scheduling still works */
static void thread_a(void) {
    for (;;) {
        klog(LOG_INFO, "Thread A: tick\n");
        for (volatile int i = 0; i < 5000000; i++);
    }
}

static void thread_b(void) {
    for (;;) {
        klog(LOG_INFO, "Thread B: tick\n");
        for (volatile int i = 0; i < 5000000; i++);
    }
}

void kernel_main(uint64_t mbi_addr) {
    /* Initialize serial for debug output (works before any paging) */
    serial_init();
    serial_puts("[PRISM] serial OK\n");

    /* Core CPU structures */
    kinit_gdt();
    serial_puts("[PRISM] GDT ok\n");
    kinit_idt();
    serial_puts("[PRISM] IDT ok\n");
    kinit_pic();
    serial_puts("[PRISM] PIC ok\n");
    kinit_timer();
    serial_puts("[PRISM] Timer ok\n");

    /* Memory management */
    pmm_init((void*)mbi_addr);
    serial_puts("[PRISM] PMM ok\n");
    paging_init((void*)mbi_addr);
    serial_puts("[PRISM] Paging ok\n");

    /* VGA — switch buffer to higher-half after paging is active */
    vga_set_higher_half();
    khello_init();
    serial_puts("[PRISM] VGA ok\n");

    int heap_ok = (heap_init() == 0);
    serial_puts(heap_ok ? "[PRISM] Heap ok\n" : "[PRISM] Heap FAILED\n");

    /* Kernel services */
    kinit_log();
    kinit_keyboard();

    /* Multitasking — must come after heap (needs kmalloc for stacks) */
    kinit_task();
    task_create(thread_a);
    task_create(thread_b);

    /* Syscalls — must come after GDT (needs user segments) */
    syscall_init();

    /* ---- Create a user-mode process ---- */
    /* 1. Create a user address space (new PML4 with kernel mappings) */
    uint64_t user_cr3 = paging_create_user_space();
    if (user_cr3) {
        serial_puts("[PRISM] User PML4 created at 0x");
        serial_puthex64(user_cr3);
        serial_puts("\n");

        /* 2. Load the ELF binary into the user address space.
         * The ELF is embedded in kernel memory (test_elf[]).
         * elf_load maps PT_LOAD segments with PAGE_USER. */
        uint64_t entry = 0;
        int rc = elf_load(test_elf, sizeof(test_elf), user_cr3, &entry);
        if (rc == 0) {
            serial_puts("[PRISM] ELF loaded, entry=0x");
            serial_puthex64(entry);
            serial_puts("\n");

            /* 3. Allocate a user stack (4 KiB, mapped at VA 0x80000000) */
            phys_addr_t user_stack_pa = pmm_alloc_frame();
            if (user_stack_pa) {
                /* Zero the user stack page */
                memset(paging_phys_to_virt(user_stack_pa), 0, PMM_FRAME_SIZE);

                /* Map it in the user's page table at 0x80000000 with USER|RW */
                uint64_t stack_va = 0x80000000ULL;
                pml4_entry_t* user_pml4 = (pml4_entry_t*)
                    paging_phys_to_virt(user_cr3 & ~0xFFFULL);

                uint64_t pi  = (stack_va >> 39) & 0x1FF;
                uint64_t pdi = (stack_va >> 30) & 0x1FF;
                uint64_t pdi2= (stack_va >> 21) & 0x1FF;
                uint64_t pti = (stack_va >> 12) & 0x1FF;
                uint64_t rw = PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;

                /* PML4 -> PDPT */
                if (!(user_pml4[pi] & PAGE_PRESENT)) {
                    phys_addr_t tbl = pmm_alloc_frame();
                    memset(paging_phys_to_virt(tbl), 0, PMM_FRAME_SIZE);
                    user_pml4[pi] = tbl | rw;
                }
                pdpt_entry_t* pdpt = (pdpt_entry_t*)
                    paging_phys_to_virt(user_pml4[pi] & ~0xFFFULL);

                /* PDPT -> PD */
                if (!(pdpt[pdi] & PAGE_PRESENT)) {
                    phys_addr_t tbl = pmm_alloc_frame();
                    memset(paging_phys_to_virt(tbl), 0, PMM_FRAME_SIZE);
                    pdpt[pdi] = tbl | rw;
                }
                pd_entry_t* pd = (pd_entry_t*)
                    paging_phys_to_virt(pdpt[pdi] & ~0xFFFULL);

                /* PD -> PT */
                if (!(pd[pdi2] & PAGE_PRESENT)) {
                    phys_addr_t tbl = pmm_alloc_frame();
                    memset(paging_phys_to_virt(tbl), 0, PMM_FRAME_SIZE);
                    pd[pdi2] = tbl | rw;
                }
                pt_entry_t* pt = (pt_entry_t*)
                    paging_phys_to_virt(pd[pdi2] & ~0xFFFULL);

                pt[pti] = (user_stack_pa & ~0xFFFULL) |
                          PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;

                serial_puts("[PRISM] User stack mapped at 0x80000000\n");

                /* 4. Create the user task */
                uint64_t user_rsp = stack_va + PMM_FRAME_SIZE; /* top of stack, grows down */
                task_t* user_task = task_create_user(
                    entry,              /* user RIP */
                    user_rsp,           /* user RSP */
                    0x200,              /* RFLAGS: IF=1 */
                    user_cr3            /* page table */
                );
                if (user_task) {
                    serial_puts("[PRISM] User task created (PID ");
                    serial_puthex64(user_task->pid);
                    serial_puts(")\n");
                } else {
                    serial_puts("[PRISM] FAILED to create user task\n");
                }
            }
        } else {
            serial_puts("[PRISM] ELF load FAILED\n");
        }
    } else {
        serial_puts("[PRISM] FAILED to create user PML4\n");
    }

    /* Banner */
    kprint("Prism Kernel v");
    kprint(PRISM_VERSION);
    kprint("\n==========================\n");
    kprint("Long mode: ACTIVE\n");
    kprint("GDT:       Initialized (user segments ready)\n");
    kprint("IDT:       Initialized\n");
    kprint("PIC:       Remapped\n");
    kprint("Timer:     PIT @ 100Hz\n");
    kprint("PMM:       Initialized\n");
    kprint("Paging:    Higher-half active\n");
    kprint(heap_ok ? "Heap:      Ready\n" : "Heap:      FAILED\n");
    kprint("Keyboard:  PS/2 installed\n");
    kprint("Syscall:   MSRs configured\n");
    kprint("Tasks:     2 threads + 1 user process\n");
    kprint("\nScheduler running.\n");
    serial_puts("[PRISM] Done.\n");

    /* Enable interrupts — PIT and keyboard IRQs are now live.
     * Timer will fire at 100Hz and trigger schedule() for preemption. */
    asm volatile("sti" ::: "memory");

    /* Keyboard echo loop: any key pressed is echoed to VGA + serial so the
     * OS is actually interactive. Backspace erases, Enter starts a new line. */
    kprint("\nPrism> ");
    for (;;) {
        if (keyboard_has_key()) {
            char c = keyboard_getchar();
            if (c == '\b') {
                vga_backspace();
            } else if (c == '\n') {
                kprint("\nPrism> ");
            } else if (c >= ' ') {
                kprintc(c);
            }
            serial_putc(c);
        }
        asm volatile("hlt" ::: "memory");
    }
}

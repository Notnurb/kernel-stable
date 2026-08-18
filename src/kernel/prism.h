/*
 * prism.h - Main kernel header with shared definitions
 */
#ifndef PRISM_H
#define PRISM_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Kernel version */
#define PRISM_VERSION "0.0.1.e1"

/* Forward declarations - each subsystem initializes itself */
void kinit_gdt(void);
void kinit_idt(void);
void kinit_pic(void);
void kinit_timer(void);
void kinit_keyboard(void);
void kinit_task(void);
void kinit_log(void);
void khello_init(void);
void vga_set_higher_half(void);
void pmm_init(void* mbi);
void paging_init(void* mbi);
int heap_init(void);
void syscall_init(void);

/* VGA text output functions */
void kprint(const char* str);
void kprintc(char c);
void kprint_hex(uint64_t val);

#endif /* PRISM_H */

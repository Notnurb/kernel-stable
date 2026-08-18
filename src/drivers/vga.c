/*
 * vga.c - VGA text mode driver for Prism kernel
 *
 * Provides simple console output via the VGA text buffer at 0xB8000.
 * 80 columns x 25 rows, 16-color text mode.
 *
 * The buffer pointer is initially identity-mapped (0xB8000) during early boot,
 * then switched to higher-half (0xFFFFFFFF800B8000) after paging_init.
 */
#include "vga.h"
#include "../kernel/prism.h"

#define VGA_WIDTH  80
#define VGA_HEIGHT 25

/* Color attribute byte: foreground + background */
/* Format: high nibble = background, low nibble = foreground */
#define VGA_COLOR(fg, bg) (((bg & 0xF) << 4) | (fg & 0xF))

/* Dynamic VGA buffer pointer — identity-mapped first, then higher-half */
static volatile uint16_t* vga_buffer = (volatile uint16_t*)0xB8000;

/* Current cursor position */
static uint8_t vga_cursor_x = 0;
static uint8_t vga_cursor_y = 0;

void vga_set_higher_half(void) {
    vga_buffer = (volatile uint16_t*)(0xFFFFFFFF80000000ULL + 0xB8000);
}

/* Encode a character + attribute into a VGA text cell (uint16_t) */
static uint16_t vga_entry(char c, uint8_t color) {
    return ((uint16_t)color << 8) | (uint16_t)c;
}

/*
 * khello_init - Initialize the VGA text buffer.
 * Clears the screen and resets the cursor.
 */
void khello_init(void) {
    uint8_t color = VGA_COLOR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga_buffer[i] = vga_entry(' ', color);
    }
    vga_cursor_x = 0;
    vga_cursor_y = 0;
}

/*
 * kprintc - Put a single character on screen.
 * Handles newlines, tabs, and scrolling.
 */
void kprintc(char c) {
    uint8_t color = VGA_COLOR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

    if (c == '\n') {
        vga_cursor_y++;
        vga_cursor_x = 0;
    } else if (c == '\r') {
        vga_cursor_x = 0;
    } else if (c == '\t') {
        vga_cursor_x = (vga_cursor_x + 8) & ~0x07;
    } else {
        int idx = vga_cursor_y * VGA_WIDTH + vga_cursor_x;
        if (idx < VGA_WIDTH * VGA_HEIGHT) {
            vga_buffer[idx] = vga_entry(c, color);
        }
        vga_cursor_x++;
        if (vga_cursor_x >= VGA_WIDTH) {
            vga_cursor_x = 0;
            vga_cursor_y++;
        }
    }

    if (vga_cursor_y >= VGA_HEIGHT) {
        for (int y = 1; y < VGA_HEIGHT; y++) {
            for (int x = 0; x < VGA_WIDTH; x++) {
                vga_buffer[(y-1) * VGA_WIDTH + x] = vga_buffer[y * VGA_WIDTH + x];
            }
        }
        for (int x = 0; x < VGA_WIDTH; x++) {
            vga_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = vga_entry(' ', color);
        }
        vga_cursor_y = VGA_HEIGHT - 1;
    }
}

void kprint(const char* str) {
    while (*str) {
        kprintc(*str);
        str++;
    }
}

void kprint_hex(uint64_t val) {
    const char hex_chars[] = "0123456789ABCDEF";
    kprint("0x");
    for (int i = 15; i >= 0; i--) {
        kprintc(hex_chars[(val >> (i * 4)) & 0xF]);
    }
}

/* Move the cursor back one cell (wrapping to the previous line if needed)
 * and blank it, emulating a terminal backspace. */
void vga_backspace(void) {
    if (vga_cursor_x == 0) {
        if (vga_cursor_y == 0) return;
        vga_cursor_y--;
        vga_cursor_x = VGA_WIDTH - 1;
    } else {
        vga_cursor_x--;
    }
    kprintc(' ');
    /* kprintc advanced the cursor; step it back again to the blanked cell */
    if (vga_cursor_x == 0) {
        vga_cursor_y--;
        vga_cursor_x = VGA_WIDTH - 1;
    } else {
        vga_cursor_x--;
    }
}

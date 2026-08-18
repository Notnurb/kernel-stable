# Makefile for Prism Kernel v0.0.1.e1
#
# Build system for the x86_64 kernel.
# Uses x86_64-elf-gcc cross-compiler (required — never use host GCC).
#
# Targets:
#   make          - Build the kernel ISO
#   make iso      - Build bootable ISO with GRUB
#   make qemu     - Build ISO and run in QEMU
#   make clean    - Remove build artifacts
#   make rebuild  - Clean and rebuild

# --- Cross-compiler ---
CC      = x86_64-elf-gcc
AS      = x86_64-elf-gcc
NASM    = nasm
OBJCOPY = x86_64-elf-objcopy
LD      = x86_64-elf-ld

# --- Compiler flags ---
# -ffreestanding: no standard library available in kernel
# -fno-stack-protector: no stack canaries (we're the OS)
# -nostdlib: don't link standard library
# -mcmodel=kernel: kernel code model (64-bit)
# -mno-red-zone: don't use red zone (could be clobbered by NMI)
# -mno-mmx/-mno-sse: don't use SIMD in kernel (no SSE state management yet)
# -fno-pic/-fno-PIE: kernel code must be non-position-independent
#                     (gcc >= 14 defaults to PIC, which mcmodel=kernel rejects)
CFLAGS  = -std=gnu11 -ffreestanding -fno-stack-protector -nostdlib \
          -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
          -fno-pic -fno-PIE \
          -Wall -Wextra -Wno-unused-parameter -Wno-unused-but-set-variable \
          -O2 -g -mno-fp-ret-in-387

# --- Assembly flags (NASM) ---
ASFLAGS = -f elf64

# --- Linker flags ---
LDFLAGS = -T linker.ld -nostdlib -nodefaultlibs -no-pie

# --- Directories ---
SRCDIR     = src
INCDIR     = $(SRCDIR)
BUILDDIR   = build
ISODIR     = iso
ISO_DIR    = $(ISODIR)/boot

# --- Source files ---
C_SOURCES = $(SRCDIR)/kernel/kernel.c \
            $(SRCDIR)/kernel/log.c \
            $(SRCDIR)/cpu/gdt.c \
            $(SRCDIR)/cpu/idt.c \
            $(SRCDIR)/cpu/pic.c \
            $(SRCDIR)/drivers/vga.c \
            $(SRCDIR)/drivers/timer.c \
            $(SRCDIR)/drivers/keyboard.c \
            $(SRCDIR)/isr/isr.c \
            $(SRCDIR)/task/task.c \
            $(SRCDIR)/mm/pmm.c \
            $(SRCDIR)/mm/paging.c \
            $(SRCDIR)/mm/heap.c \
            $(SRCDIR)/syscall/syscall.c \
            $(SRCDIR)/fs/elf.c

ASM_SOURCES = $(SRCDIR)/kernel/boot.s \
              $(SRCDIR)/isr/isr_asm.s \
              $(SRCDIR)/task/switch.s \
              $(SRCDIR)/user/usermode.s

# --- Object files ---
C_OBJECTS = $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(C_SOURCES))
ASM_OBJECTS = $(patsubst $(SRCDIR)/%.s,$(BUILDDIR)/%.o,$(ASM_SOURCES))
ALL_OBJECTS = $(C_OBJECTS) $(ASM_OBJECTS)

# --- Output ---
KERNEL_ELF = $(BUILDDIR)/prism_kernel
KERNEL_BIN = $(BUILDDIR)/prism_kernel.bin
ISO_FILE   = $(BUILDDIR)/prism.iso

# --- Include paths ---
# Add every src subdirectory so both "mm/pmm.h" and bare "pmm.h" styles resolve.
INCLUDES = -I$(INCDIR) \
           -I$(INCDIR)/cpu -I$(INCDIR)/drivers -I$(INCDIR)/fs \
           -I$(INCDIR)/isr -I$(INCDIR)/kernel -I$(INCDIR)/mm \
           -I$(INCDIR)/syscall -I$(INCDIR)/task -I$(INCDIR)/user

.PHONY: all iso qemu clean rebuild

all: iso

# --- Build directories ---
$(BUILDDIR):
	mkdir -p $(BUILDDIR)/kernel $(BUILDDIR)/cpu $(BUILDDIR)/drivers $(BUILDDIR)/isr $(BUILDDIR)/mm $(BUILDDIR)/syscall $(BUILDDIR)/fs $(BUILDDIR)/user

# --- Compile C files ---
$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# --- Assemble GAS files ---
$(BUILDDIR)/%.o: $(SRCDIR)/%.s | $(BUILDDIR)
	@mkdir -p $(dir $@)
	$(AS) -c -x assembler $< -o $@

# --- Link kernel ---
$(KERNEL_ELF): $(ALL_OBJECTS)
	@mkdir -p $(BUILDDIR)
	$(CC) $(LDFLAGS) $(ALL_OBJECTS) -o $@
	# Also produce a flat binary for reference (may warn for higher-half layout)
	$(OBJCOPY) -O binary $@ $(KERNEL_BIN) || true

# --- Build ISO ---
iso: $(KERNEL_ELF)
	@mkdir -p $(ISO_DIR)/grub
	cp $(KERNEL_ELF) $(ISO_DIR)/kernel.bin
	cp grub.cfg $(ISO_DIR)/grub/grub.cfg
	grub-mkrescue -o $(ISO_FILE) $(ISODIR) 2>/dev/null

# --- Run in QEMU ---
qemu: iso
	qemu-system-x86_64 -cdrom $(ISO_FILE) -serial stdio

# --- Clean ---
clean:
	rm -rf $(BUILDDIR)

# --- Rebuild ---
rebuild: clean all

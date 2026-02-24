# Makefile — AnubhavOS build system
#
# Cross-compiler (i686-elf-gcc) must be on PATH.
# Add to ~/.zshrc: export PATH="$HOME/opt/cross/bin:$PATH"
#
# Targets:
#   make        — build ISO (default)
#   make run    — build + launch in QEMU
#   make debug  — build + launch QEMU frozen, then attach GDB
#   make clean  — remove all build artifacts

# ---------------------------------------------------------------------------
# Toolchain
# ---------------------------------------------------------------------------
CC      := i686-elf-gcc
AS      := nasm
LD      := i686-elf-ld
OBJCOPY := i686-elf-objcopy

# Host compiler for tools (not cross-compiled)
HOSTCC  := cc

# ---------------------------------------------------------------------------
# Flags
# ---------------------------------------------------------------------------
CFLAGS  := -std=c99 \
            -ffreestanding \
            -fno-builtin \
            -fno-stack-protector \
            -O2 \
            -Wall \
            -Wextra \
            -Iinclude

ASFLAGS := -f elf32

LDFLAGS := -T linker.ld \
            -nostdlib \
            -z noexecstack

# Userspace flags (same cross-compiler, different linker script)
USER_CFLAGS  := -std=c99 -ffreestanding -fno-builtin -fno-stack-protector -O2 -Wall -Wextra
USER_LDFLAGS := -T linker_user.ld -nostdlib -z noexecstack

# ---------------------------------------------------------------------------
# Sources — kernel
# ---------------------------------------------------------------------------
KERNEL_C_SRC   := $(shell find kernel -name '*.c')
KERNEL_ASM_SRC := boot/boot.asm $(shell find kernel -name '*.asm')

# ASM objects get .asm.o suffix to avoid collision with same-named .c files
KERNEL_C_OBJS   := $(KERNEL_C_SRC:.c=.o)
KERNEL_ASM_OBJS := $(patsubst %.asm,%.asm.o,$(KERNEL_ASM_SRC))
KERNEL_OBJS     := $(KERNEL_C_OBJS) $(KERNEL_ASM_OBJS)

# ---------------------------------------------------------------------------
# Sources — userspace (shell + libraries)
# ---------------------------------------------------------------------------
USER_CRT0      := userspace/lib/crt0.asm.o
USER_SHELL_SRC := userspace/shell/shell.c
USER_LIB_SRC   := userspace/lib/string.c
USER_C_OBJS    := userspace/shell/shell.o userspace/lib/string.o
USER_OBJS      := $(USER_CRT0) $(USER_C_OBJS)

# ---------------------------------------------------------------------------
# Output paths
# ---------------------------------------------------------------------------
BUILD_DIR  := build
KERNEL     := $(BUILD_DIR)/kernel.elf
ISO        := $(BUILD_DIR)/anubhav-os.iso
ISO_DIR    := $(BUILD_DIR)/iso
SHELL_ELF  := $(BUILD_DIR)/shell.elf
SHELL_BIN  := $(BUILD_DIR)/shell.bin
INITRAMFS  := $(BUILD_DIR)/initramfs.img
MKRAMFS    := $(BUILD_DIR)/mkramfs
RAMFS_ROOT := $(BUILD_DIR)/initramfs_root

# ---------------------------------------------------------------------------
# Default target
# ---------------------------------------------------------------------------
.PHONY: all run debug clean iso userspace tools

all: $(ISO)

# ---------------------------------------------------------------------------
# Compile kernel C sources
# ---------------------------------------------------------------------------
kernel/%.o: kernel/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# ---------------------------------------------------------------------------
# Assemble NASM sources (output to .asm.o to avoid collision with .c objects)
# ---------------------------------------------------------------------------
%.asm.o: %.asm
	$(AS) $(ASFLAGS) $< -o $@

# ---------------------------------------------------------------------------
# Compile userspace C sources
# ---------------------------------------------------------------------------
userspace/%.o: userspace/%.c
	$(CC) $(USER_CFLAGS) -c $< -o $@

# ---------------------------------------------------------------------------
# Assemble userspace ASM sources (crt0)
# ---------------------------------------------------------------------------
userspace/%.asm.o: userspace/%.asm
	$(AS) $(ASFLAGS) $< -o $@

# ---------------------------------------------------------------------------
# Link kernel ELF
# ---------------------------------------------------------------------------
$(KERNEL): $(KERNEL_OBJS)
	@mkdir -p $(BUILD_DIR)
	$(LD) $(LDFLAGS) -o $@ $^
	@echo "[LD] Linked $@"

# ---------------------------------------------------------------------------
# Build host tool: mkramfs
# ---------------------------------------------------------------------------
$(MKRAMFS): tools/mkramfs.c
	@mkdir -p $(BUILD_DIR)
	$(HOSTCC) -o $@ $<
	@echo "[HOSTCC] Built $@"

# ---------------------------------------------------------------------------
# Build shell ELF → flat binary
# ---------------------------------------------------------------------------
$(SHELL_ELF): $(USER_OBJS)
	@mkdir -p $(BUILD_DIR)
	$(LD) $(USER_LDFLAGS) -o $@ $^
	@echo "[LD] Linked $@"

$(SHELL_BIN): $(SHELL_ELF)
	$(OBJCOPY) -O binary $< $@
	@echo "[OBJCOPY] Created $@"

# ---------------------------------------------------------------------------
# Build initramfs image
# ---------------------------------------------------------------------------
$(INITRAMFS): $(MKRAMFS) $(SHELL_BIN)
	@mkdir -p $(RAMFS_ROOT)
	cp $(SHELL_BIN) $(RAMFS_ROOT)/shell.bin
	@echo "Welcome to AnubhavOS!" > $(RAMFS_ROOT)/hello.txt
	@echo "This is a hobby operating system built from scratch." >> $(RAMFS_ROOT)/hello.txt
	@echo "Type 'help' for a list of commands." >> $(RAMFS_ROOT)/hello.txt
	$(MKRAMFS) $(RAMFS_ROOT) $@
	@echo "[INITRAMFS] Created $@"

# ---------------------------------------------------------------------------
# Build bootable ISO
# ---------------------------------------------------------------------------
GRUB_MKRESCUE := i686-elf-grub-mkrescue

$(ISO): $(KERNEL) $(INITRAMFS)
	@mkdir -p $(ISO_DIR)/boot/grub
	cp $(KERNEL)     $(ISO_DIR)/boot/kernel.elf
	cp $(INITRAMFS)  $(ISO_DIR)/boot/initramfs.img
	cp boot/grub.cfg $(ISO_DIR)/boot/grub/grub.cfg
	$(GRUB_MKRESCUE) -o $@ $(ISO_DIR) 2>/dev/null
	@echo "[ISO] Created $@"

iso: $(ISO)

# ---------------------------------------------------------------------------
# Run in QEMU
# ---------------------------------------------------------------------------
run: $(ISO)
	qemu-system-i386 \
	    -cdrom $(ISO) \
	    -m 32M \
	    -debugcon stdio \
	    -no-reboot \
	    -no-shutdown

# ---------------------------------------------------------------------------
# Debug: start QEMU frozen, attach GDB
# ---------------------------------------------------------------------------
debug: $(ISO)
	@echo "Starting QEMU in debug mode (frozen). Connect GDB with:"
	@echo "  gdb $(KERNEL) -ex 'target remote localhost:1234'"
	qemu-system-i386 \
	    -cdrom $(ISO) \
	    -m 32M \
	    -s -S \
	    -debugcon stdio \
	    -no-reboot \
	    -no-shutdown

# ---------------------------------------------------------------------------
# Clean
# ---------------------------------------------------------------------------
clean:
	find . -name '*.o' -delete
	find . -name '*.asm.o' -delete
	rm -rf $(BUILD_DIR)
	@echo "[CLEAN] Done"

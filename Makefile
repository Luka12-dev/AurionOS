# AurionOS Makefile
# Author: AurionOS Development Team
# Purpose: Build AurionOS from assembly, C, and Rust sources into bootable floppy image
# Target: Bootable 1.44MB floppy disk image

# Tool Configuration
NASM        := nasm
GCC         := gcc
LD          := ld
DD          := dd
OBJCOPY     := objcopy
HEXDUMP     := hexdump
GENISOIMAGE := genisoimage
MKISOFS     := mkisofs
CARGO       := cargo

# OS Detection
# WSL sets OS to nothing but has /proc/version containing "microsoft".
# We detect WSL explicitly so we can use Windows-native QEMU for CD-ROM,
# which avoids a hard crash bug in QEMU 8.2.2 on WSL2 with ATAPI devices.
IS_WSL := $(shell grep -qi microsoft /proc/version 2>/dev/null && echo yes)

ifeq ($(OS),Windows_NT)
    NASMFLAGS_ELF := -f win32 -g
    LDFLAGS := -m i386pe -nostdlib -T link.ld --oformat binary
    PYTHON := python
    MKDIR := mkdir
    RM := del /Q
    QEMU := qemu-system-i386
    QEMU_ISO := qemu-system-i386
else ifeq ($(IS_WSL),yes)
    # WSL2 - build tools are Linux, but use Windows QEMU for CD-ROM to avoid
    # the QEMU 8.2.2 iothread assertion crash on ATAPI/CD-ROM emulation.
    NASMFLAGS_ELF := -f elf32 -g -F dwarf
    LDFLAGS := -m elf_i386 -nostdlib -T link.ld --oformat binary
    PYTHON := python3
    MKDIR := mkdir -p
    RM := rm -f
    QEMU := qemu-system-i386
    QEMU_ISO := /mnt/c/Program Files/qemu/qemu-system-i386.exe
else
    # Native Linux
    NASMFLAGS_ELF := -f elf32 -g -F dwarf
    LDFLAGS := -m elf_i386 -nostdlib -T link.ld --oformat binary
    PYTHON := python3
    MKDIR := mkdir -p
    RM := rm -f
    QEMU := qemu-system-i386
    QEMU_ISO := qemu-system-i386
endif

NASMFLAGS_BIN := -f bin

# GCC flags for 32-bit freestanding
CFLAGS := -m32 -ffreestanding -nostdlib -Iinclude -fno-builtin -fno-stack-protector
CFLAGS += -O0 -g -Wall -Wextra -std=c11
CFLAGS += -fno-pie -fno-pic
CFLAGS += -mpreferred-stack-boundary=2 -mno-mmx -mno-sse -mno-sse2
CFLAGS += -c

# Linker flags
# LDFLAGS set above based on OS

# Directories and Files
SRC_DIR     := src
DRIVER_DIR  := Drivers
BUILD_DIR   := build
OBJ_DIR     := $(BUILD_DIR)/obj

# Output files
BOOT_BIN    := $(BUILD_DIR)/bootload.bin
KERNEL_BIN  := $(BUILD_DIR)/kernel.bin
FLOPPY_IMG  := $(BUILD_DIR)/aurionos.img
ISO_IMG     := $(BUILD_DIR)/aurionos.iso
ISO_DIR     := $(BUILD_DIR)/iso

HDD_IMG     := $(BUILD_DIR)/aurionos_hdd.img
HDD_SIZE_MB := 100

# Image parameters
IMG_SIZE_SECTORS := 2880           # 1.44MB floppy (2880 * 512 bytes)
BOOT_SIZE        := 512            # Boot sector size
KERNEL_START     := 1              # Kernel starts at sector 1

# Source Files
# Assembly sources
ASM_BOOT   := $(SRC_DIR)/bootload.asm
ASM_KERNEL := $(SRC_DIR)/kernel.asm \
              $(SRC_DIR)/memory.asm \
              $(SRC_DIR)/filesys.asm \
              $(SRC_DIR)/io.asm \
              $(SRC_DIR)/interrupt.asm \
              $(SRC_DIR)/vesa.asm \
              $(SRC_DIR)/icon_data.asm

# C sources - kernel files (includes real hardware drivers)
# IMPORTANT: vbe_graphics.c MUST come before rust_driver_stubs.c to override weak symbols
C_SOURCES  := $(SRC_DIR)/drivers/vbe_graphics.c \
              $(SRC_DIR)/drivers/mouse.c \
              $(SRC_DIR)/drivers/ata.c \
              $(SRC_DIR)/drivers/ne2000.c \
              $(SRC_DIR)/drivers/png.c \
              $(SRC_DIR)/drivers/icons.c \
              $(SRC_DIR)/icon_loader.c \
              $(SRC_DIR)/shell.c \
              $(SRC_DIR)/window_manager.c \
              $(SRC_DIR)/desktop.c \
              $(SRC_DIR)/terminal.c \
              $(SRC_DIR)/gui_apps.c \
              $(SRC_DIR)/commands.c \
              $(SRC_DIR)/cmd_netmode.c \
              $(SRC_DIR)/syscall.c \
              $(SRC_DIR)/console_shim.c \
              $(SRC_DIR)/utils.c \
              $(SRC_DIR)/handlers.c \
              $(SRC_DIR)/pci.c \
              $(SRC_DIR)/wifi_autostart.c \
              $(SRC_DIR)/network_interface.c \
              $(SRC_DIR)/tcp_ip_stack.c \
              $(SRC_DIR)/dhcp_client.c \
              $(SRC_DIR)/firmware_loader.c \
              $(SRC_DIR)/scrollback.c \
              $(SRC_DIR)/cmd_make.c \
              $(SRC_DIR)/cmd_python.c \
              $(SRC_DIR)/rust_driver_stubs.c

# Object files
ASM_OBJS      := $(patsubst $(SRC_DIR)/%.asm,$(OBJ_DIR)/%.o,$(ASM_KERNEL))
C_OBJS        := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(C_SOURCES))
ALL_OBJS      := $(ASM_OBJS) $(C_OBJS)

# Build Rules

.PHONY: all
all: $(FLOPPY_IMG) $(ISO_IMG)
	@echo "======================================"
	@echo "AurionOS build completed successfully!"
	@echo "Floppy: $(FLOPPY_IMG)"
	@echo "ISO:    $(ISO_IMG)"
	@echo "======================================"

# Directory Creation
$(BUILD_DIR):
	@$(MKDIR) $(BUILD_DIR)

$(OBJ_DIR):
	@$(MKDIR) $(OBJ_DIR)
	@$(MKDIR) $(OBJ_DIR)/drivers

$(ISO_DIR):
	@$(MKDIR) $(ISO_DIR)

# Bootloader Build
$(BOOT_BIN): $(ASM_BOOT) $(KERNEL_BIN) tools/build_bootloader.py | $(BUILD_DIR)
	@echo "Assembling bootloader: $<"
	@$(PYTHON) tools/build_bootloader.py $(NASM) "$(NASMFLAGS_BIN)" $< $@

# Kernel Assembly Files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.asm | $(OBJ_DIR)
	@echo "Assembling: $<"
	@$(NASM) $(NASMFLAGS_ELF) $< -o $@

# Kernel C Files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	@echo "Compiling: $<"
	@$(GCC) $(CFLAGS) $< -o $@

# Kernel Linking (with Rust library if available)
$(KERNEL_BIN): $(ALL_OBJS) link.ld | $(BUILD_DIR)
	@echo "Linking kernel..."
	$(LD) $(LDFLAGS) -o $@ $(ALL_OBJS)
	@echo "✓ Kernel linked successfully"
	@$(PYTHON) -c "import os; sz=os.path.getsize('$@'); print(f'Kernel size: {sz} bytes ({sz//1024}K)' )"

# Floppy Image Assembly (for QEMU)
$(FLOPPY_IMG): $(BOOT_BIN) $(KERNEL_BIN) tools/mkfloppy.py
	@echo "Creating floppy image..."
	@$(PYTHON) tools/mkfloppy.py $@ $(BOOT_BIN) $(KERNEL_BIN)
	@echo "✓ Floppy image created: $@"

# ISO Image Assembly
# Uses tools/mkiso.py to build a hybrid ISO 9660 image.
# El Torito no-emulation boot - compatible with VirtualBox, VMware, QEMU, real hardware.
$(ISO_IMG): $(FLOPPY_IMG) tools/mkiso.py | $(BUILD_DIR)
	@echo "Creating hybrid ISO image..."
	@$(PYTHON) tools/mkiso.py $@ $(FLOPPY_IMG)
	@echo "✓ ISO image created: $@"

# Running and Testing

# Create persistent hard disk image (only if it doesn't exist)
$(HDD_IMG): tools/mkhdd.py | $(BUILD_DIR)
	@echo "Creating persistent hard disk image ($(HDD_SIZE_MB)MB)..."
	@$(PYTHON) tools/mkhdd.py $@ $(HDD_SIZE_MB)
	@echo "✓ HDD image created (or preserved): $@"

.PHONY: run
run: $(FLOPPY_IMG) $(HDD_IMG)
	@echo "Starting QEMU with floppy + HDD..."
	@if [ "$(IS_WSL)" = "yes" ]; then \
		FLP_WIN=$$(wslpath -w $(FLOPPY_IMG)); \
		HDD_WIN=$$(wslpath -w $(HDD_IMG)); \
		QEMU_WIN="/mnt/c/Program Files/qemu/qemu-system-i386.exe"; \
		if [ -x "$$QEMU_WIN" ]; then \
			"$$QEMU_WIN" \
				-drive "file=$$FLP_WIN,if=floppy,format=raw" \
				-drive "file=$$HDD_WIN,if=ide,format=raw" \
				-boot a -m 512M \
				-nic user \
				-vga std; \
		else \
			$(QEMU) -drive file=$(FLOPPY_IMG),if=floppy,format=raw \
				-drive file=$(HDD_IMG),if=ide,format=raw \
				-boot a -m 512M -nic user -vga std; \
		fi; \
	else \
		$(QEMU) -drive file=$(FLOPPY_IMG),if=floppy,format=raw \
			-drive file=$(HDD_IMG),if=ide,format=raw \
			-boot a -m 512M \
			-nic user \
			-vga std; \
	fi

.PHONY: run-dbg
run-dbg: $(FLOPPY_IMG) $(HDD_IMG)
	@echo "Starting QEMU in debug display mode..."
	@$(QEMU) -drive file=$(FLOPPY_IMG),if=floppy,format=raw \
		-drive file=$(HDD_IMG),if=ide,format=raw \
		-boot a -m 512M \
		-vga std \
		-display sdl

.PHONY: run-debug
run-debug: $(FLOPPY_IMG) $(HDD_IMG)
	@echo "Starting QEMU with debug options..."
	@$(QEMU) -drive file=$(FLOPPY_IMG),if=floppy,format=raw \
		-drive file=$(HDD_IMG),if=ide,format=raw \
		-boot a -m 512M \
		-vga std \
		-device virtio-net-pci,disable-modern=on,netdev=net0 -netdev user,id=net0 \
		-d int,cpu_reset -no-reboot

.PHONY: run-iso
run-iso: $(ISO_IMG) $(HDD_IMG)
	@echo "Starting QEMU with ISO (CD-ROM Mode)..."
	@if [ ! -f $(ISO_IMG) ]; then \
		echo "ERROR: ISO file not found at $(ISO_IMG)"; \
		exit 1; \
	fi
	@echo "Booting ISO as CD-ROM (Drive D)..."
	@if [ "$(IS_WSL)" = "yes" ]; then \
		ISO_WIN=$$(wslpath -w $(ISO_IMG)); \
		HDD_WIN=$$(wslpath -w $(HDD_IMG)); \
		QEMU_WIN="/mnt/c/Program Files/qemu/qemu-system-i386.exe"; \
		"$$QEMU_WIN" -cdrom "$$ISO_WIN" \
			-drive "file=$$HDD_WIN,if=ide,format=raw" \
			-boot order=d,strict=on -m 512M \
			-vga std \
			-nic user \
			-no-reboot; \
	else \
		$(QEMU_ISO) -cdrom $(ISO_IMG) \
			-drive file=$(HDD_IMG),if=ide,format=raw \
			-boot order=d,strict=on -m 512M \
			-vga std \
			-nic user \
			-no-reboot; \
	fi

# Reset the hard disk (clear all saved data)
.PHONY: reset-hdd
reset-hdd:
	@echo "Resetting hard disk image..."
	@$(RM) $(HDD_IMG)
	@$(DD) if=/dev/zero of=$(HDD_IMG) bs=1M count=$(HDD_SIZE_MB) status=none
	@echo "✓ Hard disk reset complete"

# Debugging and Analysis

.PHONY: dump-boot
dump-boot: $(BOOT_BIN)
	@echo "Bootloader hexdump:"
	@$(HEXDUMP) -C $@ | head -n 32

.PHONY: dump-kernel
dump-kernel: $(KERNEL_BIN)
	@echo "Kernel hexdump (first 512 bytes):"
	@$(HEXDUMP) -C $@ | head -n 32

.PHONY: info
info:
	@echo "======================================"
	@echo "AurionOS Build Information"
	@echo "======================================"
	@if [ -f $(BOOT_BIN) ]; then \
		echo "Bootloader:  $(BOOT_BIN)"; \
		echo "  Size:      $$(stat -c%s $(BOOT_BIN)) bytes"; \
		echo ""; \
	else \
		echo "Bootloader:  $(BOOT_BIN) [NOT BUILT]"; \
		echo ""; \
	fi
	@if [ -f $(KERNEL_BIN) ]; then \
		echo "Kernel:      $(KERNEL_BIN)"; \
		echo "  Size:      $$(stat -c%s $(KERNEL_BIN)) bytes"; \
		echo ""; \
	else \
		echo "Kernel:      $(KERNEL_BIN) [NOT BUILT]"; \
		echo ""; \
	fi
	@if [ -f $(FLOPPY_IMG) ]; then \
		echo "Floppy Image: $(FLOPPY_IMG)"; \
		echo "  Size:       $$(stat -c%s $(FLOPPY_IMG)) bytes"; \
		echo ""; \
	else \
		echo "Floppy Image: $(FLOPPY_IMG) [NOT BUILT]"; \
		echo ""; \
	fi
	@if [ -f $(ISO_IMG) ]; then \
		echo "ISO Image:    $(ISO_IMG)"; \
		echo "  Size:       $$(stat -c%s $(ISO_IMG)) bytes"; \
	else \
		echo "ISO Image:    $(ISO_IMG) [NOT BUILT]"; \
	fi
	@echo "======================================"

# Cleaning
.PHONY: clean
clean:
	@echo "Cleaning build artifacts (preserving HDD for persistence)..."
	@$(RM) $(BOOT_BIN) $(KERNEL_BIN) $(FLOPPY_IMG) $(ISO_IMG)
	@echo "✓ Clean complete (HDD preserved at $(HDD_IMG))"

.PHONY: clean-all
clean-all:
	@echo "Cleaning ALL build artifacts including HDD..."
	@echo "Cleaning ALL build artifacts including HDD..."
	@$(RM) -r $(BUILD_DIR)
	@echo "✓ Full clean complete"

.PHONY: clean-objs
clean-objs:
	@echo "Cleaning object files..."
	@$(RM) -r $(OBJ_DIR)
	@echo "✓ Object files cleaned"



.PHONY: clean-img
clean-img:
	@echo "Cleaning disk images..."
	@$(RM) $(FLOPPY_IMG) $(ISO_IMG)
	@$(RM) -r $(ISO_DIR)
	@echo "✓ Disk images cleaned"

.PHONY: rebuild
rebuild: clean-all all
	@echo "Note: HDD was reset. All saved files cleared."

# Help
.PHONY: help
help:
	@echo "======================================"
	@echo "AurionOS Makefile - Available Targets"
	@echo "======================================"
	@echo "Building:"
	@echo "  make all          - Build complete system (default)"

	@echo "  make rebuild      - Clean and rebuild everything"
	@echo "  make clean        - Remove build artifacts (preserves HDD)"
	@echo "  make clean-all    - Remove ALL artifacts including HDD"

	@echo ""
	@echo "Running:"
	@echo "  make run          - Build and run in QEMU (floppy + HDD)"
	@echo "  make run-iso      - Build and run in QEMU (ISO + HDD)"
	@echo "  make run-debug    - Run with CPU debug output"
	@echo ""
	@echo "Storage:"
	@echo "  make reset-hdd    - Reset HDD (clear all saved files)"
	@echo "  HDD location: $(HDD_IMG)"
	@echo ""
	@echo "Info:"
	@echo "  make info         - Display build information"
	@echo "  make help         - Show this help message"
	@echo "======================================"
	@echo ""
	@echo "Filesystem Commands in AurionOS:"
	@echo "  TOUCH file  - Create empty file (persists after reboot)"
	@echo "  MKDIR dir   - Create directory (persists after reboot)"
	@echo "  NANO file   - Edit file (persists after reboot)"
	@echo "  CD dir      - Change directory (persists after reboot)"
	@echo "  DIR/LS      - List files in current directory"
	@echo "======================================"

# Dependencies
$(KERNEL_BIN): $(ALL_OBJS)
$(ASM_OBJS): $(OBJ_DIR)/%.o: $(SRC_DIR)/%.asm
$(C_OBJS): $(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
$(BOOT_BIN): $(ASM_BOOT)
$(FLOPPY_IMG): $(BOOT_BIN) $(KERNEL_BIN)
$(ISO_IMG): $(FLOPPY_IMG)

.DEFAULT_GOAL := all
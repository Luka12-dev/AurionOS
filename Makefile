# AurionOS Makefile
# Author: AurionOS Development Team
# Purpose: Build AurionOS from assembly, C, and Rust sources into bootable floppy image
# Target: Bootable 1.44MB floppy disk image

# Tool Configuration
NASM        := nasm
GCC         := gcc
LD          := gcc
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
ifneq ($(OS),Windows_NT)
    IS_WSL := $(shell grep -qi microsoft /proc/version 2>/dev/null && echo yes)
else
    IS_WSL := no
endif

ifeq ($(OS),Windows_NT)
    NASMFLAGS_ELF := -f win32 -g
    LDFLAGS := -m i386pe -nostdlib -T link.ld --oformat binary
    PYTHON := python
    MKDIR := mkdir
    RM := del /Q
    RMDIR := rmdir /S /Q
    QEMU := qemu-system-i386
    QEMU_ISO := qemu-system-i386
else ifeq ($(IS_WSL),yes)
    # WSL2 - build tools are Linux, but use Windows QEMU for CD-ROM to avoid
    # the QEMU 8.2.2 iothread assertion crash on ATAPI/CD-ROM emulation.
    NASMFLAGS_ELF := -f elf32 -g -F dwarf
    LDFLAGS := -m32 -nostdlib -T link.ld -Wl,--oformat,binary
    PYTHON := python3
    MKDIR := mkdir -p
    RM := rm -f
    RMDIR := rm -rf
    QEMU := qemu-system-i386
    QEMU_ISO := /mnt/c/Program Files/qemu/qemu-system-i386.exe
else
    # Native Linux
    NASMFLAGS_ELF := -f elf32 -g -F dwarf
    LDFLAGS := -m32 -nostdlib -T link.ld -Wl,--oformat,binary
    PYTHON := python3
    MKDIR := mkdir -p
    RM := rm -f
    RMDIR := rm -rf
    QEMU := qemu-system-i386
    QEMU_ISO := qemu-system-i386
endif

NASMFLAGS_BIN := -f bin

# Normalize paths for shell commands that run under Windows cmd.exe.
pathfix = $(if $(filter Windows_NT,$(OS)),$(subst /,\,$1),$1)

# GCC flags for 32-bit freestanding
CFLAGS := -m32 -ffreestanding -nostdlib -Iinclude -fno-builtin -fno-stack-protector
CFLAGS += -O0 -g -Wall -Wextra -std=c11
CFLAGS += -fno-pie -fno-pic
CFLAGS += -mpreferred-stack-boundary=2 -mno-mmx -mno-sse -mno-sse2
CFLAGS += -c

# Library path for compiler helpers (floating point, libgcc etc)
LIBGCC  := $(shell $(GCC) -m32 -print-libgcc-file-name)

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
              $(SRC_DIR)/drivers/vmware_svga.c \
              $(SRC_DIR)/icon_loader.c \
              $(SRC_DIR)/shell.c \
              $(SRC_DIR)/window_manager.c \
              $(SRC_DIR)/desktop.c \
              $(SRC_DIR)/terminal.c \
              $(SRC_DIR)/gui_apps.c \
              $(SRC_DIR)/installer.c \
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
    $(SRC_DIR)/http_client.c \
    $(SRC_DIR)/firmware_loader.c \
              $(SRC_DIR)/scrollback.c \
              $(SRC_DIR)/cmd_make.c \
              $(SRC_DIR)/cmd_python.c \
              $(SRC_DIR)/cmd_net_test.c \
              $(SRC_DIR)/Blaze/blaze_core.c \
              $(SRC_DIR)/Blaze/blaze_html.c \
              $(SRC_DIR)/Blaze/blaze_css.c \
              $(SRC_DIR)/Blaze/blaze_layout.c \
              $(SRC_DIR)/Blaze/blaze_render.c \
              $(SRC_DIR)/Blaze/blaze_net.c \
              $(SRC_DIR)/Blaze/blaze_js.c \
              $(SRC_DIR)/Blaze/blaze_app.c \
              $(SRC_DIR)/rust_driver_stubs.c

# Object files
ASM_OBJS      := $(patsubst $(SRC_DIR)/%.asm,$(OBJ_DIR)/%.o,$(ASM_KERNEL))
C_OBJS        := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(C_SOURCES))
ALL_OBJS      := $(ASM_OBJS) $(C_OBJS)

# Build Rules

.PHONY: all all-debug
all: check-build-type $(FLOPPY_IMG) $(ISO_IMG) $(HDD_IMG)
	@echo "======================================"
	@echo "AurionOS build completed successfully!"
	@echo "Floppy: $(FLOPPY_IMG)"
	@echo "ISO:    $(ISO_IMG)"
	@echo "HDD:    $(HDD_IMG) (persistent storage)"
	@echo ""
	@echo "IMPORTANT: For persistent installation:"
	@echo "  1. First boot: Use ISO to install"
	@echo "  2. After install: Boot from HDD for persistence"
	@echo "  VMware: Add both ISO (CD-ROM) and HDD as hard disk"
	@echo "  VirtualBox: Add both ISO and HDD, set boot order"
	@echo "======================================"

# Check if we need to clean due to build type change
.PHONY: check-build-type
check-build-type:
	@if [ -f $(BUILD_DIR)/.debug_build ]; then \
		echo "Switching from debug to release build - cleaning objects..."; \
		$(MAKE) clean-objs; \
		$(RM) $(BUILD_DIR)/.debug_build; \
	fi

# Debug build with pre-installed system (skips installer)
all-debug: CFLAGS += -DDEBUG_SKIP_INSTALL
all-debug: check-debug-type clean-objs
	@$(MAKE) $(KERNEL_BIN) $(BOOT_BIN) CFLAGS="$(CFLAGS) -DDEBUG_SKIP_INSTALL"
	@touch $(BUILD_DIR)/.debug_build
	@echo "======================================"
	@echo "Building debug version..."
	@echo "======================================"
	@$(PYTHON) tools/mkfloppy.py $(BUILD_DIR)/aurionos_debug.img $(BOOT_BIN) $(KERNEL_BIN)
	@echo "INSTALLED" > $(BUILD_DIR)/installed_marker.tmp
	@$(PYTHON) -c "import sys; sys.path.insert(0, 'tools'); from mkfloppy import add_file_to_floppy; add_file_to_floppy('$(BUILD_DIR)/aurionos_debug.img', '/installed.sys', '$(BUILD_DIR)/installed_marker.tmp')" 2>/dev/null || true
	@$(RM) $(BUILD_DIR)/installed_marker.tmp 2>/dev/null || true
	@$(PYTHON) tools/mkiso.py $(BUILD_DIR)/aurionos_debug.iso $(BUILD_DIR)/aurionos_debug.img
	@echo ""
	@echo "======================================"
	@echo "Debug build complete!"

.PHONY: check-debug-type
check-debug-type:
	@if [ ! -f $(BUILD_DIR)/.debug_build ]; then \
		echo "Switching to debug build - cleaning objects..."; \
	fi
	@echo "======================================"
	@echo "Debug IMG: $(BUILD_DIR)/aurionos_debug.img"
	@echo "Debug ISO: $(BUILD_DIR)/aurionos_debug.iso"
	@echo ""
	@echo "Installation screen is skipped."
	@echo ""
	@echo "To run in v86:"
	@echo "  1. Open v86-web/index.html in browser"
	@echo "  2. It will auto-load aurionos_debug.iso"
	@echo ""
	@echo "======================================"

# Directory Creation
$(BUILD_DIR):
	@$(MKDIR) $(BUILD_DIR)

$(OBJ_DIR):
	@$(MKDIR) $(OBJ_DIR)
	@$(MKDIR) $(OBJ_DIR)/drivers
	@$(MKDIR) $(OBJ_DIR)/Blaze

$(ISO_DIR):
	@$(MKDIR) $(ISO_DIR)

# Bootloader Build
$(BOOT_BIN): $(ASM_BOOT) $(KERNEL_BIN) tools/build_bootloader.py | $(BUILD_DIR)
	@echo "Assembling bootloader: $<"
	@$(PYTHON) tools/build_bootloader.py $(NASM) "$(NASMFLAGS_BIN)" $< $@

# Kernel Assembly Files
$(OBJ_DIR)/icon_data.o: $(SRC_DIR)/icon_data.asm icons/terminal.rle icons/Browser.rle icons/notepad.rle icons/calculator.rle icons/files.rle icons/clock.rle icons/paint.rle icons/sys-info.rle icons/folder.rle icons/file_on_desktop.rle icons/settings.rle icons/snake.rle | $(OBJ_DIR)
	@echo "Assembling: $< (with icons)"
	@$(NASM) $(NASMFLAGS_ELF) $< -o $@

# Ensure subdirectories in OBJ_DIR exist before compilation
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.asm
	@$(MKDIR) $(dir $@)
	@echo "Assembling: $<"
	@$(NASM) $(NASMFLAGS_ELF) $< -o $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@$(MKDIR) $(dir $@)
	@echo "Compiling: $<"
	@$(GCC) $(CFLAGS) $< -o $@

# Kernel Linking (with Rust library if available)
$(KERNEL_BIN): $(ALL_OBJS) link.ld | $(BUILD_DIR)
	@echo "Linking kernel..."
	$(LD) $(LDFLAGS) -o $@ $(ALL_OBJS) $(LIBGCC)
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

# Create persistent hard disk image (pre-installed)
$(HDD_IMG): $(FLOPPY_IMG) tools/mkfloppy.py | $(BUILD_DIR)
	@if [ ! -f $@ ]; then \
		echo "Creating pre-installed HDD image..."; \
		cp $(FLOPPY_IMG) $@; \
		echo "INSTALLED" > $(BUILD_DIR)/.installed_marker; \
		$(PYTHON) -c "import sys; sys.path.insert(0, 'tools'); from mkfloppy import add_file_to_floppy; add_file_to_floppy('$@', '/installed.sys', '$(BUILD_DIR)/.installed_marker')" 2>/dev/null || true; \
		$(RM) $(BUILD_DIR)/.installed_marker 2>/dev/null || true; \
		echo "✓ Pre-installed HDD created: $@"; \
	else \
		echo "✓ HDD image preserved: $@ (delete to recreate)"; \
	fi

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
				-netdev user,id=net0 \
				-device virtio-net-pci,disable-modern=on,netdev=net0 \
				-vga std; \
		else \
			$(QEMU) -drive file=$(FLOPPY_IMG),if=floppy,format=raw \
				-drive file=$(HDD_IMG),if=ide,format=raw \
				-boot a -m 512M \
				-netdev user,id=net0 \
				-device virtio-net-pci,disable-modern=on,netdev=net0 \
				-vga std; \
		fi; \
	else \
		$(QEMU) -drive file=$(FLOPPY_IMG),if=floppy,format=raw \
			-drive file=$(HDD_IMG),if=ide,format=raw \
			-boot a -m 512M \
			-netdev user,id=net0 \
			-device virtio-net-pci,disable-modern=on,netdev=net0 \
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
			-device virtio-net-pci,disable-modern=on,netdev=net0 -netdev user,id=net0 \
			-no-reboot; \
	else \
		$(QEMU_ISO) -cdrom $(ISO_IMG) \
			-drive file=$(HDD_IMG),if=ide,format=raw \
			-boot order=d,strict=on -m 512M \
			-vga std \
			-device virtio-net-pci,disable-modern=on,netdev=net0 -netdev user,id=net0 \
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
	@$(RM) $(call pathfix,$(BOOT_BIN)) $(call pathfix,$(KERNEL_BIN)) $(call pathfix,$(FLOPPY_IMG)) $(call pathfix,$(ISO_IMG))
	@echo "✓ Clean complete (HDD preserved at $(HDD_IMG))"

.PHONY: clean-all
clean-all:
	@echo "Cleaning ALL build artifacts including HDD..."
	@echo "Cleaning ALL build artifacts including HDD..."
	@$(RMDIR) $(call pathfix,$(BUILD_DIR))
	@echo "✓ Full clean complete"

.PHONY: clean-objs
clean-objs:
	@echo "Cleaning object files..."
	@$(RMDIR) $(call pathfix,$(OBJ_DIR))
	@echo "✓ Object files cleaned"



.PHONY: clean-img
clean-img:
	@echo "Cleaning disk images..."
	@$(RM) $(call pathfix,$(FLOPPY_IMG)) $(call pathfix,$(ISO_IMG))
	@$(RMDIR) $(call pathfix,$(ISO_DIR))
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
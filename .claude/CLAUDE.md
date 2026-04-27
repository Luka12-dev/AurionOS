# AurionOS - Claude Development Guide

This document provides context and guidance for working with the AurionOS codebase. AurionOS is a complete operating system built from scratch in x86 assembly and C - no Linux kernel, no POSIX, no borrowed code.

---

## Project Overview

AurionOS is a modern, bootable operating system featuring:

- Custom bootloader and kernel written in x86 assembly
- Desktop environment with windowed GUI
- Dual-mode operation (GUI and DOS modes)
- Custom filesystem with persistence
- Network stack (TCP/IP, DHCP, HTTP/HTTPS)
- Built-in applications (browser, terminal, paint, calculator, games)
- AurionGL 3D graphics library (software rasterization)
- Blaze browser engine (HTML/CSS/JavaScript)

**Target**: Real hardware and virtual machines (QEMU, VMware, VirtualBox)
**Architecture**: x86 32-bit protected mode
**Language**: Assembly (NASM) + C11 (freestanding)

---

## Build System

### Platform Detection

The Makefile automatically detects the platform:
- **Windows**: Native Windows build (rarely used)
- **WSL2**: Linux tools with Windows QEMU for CD-ROM (avoids QEMU 8.2.2 crash)
- **Linux**: Native Linux build

### Build Commands

```bash
# Standard build (creates floppy, ISO, HDD images)
make all

# Debug build (skips installer, pre-installed system)
make all-debug

# Run in QEMU
make run          # Floppy + HDD
make run-iso      # ISO + HDD (CD-ROM boot)
make run-debug    # With CPU debug output

# Clean
make clean        # Preserves HDD (persistent storage)
make clean-all    # Removes everything including HDD
make reset-hdd    # Clear HDD but keep structure
```

### Build Artifacts

- `build/bootload.bin` - 512-byte boot sector
- `build/kernel.bin` - Flat kernel binary
- `build/aurionos.img` - 1.44MB floppy image
- `build/aurionos.iso` - Bootable ISO (El Torito)
- `build/aurionos_hdd.img` - 60MB persistent hard disk
- `build/aurionos_debug.iso` - Debug ISO (skips installer)

### Important Build Notes

1. **Always use WSL on Windows**: `wsl make all`
2. **HDD persistence**: The HDD image preserves user data across reboots
3. **Icon embedding**: Icons are embedded at LBA 20000 in HDD/ISO
4. **Wallpaper embedding**: Wallpaper embedded at specific LBA for installer
5. **Debug builds**: Use `make all-debug` to skip installer and boot directly to desktop

---

## Architecture

### Boot Process

```
BIOS loads bootloader (sector 0)
  ↓
Bootloader loads kernel from disk
  ↓
Switch to 32-bit protected mode
  ↓
Initialize IDT, PIC, memory, drivers
  ↓
Check boot_mode_flag
  ↓
├─ GUI Mode: VESA graphics → Desktop
└─ DOS Mode: VGA text → Shell
```

### Memory Layout

```
0x00007C00 - Bootloader (512 bytes)
0x00010000 - Kernel loaded here
0x000A0000 - VGA memory
0x00100000+ - Extended memory (heap)
```

### Key Components

**Boot Layer**
- `src/bootload.asm` - Bootloader (loads kernel, switches to protected mode)
- `src/kernel.asm` - Kernel entry point
- `src/memory.asm` - Memory management
- `src/interrupt.asm` - IDT and ISRs
- `src/vesa.asm` - VESA VBE mode setup

**Core System**
- `src/shell.c` - DOS mode shell
- `src/commands.c` - Shell command implementations
- `src/desktop.c` - Desktop environment
- `src/window_manager.c` - Window management
- `src/menu_bar.c` - Desktop menu bar
- `src/terminal.c` - Terminal application
- `src/installer.c` - Installation wizard
- `src/boot_screen.c` - Boot splash screen
- `src/login_screen.c` - Login screen
- `src/panic.c` - Kernel panic handler

**Drivers**
- `src/drivers/vbe_graphics.c` - VESA VBE graphics
- `src/drivers/vmware_svga.c` - VMware SVGA acceleration
- `src/drivers/mouse.c` - PS/2 mouse driver
- `src/drivers/ata.c` - ATA disk driver
- `src/drivers/ne2000.c` - NE2000 network card
- `src/drivers/rtl8139.c` - RTL8139 network card
- `src/drivers/bmp.c` - BMP image loader
- `src/drivers/png.c` - PNG image loader
- `src/drivers/icons.c` - Icon management

**Applications**
- `src/gui_apps.c` - GUI application framework
- `src/app_3d_demo.c` - 3D demo application
- `src/Blaze/` - Blaze browser engine
  - `blaze_core.c` - Core engine
  - `blaze_html.c` - HTML parser
  - `blaze_css.c` - CSS parser
  - `blaze_layout.c` - Layout engine
  - `blaze_render.c` - Rendering engine
  - `blaze_js.c` - JavaScript interpreter
  - `blaze_net.c` - Network integration

**Graphics**
- `AurionGL/auriongl.c` - 3D graphics library
- `AurionGL/auriongl.h` - API header
- Software rasterization with z-buffering
- OpenGL-inspired immediate mode API

**Network**
- `src/Network/virtio_net.c` - VirtIO network driver
- `src/Network/wifi_driver.c` - WiFi driver
- `src/Network/http_full.c` - HTTP client
- `src/Network/https_client.c` - HTTPS client
- `src/Network/tls12_client.c` - TLS 1.2 implementation
- `src/tcp_ip_stack.c` - TCP/IP stack
- `src/dhcp_client.c` - DHCP client

**Filesystem**
- `src/filesys.asm` - Filesystem implementation
- `src/iso9660.c` - ISO 9660 CD-ROM filesystem
- Custom persistent filesystem on HDD

---

## Code Style

### Assembly (NASM)

- Intel syntax
- Labels end with colon
- Comments explain WHY, not WHAT
- Use meaningful label names

```asm
; Good
load_kernel:
    mov si, loading_msg
    call print_string
    ret

; Bad
lk:
    mov si, msg  ; Move message to SI
    call ps      ; Print string
    ret
```

### C Code

- C11 standard, freestanding (no standard library)
- snake_case for functions and variables
- PascalCase for types and structs
- No external dependencies
- Custom implementations of string functions

```c
// Good
void window_draw(Window *win) {
    if (!win || !win->visible) return;
    draw_rectangle(win->x, win->y, win->width, win->height, win->color);
}

// Bad
void DrawWin(void* w) {
    // No validation
    draw_rect(((Window*)w)->x, ((Window*)w)->y, ...);
}
```

### Compiler Flags

```makefile
CFLAGS := -m32 -ffreestanding -nostdlib -Iinclude
CFLAGS += -fno-builtin -fno-stack-protector
CFLAGS += -O0 -g -Wall -Wextra -std=c11
CFLAGS += -fno-pie -fno-pic
```

---

## Common Tasks

### Adding a New Application

1. Define app structure in `src/gui_apps.c`
2. Implement render and event handlers
3. Add icon to `icons/` directory (BMP format)
4. Register app in desktop app list
5. Rebuild: `make all`

### Adding a Shell Command

1. Add command handler in `src/commands.c`
2. Register in command table
3. Update help text
4. Rebuild: `make all`

### Modifying Graphics

- VBE graphics: `src/drivers/vbe_graphics.c`
- Window manager: `src/window_manager.c`
- Desktop: `src/desktop.c`
- AurionGL: `AurionGL/auriongl.c`

### Network Changes

- Stack: `src/tcp_ip_stack.c`
- Drivers: `src/Network/`
- HTTP: `src/Network/http_full.c`
- HTTPS/TLS: `src/Network/https_client.c`, `src/Network/tls12_client.c`

### Filesystem Operations

- Core: `src/filesys.asm`
- ISO 9660: `src/iso9660.c`
- File commands: `src/commands.c`

---

## Debugging

### QEMU Debug Options

```bash
# CPU debug output
make run-debug

# Monitor console
qemu-system-i386 -monitor stdio ...

# GDB debugging
qemu-system-i386 -s -S ...
gdb build/kernel.bin
(gdb) target remote localhost:1234
```

### Common Issues

**Black screen on boot**
- Check VESA mode support
- Try different resolution
- Verify bootloader loads kernel correctly

**Kernel panic**
- Check `src/panic.c` for panic handler
- Look for triple fault (CPU reset)
- Verify IDT is properly initialized

**Graphics glitches**
- Check framebuffer pointer
- Verify VESA mode is set correctly
- Look for buffer overruns

**Network not working**
- Run `NETSTART` command
- Check driver initialization
- Verify DHCP response

---

## Testing

### Virtual Machines

**QEMU** (fastest for development)
```bash
make run
```

**VMware** (best graphics performance)
- Create VM: Other 32-bit
- Add ISO as CD-ROM
- Add HDD image
- Boot from CD first time

**VirtualBox** (good compatibility)
- Create VM: Other/Unknown 32-bit
- Add ISO and HDD
- Enable I/O APIC

### Real Hardware

1. Burn ISO to CD or USB
2. Boot from CD/USB
3. Install to hard disk
4. Reboot from hard disk

**Tested Hardware**
- Various x86 PCs with PS/2 keyboard/mouse
- VMware Workstation (SVGA acceleration)
- VirtualBox (VBE graphics)
- QEMU (full support)

---

## File Organization

```
AurionOS/
├── .claude/              # Claude AI context (this file)
├── AurionGL/             # 3D graphics library
│   ├── auriongl.c        # Implementation
│   ├── auriongl.h        # API header
│   ├── example.c         # Usage examples
│   ├── INTEGRATION.md    # Integration guide
│   └── README.md         # Documentation
├── assets/               # Screenshots
├── build/                # Build output (generated)
├── icons/                # Application icons (BMP)
├── include/              # Header files
│   ├── firmware.h
│   ├── fs.h
│   ├── network.h
│   └── ...
├── src/                  # Source code
│   ├── *.asm             # Assembly sources
│   ├── *.c               # C sources
│   ├── *.h               # Headers
│   ├── Blaze/            # Browser engine
│   ├── drivers/          # Hardware drivers
│   └── Network/          # Network stack
├── tools/                # Build scripts
│   ├── build_bootloader.py
│   ├── mkfloppy.py
│   ├── mkiso.py
│   ├── mkhdd.py
│   ├── embed_icons.py
│   └── embed_wallpaper.py
├── v86-web/              # Web demo files
├── Wallpaper/            # Desktop wallpapers
├── Makefile              # Build system
├── link.ld               # Linker script
├── README.md             # User documentation
├── CHANGELOG.md          # Version history
└── LICENSE               # License file
```

---

## Important Files

### Build System
- `Makefile` - Main build orchestration
- `link.ld` - Linker script for kernel
- `tools/build_bootloader.py` - Bootloader assembly
- `tools/mkfloppy.py` - Floppy image creation
- `tools/mkiso.py` - ISO image creation
- `tools/mkhdd.py` - HDD image creation

### Boot
- `src/bootload.asm` - Bootloader (512 bytes)
- `src/kernel.asm` - Kernel entry point
- `src/memory.asm` - Memory management
- `src/interrupt.asm` - Interrupt handling

### Core
- `src/desktop.c` - Desktop environment
- `src/window_manager.c` - Window management
- `src/shell.c` - DOS mode shell
- `src/commands.c` - Shell commands

### Graphics
- `src/drivers/vbe_graphics.c` - VESA graphics
- `AurionGL/auriongl.c` - 3D graphics library

### Network
- `src/tcp_ip_stack.c` - TCP/IP implementation
- `src/Network/http_full.c` - HTTP client
- `src/Network/https_client.c` - HTTPS client

---

## Development Workflow

### Typical Development Cycle

1. **Make changes** to source files
2. **Build**: `wsl make all` (Windows) or `make all` (Linux)
3. **Test**: `make run` or `make run-iso`
4. **Debug**: Check output, add debug prints, use `make run-debug`
5. **Iterate**: Repeat until working

### Quick Iteration

For faster development:
```bash
# Build only kernel (skip image creation)
wsl make build/kernel.bin

# Build and run immediately
wsl make all && make run
```

### Debug Build

For development without installer:
```bash
wsl make all-debug
make run-iso
```

This boots directly to desktop with pre-installed system.

---

## AurionGL Integration

AurionGL is a software-based 3D graphics library with an OpenGL-inspired API.

### Key Features
- Software rasterization (no GPU required)
- Z-buffering for depth sorting
- Perspective-correct texture mapping
- Lighting system (up to 4 lights)
- Matrix transformation stack
- Immediate mode rendering

### Usage Example

```c
#include "AurionGL/auriongl.h"

// Initialize
uint32_t *fb = gpu_get_framebuffer();
aglInit(gpu_get_width(), gpu_get_height(), fb);

// Setup projection
aglMatrixMode(AGL_PROJECTION);
aglLoadIdentity();
aglPerspective(45.0f, 16.0f/9.0f, 0.1f, 100.0f);

// Setup camera
aglMatrixMode(AGL_MODELVIEW);
aglLoadIdentity();
aglLookAt(0.0f, 0.0f, 5.0f,
          0.0f, 0.0f, 0.0f,
          0.0f, 1.0f, 0.0f);

// Enable features
aglEnable(AGL_DEPTH_TEST);
aglEnable(AGL_CULL_FACE);

// Render loop
aglClear(AGL_COLOR_BUFFER_BIT | AGL_DEPTH_BUFFER_BIT);
aglPushMatrix();
aglRotate(angle, 1.0f, 1.0f, 0.0f);
aglColor3f(1.0f, 0.5f, 0.2f);
aglDrawCube();
aglPopMatrix();
aglFlush();
```

See `AurionGL/README.md` for full API documentation.

---

## Blaze Browser Engine

Blaze is a custom HTML/CSS/JavaScript engine built for AurionOS.

### Components
- `blaze_html.c` - HTML parser (DOM tree construction)
- `blaze_css.c` - CSS parser (style rules)
- `blaze_layout.c` - Layout engine (box model)
- `blaze_render.c` - Rendering (draw to framebuffer)
- `blaze_js.c` - JavaScript interpreter
- `blaze_net.c` - Network integration (HTTP/HTTPS)

### Supported Features
- HTML tags: div, p, h1-h6, a, img, span, etc.
- CSS properties: color, background, margin, padding, border, etc.
- JavaScript: Basic DOM manipulation
- Networking: HTTP/HTTPS requests
- Bookmarks: Persistent bookmark storage

---

## Network Stack

### Architecture

```
Application Layer
  ↓
HTTP/HTTPS (src/Network/http_full.c, https_client.c)
  ↓
TLS 1.2 (src/Network/tls12_client.c)
  ↓
TCP/IP Stack (src/tcp_ip_stack.c)
  ↓
DHCP Client (src/dhcp_client.c)
  ↓
Network Drivers (virtio_net.c, ne2000.c, rtl8139.c)
  ↓
Hardware
```

### Supported Protocols
- TCP/IP (IPv4)
- DHCP (automatic IP configuration)
- HTTP (web requests)
- HTTPS (TLS 1.2 encrypted)
- DNS (basic resolution)

### Network Commands
- `NETSTART` - Initialize network
- `PING <host>` - Ping host
- `WGET <url>` - Download file
- `NETTEST` - Network diagnostics

---

## Persistence

### HDD Image

The `build/aurionos_hdd.img` file provides persistent storage:

- Created once, preserved across builds
- 60MB size (configurable in Makefile)
- Custom filesystem
- Stores user files, config, app data

### Filesystem Operations

**From DOS mode:**
```
TOUCH file.txt    - Create file
MKDIR mydir       - Create directory
NANO file.txt     - Edit file
CD mydir          - Change directory
LS / DIR          - List files
RM file.txt       - Delete file
```

**From GUI:**
- File Manager app
- Drag and drop
- Right-click context menu

---

## Performance Considerations

### Boot Time
- Cold boot to desktop: ~3 seconds (QEMU)
- Mode switch (GUI ↔ DOS): ~1 second

### Memory Usage
- Kernel: ~400 KB
- Desktop: ~2 MB
- Per app: ~100-500 KB
- Minimum: 16 MB RAM
- Recommended: 512 MB RAM

### Graphics Performance
- Software rendering (no GPU)
- Resolution affects performance
- 800x600: Fast
- 1920x1080: Slower but usable
- AurionGL: Keep scenes under 10K triangles

---

## Known Issues

### QEMU 8.2.2 on WSL2
- CD-ROM emulation causes iothread assertion crash
- Workaround: Use Windows-native QEMU for ISO boot
- Makefile automatically handles this

### Graphics
- Some VESA modes not supported on all hardware
- VMware SVGA acceleration requires VMware
- VirtualBox uses standard VBE

### Network
- WiFi drivers experimental
- Best with VirtIO or RTL8139 emulation
- DHCP required (no static IP yet)

---

## Future Enhancements

### Planned Features
- USB support (UHCI, OHCI, EHCI, xHCI)
- Audio drivers (AC97, HDA)
- More applications (email, IRC, games)
- Better filesystem (journaling)
- SMP support (multi-core)
- 64-bit mode

### AurionGL Improvements
- Vertex buffer objects (VBO)
- Display lists
- Mipmapping
- Fog effects
- SIMD optimizations

---

## Resources

### Documentation
- `README.md` - User guide
- `CHANGELOG.md` - Version history
- `COMMANDS.md` - Command reference
- `AurionGL/README.md` - 3D graphics API
- `AurionGL/INTEGRATION.md` - Integration guide

### External Resources
- [OSDev Wiki](https://wiki.osdev.org) - OS development
- [NASM Manual](https://www.nasm.us/doc/) - Assembly syntax
- [Intel Manuals](https://www.intel.com/sdm) - x86 architecture
- [VESA VBE](https://en.wikipedia.org/wiki/VESA_BIOS_Extensions) - Graphics

---

## Contact

- **Issues**: GitHub Issues
- **Discussions**: GitHub Discussions
- **Web Demo**: https://aurionos.vercel.app

---

## License

See LICENSE file for details.

---

**Last Updated**: 2026-04-11

This guide is maintained alongside the AurionOS codebase. When making significant architectural changes, update this document to reflect the new structure.

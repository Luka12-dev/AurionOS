# AurionOS Architecture

This document describes the internal architecture of AurionOS - a complete operating system built from scratch in x86 assembly and C.

---

## System Overview

AurionOS is a 32-bit protected mode operating system for x86 architecture. It boots on real hardware and virtual machines (QEMU, VMware, VirtualBox). The system is organized into distinct layers:

```
+---------------------------+
|     User Applications     |
|   (Browser, Terminal...)  |
+---------------------------+
|    Window Manager / GUI    |
+---------------------------+
|    AurionGL / Blaze       |
|    (3D Graphics/Browser) |
+---------------------------+
|   Kernel Services         |
|  (Syscalls, FS, Network)  |
+---------------------------+
|   Hardware Drivers        |
| (VBE, ATA, NE2000, etc)  |
+---------------------------+
|  x86 Assembly Kernel      |
| (Boot, IDT, Memory, VFS)  |
+---------------------------+
```

---

## Boot Process

```
BIOS ROM
   |
   v
[Boot Sector - 512 bytes]
   - Loads kernel from LBA 1+
   - Switches to 32-bit protected mode
   - Jumps to kernel entry
   v
[Kernel Entry - kernel.asm]
   - Sets up GDT, IDT
   - Initializes PIC (IRQ controllers)
   - Initializes memory manager
   - Initializes VESA graphics OR VGA text mode
   v
[Desktop/Shell]
   - GUI Mode: VESA graphics -> Window Manager -> Desktop
   - DOS Mode: VGA text -> Shell
```

### Boot Modes

The kernel checks `boot_mode_flag` (set by bootloader):

- **GUI Mode (0)**: Initializes VESA graphics, loads desktop environment
- **DOS Mode (1)**: Uses VGA text mode, starts command shell

### Memory Layout

```
0x00007C00 - 0x00007FFF    Boot sector (512 bytes)
0x00010000 - 0x0009FFFF    Kernel code + data (loaded here by bootloader)
0x000A0000 - 0x000BFFFF    VGA/Video memory
0x000C0000 - 0x000FFFFF    BIOS/ROM area
0x00100000+                  Extended memory (heap/malloc region)
```

---

## Kernel Architecture

### Assembly Components (src/*.asm)

| File | Purpose |
|------|---------|
| `bootload.asm` | Stage 1 bootloader - loads kernel from disk |
| `kernel.asm` | Kernel entry, GDT/IDT setup, interrupt handlers |
| `memory.asm` | Physical/virtual memory management, kmalloc/kfree |
| `filesys.asm` | Custom filesystem implementation |
| `io.asm` | I/O port utilities, string functions |
| `interrupt.asm` | IRQ handlers, PIC initialization |
| `vesa.asm` | VBE graphics mode switching |

### C Components (src/*.c)

| File | Purpose |
|------|---------|
| `syscall.c` | System call interface layer |
| `commands.c` | Shell commands implementation |
| `shell.c` | DOS-mode command shell |
| `terminal.c` | GUI terminal emulator |
| `window_manager.c` | macOS-style window management |
| `desktop.c` | Desktop environment |
| `menu_bar.c` | Top menu bar |
| `gui_apps.c` | Built-in applications |
| `handlers.c` | Event handlers |
| `pci.c` | PCI bus enumeration |
| `panic.c` | Kernel panic handling |
| `utils.c` | General utilities |

---

## Filesystem

### Custom Filesystem (Primary)

The system uses a custom filesystem stored on the HDD image:

```
+------------------------+
|   Boot Sector (512B)   |
+------------------------+
|   Superblock           |
|   (filesystem metadata) |
+------------------------+
|   Inode Table          |
|   (file entries)       |
+------------------------+
|   Data Blocks          |
|   (file content)       |
+------------------------+
```

- **Location**: LBA 1000+ on HDD image
- **Max files**: 128
- **Max filename**: 56 characters
- **Features**: Directory support, file attributes, persistent storage

### FAT32 Support (Planned)

Read-only FAT32 compatibility for USB drives:

```
+------------------------+
|   BPB (BIOS ParBlock)  |
+------------------------+
|   FSInfo Sector        |
+------------------------+
|   Backup Boot Sector   |
+------------------------+
|   FAT (File Alloc Tbl) |
+------------------------+
|   Root Directory       |
+------------------------+
|   Data Region          |
+------------------------+
```

- **Status**: Read-only implementation planned
- **Supported**: FAT12, FAT16, FAT32
- **Use case**: Access USB drives formatted with standard Windows FAT

### ISO9660 (CD-ROM)

- Used for live ISO boot
- Rock Ridge extensions for long filenames
- El Torito bootable ISO specification

---

## Network Stack

### Architecture

```
+---------------------------+
|   Application Layer       |
|   (HTTP, HTTPS, DNS)      |
+---------------------------+
|   Transport Layer         |
|   (TCP, UDP)              |
+---------------------------+
|   Network Layer          |
|   (IPv4, DHCP)            |
+---------------------------+
|   Data Link Layer         |
|   (Ethernet, WiFi)        |
+---------------------------+
|   Device Drivers          |
| (RTL8139, NE2000, VirtIO)|
+---------------------------+
```

### Components (src/Network/)

| File | Purpose |
|------|---------|
| `tcp_ip_stack.c` | TCP/IP state machine |
| `dhcp_client.c` | DHCP address acquisition |
| `http_client.c` | Basic HTTP client |
| `https_client.c` | HTTPS with TLS 1.2 |
| `tls12_client.c` | TLS 1.2 implementation |
| `network_bringup.c` | Network initialization |
| `virtio_net.c` | VirtIO network driver |
| `wifi_driver.c` | WiFi driver interface |
| `wifi_pci.c` | WiFi PCI enumeration |

### Device Support

- **RTL8139**: Realtek 10/100Mbit NIC
- **NE2000**: NE2000 compatible NIC
- **VirtIO**: Virtualized network (QEMU/KVM)
- **WiFi**: PCI WiFi adapters (limited)

---

## Window Manager

The window manager provides a macOS-inspired GUI:

### Window Structure

```
+----------------------------------+
|  [Traffic Lights]  Title Bar     |  <- Title bar (30px)
+----------------------------------+
|                                  |
|     Client Area                  |
|     (Application content)        |
|                                  |
+----------------------------------+
|  [Icon] [Icon] ...    Taskbar    |  <- Dock (50px)
+----------------------------------+
```

### Features

- **Vibrancy**: Blur effect on focused window (glass material)
- **Shadows**: Soft shadows with alpha blending
- **Drag & Drop**: Move windows by title bar
- **Resize**: Drag edges/corners to resize
- **Minimize/Maximize/Close**: Traffic light buttons
- **Multi-window**: Multiple overlapping windows
- **Desktop Icons**: File/folder icons on desktop

### GPU Acceleration

Uses VBE 2.0+ for graphics:

- **Back buffer**: Off-screen rendering
- **Blitting**: GPU_memcpy to front buffer
- **Blur**: Box blur approximation for vibrancy

---

## AurionGL 3D Graphics

### Pipeline

```
Vertex Data
    |
    v
[ModelView Matrix] --> [Projection Matrix]
    |
    v
[Clip Space] --> [Viewport Transform]
    |
    v
[Rasterization] --> [Fragment Processing]
    |
    v
[Depth Test] --> [Blending] --> [Framebuffer]
```

### Features

- **Matrix Stack**: Model, View, Projection matrices
- **Depth Buffering**: Z-test with LESS/LINEAR functions
- **Backface Culling**: Front/back face culling
- **Lighting**: Ambient, diffuse, specular (Phong model)
- **Textures**: 2D textures with bilinear filtering
- **Primitives**: Points, lines, triangles, quads
- **Scanline Rasterization**: Software rendering

### Files

| File | Purpose |
|------|---------|
| `AurionGL/auriongl.h` | Public API definitions |
| `AurionGL/auriongl.c` | Full implementation |
| `AurionGL/example.c` | Usage example |

---

## Blaze Browser Engine

### Components

```
HTML Input
    |
    v
[HTML Parser] --> [DOM Tree]
    |
    v
[CSS Parser] --> [CSS Rules]
    |
    v
[Layout Engine] --> [Box Model]
    |
    v
[Render] --> [Framebuffer]
```

### Features

- **HTML5**: Basic HTML parsing
- **CSS2.1**: Stylesheet parsing and application
- **CSS Flexbox**: Flexible box layout
- **CSS Grid**: Grid layout (NEW)
- **JavaScript**: Basic JS execution engine
- **HTTP/HTTPS**: Network requests
- **Bookmarks**: Saved URLs

### Files (src/Blaze/)

| File | Purpose |
|------|---------|
| `blaze.h` | Main header, types, constants |
| `blaze_html.c` | HTML parser |
| `blaze_css.c` | CSS parser and style application |
| `blaze_layout.c` | Layout engine (Flexbox, Grid) |
| `blaze_render.c` | Rendering to framebuffer |
| `blaze_js.c` | JavaScript engine |
| `blaze_net.c` | Network requests |
| `blaze_app.c` | Browser application |

---

## Drivers

### Video

| Driver | Purpose |
|--------|---------|
| `vbe_graphics.c` | VBE 2.0+ graphics mode |
| `vmware_svga.c` | VMware SVGA driver |

### Storage

| Driver | Purpose |
|--------|---------|
| `ata.c` | ATA/IDE PIO mode driver |
| `iso9660.c` | ISO9660 CD-ROM driver |

### Network

| Driver | Purpose |
|--------|---------|
| `rtl8139.c` | Realtek RTL8139 NIC |
| `ne2000.c` | NE2000 compatible NIC |
| `virtio_net.c` | VirtIO network |

### Input

| Driver | Purpose |
|--------|---------|
| `mouse.c` | PS/2 mouse driver |

### Image

| Driver | Purpose |
|--------|---------|
| `png.c` | PNG decoder |
| `bmp.c` | BMP decoder |

---

## Build System

### Makefile Targets

```bash
make all          # Standard build (floppy + ISO + HDD)
make all-debug    # Debug build (skips installer)
make run          # Run in QEMU
make run-iso      # Run ISO in QEMU
make clean        # Clean build artifacts (preserves HDD)
make clean-all    # Full clean including HDD
```

### Build Output

| File | Description |
|------|-------------|
| `build/bootload.bin` | 512-byte boot sector |
| `build/kernel.bin` | Kernel binary |
| `build/aurionos.img` | 1.44MB floppy image |
| `build/aurionos.iso` | Bootable ISO |
| `build/aurionos_hdd.img` | 60MB HDD image |

### Platform Support

- **Windows**: WSL2 with Linux tools + Windows QEMU
- **Linux**: Native build with GCC
- **macOS**: Native build with GCC/LLVM

---

## Application Lifecycle

### Launching Apps

1. User clicks icon in dock/desktop
2. Window Manager creates Window structure
3. Application init function called
4. App registers event handlers
5. Main loop: process events, render, repeat

### Event Flow

```
Hardware Interrupt (IRQ)
    |
    v
Interrupt Handler (IDT entry)
    |
    v
drivers/mouse.c / keyboard handler
    |
    v
Event Queue
    |
    v
Window Manager
    |
    v
Target Application Window
    |
    v
Application Handler
```

---

## Future Improvements

1. **FAT32 Read/Write**: Enable writing to FAT32 USB drives
2. **Python Port**: MicroPython or simplified CPython
3. **Audio**: AC97/Sound Blaster 16 drivers
4. **USB**: OHCI/EHCI/xHCI USB host drivers
5. **Multi-threading**: Preemptive scheduling
6. **Ext2/Ext3**: Linux filesystem support
7. **WebGL**: Hardware-accelerated 3D in browser

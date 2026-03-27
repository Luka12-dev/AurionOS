# Changelog

All notable changes to AurionOS are documented in this file.

## [1.0.0] - 2026-03-27

### Release Notes

This is the first production release of AurionOS. The system is stable, feature-complete, and ready for real-world use.

### What's New in 1.0

#### Core System
- **32-bit Protected Mode Kernel** - Fully custom x86 kernel with no external dependencies
- **VESA Graphics** - Hardware-accelerated graphics with support for multiple resolutions
- **Multi-Platform Support** - Runs on QEMU, VMware, VirtualBox, and real hardware
- **Persistent Storage** - Full filesystem with read/write support for configuration and user data

#### Desktop Environment
- **Modern GUI** - Clean, responsive desktop with window management
- **Window Manager** - Full-featured with dragging, resizing, minimize, maximize, close
- **Taskbar & Dock** - Quick access to applications with visual feedback
- **Desktop Icons** - Drag-and-drop file management on desktop
- **Multiple Resolutions** - Automatic detection and support for 800x600, 1024x768, 1280x1024, 1920x1080

#### Applications
- **Terminal** - Full-featured command-line interface within GUI
- **File Manager** - Browse, create, delete files and folders
- **Text Editor (Notepad)** - Edit text files with syntax awareness
- **Paint** - Pixel-perfect drawing application with tools and colors
- **Calculator** - Standard and scientific calculator modes
- **Clock** - Real-time clock display with date
- **System Info** - Hardware detection and system statistics
- **Snake Game** - Classic snake game with scoring
- **Blaze Browser** - Custom web browser with HTML/CSS rendering and JavaScript support

#### Blaze Browser Features
- HTML5 parsing and rendering
- CSS styling with selectors, colors, fonts, layouts
- JavaScript execution engine
- HTTP/HTTPS support
- Bookmark management
- Tab support
- Local file:// protocol support

#### DOS Mode
- **Classic CLI** - Full DOS-style command-line interface
- **DOSMODE Command** - Switch between GUI and CLI seamlessly
- **GUIMODE Command** - Return to GUI from CLI
- **Command History** - Arrow key navigation through previous commands
- **Tab Completion** - File and command completion

#### Developer Tools
- **Python Interpreter** - Embedded Python 0.2 with basic standard library
- **Make System** - Build automation with Makefile support
- **Text Processing** - grep, sed, awk-like utilities
- **Network Testing** - ping, netstat, ifconfig equivalents

#### Input Devices
- **PS/2 Mouse** - Full support with acceleration and sensitivity control
- **USB Mouse** - UHCI, OHCI, EHCI, xHCI controller support
- **VMware VMMouse** - Absolute positioning in VMware
- **Keyboard** - Full keyboard support with special keys

#### Installer
- **Guided Setup** - Step-by-step installation wizard
- **App Selection** - Choose which applications to install
- **Keyboard Layout** - English and Serbian layouts
- **Persistent Install** - Save configuration to disk

### Fixed Issues

#### Mouse System
- Fixed mouse acceleration causing freeze on slow movement
- Implemented sub-pixel accumulation for smooth tracking at all speeds
- Adjusted sensitivity to 75% for comfortable cursor control
- Eliminated jitter and stuttering during precise movements

#### Video System
- Fixed DOSMODE crash in VMware by properly disabling SVGA
- Eliminated random symbols on terminal boot
- Improved text mode switching with proper hardware delays
- Fixed cursor visibility and positioning in text mode

#### Installer
- Removed confusing "Continue" button - now only shows "Reboot"
- Streamlined post-installation flow
- Improved visual feedback during installation

### Technical Improvements
- Optimized mouse driver with better hardware compatibility
- Enhanced VGA text mode restoration for all platforms
- Improved boot sequence with silent initialization
- Better memory management and heap allocation
- Reduced kernel size while adding features

### Platform Compatibility
- **QEMU** - Full support, recommended for development
- **VMware Workstation/Player** - Full support with SVGA acceleration
- **VirtualBox** - Full support with VBE graphics
- **Real Hardware** - PS/2 and USB mouse support, VGA/VESA graphics

### Known Limitations
- WiFi requires manual driver configuration
- Some USB 3.0 controllers may have compatibility issues
- Maximum resolution depends on graphics hardware
- Network stack does not support IPv6

### Build Information
- Kernel Size: ~412 KB
- Bootloader: 512 bytes
- Total Image: ~1.8 MB (ISO)
- Build System: GNU Make + NASM + GCC
- Target: i386 (32-bit x86)

---

## Future Roadmap

Potential features for future releases:
- USB storage device support
- Audio subsystem
- Multi-core CPU support
- 64-bit kernel option
- More network protocols
- Additional applications
- Plugin system for Blaze browser

---

**Note**: This is a hobby operating system project built for learning and experimentation. While stable and functional, it is not intended to replace production operating systems.
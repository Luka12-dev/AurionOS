BITS 32

; VESA mode initialization from protected mode.
; Uses Bochs VBE extensions (works on QEMU -vga std, VirtualBox VBoxVGA/VMSVGA,
; VMware with Bochs-compatible adapter).
; The PCI-based LFB detection in vbe_graphics.c handles finding the correct
; framebuffer address for all platforms.

global vesa_reinit_mode
global vesa_set_mode

%define VBE_DISPI_IOPORT_INDEX 0x01CE
%define VBE_DISPI_IOPORT_DATA  0x01CF
%define VBE_DISPI_INDEX_ID     0
%define VBE_DISPI_INDEX_XRES   1
%define VBE_DISPI_INDEX_YRES   2
%define VBE_DISPI_INDEX_BPP    3
%define VBE_DISPI_INDEX_ENABLE 4
%define VBE_DISPI_DISABLED     0x00
%define VBE_DISPI_ENABLED      0x01
%define VBE_DISPI_LFB_ENABLED  0x40

section .text

vesa_reinit_mode:
    push eax
    push edx
    push ebx

    ; Check if bootloader already set VBE mode (LFB address at 0x9000 is non-zero)
    cmp dword [0x9000], 0
    jne .done

    ; Set desired resolution in the boot info block at 0x9000
    mov word [0x9004], 1920     ; width
    mov word [0x9006], 1080     ; height
    mov byte [0x9008], 32       ; bpp
    mov byte [0x9009], 0
    mov word [0x900A], 0        ; pitch (calculated after VBE init)
    mov word [0x900C], 0

    ; Check if Bochs VBE is available by testing ID register
    mov dx, VBE_DISPI_IOPORT_INDEX
    mov ax, VBE_DISPI_INDEX_ID
    out dx, ax
    mov dx, VBE_DISPI_IOPORT_DATA
    in ax, dx
    cmp ax, 0xFFFF      ; Check for non-existent port (returns all 1s)
    je .no_bochs
    cmp ax, 0xB0C0
    jb .no_bochs

    ; Bochs VBE detected - program it
    ; Disable VBE first
    mov dx, VBE_DISPI_IOPORT_INDEX
    mov ax, VBE_DISPI_INDEX_ENABLE
    out dx, ax
    mov dx, VBE_DISPI_IOPORT_DATA
    mov ax, VBE_DISPI_DISABLED
    out dx, ax

    ; Set X resolution
    mov dx, VBE_DISPI_IOPORT_INDEX
    mov ax, VBE_DISPI_INDEX_XRES
    out dx, ax
    mov dx, VBE_DISPI_IOPORT_DATA
    mov ax, [0x9004]
    out dx, ax

    ; Set Y resolution
    mov dx, VBE_DISPI_IOPORT_INDEX
    mov ax, VBE_DISPI_INDEX_YRES
    out dx, ax
    mov dx, VBE_DISPI_IOPORT_DATA
    mov ax, [0x9006]
    out dx, ax

    ; Set BPP
    mov dx, VBE_DISPI_IOPORT_INDEX
    mov ax, VBE_DISPI_INDEX_BPP
    out dx, ax
    mov dx, VBE_DISPI_IOPORT_DATA
    movzx ax, byte [0x9008]
    out dx, ax

    ; Enable VBE with LFB
    mov dx, VBE_DISPI_IOPORT_INDEX
    mov ax, VBE_DISPI_INDEX_ENABLE
    out dx, ax
    mov dx, VBE_DISPI_IOPORT_DATA
    mov ax, VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED
    out dx, ax

    ; Default LFB address (QEMU standard, will be overridden by PCI scan)
    mov dword [0x9000], 0xFD000000

    ; Calculate pitch = width * 4
    movzx eax, word [0x9004]
    shl eax, 2
    mov word [0x900A], ax

    jmp .done

.no_bochs:
    ; No Bochs VBE - clear framebuffer.
    ; gpu_setup_framebuffer() in vbe_graphics.c will detect via PCI
    ; and fall back to VGA Mode 13h if nothing is found.
    mov dword [0x9000], 0

.done:
    pop ebx
    pop edx
    pop eax
    ret

; Legacy stub
vesa_set_mode:
    xor eax, eax
    ret
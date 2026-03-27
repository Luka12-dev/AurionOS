BITS 32

; VESA mode initialization from protected mode.
; Uses Bochs VBE extensions (works on QEMU -vga std, VirtualBox VBoxVGA/VMSVGA,
; VMware with Bochs-compatible adapter).

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

    ; 1. Sanity check or set defaults
    cmp word [0x9004], 640
    jb .set_defaults
    cmp word [0x9006], 480
    jb .set_defaults
    jmp .step2

.set_defaults:
    mov word [0x9004], 1024
    mov word [0x9006], 768
    mov byte [0x9008], 32
    mov byte [0x9009], 0
    mov word [0x900A], 0
    mov word [0x900C], 0

.step2:
    ; 2. Check Bochs presence
    mov dx, VBE_DISPI_IOPORT_INDEX
    mov ax, VBE_DISPI_INDEX_ID
    out dx, ax
    mov dx, VBE_DISPI_IOPORT_DATA
    in ax, dx
    cmp ax, 0xFFFF
    je .no_bochs
    cmp ax, 0xB0C0
    jb .no_bochs

    ; 3. Program hardware registers every time to ensure they match [0x9004/6]
    ; Disable VBE
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

    ; Enable VBE + LFB
    mov dx, VBE_DISPI_IOPORT_INDEX
    mov ax, VBE_DISPI_INDEX_ENABLE
    out dx, ax
    mov dx, VBE_DISPI_IOPORT_DATA
    mov ax, VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED
    out dx, ax

    ; Read back virtual width (for pitch calculation)
    mov dx, VBE_DISPI_IOPORT_INDEX
    mov ax, 0x06  ; VBE_DISPI_INDEX_VIRT_WIDTH
    out dx, ax
    mov dx, VBE_DISPI_IOPORT_DATA
    in ax, dx
    
    ; Calculate pitch: virt_width * (bpp / 8)
    movzx ebx, ax
    movzx eax, byte [0x9008]
    shr eax, 3  ; bpp / 8
    imul eax, ebx
    mov [0x900A], ax  ; Store pitch at offset 10
    mov [0x900C], ax  ; Store lin_pitch at offset 12

    ; Ensure LFB default address is set in info block if missing
    cmp dword [0x9000], 0
    jne .done
    mov dword [0x9000], 0xFD000000
    jmp .done

.no_bochs:
    ; If no Bochs VBE, ensure we don't try to use hardware if FB is missing
    cmp dword [0x9000], 0
    jne .done
    ; (vbe_graphics.c will fall back to VGA)

.done:
    pop ebx
    pop edx
    pop eax
    ret

vesa_set_mode:
    xor eax, eax
    ret

BITS 16
org 0x7C00

%ifndef KERNEL_LBA
%define KERNEL_LBA 1
%endif
%ifndef KERNEL_DEST
%define KERNEL_DEST 0x10000
%endif
%ifndef KERNEL_SECTORS
%define KERNEL_SECTORS 64
%endif
%define KERNEL_SEG (KERNEL_DEST >> 4)

start:
    jmp short main
    nop

    ; BPB (Bios Parameter Block)
    db "AURIONOS"
    dw 512, 1, 1, 2, 224, 2880, 0xF0, 9
spt: dw 18
nhd: dw 2
    dd 0, 0
drv: db 0
    db 0, 0x29
    dd 0x12345678
    db "AURION OS  "
    db "FAT12   "

main:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
    mov [drv], dl

    ; Load Kernel
    mov word [flba], KERNEL_LBA
    mov ax, KERNEL_SEG
    mov [curseg], ax
    mov cx, KERNEL_SECTORS
.fl:
    push cx
    mov ax, [flba]
    xor dx, dx
    div word [spt]
    inc dl
    mov cl, dl
    xor dx, dx
    div word [nhd]
    mov ch, al
    mov dh, dl
    mov dl, [drv]
    mov es, [curseg]
    xor bx, bx
    mov ax, 0x0201
    int 0x13
    jc err_halt
    pop cx
    inc word [flba]
    add word [curseg], 32
    loop .fl

    ; VBE Mode Setup (Optimized)
    xor ax, ax
    mov es, ax
    mov di, 0x9000
    mov cx, 16
    rep stosb

    mov ax, 0x4F00
    mov di, 0x8000
    int 0x10
    cmp ax, 0x004F
    jne .vbe_fail

    mov si, [0x800E]
    mov fs, [0x8010]

.next_mode:
    mov bx, [fs:si]
    add si, 2
    cmp bx, 0xFFFF
    je .vbe_done

    mov ax, 0x4F01
    mov cx, bx
    mov di, 0x8200
    int 0x10
    cmp ax, 0x004F
    jne .next_mode

    ; Check LFB + Color
    mov al, [0x8200]
    test al, 0x80
    jz .next_mode
    cmp byte [0x8219], 24
    jb .next_mode

    ; Filter: 1920x1080?
    cmp word [0x8212], 1920
    jne .check_720
    cmp word [0x8214], 1080
    je .found_1080

.check_720:
    ; 1280x720?
    cmp word [0x8212], 1280
    jne .check_768
    cmp word [0x8214], 720
    je .found_720

.check_768:
    ; 1024x768?
    cmp word [0x8212], 1024
    jne .check_800
    cmp word [0x8214], 768
    je .found_768

.check_800:
    ; 800x600?
    cmp word [0x8212], 800
    jne .check_640
    cmp word [0x8214], 600
    je .found_800

.check_640:
    ; 640x480?
    cmp word [0x8212], 640
    jne .next_mode
    cmp word [0x8214], 480
    jne .next_mode

.found_640:
    mov cx, 5
    jmp .try_save
.found_800:
    mov cx, 10
    jmp .try_save
.found_768:
    mov cx, 20
    jmp .try_save
.found_720:
    mov cx, 30
    jmp .try_save
.found_1080:
    mov cx, 100

.try_save:
    ; Compare "score" in CX with current best score in [vmode_score]
    cmp cx, [vmode_score]
    jbe .next_mode

    ; Save this as the new best mode
    mov [vmode_score], cx
    mov eax, [0x8228]
    mov [0x9000], eax       ; LFB address
    mov ax, [0x8212]
    mov [0x9004], ax       ; Width
    mov ax, [0x8214]
    mov [0x9006], ax       ; Height
    mov al, [0x8219]
    mov [0x9008], al       ; BPP
    mov ax, [0x8210]
    mov [0x900A], ax       ; Pitch
    mov [vmode], bx        ; VBE Mode Number
    
    ; If it's 1080, we are done searching (highest possible score)
    cmp cx, 100
    je .vbe_done
    jmp .next_mode

.vbe_done:
    cmp word [0x9004], 0
    je .vbe_fail
    
    mov ax, 0x4F02
    mov bx, [vmode]
    or bx, 0x4000
    int 0x10
    jmp go_pm

.vbe_fail:
    mov dword [0x9000], 0

go_pm:
    cli
    in al, 0x92
    or al, 2
    out 0x92, al
    lgdt [gdtp]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:pm_entry

err_halt:
    hlt
    jmp $

BITS 32
pm_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov esp, 0x1F0000
    jmp KERNEL_DEST

BITS 16
gdt:
    dq 0
    dq 0x00CF9A000000FFFF ; Code
    dq 0x00CF92000000FFFF ; Data
gdte:
gdtp:
    dw gdte - gdt - 1
    dd gdt

curseg: dw 0
flba:   dw 0
vmode:  dw 0
vmode_score: dw 0

times 510-($-$$) db 0
dw 0xAA55
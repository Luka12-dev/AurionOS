BITS 16
org 0x7C00

; ------------------------------------------------------------------
; Boot parameters - override these from the build script with -D flags
; ------------------------------------------------------------------
%ifndef KERNEL_LBA
%define KERNEL_LBA 1
%endif
%ifndef KERNEL_DEST
%define KERNEL_DEST 0x10000
%endif
%ifndef KERNEL_SECTORS
%define KERNEL_SECTORS 800
%endif
%define KERNEL_SEG (KERNEL_DEST >> 4)

; ------------------------------------------------------------------
; FAT12 BPB - required for El Torito floppy emulation.
; The BIOS checks the BPB to determine geometry for INT 13h emulation.
; Values must match the floppy image geometry: 1.44MB, 18 spt, 2 heads.
; ------------------------------------------------------------------
    jmp short main
    nop

; OEM Name (8 bytes)
    db "AURIONOS"
; BPB_BytsPerSec (2)
    dw 512
; BPB_SecPerClus (1)
    db 1
; BPB_RsvdSecCnt (2)
    dw 1
; BPB_NumFATs (1)
    db 2
; BPB_RootEntCnt (2)
    dw 224
; BPB_TotSec16 (2)
    dw 2880
; BPB_Media (1)
    db 0xF0
; BPB_FATSz16 (2)
    dw 9
; BPB_SecPerTrk (2) = sectors per track
spt: dw 18
; BPB_NumHeads (2) = number of heads
nhd: dw 2
; BPB_HiddSec (4)
    dd 0
; BPB_TotSec32 (4)
    dd 0
; BS_DrvNum (1) - BIOS may overwrite this with DL=0x00
drv: db 0
; BS_Reserved1 (1)
    db 0
; BS_BootSig (1)
    db 0x29
; BS_VolID (4)
    dd 0x12345678
; BS_VolLab (11)
    db "AURION OS  "
; BS_FilSysType (8)
    db "FAT12   "

; ------------------------------------------------------------------
; Bootloader entry point
; ------------------------------------------------------------------
main:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    ; Stack just below the bootloader - grows downward safely into free RAM
    mov sp, 0x7C00
    sti

    ; Save the drive number the BIOS gave us.
    ; In El Torito floppy emulation mode this is always 0x00 (virtual floppy A:).
    ; In El Torito no-emulation mode this would be the CD-ROM (0xE0 or similar).
    ; We only support floppy emulation now so we force DL=0x00 here to be safe.
    mov byte [drv], 0x00

    ; Clear our VBE result storage area at 0x9000 (32 bytes).
    push es
    push di
    xor ax, ax
    mov di, 0x9000
    mov cx, 16
    rep stosw
    pop di
    pop es

    ; Set up the read pointer: start at kernel LBA sector, load into KERNEL_DEST
    mov dword [lba], KERNEL_LBA
    mov word  [cseg], KERNEL_SEG
    mov cx, KERNEL_SECTORS

.read_loop:
    push cx

    ; Convert flat LBA to CHS for INT 13h
    ; cylinder = lba / (spt * nhd)
    ; head     = (lba / spt) % nhd
    ; sector   = (lba % spt) + 1
    mov ax, [lba]
    xor dx, dx
    div word [spt]          ; ax = lba/spt, dx = lba%spt
    inc dx                  ; sector is 1-based
    mov cl, dl              ; CL = sector number
    xor dx, dx
    div word [nhd]          ; ax = cylinder, dx = head
    mov ch, al              ; CH = cylinder (low 8 bits)
    mov dh, dl              ; DH = head

    ; Set up read: DL = drive (0x00 = floppy), ES:BX = destination
    mov dl, byte [drv]
    mov es, word [cseg]
    xor bx, bx

    ; INT 13h Read - retry up to 3 times on failure
    mov si, 3
.retry:
    mov ax, 0x0201          ; AH=02 (read), AL=01 (1 sector)
    int 0x13
    jnc .ok                 ; carry clear = success
    dec si
    jz  halt                ; tried 3 times, give up
    ; Reset the floppy drive before retrying
    xor ax, ax
    int 0x13
    jmp .retry

.ok:
    inc dword [lba]
    add word [cseg], 32     ; advance segment by 512 bytes (32 paragraphs)
    pop cx
    loop .read_loop

    ; ------------------------------------------------------------------
    ; Kernel is loaded. Now detect the best VESA video mode.
    ; Results stored at physical 0x9000 for the kernel to read.
    ; ------------------------------------------------------------------

    ; Reset ES back to 0 for VBE calls (int 10h uses ES:DI)
    xor ax, ax
    mov es, ax

    ; Get VESA BIOS info block into 0x8000
    mov di, 0x8000
    mov dword [di], 'VBE2'
    mov ax, 0x4F00
    int 0x10
    cmp ax, 0x004F
    jne .enter_pm           ; No VBE - just go with text mode

    ; Walk the mode list (pointer at word [0x800E] = seg:off)
    lfs si, [0x800E]
    xor bx, bx
    mov [best_rank], bx

.next_mode:
    mov cx, [fs:si]
    add si, 2
    cmp cx, 0xFFFF
    je  .find_done

    ; Get mode info for this mode into 0x8200
    mov di, 0x8200
    mov ax, 0x4F01
    int 0x10
    cmp ax, 0x004F
    jne .next_mode

    ; Must have: linear framebuffer support (bit 7 of attributes)
    test byte [0x8200], 0x80
    jz  .next_mode

    ; Must be at least 24-bit color
    cmp byte [0x8219], 24
    jb  .next_mode

    ; Rank by resolution: prefer 1920x1080, else by pixel count
    mov ax, [0x8212]        ; mode width
    mov dx, [0x8214]        ; mode height
    cmp ax, 1920
    jne .pix
    cmp dx, 1080
    jne .pix
    mov eax, 0x1000000      ; huge rank - always prefer native 1080p
    jmp .compare

.pix:
    mul dx                  ; dx:ax = width * height
    shl edx, 16
    mov dx, ax
    mov eax, edx

.compare:
    cmp eax, [best_rank]
    jbe .next_mode
    mov [best_rank], eax
    mov [best_mode], cx

    ; Store useful mode info for the kernel at 0x9000:
    ;   [0x9000] = framebuffer physical address (dword)
    ;   [0x9004] = width | height packed (dword: lo=width, hi=height)
    ;   [0x9008] = bits per pixel (byte)
    ;   [0x900A] = bytes per scan line / pitch (word)
    mov eax, [0x8228]
    mov [0x9000], eax
    mov eax, [0x8212]
    mov [0x9004], eax
    mov al, [0x8219]
    mov [0x9008], al
    mov ax, [0x8210]
    mov [0x900A], ax
    mov ax, [0x8232]
    mov [0x900C], ax
    jmp .next_mode

.find_done:
    ; Set the best mode (with linear framebuffer flag = bit 14)
    mov bx, [best_mode]
    or  bx, bx
    jz  .enter_pm
    mov ax, 0x4F02
    or  bx, 0x4000
    int 0x10

.enter_pm:
    ; ------------------------------------------------------------------
    ; Detect physical RAM size using BIOS INT 15h, AX=E801h
    ; ------------------------------------------------------------------
    mov ax, 0xE801
    int 0x15
    jc  .mem_fallback

    test cx, cx
    jnz .use_cx_dx
    mov cx, ax
    mov dx, bx
.use_cx_dx:
    movzx eax, dx
    shl eax, 6
    add ax, cx
    add eax, 1024
    jmp .mem_done

.mem_fallback:
    mov eax, 65536         ; 64 MB fallback
.mem_done:
    mov [0x9010], eax      ; Store KB total at 0x9010
    ; ------------------------------------------------------------------
    ; Enter 32-bit protected mode and jump to kernel
    ; ------------------------------------------------------------------
    cli
    ; Enable A20 line via fast port (most reliable)
    in  al, 0x92
    or  al, 2
    out 0x92, al

    lgdt [gdtp]

    mov eax, cr0
    or  eax, 1
    mov cr0, eax

    jmp 0x08:pm32

halt:
    hlt
    jmp halt

; ------------------------------------------------------------------
; 32-bit protected mode entry
; ------------------------------------------------------------------
BITS 32
pm32:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x1F0000       ; Stack at 1MB + 960KB - well above BSS at 2MB
    jmp KERNEL_DEST

; ------------------------------------------------------------------
; GDT - flat 4GB code and data segments
; ------------------------------------------------------------------
BITS 16
gdt:
    dq 0                            ; null descriptor
    dq 0x00CF9A000000FFFF           ; 0x08: code, 32-bit, ring 0, 4GB
    dq 0x00CF92000000FFFF           ; 0x10: data, 32-bit, ring 0, 4GB
gdtp:
    dw 23
    dd gdt

; Working variables
lba:       dd 0
cseg:      dw 0
best_mode: dw 0
best_rank: dd 0

times 510-($-$$) db 0
dw 0xAA55
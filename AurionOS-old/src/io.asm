BITS 32

section .data
align 4

screen_cols     dd 80
screen_rows     dd 25
video_base      dd 0xB8000
default_attr    db 0x07

cursor_row      dd 0
cursor_col      dd 0

VGA_INDEX       equ 0x3D4
VGA_DATA        equ 0x3D5
CURSOR_HIGH     equ 0x0E
CURSOR_LOW      equ 0x0F

; Serial Port Constants
PORT_COM1       equ 0x3F8

section .text
  global vga_putc
  global vga_puts
  global vga_cls
  global set_attr
  global io_wait
  global serial_init
  global serial_putc
  global serial_puts

; Initialize Serial Port COM1
serial_init:
    pusha
    mov dx, PORT_COM1 + 1    ; Interrupt Enable Register
    mov al, 0x00             ; Disable all interrupts
    out dx, al
    
    mov dx, PORT_COM1 + 3    ; Line Control Register
    mov al, 0x80             ; Enable DLAB (set baud rate divisor)
    out dx, al
    
    mov dx, PORT_COM1 + 0    ; Set divisor to 3 (38400 baud) - lo byte
    mov al, 0x03
    out dx, al
    
    mov dx, PORT_COM1 + 1    ;                               - hi byte
    mov al, 0x00
    out dx, al
    
    mov dx, PORT_COM1 + 3    ; Line Control Register
    mov al, 0x03             ; 8 bits, no parity, one stop bit
    out dx, al
    
    mov dx, PORT_COM1 + 2    ; FIFO Control Register
    mov al, 0xC7             ; Enable FIFO, clear them, with 14-byte threshold
    out dx, al
    
    mov dx, PORT_COM1 + 4    ; Modem Control Register
    mov al, 0x0B             ; IRQs enabled, RTS/DSR set
    out dx, al
    popa
    ret

; Write character to Serial Port
serial_putc:
    push ebp
    mov ebp, esp
    push eax
    push edx

    mov dx, 0x3FD            ; Line Status Register (COM1 + 5)
.wait_tx:
    in al, dx
    test al, 0x20            ; Check if transmit buffer is empty
    jz .wait_tx

    mov al, [ebp + 8]        ; Get char from stack
    mov dx, 0x3F8            ; Data Register (COM1 + 0)
    out dx, al

    pop edx
    pop eax
    pop ebp
    ret

; Write string to Serial Port
serial_puts:
    push ebp
    mov ebp, esp
    push esi
    push eax

    mov esi, [ebp + 8]       ; Argument string pointer
.loop:
    lodsb
    test al, al
    jz .done

    movzx eax, al            ; Zero extend AL to EAX
    push eax                 ; Push char (as 32-bit value)
    call serial_putc
    pop eax                  ; Clean stack
    jmp .loop

.done:
    pop eax
    pop esi
    pop ebp
    ret
global io_init
global io_set_attr
global set_cursor_pos
global set_cursor_hardware
global hide_cursor
global cursor_row
global cursor_col

; Hide hardware cursor (move it off-screen)
hide_cursor:
    push eax
    push edx
    
    ; Set cursor to position 2000 (off screen - 80*25 = 2000)
    mov dx, VGA_INDEX
    mov al, CURSOR_HIGH
    out dx, al
    mov dx, VGA_DATA
    mov al, 0x07  ; High byte of 2000 (0x07D0)
    out dx, al
    
    mov dx, VGA_INDEX
    mov al, CURSOR_LOW
    out dx, al
    mov dx, VGA_DATA
    mov al, 0xD0  ; Low byte of 2000
    out dx, al
    
    pop edx
    pop eax
    ret

; Hardware Cursor Update
set_cursor_hardware:
    push eax
    push ebx
    push edx

    mov eax, [cursor_row]
    imul eax, dword [screen_cols]
    add eax, [cursor_col]
    mov ebx, eax

    mov dx, VGA_INDEX
    mov al, CURSOR_HIGH
    out dx, al
    mov dx, VGA_DATA
    mov al, bh
    out dx, al

    mov dx, VGA_INDEX
    mov al, CURSOR_LOW
    out dx, al
    mov dx, VGA_DATA
    mov al, bl
    out dx, al

    pop edx
    pop ebx
    pop eax
    ret

; Init
io_init:
    pusha
    mov dword [cursor_row], 0
    mov dword [cursor_col], 0
    call vga_cls
    call set_cursor_hardware
    popa
    ret

; Clear Screen
vga_cls:
    pusha
    mov edi, [video_base]
    mov ecx, 2000  ; 80x25 = 2000 characters
    mov ah, [default_attr]
    mov al, ' '
    rep stosw
    mov dword [cursor_row], 0
    mov dword [cursor_col], 0
    call set_cursor_hardware
    popa
    ret

; Scroll Up
scroll_up:
    pusha
    
    ; Capture top line to scrollback before scrolling
    extern scrollback_capture_line
    call scrollback_capture_line
    
    mov edi, [video_base]
    mov esi, [video_base]
    add esi, 160  ; One line = 80 chars * 2 bytes = 160
    mov ecx, 1920  ; 24 lines * 80 = 1920
    rep movsw
    
    mov edi, [video_base]
    add edi, 3840  ; 24 lines * 160 bytes = 3840
    mov ecx, 80
    mov ah, [default_attr]
    mov al, ' '
    rep stosw
    popa
    ret

; void vga_putc(char c)

vga_putc:
    push ebp
    mov ebp, esp
    push eax
    push ebx
    push ecx
    push edi

    mov al, byte [ebp + 8]

    cmp al, 0x0A
    je .nl
    cmp al, 0x0D
    je .cr
    cmp al, 0x08
    je .bs

    mov ecx, [cursor_row]
    imul ecx, 80
    add ecx, [cursor_col]
    shl ecx, 1
    mov edi, [video_base]
    add edi, ecx
    
    mov ah, [default_attr]
    mov [edi], ax
    
    inc dword [cursor_col]
    jmp .check_wrap

.bs:
    cmp dword [cursor_col], 0
    je .done
    dec dword [cursor_col]
    mov ecx, [cursor_row]
    imul ecx, 80
    add ecx, [cursor_col]
    shl ecx, 1
    mov edi, [video_base]
    add edi, ecx
    mov al, ' '
    mov ah, [default_attr]
    mov [edi], ax
    jmp .done

.cr:
    mov dword [cursor_col], 0
    jmp .done

.nl:
    mov dword [cursor_col], 0
    inc dword [cursor_row]
    jmp .check_scroll

.check_wrap:
    cmp dword [cursor_col], 80
    jl .done
    mov dword [cursor_col], 0
    inc dword [cursor_row]

.check_scroll:
    cmp dword [cursor_row], 25
    jl .done
    call scroll_up
    mov dword [cursor_row], 24

.done:
    call set_cursor_hardware
    pop edi
    pop ecx
    pop ebx
    pop eax
    leave
    ret

; void vga_puts(const char* s)

vga_puts:
    push ebp
    mov ebp, esp
    push esi
    push eax
    
    mov esi, [ebp+8]
.loop:
    mov al, [esi]
    test al, al
    jz .done
    push eax
    call vga_putc
    add esp, 4
    inc esi
    jmp .loop
.done:
    pop esi
    pop eax
    pop ebp
    ret

; Set Attribute
io_set_attr:
    push ebp
    mov ebp, esp
    mov al, [ebp+8]
    mov [default_attr], al
    leave
    ret

set_cursor_pos:
    ret

; Provide names expected by C code (commands.c, handlers.c)
; Only set_attr wrapper remains.
set_attr:
    jmp io_set_attr

; Small I/O wait (used by PIC remap). Traditional 0x80 port delay.
io_wait:
    push eax
    mov al, 0
    out 0x80, al
    pop eax
    ret
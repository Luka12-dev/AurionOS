BITS 32

extern timer_handler
extern syscall_handler
extern isr_handler
extern pic_remap

%define PIC1_CMD    0x20
%define PIC1_DATA   0x21
%define EOI         0x20

section .data
align 4
key_buffer_size equ 256
key_buffer:     times 256 dw 0  ; Changed to words (16-bit)
key_buffer_head dd 0
key_buffer_tail dd 0

kb_shift db 0
kb_caps  db 0
kb_ctrl  db 0
kb_e0    db 0  ; Extended key prefix flag

scan_to_ascii:
    db 0,27,'1','2','3','4','5','6','7','8','9','0','-','=',8,9
    db 'q','w','e','r','t','y','u','i','o','p','[',']',13,0
    db 'a','s','d','f','g','h','j','k','l',';',39,96,0,92
    db 'z','x','c','v','b','n','m',',','.','/',0,'*',0,' '
    times (128-104) db 0

scan_to_ascii_shift:
    db 0,27,'!','@','#','$','%','^','&','*','(',')','_','+',8,9
    db 'Q','W','E','R','T','Y','U','I','O','P','{','}',13,0
    db 'A','S','D','F','G','H','J','K','L',':',34,'~',0,'|'
    db 'Z','X','C','V','B','N','M','<','>','?',0,'*',0,' '
    times (128-104) db 0

pgup_msg        db "[PgUp]", 10, 0
pgdn_msg        db "[PgDn]", 10, 0

global ticks
ticks           dd 0

mouse_buffer_size equ 256
mouse_buffer:     times 256 db 0
mouse_head      dd 0
mouse_tail      dd 0

section .text
global getkey_block
global getkey_block
global sys_getkey
global c_mouse_read
global init_interrupts
global syscall_stub

; Common ISR Stub
isr_common_stub:
    pushad
    push ds
    push es
    push fs
    push gs
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    push esp
    call isr_handler
    add esp, 4
    pop gs
    pop fs
    pop es
    pop ds
    popad
    add esp, 8
    iretd

; Common IRQ Stub
irq_common_stub:        
    pushad
    push ds
    push es
    push fs
    push gs
    mov ax, 0x10
    mov ds, ax
    mov es, ax

    mov eax, [esp + 48] ; Get Int No from stack

    ; Check if Mouse (IRQ 12 = INT 44)
    cmp eax, 44
    jne .check_keyboard
    
    ; Mouse IRQ - Buffer the data
    in al, 0x60         ; Read mouse data byte
    
    ; Store in buffer
    push ebx
    mov ebx, [mouse_head]
    mov [mouse_buffer + ebx], al
    inc ebx
    and ebx, mouse_buffer_size - 1
    mov [mouse_head], ebx
    pop ebx
    
    ; Send EOI to slave PIC (0xA0) then master (0x20)
    mov al, 0x20
    out 0xA0, al
    out 0x20, al
    jmp .irq_done_no_eoi

.check_keyboard:
    cmp eax, 33         ; Check if Keyboard (IRQ 1)
    jne .timer_check

    in al, 0x60         ; Read scancode
    
    ; Check if E0 prefix
    cmp al, 0xE0
    je .e0_prefix
    
    ; Check if we had an E0 prefix
    test byte [kb_e0], 1
    jnz .handle_e0
    
    ; Handle Release (Break) Codes for normal keys
    test al, 0x80
    jnz .handle_release

    ; Standard Key Handling
    cmp al, 0x2A ; L-Shift
    je .shift_on
    cmp al, 0x36 ; R-Shift
    je .shift_on
    cmp al, 0x1D ; Ctrl
    je .ctrl_on
    cmp al, 0x3A ; Caps Lock
    je .caps_toggle

    ; Check if Ctrl is pressed
    test byte [kb_ctrl], 1
    jz .no_ctrl
    ; Ctrl is pressed - handle Ctrl+Key combinations
    cmp bl, 0x2E ; Ctrl+C (scancode for C)
    je .ctrl_c
    ; Add other Ctrl combinations here if needed
    jmp .no_ctrl
    
.e0_prefix:
    mov byte [kb_e0], 1
    jmp .done_irq

.handle_e0:
    ; Clear prefix for extended keys
    mov byte [kb_e0], 0

    ; Ignore E0 key releases (bit 7 set)
    test al, 0x80
    jnz .done_irq
    
    ; Map Arrow Keys (Scan Code 2 map)
    ; Up: E0 48, Down: E0 50, Left: E0 4B, Right: E0 4D
    ; PgUp: E0 49, PgDn: E0 51
    
    cmp al, 0x48 ; Up
    je .arrow_up
    cmp al, 0x50 ; Down
    je .arrow_down
    cmp al, 0x4B ; Left
    je .arrow_left
    cmp al, 0x4D ; Right
    je .arrow_right
    cmp al, 0x49 ; PgUp
    je .page_up
    cmp al, 0x51 ; PgDn
    je .page_down
    
    jmp .done_irq ; Ignore other extended keys for now

.arrow_up:
    mov ah, 0x48 ; Scan code in high byte
    mov al, 0    ; No ASCII
    jmp .store_full_word
.arrow_down:
    mov ah, 0x50
    mov al, 0
    jmp .store_full_word
.arrow_left:
    mov ah, 0x4B
    mov al, 0
    jmp .store_full_word
.arrow_right:
    mov ah, 0x4D
    mov al, 0
    jmp .store_full_word

.page_up:
    ; Call scrollback for DOS mode VGA text scrollback
    pusha
    extern scrollback_scroll_up
    call scrollback_scroll_up
    popa
    ; Also store in key buffer so GUI terminal can use it
    mov ah, 0x49
    mov al, 0
    jmp .store_full_word

.page_down:
    pusha
    extern scrollback_scroll_down
    call scrollback_scroll_down
    popa
    mov ah, 0x51
    mov al, 0
    jmp .store_full_word

.ctrl_c:
    mov al, 3  ; ASCII 3 for Ctrl+C
    jmp .store
    
.no_ctrl:
    movzx ebx, al
    lea esi, [scan_to_ascii]
    test byte [kb_shift], 1
    jz .no_shift
    lea esi, [scan_to_ascii_shift]
.no_shift:
    mov al, [esi + ebx]
    test al, al
    jz .done_irq
    
    ; Shift XOR Caps logic for letter casing
    cmp al, 'a'
    jl .check_upper
    cmp al, 'z'
    jg .check_upper
    mov cl, [kb_shift]
    xor cl, [kb_caps]
    test cl, 1
    jz .store
    sub al, 32 ; Make Big
    jmp .store

.check_upper:
    cmp al, 'A'
    jl .store
    cmp al, 'Z'
    jg .store
    mov cl, [kb_shift]
    xor cl, [kb_caps]
    test cl, 1
    jz .store
    add al, 32 ; Make Small

.store:
    mov ah, 0 ; No scan code in high byte for ASCII keys yet (simplified)
.store_full_word:
    mov ebx, [key_buffer_head]
    shl ebx, 1 ; Multiply index by 2 for word access
    mov [key_buffer + ebx], ax
    shr ebx, 1 ; Restore index
    inc ebx
    and ebx, 255
    mov [key_buffer_head], ebx
    jmp .done_irq

.shift_on:
    mov byte [kb_shift], 1
    jmp .done_irq
.ctrl_on:
    mov byte [kb_ctrl], 1
    jmp .done_irq
.caps_toggle:
    xor byte [kb_caps], 1
    jmp .done_irq
.handle_release:
    and al, 0x7F
    cmp al, 0x2A
    je .shift_off
    cmp al, 0x36
    je .shift_off
    cmp al, 0x1D
    je .ctrl_off
    jmp .done_irq
.shift_off:
    mov byte [kb_shift], 0
    jmp .done_irq
.ctrl_off:
    mov byte [kb_ctrl], 0
    jmp .done_irq

.timer_check:
    push esp
    call timer_handler
    add esp, 4

.done_irq:
    mov al, 0x20
    out 0x20, al
.irq_done_no_eoi:       ; Used by handlers that already sent EOI
    pop gs
    pop fs
    pop es
    pop ds
    popad
    add esp, 8
    iretd

%macro ISR_NOERR 1
global isr%1
isr%1:
    push dword 0
    push dword %1
    jmp isr_common_stub
%endmacro

%macro IRQ_STUB 2
global irq%1
irq%1:
    push dword 0
    push dword %2
    jmp irq_common_stub
%endmacro

ISR_NOERR 0
ISR_NOERR 13
IRQ_STUB 0, 32
IRQ_STUB 1, 33
IRQ_STUB 12, 44  ; PS/2 Mouse (IRQ12 = INT 44)

getkey_block:
.wait:
    cli
    mov eax, [key_buffer_tail]
    cmp eax, [key_buffer_head]
    jne .ready
    sti
    hlt
    jmp .wait
.ready:
    mov eax, [key_buffer_tail]
    shl eax, 1 ; Multiply for word access
    movzx eax, word [key_buffer + eax] ; Read 16-bit key
    mov ebx, [key_buffer_tail]
    inc ebx
    and ebx, 255
    mov [key_buffer_tail], ebx
    sti
    ret

sys_getkey: jmp getkey_block

global sys_kb_hit
sys_kb_hit:
    mov eax, [key_buffer_tail]
    cmp eax, [key_buffer_head]
    jne .yes
    mov eax, 0
    ret
.yes:
    mov eax, 1
    ret

c_mouse_read:
    mov eax, [mouse_tail]
    cmp eax, [mouse_head]
    je .empty
    
    mov ebx, eax
    movzx eax, byte [mouse_buffer + ebx]
    
    inc ebx
    and ebx, mouse_buffer_size - 1
    mov [mouse_tail], ebx
    ret

.empty:
    mov eax, -1
    ret

syscall_stub:
    push dword 0
    push dword 0x80
    jmp isr_common_stub

install_isr:
    ; EAX = Vector, EBX = Addr, CL = Flags
    push edi
    mov edi, idt_table
    shl eax, 3
    add edi, eax
    mov [edi], bx       ; Low 16 bits
    mov word [edi+2], 0x08 ; Selector
    mov byte [edi+4], 0
    mov [edi+5], cl     ; Flags
    shr ebx, 16         ; Get High 16 bits
    mov [edi+6], bx
    pop edi
    ret

init_interrupts:
    ; Inline PIC remap instead of calling C function
    push eax
    
    ; ICW1 - start initialization
    mov al, 0x11
    out 0x20, al
    out 0x80, al  ; io_wait
    out 0xA0, al
    out 0x80, al
    
    ; ICW2 - set vector offsets
    mov al, 0x20
    out 0x21, al
    out 0x80, al
    mov al, 0x28
    out 0xA1, al
    out 0x80, al
    
    ; ICW3 - setup cascading
    mov al, 0x04
    out 0x21, al
    out 0x80, al
    mov al, 0x02
    out 0xA1, al
    out 0x80, al
    
    ; ICW4 - 8086 mode
    mov al, 0x01
    out 0x21, al
    out 0x80, al
    out 0xA1, al
    out 0x80, al
    
    ; Set masks
    mov al, 0xF8        ; Unmask IRQ0 (timer), IRQ1 (keyboard), IRQ2 (cascade)
    out 0x21, al
    out 0x80, al        ; io_wait
    mov al, 0xEF        ; Unmask IRQ12 (mouse)
    out 0xA1, al
    out 0x80, al

    
    pop eax
    
    mov eax, 0
    mov ebx, isr0
    mov cl, 0x8E
    call install_isr

    mov eax, 13
    mov ebx, isr13
    mov cl, 0x8E
    call install_isr

    mov eax, 32
    mov ebx, irq0
    mov cl, 0x8E
    call install_isr

    mov eax, 33
    mov ebx, irq1
    mov cl, 0x8E
    call install_isr

    mov eax, 44          ; IRQ12 = INT 44 (32 + 12)
    mov ebx, irq12
    mov cl, 0x8E
    call install_isr

    mov eax, 0x80
    mov ebx, syscall_stub
    mov cl, 0xEE
    call install_isr

    lidt [idt_desc]
    ret

align 16
idt_table: times 256 dq 0
idt_desc:
    dw 256*8-1
    dd idt_table
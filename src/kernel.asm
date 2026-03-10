[BITS 32]
[EXTERN init_interrupts]
[EXTERN io_init]
[EXTERN mem_init]
[EXTERN shell_main]
[EXTERN desktop_main]
[EXTERN get_ticks]
[EXTERN puts]
[EXTERN putc]
[EXTERN cls]
[EXTERN getkey_block]
[EXTERN io_set_attr]
[EXTERN c_puts]
[EXTERN set_attr]
[EXTERN vesa_reinit_mode]
[EXTERN serial_init]
[EXTERN serial_puts]
[EXTERN __bss_start]
[EXTERN __bss_end]

[GLOBAL kernel_entry]
[GLOBAL sys_reboot]
[GLOBAL boot_mode_flag]

section .data
; 0 = GUI mode (default), 1 = DOS/CLI mode
boot_mode_flag dd 0

section .text.start
kernel_entry:
    cli
    
    ; Set up segments
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Setup stack above BSS
    mov esp, 0x001F0000
    mov ebp, esp
    cld

    ; Zero out the .bss section
    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi
    shr ecx, 2      ; Number of dwords
    xor eax, eax
    rep stosd       ; Clear memory

    ; Mask PICs fully initially
    mov al, 0xFF
    out 0x21, al
    out 0xA1, al

    ; Initialize IDT/PIC
    call init_interrupts

    ; Init I/O (text mode console -- used for debug and DOS mode)
    call io_init

    ; Clear screen
    call cls

    push dword kernel_ok_msg
    call puts
    add esp, 4

    push dword system_ready_msg
    call puts
    add esp, 4


    ; Init memory - heap starts at 4MB (above stack region), 128MB size
    ; Stack is at 0x1F0000 growing down - heap starts well above at 0x400000
    mov eax, 0x00400000         ; Heap start at 4MB
    mov ebx, 0x08000000         ; 128MB heap
    call mem_init


    push dword mem_init_msg
    call puts
    add esp, 4

    push dword enabling_int_msg
    call puts
    add esp, 4

    ; --- Program PIT channel 0 to 100 Hz ---
    ; PIT base clock = 1,193,182 Hz. Divisor = 11932 = 0x2E9C (~100.0 Hz)
    ; Must be done AFTER sti so IRQ0 fires correctly, but we set it here
    ; before sti to avoid spurious ticks during setup.
    mov al, 0x36        ; Channel 0, lobyte/hibyte, mode 3 (square wave)
    out 0x43, al
    mov al, 0x00        ; Delay
    out 0x80, al
    
    mov al, 0x9C        ; low byte of 11932 (0x2E9C)
    out 0x40, al
    mov al, 0x00        ; Delay
    out 0x80, al
    
    mov al, 0x2E        ; high byte of 11932 (0x2E9C)
    out 0x40, al
    mov al, 0x00        ; Delay
    out 0x80, al

    ; Unmask timer, keyboard, cascade (IRQ0, IRQ1, IRQ2)
    mov al, 0xF8
    out 0x21, al
    ; Unmask IRQ12 on slave PIC for PS/2 mouse
    mov al, 0xEF
    out 0xA1, al

    sti


    ; Small delay for hardware settle
    mov ecx, 5000000
.delay:
    dec ecx
    jnz .delay


    ; Debug: Pre-VESA
    push dword msg_pre_vesa
    call serial_puts
    add esp, 4

.mode_loop:
    ; Check boot mode flag
    cmp dword [boot_mode_flag], 1
    je .dos_mode

    ; GUI Mode
    push dword starting_gui_msg
    call puts
    add esp, 4


    ; Initialize VESA mode (uses bootloader BIOS info or Bochs VBE fallback)
    call vesa_reinit_mode


    ; Debug: Post-VESA
    push dword msg_post_vesa
    call serial_puts
    add esp, 4

    call desktop_main
    ; desktop_main returns 0 = switch to DOS mode, 1 = shutdown
    cmp eax, 0
    je .switch_to_dos
    jmp .hang

.switch_to_dos:
    mov dword [boot_mode_flag], 1
    ; Text mode is set by desktop_main before returning
    ; Re-init I/O for text console
    call io_init

    push dword dos_mode_msg
    call puts
    add esp, 4

    call shell_main
    ; shell_main returns when GUIMODE is typed
    mov dword [boot_mode_flag], 0

    ; Re-initialize VESA mode
    call vesa_reinit_mode
    jmp .mode_loop

.dos_mode:
    push dword calling_shell_msg
    call puts
    add esp, 4

    call shell_main
    ; If shell returns (GUIMODE typed), switch to GUI
    mov dword [boot_mode_flag], 0
    call vesa_reinit_mode
    jmp .mode_loop

.hang:
    cli
    push dword shell_exit_msg
    call puts
    add esp, 4

.halt_loop:
    hlt
    jmp .halt_loop

sys_reboot:
    cli
    mov al, 0xFE
    out 0x64, al
    hlt
    jmp $

section .rodata
kernel_ok_msg       db "Aurion OS Kernel v1.0 Beta", 13, 10, 0
system_ready_msg    db "System initialized", 13, 10, 0
mem_init_msg        db "Memory manager ready (128MB heap)", 13, 10, 0
enabling_int_msg    db "Enabling interrupts...", 13, 10, 0
calling_shell_msg   db "Starting Aurion Shell...", 13, 10, 13, 10, 0
starting_gui_msg    db "Starting Aurion Desktop...", 13, 10, 0
dos_mode_msg        db 13, 10, "Entering DOS compatibility mode...", 13, 10, 13, 10, 0
shell_exit_msg      db 13, 10, "System halted.", 13, 10, 0

    ; Serial debug messages
    msg_kernel_entry db "[SERIAL] Kernel entry", 13, 10, 0
    msg_segments_ok  db "[SERIAL] Segments OK", 13, 10, 0
    msg_stack_ok     db "[SERIAL] Stack OK", 13, 10, 0
    msg_pic_masked   db "[SERIAL] PIC masked", 13, 10, 0
    msg_idt_ok       db "[SERIAL] IDT initialized", 13, 10, 0
    msg_io_ok        db "[SERIAL] I/O initialized", 13, 10, 0
    msg_cls_ok       db "[SERIAL] CLS called", 13, 10, 0
    msg_kernel_start db "Kernel: Started", 10, 0
    msg_pre_vesa     db "Kernel: Pre-VESA", 10, 0
    msg_post_vesa    db "Kernel: Post-VESA, calling desktop_main", 10, 0
    msg_gui_ret      db "Kernel: Desktop returned", 10, 0

    ; Kernel stack
    SECTION .bss
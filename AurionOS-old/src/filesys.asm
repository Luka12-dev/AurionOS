BITS 32

section .data
align 4

; config and computed values
boot_sector_lba_saved   dd 0
bpb_bytes_per_sector    dd 0
bpb_sectors_per_cluster dd 0
bpb_reserved_sectors    dd 0
bpb_num_fats            dd 0
bpb_max_root_entries    dd 0
bpb_total_sectors16     dd 0
bpb_media               db 0
                        db 0,0,0  ; padding
bpb_fat_size_sectors    dd 0
bpb_sectors_per_track   dd 0
bpb_num_heads           dd 0
bpb_hidden_sectors      dd 0
bpb_total_sectors32     dd 0

; computed locations (all in LBA sector units)
fs_first_fat_lba        dd 0
fs_first_root_lba       dd 0
fs_first_data_lba       dd 0
fs_total_clusters       dd 0
fs_root_dir_sectors     dd 0

; small handle table
FS_MAX_HANDLES          equ 16

; file handle struct layout (32 bytes each)
; offset 0: dword first_cluster
; offset 4: dword file_size
; offset 8: dword current_cluster
; offset12: dword position
; offset16: dword cluster_buffer_ptr (kmalloc'd)
; offset20: dword cluster_offset
; offset24: dword flags (bit0 = in-use)
; offset28: dword reserved

file_handle_area:
    times FS_MAX_HANDLES * 8 dd 0

; temporary buffer pointer
fs_temp_buf_ptr dd 0

; cluster buffer for reading
cluster_buffer_ptr dd 0

section .text

global fs_init
global fs_list_root
global fs_find_file
global fs_open
global fs_read
global fs_close

extern disk_read_lba   ; int disk_read_lba(uint32_t lba, uint32_t count, void *buf)
extern kmalloc
extern kfree

; Helper: read_sector_lba
; Input: EAX = lba, EBX = count, ECX = buffer
; Returns: EAX = 0 success, non-zero error

read_sector_lba:
    push ebp
    mov ebp, esp
    push ecx
    push ebx
    push eax
    call disk_read_lba
    add esp, 12
    leave
    ret

; fs_init
; Input: EAX = boot_sector_lba, EBX = temp_buf_ptr
; Returns: EAX = 0 on success, non-zero error

fs_init:
    push ebp
    mov ebp, esp
    push ebx
    push ecx
    push edx
    push esi
    push edi

    ; save parameters
    mov [boot_sector_lba_saved], eax
    mov [fs_temp_buf_ptr], ebx

    ; read boot sector into temp buffer
    mov ecx, ebx
    mov ebx, 1
    call read_sector_lba
    test eax, eax
    jnz .fail_read_boot

    ; parse BPB fields from temp buffer
    mov esi, [fs_temp_buf_ptr]
    
    ; offset 11: bytes per sector (word)
    movzx eax, word [esi + 11]
    mov [bpb_bytes_per_sector], eax
    
    ; offset 13: sectors per cluster (byte)
    movzx eax, byte [esi + 13]
    mov [bpb_sectors_per_cluster], eax
    
    ; offset 14: reserved sectors (word)
    movzx eax, word [esi + 14]
    mov [bpb_reserved_sectors], eax
    
    ; offset 16: num FATs (byte)
    movzx eax, byte [esi + 16]
    mov [bpb_num_fats], eax
    
    ; offset 17: root entries (word)
    movzx eax, word [esi + 17]
    mov [bpb_max_root_entries], eax
    
    ; offset 19: total sectors (word)
    movzx eax, word [esi + 19]
    mov [bpb_total_sectors16], eax
    
    ; offset 21: media (byte)
    mov al, byte [esi + 21]
    mov [bpb_media], al
    
    ; offset 22: sectors per FAT (word)
    movzx eax, word [esi + 22]
    mov [bpb_fat_size_sectors], eax
    
    ; offset 24: sectors per track (word)
    movzx eax, word [esi + 24]
    mov [bpb_sectors_per_track], eax
    
    ; offset 26: num heads (word)
    movzx eax, word [esi + 26]
    mov [bpb_num_heads], eax
    
    ; offset 28: hidden sectors (dword)
    mov eax, dword [esi + 28]
    mov [bpb_hidden_sectors], eax
    
    ; offset 32: total sectors (dword)
    mov eax, dword [esi + 32]
    mov [bpb_total_sectors32], eax

    ; compute first FAT LBA = boot_sector_lba + reserved_sectors
    mov eax, [boot_sector_lba_saved]
    add eax, [bpb_reserved_sectors]
    mov [fs_first_fat_lba], eax

    ; first root LBA = first FAT + (num_fats * fat_size)
    mov eax, [bpb_num_fats]
    imul eax, [bpb_fat_size_sectors]
    add eax, [fs_first_fat_lba]
    mov [fs_first_root_lba], eax

    ; root occupies ceil(root_entries*32 / bytes_per_sector) sectors
    mov eax, [bpb_max_root_entries]
    shl eax, 5  ; multiply by 32
    xor edx, edx
    div dword [bpb_bytes_per_sector]
    test edx, edx
    jz .no_root_rem
    inc eax
.no_root_rem:
    mov [fs_root_dir_sectors], eax
    
    ; first data LBA = first_root_lba + root_dir_sectors
    add eax, [fs_first_root_lba]
    mov [fs_first_data_lba], eax

    ; total clusters = (total_sectors - first_data_lba) / sectors_per_cluster
    mov eax, [bpb_total_sectors16]
    test eax, eax
    jnz .have_total
    mov eax, [bpb_total_sectors32]
.have_total:
    sub eax, [fs_first_data_lba]
    xor edx, edx
    div dword [bpb_sectors_per_cluster]
    mov [fs_total_clusters], eax

    ; allocate cluster buffer
    mov eax, [bpb_bytes_per_sector]
    imul eax, [bpb_sectors_per_cluster]
    push eax
    call kmalloc
    add esp, 4
    mov [cluster_buffer_ptr], eax

    ; initialize file handle table
    xor eax, eax
    mov ecx, FS_MAX_HANDLES * 8
    mov edi, file_handle_area
    rep stosd

    ; success
    xor eax, eax
    jmp .exit

.fail_read_boot:
    mov eax, 1

.exit:
    pop edi
    pop esi
    pop edx
    pop ecx
    pop ebx
    leave
    ret

; fs_list_root
; Input: EAX = out_dir_buffer ptr, EBX = max_entries
; Returns: EAX = number of entries read

fs_list_root:
    push ebp
    mov ebp, esp
    push ebx
    push ecx
    push edx
    push esi
    push edi

    mov edi, eax        ; out buffer
    mov dword [ebp-20], ebx  ; max entries (use stack location)
    
    xor ebx, ebx        ; sector index
    xor edx, edx        ; entries_written

.loop_sectors:
    cmp ebx, [fs_root_dir_sectors]
    jge .done_list
    
    ; read sector into temp buffer
    mov eax, [fs_first_root_lba]
    add eax, ebx
    push ebx
    push edx
    push edi
    mov ebx, 1
    mov ecx, [fs_temp_buf_ptr]
    call read_sector_lba
    pop edi
    pop edx
    pop ebx
    test eax, eax
    jnz .err
    
    ; copy entries from temp buffer (32 bytes each)
    mov esi, [fs_temp_buf_ptr]
    xor ecx, ecx
    
.entry_loop:
    cmp ecx, [bpb_bytes_per_sector]
    jge .next_sector
    
    ; check first byte
    mov al, byte [esi + ecx]
    test al, al
    jz .done_list
    cmp al, 0xE5
    je .skip_entry
    
    ; copy 32 bytes
    push ecx
    push esi
    push edi
    lea esi, [esi + ecx]
    mov ecx, 8
.copy_loop:
    mov eax, [esi]
    mov [edi], eax
    add esi, 4
    add edi, 4
    dec ecx
    jnz .copy_loop
    pop edi
    pop esi
    pop ecx
    add edi, 32
    inc edx
    
.skip_entry:
    add ecx, 32
    jmp .entry_loop
    
.next_sector:
    inc ebx
    jmp .loop_sectors

.done_list:
    mov eax, edx
    jmp .exit

.err:
    mov eax, -1

.exit:
    pop edi
    pop esi
    pop edx
    pop ecx
    pop ebx
    leave
    ret

; FAT12 helper: read_fat_entry
; Input: EAX = cluster_number
; Output: EAX = next cluster (0xFFF for EOF)

read_fat_entry:
    push ebp
    mov ebp, esp
    push ebx
    push ecx
    push edx
    push esi
    push edi

    mov edi, eax        ; save cluster number
    
    ; compute fat offset: cluster * 3/2
    mov eax, edi
    mov edx, eax
    add eax, edx
    add eax, edi
    shr eax, 1          ; eax = (cluster * 3) / 2

    ; determine FAT sector and offset within sector
    xor edx, edx
    div dword [bpb_bytes_per_sector]
    mov esi, edx        ; offset within sector
    
    ; read FAT sector
    add eax, [fs_first_fat_lba]
    mov ebx, 1
    mov ecx, [fs_temp_buf_ptr]
    push edi
    push esi
    call read_sector_lba
    pop esi
    pop edi
    test eax, eax
    jnz .fail

    ; read 16-bit word from offset
    mov ebx, [fs_temp_buf_ptr]
    movzx eax, word [ebx + esi]
    
    ; check if we need next byte (crosses sector boundary)
    inc esi
    cmp esi, [bpb_bytes_per_sector]
    jl .no_cross
    
    ; read next sector
    mov eax, [fs_first_fat_lba]
    add eax, 1
    mov ebx, 1
    mov ecx, [fs_temp_buf_ptr]
    push edi
    call read_sector_lba
    pop edi
    test eax, eax
    jnz .fail
    
    mov ebx, [fs_temp_buf_ptr]
    movzx edx, byte [ebx]
    shl edx, 16
    or eax, edx
    
.no_cross:
    ; check cluster parity
    test edi, 1
    jz .even_cluster
    
    ; odd cluster: shift right 4 bits
    shr eax, 4
    jmp .mask_entry
    
.even_cluster:
    ; even cluster: mask low 12 bits
    
.mask_entry:
    and eax, 0xFFF
    
    ; check for EOF markers
    cmp eax, 0xFF8
    jge .eof
    jmp .exit
    
.eof:
    mov eax, 0xFFF
    jmp .exit

.fail:
    xor eax, eax

.exit:
    pop edi
    pop esi
    pop edx
    pop ecx
    pop ebx
    leave
    ret

; Helper: cluster_to_lba
; Input: EAX = cluster_number
; Returns: EAX = LBA of first sector

cluster_to_lba:
    push ebp
    mov ebp, esp
    push ebx
    
    sub eax, 2
    imul eax, [bpb_sectors_per_cluster]
    add eax, [fs_first_data_lba]
    
    pop ebx
    leave
    ret

; fs_find_file
; Input: EAX = pointer to 11-byte filename (padded, uppercase)
; Output: EAX = directory entry index or -1

fs_find_file:
    push ebp
    mov ebp, esp
    push ebx
    push ecx
    push edx
    push esi
    push edi

    mov dword [ebp-24], eax  ; save filename ptr
    xor ebx, ebx        ; sector index
    xor edx, edx        ; entry index

.loop_sectors:
    cmp ebx, [fs_root_dir_sectors]
    jge .not_found
    
    ; read root sector
    mov eax, [fs_first_root_lba]
    add eax, ebx
    push ebx
    push edx
    mov ebx, 1
    mov ecx, [fs_temp_buf_ptr]
    call read_sector_lba
    pop edx
    pop ebx
    test eax, eax
    jnz .disk_err
    
    ; iterate entries in sector
    mov esi, [fs_temp_buf_ptr]
    xor ecx, ecx
    
.entry_loop:
    cmp ecx, [bpb_bytes_per_sector]
    jge .next_sector
    
    ; check first byte
    mov al, byte [esi + ecx]
    test al, al
    jz .not_found
    cmp al, 0xE5
    je .skip_entry
    
    ; compare 11 bytes
    push ecx
    push esi
    push edi
    lea edi, [esi + ecx]
    mov esi, [ebp-24]
    mov ecx, 11
    repe cmpsb
    pop edi
    pop esi
    pop ecx
    je .found
    
.skip_entry:
    inc edx
    add ecx, 32
    jmp .entry_loop
    
.next_sector:
    inc ebx
    jmp .loop_sectors

.found:
    mov eax, edx
    jmp .exit

.not_found:
    mov eax, -1
    jmp .exit

.disk_err:
    mov eax, -2

.exit:
    pop edi
    pop esi
    pop edx
    pop ecx
    pop ebx
    leave
    ret

; fs_open
; Input: EAX = filename ptr, EBX = mode, ECX = out_handle ptr
; Returns: EAX = 0 success, -1 error

fs_open:
    push ebp
    mov ebp, esp
    push ebx
    push ecx
    push edx
    push esi
    push edi

    ; find file in root directory
    push ecx
    call fs_find_file
    pop ecx
    cmp eax, -1
    je .not_found
    
    ; find free handle
    xor ebx, ebx
    mov edi, file_handle_area
.find_handle:
    cmp ebx, FS_MAX_HANDLES
    jge .no_handles
    test dword [edi + 24], 1
    jz .handle_found
    add edi, 32
    inc ebx
    jmp .find_handle
    
.handle_found:
    ; read directory entry
    push eax
    push ebx
    push ecx
    push edi
    
    ; calculate which sector contains this entry
    mov edx, eax
    shr eax, 4          ; divide by 16 (entries per sector)
    add eax, [fs_first_root_lba]
    mov ebx, 1
    mov ecx, [fs_temp_buf_ptr]
    call read_sector_lba
    
    pop edi
    pop ecx
    pop ebx
    pop eax
    
    ; get entry offset in sector
    and eax, 15
    shl eax, 5          ; multiply by 32
    mov esi, [fs_temp_buf_ptr]
    add esi, eax
    
    ; extract first cluster (offset 26) and size (offset 28)
    movzx eax, word [esi + 26]
    mov [edi], eax      ; first_cluster
    mov eax, [esi + 28]
    mov [edi + 4], eax  ; file_size
    mov [edi + 8], eax  ; current_cluster = first_cluster
    mov dword [edi + 12], 0  ; position
    mov eax, [cluster_buffer_ptr]
    mov [edi + 16], eax ; cluster_buffer_ptr
    mov dword [edi + 20], 0  ; cluster_offset
    mov dword [edi + 24], 1  ; flags = in-use
    
    ; return handle
    mov [ecx], ebx
    xor eax, eax
    jmp .exit

.not_found:
    mov eax, -1
    jmp .exit
    
.no_handles:
    mov eax, -2

.exit:
    pop edi
    pop esi
    pop edx
    pop ecx
    pop ebx
    leave
    ret

; fs_read
; Input: EAX = handle, EBX = buffer, ECX = bytes_to_read, EDX = bytes_read ptr
; Returns: EAX = 0 success

fs_read:
    push ebp
    mov ebp, esp
    push ebx
    push ecx
    push edx
    push esi
    push edi

    ; validate handle
    cmp eax, FS_MAX_HANDLES
    jge .invalid_handle
    
    ; get handle struct
    shl eax, 5
    lea edi, [file_handle_area + eax]
    test dword [edi + 24], 1
    jz .invalid_handle
    
    ; TODO: implement actual read logic with cluster chain walking
    ; For now, return 0 bytes read
    mov dword [edx], 0
    xor eax, eax
    jmp .exit

.invalid_handle:
    mov eax, -1

.exit:
    pop edi
    pop esi
    pop edx
    pop ecx
    pop ebx
    leave
    ret

; fs_close
; Input: EAX = handle
; Returns: EAX = 0 success

fs_close:
    push ebp
    mov ebp, esp
    push ebx
    push edi

    ; validate handle
    cmp eax, FS_MAX_HANDLES
    jge .invalid_handle
    
    ; get handle struct
    shl eax, 5
    lea edi, [file_handle_area + eax]
    test dword [edi + 24], 1
    jz .invalid_handle
    
    ; mark as free
    mov dword [edi + 24], 0
    xor eax, eax
    jmp .exit

.invalid_handle:
    mov eax, -1

.exit:
    pop edi
    pop ebx
    leave
    ret
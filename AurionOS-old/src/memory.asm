BITS 32

section .data
align 4

; Constants
MCB_MAGIC       equ 0x4D43424B      ; "MCKB" magic
MCB_HEADER_SIZE equ 20              ; Size of MCB header
MIN_SPLIT_SIZE  equ 32              ; Minimum size to split block
MCB_FLAG_FREE   equ 1               ; Block is free
MCB_FLAG_USED   equ 0               ; Block is used

; Heap management
heap_base       dd 0
heap_size       dd 0
heap_end        dd 0
heap_free_head  dd 0

; Statistics
mem_total_free  dd 0
mem_total_used  dd 0
mem_num_blocks  dd 0

; Debug flag (set to 1 to enable validation)
debug_enabled   dd 0

section .text
global mem_init
global kmalloc
global kfree
global mem_get_stats
global mem_validate_heap

; mem_init - Initialize heap allocator
; Input: 
;   EAX = heap_start address
;   EBX = heap_size in bytes
; Returns:
;   EAX = 0 on success, non-zero on error

mem_init:
    push ebp
    mov ebp, esp
    push ebx
    push ecx
    push edx
    push esi
    push edi

    ; Validate parameters
    test eax, eax
    jz .error_invalid
    cmp ebx, 1024           ; Minimum 1KB heap
    jb .error_invalid

    ; Store heap parameters
    mov [heap_base], eax
    mov [heap_size], ebx
    add ebx, eax
    mov [heap_end], ebx

    ; Initialize first MCB at heap_start
    mov edi, eax
    
    ; Clear MCB header
    xor ecx, ecx
    mov [edi + 0], ecx      ; prev = NULL
    mov [edi + 4], ecx      ; next = NULL

    ; Calculate payload size
    mov ecx, [heap_size]
    sub ecx, MCB_HEADER_SIZE
    jle .error_too_small
    mov [edi + 8], ecx      ; size

    ; Set magic and flags
    mov dword [edi + 12], MCB_MAGIC
    mov dword [edi + 16], MCB_FLAG_FREE

    ; Set free list head
    mov [heap_free_head], edi

    ; Initialize statistics
    mov [mem_total_free], ecx
    mov dword [mem_total_used], 0
    mov dword [mem_num_blocks], 1

    ; Success
    xor eax, eax
    jmp .done

.error_invalid:
    mov eax, 1
    jmp .done

.error_too_small:
    mov eax, 2

.done:
    pop edi
    pop esi
    pop edx
    pop ecx
    pop ebx
    leave
    ret

; kmalloc - Allocate memory block
; Input:
;   EAX = requested size in bytes
; Returns:
;   EAX = pointer to allocated memory (NULL on failure)

kmalloc:
    push ebp
    mov ebp, esp
    push ebx
    push ecx
    push edx
    push esi
    push edi

    ; Validate size
    test eax, eax
    jz .alloc_failed

    ; Align size to 4-byte boundary
    add eax, 3
    and eax, 0xFFFFFFFC
    mov [ebp - 20], eax     ; Save aligned size

    ; Search free list for suitable block (first-fit)
    mov esi, [heap_free_head]

.search_loop:
    test esi, esi
    jz .alloc_failed

    ; Validate block magic
    cmp dword [esi + 12], MCB_MAGIC
    jne .corrupted_heap

    ; Check if block is free
    cmp dword [esi + 16], MCB_FLAG_FREE
    jne .next_block

    ; Check if block is large enough
    mov ebx, [esi + 8]      ; block size
    cmp ebx, eax
    jb .next_block

    ; Found suitable block
    jmp .allocate_block

.next_block:
    mov esi, [esi + 4]      ; next block
    jmp .search_loop

.allocate_block:
    ; ESI points to suitable block
    ; EAX contains requested size
    mov ebx, [esi + 8]      ; block size

    ; Check if we should split the block
    mov ecx, ebx
    sub ecx, eax            ; remaining space
    sub ecx, MCB_HEADER_SIZE
    cmp ecx, MIN_SPLIT_SIZE
    jb .use_whole_block

    ; Split block
    ; Create new free block after allocated portion
    mov edi, esi
    add edi, MCB_HEADER_SIZE
    add edi, eax            ; EDI = new block address

    ; Setup new block
    mov [edi + 0], esi      ; prev = current block
    mov edx, [esi + 4]
    mov [edi + 4], edx      ; next = old next

    ; Calculate new block size
    mov ecx, [esi + 8]
    sub ecx, eax
    sub ecx, MCB_HEADER_SIZE
    mov [edi + 8], ecx      ; size

    mov dword [edi + 12], MCB_MAGIC
    mov dword [edi + 16], MCB_FLAG_FREE

    ; Update current block
    mov [esi + 4], edi      ; current.next = new block
    mov [esi + 8], eax      ; current.size = requested

    ; Update next block's prev pointer
    test edx, edx
    jz .split_done
    mov [edx + 0], edi      ; next.prev = new block

.split_done:
    ; Mark current block as used
    mov dword [esi + 16], MCB_FLAG_USED

    ; Return payload pointer
    mov eax, esi
    add eax, MCB_HEADER_SIZE
    jmp .alloc_success

.use_whole_block:
    ; Remove block from free list
    mov edx, [esi + 0]      ; prev
    mov ecx, [esi + 4]      ; next

    ; Update prev's next pointer
    test edx, edx
    jz .no_prev
    mov [edx + 4], ecx
    jmp .update_next

.no_prev:
    ; Block is head of free list
    mov [heap_free_head], ecx

.update_next:
    ; Update next's prev pointer
    test ecx, ecx
    jz .remove_done
    mov [ecx + 0], edx

.remove_done:
    ; Mark block as used
    mov dword [esi + 16], MCB_FLAG_USED

    ; Return payload pointer
    mov eax, esi
    add eax, MCB_HEADER_SIZE
    jmp .alloc_success

.alloc_failed:
    xor eax, eax
    jmp .done

.corrupted_heap:
    ; Heap corruption detected
    xor eax, eax

.alloc_success:
    ; Update statistics (approximate)
    mov ebx, [ebp - 20]     ; requested size
    add ebx, MCB_HEADER_SIZE
    mov ecx, [mem_total_free]
    sub ecx, ebx
    mov [mem_total_free], ecx
    mov ecx, [mem_total_used]
    add ecx, ebx
    mov [mem_total_used], ecx

.done:
    pop edi
    pop esi
    pop edx
    pop ecx
    pop ebx
    leave
    ret

; kfree - Free allocated memory block
; Input:
;   EAX = pointer to memory block (from kmalloc)
; Returns: nothing

kfree:
    push ebp
    mov ebp, esp
    push eax
    push ebx
    push ecx
    push edx
    push esi
    push edi

    ; Validate pointer
    test eax, eax
    jz .done

    ; Get MCB header
    mov esi, eax
    sub esi, MCB_HEADER_SIZE

    ; Validate block
    cmp dword [esi + 12], MCB_MAGIC
    jne .invalid_free

    ; Check if already free
    cmp dword [esi + 16], MCB_FLAG_FREE
    je .done                ; Double free protection

    ; Mark block as free
    mov dword [esi + 16], MCB_FLAG_FREE

    ; Try to coalesce with next block
    mov edi, [esi + 4]      ; next block
    test edi, edi
    jz .check_prev

    ; Validate next block
    cmp dword [edi + 12], MCB_MAGIC
    jne .check_prev

    ; Check if next is free
    cmp dword [edi + 16], MCB_FLAG_FREE
    jne .check_prev

    ; Coalesce with next
    ; current.size += MCB_HEADER_SIZE + next.size
    mov eax, [esi + 8]
    add eax, MCB_HEADER_SIZE
    add eax, [edi + 8]
    mov [esi + 8], eax

    ; current.next = next.next
    mov ebx, [edi + 4]
    mov [esi + 4], ebx

    ; Update next.next.prev if exists
    test ebx, ebx
    jz .check_prev
    mov [ebx + 0], esi

.check_prev:
    ; Try to coalesce with previous block
    mov edi, [esi + 0]      ; prev block
    test edi, edi
    jz .add_to_free_list

    ; Validate prev block
    cmp dword [edi + 12], MCB_MAGIC
    jne .add_to_free_list

    ; Check if prev is free
    cmp dword [edi + 16], MCB_FLAG_FREE
    jne .add_to_free_list

    ; Coalesce with prev
    ; prev.size += MCB_HEADER_SIZE + current.size
    mov eax, [edi + 8]
    add eax, MCB_HEADER_SIZE
    add eax, [esi + 8]
    mov [edi + 8], eax

    ; prev.next = current.next
    mov ebx, [esi + 4]
    mov [edi + 4], ebx

    ; Update current.next.prev if exists
    test ebx, ebx
    jz .done
    mov [ebx + 0], edi
    jmp .done

.add_to_free_list:
    ; Add block to head of free list
    mov eax, [heap_free_head]
    mov [esi + 0], dword 0
    mov [esi + 4], eax
    mov [heap_free_head], esi

    ; Update old head's prev pointer
    test eax, eax
    jz .done
    mov [eax + 0], esi
    jmp .done

.invalid_free:
    ; Invalid pointer - ignore for safety

.done:
    pop edi
    pop esi
    pop edx
    pop ecx
    pop ebx
    pop eax
    leave
    ret

; mem_get_stats - Get memory statistics
; Input:
;   EAX = pointer to stats structure (or NULL)
; Stats structure:
;   offset 0: dword total_free
;   offset 4: dword total_used
;   offset 8: dword num_blocks
; Returns:
;   EAX = 0 on success

mem_get_stats:
    push ebp
    mov ebp, esp
    push ebx
    push ecx
    push edx
    push esi
    push edi

    mov edi, eax            ; Save output pointer

    ; Scan entire heap to compute accurate stats
    xor eax, eax            ; total_free
    xor ebx, ebx            ; total_used
    xor ecx, ecx            ; num_blocks

    mov esi, [heap_base]

.scan_loop:
    ; Check if we're past the heap end
    mov edx, [heap_end]
    cmp esi, edx
    jae .scan_done

    ; Validate block magic
    cmp dword [esi + 12], MCB_MAGIC
    jne .scan_done          ; Stop on corrupted block

    ; Get block size
    mov edx, [esi + 8]

    ; Check if free or used
    cmp dword [esi + 16], MCB_FLAG_FREE
    jne .used_block

    ; Free block
    add eax, edx
    jmp .next_scan_block

.used_block:
    ; Used block
    add ebx, edx

.next_scan_block:
    inc ecx                 ; Count block

    ; Move to next block
    add esi, MCB_HEADER_SIZE
    add esi, edx

    jmp .scan_loop

.scan_done:
    ; Update global statistics
    mov [mem_total_free], eax
    mov [mem_total_used], ebx
    mov [mem_num_blocks], ecx

    ; Store in output structure if provided
    test edi, edi
    jz .no_output

    mov [edi + 0], eax      ; total_free
    mov [edi + 4], ebx      ; total_used
    mov [edi + 8], ecx      ; num_blocks

.no_output:
    xor eax, eax            ; Return success

    pop edi
    pop esi
    pop edx
    pop ecx
    pop ebx
    leave
    ret

; mem_validate_heap - Validate heap integrity
; Returns:
;   EAX = 0 if valid, error code otherwise

mem_validate_heap:
    push ebp
    mov ebp, esp
    push ebx
    push ecx
    push esi

    mov esi, [heap_base]

.validate_loop:
    ; Check bounds
    mov ebx, [heap_end]
    cmp esi, ebx
    jae .valid

    ; Check magic
    cmp dword [esi + 12], MCB_MAGIC
    jne .corrupted

    ; Check flags
    mov eax, [esi + 16]
    cmp eax, MCB_FLAG_FREE
    je .next_validate
    cmp eax, MCB_FLAG_USED
    jne .corrupted

.next_validate:
    ; Move to next block
    mov ecx, [esi + 8]
    add esi, MCB_HEADER_SIZE
    add esi, ecx
    jmp .validate_loop

.valid:
    xor eax, eax
    jmp .done

.corrupted:
    mov eax, 1

.done:
    pop esi
    pop ecx
    pop ebx
    leave
    ret

; Helper: dump_block_info (for debugging)
; Input: ESI = block pointer

dump_block_info:
    push ebp
    mov ebp, esp
    push eax
    push ebx

    ; This would print block info if we had printf
    ; For now it's a placeholder

    pop ebx
    pop eax
    leave
    ret

; Data for testing and debugging

section .rodata
align 4

heap_init_msg   db "Memory manager initialized", 10, 0
heap_error_msg  db "Heap corruption detected!", 10, 0
oom_msg         db "Out of memory!", 10, 0
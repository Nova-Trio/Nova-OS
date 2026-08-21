bits 16

MMAP_START equ 0x500
MMAP_START_SEGMENT equ MMAP_START /16
MAX_MMAP_ENTRY_COUNT equ 128
AFTER_MMAP equ MMAP_START +(MAX_MMAP_ENTRY_COUNT *24)

struc Mmap_entry24
    .address_low32: resd 1
    .address_high32: resd 1
    .length_low32: resd 1
    .length_high32: resd 1
    .type: resd 1
    .acpi3_info: resd 1
endstruc

; Loads 24-byte memory map entries starting at 0x500
; Returns:
;   AL = 1 on success, AL = 0 on failure,
;   AH = 1 if 24 bytes are initialized, AH = 0 if only 20 bytes are initialized
;   BX = entry count
getMmap:
    ; Push registers
    push edx
    push es
    push di
    pushfd
    ; Set local stack
    mov bp, sp
    sub sp, 4
    .entry_count equ -2
    .bytes24 equ -3

    ; Set entry count
    mov word [bp +.entry_count], 0
    mov byte [bp +.bytes24], 0
    
    ; Set ES:DI destination buffer address
    mov ax, MMAP_START_SEGMENT
    mov es, ax
    xor di, di ; 0x0

    ; Set internal BIOS continuation value (must be 0 for start)
    mov ebx, 0

    ; Get memory map
.MAP_LOOP:
    mov eax, 0x0000E820
    mov edx, 0x534D4150 ; 'SMAP'
    mov ecx, 24 ; Buffer size
    int 0x15
    jnc .CARRY_CLEAR

    ; Check success and if another iteration is needed
    ; CF = 1 (set) & entry count = 0 means error
    ; CF = 1 (set) & entry count != 0 means we already parsed all memory
    mov ax, [bp +.entry_count]
    test ax, ax
    jz .FAILED
    jmp .SUCCEEDED ; No need to increment entry count
.CARRY_CLEAR:
    ; Check call validity
    ; ~~ For some reason checking if EAX = 'SMAP' is failing in QEMU ~~
    cmp eax, 0x534D4150 ; 'SMAP'
    jne .FAILED
    cmp ecx, 24
    je .GOT_24
    cmp ecx, 20
    je .GOT_20
    jmp .FAILED

.GOT_24:
    mov byte [bp +.bytes24], 1
.GOT_20:
    ; Force ACPI 3.0 valid bit to 1
    mov dword [es:di], 1
    ; CF = 0 (clear) & EBX = 0 means we just parsed all memory
    ; Anything other than the 3 cases means we should iterate again
    ; Increment entry count
    inc word [bp +.entry_count]
    ; Check
    test ebx, ebx
    jz .SUCCEEDED
    ; If we should iterate again, check if iteration limit is reached
    mov ax, [bp +.entry_count]
    cmp ax, MAX_MMAP_ENTRY_COUNT
    je .FAILED
    add di, 24
    jmp .MAP_LOOP

.SUCCEEDED:
    mov al, 1
    jmp .AFTER_END

.FAILED:
    xor al, al

.AFTER_END:
    ; Get return values
    mov ah, [bp +.bytes24]
    mov bx, [bp +.entry_count]
    ; Remove local stack
    mov sp, bp
    ; Pop registers
    popfd
    pop di
    pop es
    pop edx
    ; Return
    ret

bits 16

; Returns AX = 0 if the CPU is an ancient 16/32-bit or modern 32-bit CPU
;   AX = 1 if it's a 64-bit CPU
; The other half of EAX may be altered
check64:
    ; Push 16-bit registers
    pushf

    ; Check if it's a 16-bit CPU by clearing some flags
    ; Get flags
    pushf
    pop ax
    ; Try to clear flag bits 12-15
    and ax, 0x0FFF
    push ax
    popf
    ; Get flags again
    pushf
    pop ax
    ; Check if flag bits 12-15 are still set
    and ax, 0xF000
    cmp ax, 0xF000
    je .ANCIENT_16

    ; Check if it's a 16-bit CPU by setting some flags
    ; Get flags
    pushf
    pop ax
    ; Try to set flag bits 12-15
    or ax, 0xF000
    push ax
    popf
    ; Get flags again
    pushf
    pop ax
    ; Check if flag bits 12-15 are still clear
    and ax, 0xF000
    jz .ANCIENT_16

    ; Push 32-bit registers
    pushfd
    push ebx
    push ecx
    push edx

    ; Check if it's a 32-bit CPU which doesn't support CPUID
    ; Get flags
    pushfd
    pop eax
    ; Save original flags
    mov ebx, eax
    ; Try to toggle flag bit 21 (ID flag)
    xor eax, 0x200000
    push eax
    popfd
    ; Get flags again
    pushfd
    pop eax
    ; Check if flag bit 21 didn't change
    xor eax, ebx
    and eax, 0x200000
    jz .ANCIENT_32
    ; Put original flags back
    push ebx
    popfd

    ; Check if it's a 64-bit CPU
    ; Check if extended CPUID function 0x8000_0001 is supported
    mov eax, 0x8000_0000
    cpuid
    cmp eax, 0x8000_0001
    jb .ANCIENT_32
    ; Check bit 29 (long mode bit) from result EDX
    mov eax, 0x8000_0001
    cpuid
    test edx, 1 << 29
    jz .ANCIENT_32

    ; 64-bit CPU confirmed
    ; Pop 32-bit registers
    pop edx
    pop ecx
    pop ebx
    popfd
    ; Pop 16-bit registers and return 1
    popf
    mov ax, 1
    ret

.ANCIENT_16:
    ; Pop 16-bit registers and return 0
    popf
    xor ax, ax
    ret
.ANCIENT_32:
    ; Pop 32-bit registers
    pop edx
    pop ecx
    pop ebx
    popfd
    ; Pop 16-bit registers and return 0
    popf
    xor ax, ax
    ret

bits 16
%include "inc/s1t2.inc"
org STAGE2_ADDRESS

jmp START16
%include "inc/gdt.inc"
%include "inc/check64.inc"
%include "inc/a20.inc"
%include "inc/mmap.inc"

START16:
    ; Disable interrupts
    cli
    ; Set data and extra segments
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    ; Enable interrupts
    sti
    
    ; Save ball
    mov [p_s1t2ball], dx

    ; Check if CPU is 64-bit
    call check64
    test al, al
    jnz .CHECK64_DONE
    mov si, MSG_CHECK64_FAILED
    jmp ERROR16
.CHECK64_DONE:

    ; Enable the A20 line
    call enableA20
    test al, al
    jnz .A20_DONE
    mov si, MSG_A20_FAILED
    jmp ERROR16
.A20_DONE:
    
    ; Get memory map
    call getMmap
    test al, al
    jnz .MMAP_DONE
    mov si, MSG_MMAP_FAILED
    jmp ERROR16
.MMAP_DONE:
    mov [mmap.entry_count], bx
    mov [mmap.bytes24], ah

    ; Get EBDA address
    push ds
    xor ax, ax
    mov ds, ax
    movzx eax, word [0x40E] ; EBDA address segment SHOULD be stored here
    pop ds
    shl eax, 4 ; Multiply segment by 16 for a flat address
    ; Check if 0 (for poorly written emulators)
    test eax, eax
    jnz .EBDA_DONE
    mov si, MSG_EBDA_FAILED
    jmp ERROR16
.EBDA_DONE:
    mov dword [EBDA_address], eax
    mov dword [EBDA_address +4], 0
    
    ; Disable maskable and non-maskable interrupts
    cli
    in al, 0x70 ; Read current CMOS port value
    or al, 0x80 ; Set bit 7 (disable non-maskable interrupts)
    out 0x70, al ; Send new value

    ; Load 32-bit GDT and jump to 32-bit code
    lgdt [GDT32_DESCRIPTOR]
    mov eax, cr0
    or eax, 0x1 ; Set bit 0 (protection enable)
    mov cr0, eax
    jmp GDT32_CODE_SS:START32

.HALT_LOOP:
    hlt
    jmp .HALT_LOOP



; Helper functions/routines
; Takes string in SI
ERROR16:
    mov ax, 0xB800 ; VGA address segment
    mov es, ax
    xor di, di
    mov ah, 0x7 ; Light grey fg, black bg
.PRINT_LOOP:
    lodsb ; [DS:SI] -> AL, SI++
    test al, al
    jz .PRINT_DONE
    stosw ; AX -> [ES:DI], DI+= 2
    jmp .PRINT_LOOP
.PRINT_DONE:
    cli
.HALT_LOOP:
    hlt
    jmp .HALT_LOOP

; Expects: SI = number to print
PRINT_NUM_HALT:
    mov ax, 0xB800      ; VGA text buffer segment
    mov es, ax
    xor di, di          ; Start at the top-left corner
    
    mov ax, si          ; Put number in AX for division
    mov bx, 10          ; Divisor for decimal conversion
    xor cx, cx          ; Count digits

.convert_loop:
    xor dx, dx
    div bx              ; DX:AX / 10 -> AX=quotient, DX=remainder
    add dl, '0'         ; Convert remainder to ASCII
    push dx             ; Push char onto stack to reverse order later
    inc cx              ; Increment digit counter
    test ax, ax         ; Is quotient 0?
    jnz .convert_loop

.print_loop:
    pop ax              ; Pop character from stack
    mov ah, 0x07        ; Attribute: Light grey on black
    mov [es:di], ax     ; Write to VGA memory
    add di, 2           ; Advance to next character position
    loop .print_loop

.halt:
    cli                 ; Disable interrupts
    hlt                 ; Halt CPU
    jmp .halt           ; Keep halted if interrupted

; Mutable data
p_s1t2ball: dw 0
mmap:
    .entry_count: dw 0
    .bytes24: db 0
EBDA_address: dq 0

; Constant data
MSG_CHECK64_FAILED: db "Error: CPU isn't 64-bit", 0
MSG_A20_FAILED: db "Error: failed to enable Address Line 20", 0
MSG_MMAP_FAILED: db "Error: failed to map memory", 0
MSG_EBDA_FAILED: db "Error: EBDA address segment at 0x40E is 0", \
                        " (the problem may be due to a poorly-written emulator)", 0
align 4
GDT32:
; DEF_SEGMENT_DESCRIPTOR args: base address, limit address, access byte flags, other flags
DEF_SEGMENT_DESCRIPTOR 0, 0, 0, 0 ; Null descriptor
.CODE_SD: DEF_SEGMENT_DESCRIPTOR 0, 0xFFFFF, 10011010b, 1100b
.DATA_SD: DEF_SEGMENT_DESCRIPTOR 0, 0xFFFFF, 10010010b, 1100b
GDT32_END:
GDT32_DESCRIPTOR:
    dw GDT32_END -GDT32 -1 ; Size
    dd GDT32               ; Address
; Segment selectors
GDT32_CODE_SS equ GDT32.CODE_SD -GDT32
GDT32_DATA_SS equ GDT32.DATA_SD -GDT32



bits 32
%include "inc/paging.inc"

START32:
    ; Set segment registers
    mov ax, GDT32_DATA_SS
    mov ds, ax
    mov ss, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    ; Set stack from 0x7,FFFF going downwards
    mov esp, 0x8_0000

    ; Set basic 64-bit paging in memory
    call setBasicPaging
    test al, al
    jnz .PAGING_DONE
    mov esi, MSG_PAGING_FAILED
    jmp ERROR32
.PAGING_DONE:

    ; Load PML4
    mov eax, PML4_ADDRESS
    mov cr3, eax

    ; Enable PAE (Physical Address Extension)
    mov eax, cr4
    or eax, 100_000b ; Set PAE bit
    mov cr4, eax

    ; Enable long mode in the EFER MSR (Extended Feature Enable Module-Specific Register)
    mov ecx, 0xC000_0080 ; EFER identifier
    rdmsr ; EFER -> {EDX, EAX}
    or eax, 10000_0000b ; Set LME (Long Mode Enable)
    wrmsr ; {EDX, EAX} -> EFER
    
    ; Load 64-bit GDT
    lgdt [GDT64_DESCRIPTOR]

    ; Activate paging and long mode
    mov eax, cr0
    or eax, 1 << 31 ; Set bit 31 (paging bit)
    mov cr0, eax

    ; Jump to 64-bit code
    jmp GDT64_CODE_SS:START64

.HALT_LOOP:
    hlt
    jmp .HALT_LOOP



; Helper functions/routines
; Takes string in ESI
ERROR32:
    mov edi, 0xB8000 ; VGA address
    mov ah, 0x7 ; Light grey fg, black bg
.PRINT_LOOP:
    lodsb ; [ESI] -> AL, ESI++
    test al, al
    jz .HALT_LOOP
    stosw ; AX -> [ES:EDI], DI+= 2
    jmp .PRINT_LOOP
.HALT_LOOP:
    hlt
    jmp .HALT_LOOP

; Constant data
MSG_PAGING_FAILED: db "Error: failed to set up 64-bit paging in memory", 0
align 8
GDT64:
; DEF_SEGMENT_DESCRIPTOR args: base address, limit address, access byte flags, other flags
DEF_SEGMENT_DESCRIPTOR 0, 0, 0, 0 ; Null descriptor
.CODE_SD: DEF_SEGMENT_DESCRIPTOR 0, 0xFFFFF, 10011010b, 1010b
.DATA_SD: DEF_SEGMENT_DESCRIPTOR 0, 0xFFFFF, 10010010b, 1000b
GDT64_END:
GDT64_DESCRIPTOR:
    dw GDT64_END -GDT64 -1 ; Size
    dq GDT64               ; Address
; Segment selectors
GDT64_CODE_SS equ GDT64.CODE_SD -GDT64
GDT64_DATA_SS equ GDT64.DATA_SD -GDT64



bits 64
%include "inc/elf.inc"

START64:
    ; Set segment registers
    mov ax, GDT64_DATA_SS
    mov ds, ax
    mov ss, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    ; Set stack from 0x7,FFFF going downwards
    mov rsp, 0x8_0000
    ; Clear direction
    cld

    ; Check ELF header validity and store some info
    ; Check signature
    cmp dword [ELF64 +ELF64_ehdr.e_ident_magic], 0x46_4C_45_7F ; (0x7F E L F) signature
    jne .ELF_INVALID
    ; Check if 64-bit
    cmp byte [ELF64 +ELF64_ehdr.e_ident_class], 2
    jne .ELF_INVALID
    ; Check if little-endian
    cmp byte [ELF64 +ELF64_ehdr.e_ident_data], 1
    jne .ELF_INVALID
    ; Check if ELF version is 1 (only valid version)
    cmp byte [ELF64 +ELF64_ehdr.e_ident_version], 1
    jne .ELF_INVALID
    ; Check if ABI is System V ABI
    cmp byte [ELF64 +ELF64_ehdr.e_ident_abi], 0
    jne .ELF_INVALID
    ; Check if ABI version is 0
    cmp byte [ELF64 +ELF64_ehdr.e_ident_abi_version], 0
    jne .ELF_INVALID
    
    ; Check if type is executable
    cmp word [ELF64 +ELF64_ehdr.e_type], 2
    jne .ELF_INVALID
    ; Check if architecture is x86-64
    cmp word [ELF64 +ELF64_ehdr.e_machine], 62 ; Not a typo it's 62
    jne .ELF_INVALID
    ; Check if the repeated ELF version is also 1
    cmp dword [ELF64 +ELF64_ehdr.e_version], 1
    jne .ELF_INVALID
    ; Check if entry point virtual address is not 0
    cmp qword [ELF64 +ELF64_ehdr.e_entry], 0
    jz .ELF_INVALID
    ; Check if 0 and save program header table offset
    mov rbx, qword [ELF64 +ELF64_ehdr.e_phoff]
    test rbx, rbx
    jz .ELF_INVALID
    
    ; Check ELF header size is standard 64-bit 64 bits size
    cmp word [ELF64 +ELF64_ehdr.e_ehsize], 64
    jne .ELF_INVALID
    ; Save program header table entry size
    movzx rdx, word [ELF64 +ELF64_ehdr.e_phentsize]
    ; Check if 0 and save program header table entry count
    movzx rcx, word [ELF64 +ELF64_ehdr.e_phnum]
    test rcx, rcx
    jz .ELF_INVALID

    ; Loop over segments (loadable program header table entries) and load them
    ; R15 = PHT entry index
    ; RBX = PHT entry address (was PHT offset but changes in the following code)
    ; RCX = PHT entry count
    ; RDX = PHT entry size

    ; Set entry index
    xor r15, r15
    ; Set entry address (PHT address + (index * entry size))
    ;   but because index = 0 it's just the PHT address initially
    add rbx, ELF64 ; File address + PHT offset

.ELF_LOOP:
    ; Check if we checked all entries (or entries are 0)
    cmp r15, rcx
    je .ELF_STOP_LOOP

    ; Check if segment is loadable (it's ok if it's not, no error)
    cmp dword [rbx +ELF64_phdr_entry.p_type], 1
    jne .ELF_ITERATION_END

    ; Save RCX
    push rcx

    ; Copy in-file part of segment
    ; Get in-file segment size
    mov rcx, qword [rbx +ELF64_phdr_entry.p_filesz]
    ; Segment source address = file + offset value in entry
    mov rsi, qword [rbx +ELF64_phdr_entry.p_offset]
    add rsi, ELF64
    ; Get segment virtual address
    mov rdi, qword [rbx +ELF64_phdr_entry.p_vaddr]
    ; Copy RCX amount of bytes from [RSI] to [RDI]
    rep movsb

    ; Zero outside-file part of segment (most likely BSS)
    ; Get outside-file size
    mov rcx, qword [rbx +ELF64_phdr_entry.p_memsz]
    sub rcx, qword [rbx +ELF64_phdr_entry.p_filesz]
    jna .AFTER_ZERO
    
    ; Copy RCX amount of AL bytes into [RDI]
    xor al, al
    rep stosb

.AFTER_ZERO:
    ; Restore RCX
    pop rcx 

.ELF_ITERATION_END:
    ; Increment entry index
    inc r15
    ; Increment entry address (i + entry size)
    add rbx, rdx
    ; Reiterate (index check at iteration start)
    jmp .ELF_LOOP

.ELF_STOP_LOOP:

    ; Pass C arguments (System V ABI)
    movzx rdi, word [p_s1t2ball]
    mov rsi, mmap
    mov rdx, MMAP_START
    mov rcx, qword [EBDA_address]
    
    ; Jump to ELF file entry
    call [ELF64 +ELF64_ehdr.e_entry]

.ELF_INVALID:
    mov rsi, MSG_ELF_INVALID
    jmp ERROR64



; Helper functions/routines
; Takes string in RSI
ERROR64:
    mov rdi, 0xB8000 ; VGA address
    mov ah, 0x7 ; Light grey fg, black bg
.PRINT_LOOP:
    lodsb ; [RSI] -> AL, RSI++
    test al, al
    jz .HALT_LOOP
    stosw ; AX -> [ES:RDI], DI+= 2
    jmp .PRINT_LOOP
.HALT_LOOP:
    hlt
    jmp .HALT_LOOP

; Constant data
MSG_ELF_INVALID: db "Error: invalid bootloader ELF file", 0

; MUST BE MERGED WITH STAGE 2 VIA AN EXTERNAL TOOL
ELF64:

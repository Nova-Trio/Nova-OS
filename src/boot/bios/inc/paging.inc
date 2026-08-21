bits 32

PAGING_START equ (AFTER_MMAP + 4095) & ~4095
PML4_ADDRESS equ PAGING_START
PDPT_ADDRESS equ PML4_ADDRESS +(8 *512)
PD_ADDRESS equ PDPT_ADDRESS +(8 *512)
PT_ADDRESS equ PD_ADDRESS +(8 *512)
AFTER_PAGING equ PT_ADDRESS +(8 *512)

; Check for table alignment & 36-bits max bit width (it's the minimum presumed address bit width)
%if (PML4_ADDRESS & 0xFFF) != 0
    %error "PML4 isn't aligned correctly by 4KB"
%elif (PML4_ADDRESS & ~0xF_FFFF_FFFF) != 0
    %error "PML4 address exceeds 36-bits"
%endif
%if (PDPT_ADDRESS & 0xFFF) != 0
    %error "PDPT isn't aligned correctly by 4KB"
%elif (PDPT_ADDRESS & ~0xF_FFFF_FFFF) != 0
    %error "PDPT address exceeds 36-bits"
%endif
%if (PD_ADDRESS & 0xFFF) != 0
    %error "PD isn't aligned correctly by 4KB"
%elif (PD_ADDRESS & ~0xF_FFFF_FFFF) != 0
    %error "PD address exceeds 36-bits"
%endif
%if (PT_ADDRESS & 0xFFF) != 0
    %error "PT isn't aligned correctly by 4KB"
%elif (PT_ADDRESS & ~0xF_FFFF_FFFF) != 0
    %error "PT address exceeds 36-bits"
%endif

; Pointer (non-page-sized) page table entry
; For PML4, PDPT, and PD entries
; NOTICE: "address" in the fields actually starts from bit 12 of the full address not 0
struc PTE_ptr
    ; Lower flags:
    ;   0: P (Present) (set = available in physical memory, clear = not available (page fault when used))
    ;   1: R/W (set = RW, clear = read-only)
    ;   2: U/S (User/Supervisor) (set = anyone can access, clear = only supervisor can access)
    ;   3: PWT (Page Write-Through) (set = write-through caching, clear = write-back caching)
    ;   4: PCD (Page Cache-Disable) (set = disable cache, clear = enable cache)
    ;   5: A (Access) (set = entry accessed, clear = entry not accessed) (set by CPU, but not cleared automatically)
    ;   6: AVL low (Available) (AVL bits are ignored by the CPU, the OS can use them)
    ;   7: Reserved/PS (Page size) (set = page sized (points to a memory page), clear = points to other entries)
    ;        (must be clear for this struc's purpose)
    .lower_flags: resb 1

    ; AVL middle-low & address low:
    ;   0-3: AVL middle-low
    ;   4-31: address low (INCLUDES POSSIBLE RESERVED BITS)
    .avl_midlow_address_low: resd 1

    ; Address high, AVL middle-high:
    ;   0-11: address high (INCLUDES POSSIBLE RESERVED BITS)
    ;   12-15: AVL middle-high
    .address_high_avl_midhigh: resw 1

    ; AVL high, XD flag:
    ;   0-6: AVL high
    ;   7: XD (Execute Disable) (set = execution disabled, set = execution enabled)
    ;         (used if bit 11 (NXE) in the EFER registeris set, otherwise the XD bit is reserved)
    .avl_high_xd: resb 1
endstruc

; Page (page-sized) table entry
; For PDPT/PD entries pointing directly to a 2MB/1GB block of memory, or standard PT entries
; NOTICE: "address" in the fields actually doesn't start from bit 0 but starts from:
;   bit 30 of the full address for PDPT direct pages
;   bit 21 of the full address for PD direct pages
;   bit 12 of the full address for PT pages
struc PTE_page
    ; Lower flags:
    ;   0: P (Present) (set = available in physical memory, clear = not available (page fault when used))
    ;   1: R/W (set = RW, clear = read-only)
    ;   2: U/S (User/Supervisor) (set = anyone can access, clear = only supervisor can access)
    ;   3: PWT (Page Write-Through) (set = write-through caching, clear = write-back caching)
    ;   4: PCD (Page Cache-Disable) (set = disable cache, clear = enable cache)
    ;   5: A (Access) (set = entry accessed, clear = entry not accessed) (set by CPU, but not cleared automatically)
    ;   6: D (Dirty) (set = page has been written to, clear = page hasn't been written to)
    ;   7: In PDPT and PD: PS (Page size) (set = page sized (points to a memory page), clear = points to other entries)
    ;        (must be set for this struc's purpose)
    ;      In standard PT: PAT (Page Attribute Table) (osdev.org didn't say what it does exactly. not my fault)
    ;        (If PAT is supported, then PAT with PCD and PWT indicate the memory caching type, otherwise it's reserved)
    .lower_flags: resb 1

    ; G, AVL low, PAT, address low:
    ;   0: G (Global) (set = CPU shouldn't invalidate the TLB (Translation lookaside buffer) entry corresponding to the page upon a MOV to CR3,
    ;                    clear = oppsoite)
    ;        (Bit 7 (PGE) of CR4 must be set to enable global pages)
    ;   1-3: AVL low (Available) (AVL bits are ignored by the CPU, the OS can use them)
    ;   4: In PDPT and PD: PAT (Page Attribute Table) (osdev.org didn't say what it does exactly. not my fault)
    ;        (If PAT is supported, then PAT with PCD and PWT indicate the memory caching type, otherwise it's reserved)
    ;      In standard PT: part of address low
    ;   5-31: address low (INCLUDES POSSIBLE RESERVED BITS)
    .flags_avl_low_address_low: resd 1

    ; Address high, AVL middle:
    ;   0-11: address high (INCLUDES POSSIBLE RESERVED BITS)
    ;   12-15: AVL middle
    .address_high_avl_mid: resw 1

    ; AVL high, PK, XD flag:
    ;   0-2: AVL high
    ;   3-6: PK (Protection Key) (Allows the OS to enable/disable access rights for page entries)
    ;   7: XD (Execute Disable) (set = execution disabled, set = execution enabled)
    ;         (used if bit 11 (NXE) in the EFER registeris set, otherwise the XD bit is reserved)
    .avl_high_pk_xd: resb 1
endstruc



; Sets paging in memory in the following layout:
; PML4 (entry 0) -> PDPT (entry 0) ->
;   PD (entry 0) -> PT (512 entries) -> 4KB pages covering 2MB of memory (different caching types)
;      (entry 1-4) -> three 2MB pages (write-back caching)
setBasicPaging:
    ; Push registers
    pushf
    push eax
    push ecx
    push esi
    push edi
    
    ; Zero the tables
    mov edi, PAGING_START
    mov ecx, (4 *512 *8) /4 ; 4 tables, 512 8-byte entries per table, copying 4 bytes at a time time
    xor eax, eax ; Set copied value (zero)
    rep stosd ; Copy

    ; You can simply OR flags with target address because it's aligned and low bits are 0
    ; Set a single PML4 entry
    mov dword [PML4_ADDRESS], 00000011b | PDPT_ADDRESS
    ; Set a single PDPT entry
    mov dword [PDPT_ADDRESS], 00000011b | PD_ADDRESS

    ; Set the first PD entry (points to PT)
    mov dword [PD_ADDRESS], 00000011b | PT_ADDRESS

    ; Map the first 2MB using 4KB pages in PT
    mov esi, PT_ADDRESS ; Entry address
    mov edi, 0x0 ; Actual page address
.PT_LOOP:
    ; Determine caching type depending on address
    cmp edi, 0xA_0000 ; Video memory and above
    jae .NO_CACHING
    mov eax, 00000011b ; Write-back caching flags
    jmp .AFTER_CACHE
.NO_CACHING:
    mov eax, 00011011b ; No caching flags
.AFTER_CACHE:
    ; Set flags and page address
    or eax, edi
    mov dword [esi], eax
    ; Increment entry and page addresses
    add esi, 8
    add edi, (1024 *4) ; 4KB
    cmp edi, (0x10_0000 *2) ; Check if we reached 2MB
    jne .PT_LOOP

    ; Map the 6MB more using 2MB pages in PD (can presume 2MB paging support in long mode)
    mov esi, PD_ADDRESS +8 ; Entry address
    ; Reuse EDI from previos code (value = 2MB)
.PD_LOOP:
    ; Set flags and page address
    mov eax, 10000011b ; Write-back caching + page-sized flags
    or eax, edi
    mov dword [esi], eax
    ; Increment entry and page
    add esi, 8
    add edi, (0x10_0000 *2) ; 2MB
    cmp edi, (0x10_0000 *8) ; Check if we reached 8MB
    jne .PD_LOOP

    ; Pop registers
    pop edi
    pop esi
    pop ecx
    pop eax
    popf
    ret

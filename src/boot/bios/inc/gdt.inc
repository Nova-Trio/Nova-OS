
; A GDT entry
struc Segment_descriptor
    .limit_low16: resw 1
    .base_low24: resb 3
    ; Access byte flags:
    ;   0: Accessed (CPU uses this)
    ;   1: Readable code or writable data
    ;   2: Data direction down (offset must be greater than limit)
    ;        or code conforming (0 = code can only be executed by the same priviledge level
    ;                            1 = code can be executed by the same or lower priviledge level)
    ;   3: Executable (0 = data, 1 = code)
    ;   4: Descriptor type (1 = code or data, 0 = for system (e.g. Task State Segment))
    ;   5-6: Priviledge level (0-3)
    ;   7: Present (1 = valid segment)
    .access_flags: resb 1
    ; Flags:
    ;   0: Reserved
    ;   1: Long-mode code (1 = long mode code segment, 0 = something else)
    ;   2: Size flag (0 = 16-bit protected mode, 1 = 32-bit protected mode
    ;        (same GDT can contain both entry types) (must be clear if the Long-mode code flag is set))
    ;   3: Granularity (0 = limit is in 1 byte blocks, 1 = limit is in 4 KiB blocks)
    .limit_high4_flags: resb 1 ; Bottom 4 bits = limit bits 16-19, top 4 bits = other flags
    .base_high8: resb 1
endstruc

; Macro to define a GDT entry
; Arguments: 32-bit base address, 20-bit limit address, access byte flags, other flags
%macro DEF_SEGMENT_DESCRIPTOR 4
dw (%2 & 0xFFFF) ; Limit address low 16 bits
dw (%1 & 0xFFFF) ; Base address low 16 bits
db ((%1 >> 16) & 0xFF) ; Base address middle 8 bits
db %3 ; access byte flags
db (((%2 >> 16) & 0x0F) | (%4 << 4)) ; Limit address high 4 bits and other flags
db ((%1 >> 24) & 0xFF) ; Base address high 8 bits
%endmacro

bits 64

; ELF header
struc ELF64_ehdr
    .e_ident_magic: resd 1 ; (0x7F E L F) signature
    .e_ident_class: resb 1 ; 1 = 32-bit, 2 = 64-bit
    .e_ident_data: resb 1 ; Encoding (1 = little endian, 2 = big endian)
    .e_ident_version: resb 1 ; ELF version (literally always 1)
    .e_ident_abi: resb 1 ; ABI (0 = System V)
    .e_ident_abi_version: resb 1 ; ABI version (depends on OS)
    .e_ident_pad: resb 7 ; Reserved

    .e_type: resw 1 ; 0 = none, 1 = object, 2 = executable, 3 = dynamic lib, 4 = core dump
    .e_machine: resw 1 ; Architecture (62 (not a typo) = x86-64)
    ; Not a mistake btw the ELF version is repeated here
    .e_version: resd 1 ; ELF version (literally always 1)
    .e_entry: resq 1 ; Entry point virtual address (0 for none)
    .e_phoff: resq 1 ; Program header table offset (0 for none)
    .e_shoff: resq 1 ; Section header table offset (0 for none)
    .e_flags: resd 1 ; Processor-specific flags

    .e_ehsize: resw 1 ; ELF header size in bytes (52 for 32-bit, 64 for 64-bit)
    .e_phentsize: resw 1 ; Program header table entry size
    .e_phnum: resw 1 ; Program header table entry count
    .e_shentsize: resw 1 ; Section header table entry size
    .e_shnum: resw 1 ; Section header table entry count
    .e_shstrndx: resw 1 ; Section header string table index
endstruc

; Program header entry
struc ELF64_phdr_entry
    .p_type: resd 1 ; Segment type (1 = PT_LOAD for loadable)
    .p_flags: resd 1 ; Segment flags (executable/readable/writable)
    .p_offset: resq 1 ; Segment offset relative to the file start
    .p_vaddr: resq 1 ; Segment destination virtual address
    .p_paddr: resq 1 ; Segment destination physical address
    .p_filesz: resq 1 ; Segment size in file
    .p_memsz: resq 1 ; Segment size in memory (if > file size, the subtraction remainding size must be zeroed)
    .p_align: resq 1 ; Segment alignment
endstruc

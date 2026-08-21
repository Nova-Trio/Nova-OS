bits 16
org 0x0

; Jump to start and set code segment
jmp 0x7C0:START

%include "inc/s1t2.inc"
%include "inc/auto.inc"

START:
    ; Disable interrupts
    cli
    ; Set data and extra segments
    mov ax, 0x7C0
    mov ds, ax
    mov es, ax
    ; Set stack from 0x7_FFFF to 0x7_0000 (physical start address 0x8_0000)
    mov ax, 0x7000
    mov ss, ax
    mov sp, 0x0
    ; Enable interrupts
    sti
    ; Save boot drive
    mov [boot_drive], dl
    ; Clear direction flag
    cld

    ; Set video mode (also clears screen)
    mov ah, 0x0
    mov al, 0x3 ; 80x25 text
    int 0x10

    ; Disable cursor
    mov ah, 0x1
    mov cx, 0010_0000__0000_000b ; Turn off cursor fully
    int 0x10

    ; Enhanced Disk Drive (EDD) check
    mov ah, 0x41
    mov bx, 0x55AA
    mov dl, [boot_drive]
    int 0x13
    jc EDD11_NOT_SUPPORTED ; Check if extensions are supported
    cmp bx, 0xAA55 ; Check if extensions are installed
    jne EDD11_NOT_SUPPORTED

    ; Get drive parameters
    mov word [drive_params +Drive_params26.SIZE], 26
    mov ah, 0x48
    mov dl, [boot_drive]
    mov si, drive_params
    int 0x13
    jc DRIVE_PARAMS_FAILED

    ; Get stage 2 sector count to load
    xor dx, dx
    mov ax, STAGE2_SIZE
    div word [drive_params +Drive_params26.bytes_per_sector]
    ; Check remainder
    test dx, dx
    jz .AFTER_REMAINDER
    inc ax
.AFTER_REMAINDER:
    mov [disk_packet +Disk_packet.sector_count], ax

    ; Load stage 2 into memory
    mov ah, 0x42
    mov dl, [boot_drive]
    mov si, disk_packet
    int 0x13
    jc STAGE2_READ_FAILED ; Check if read succeeded

    ; Save parition table offset into ball
    mov word [ball +S1t2Ball.p_partition_table], (PARTITION_TABLE +0x7C00)
    ; Save drive parameters into ball
    mov word [ball +S1t2Ball.p_drive_params], (drive_params +0x7C00)
    ; Save boot drive ID into ball
    mov al, [boot_drive]
    mov byte [ball +S1t2Ball.boot_drive], al
    ; Pass ball pointer to stage 2
    mov dx, ball
    ; Jump to stage 2
    jmp STAGE2_SEGMENT:0x0



; Helper routines
EDD11_NOT_SUPPORTED:
mov si, MSG_EDD11_NOT_SUPPORTED
jmp ERROR

DRIVE_PARAMS_FAILED:
mov si, MSG_DRIVE_PARAMS_FAILED
jmp ERROR

STAGE2_READ_FAILED:
mov si, MSG_STAGE2_READ_FAILED
jmp ERROR

; Prints error message from SI then halts
ERROR:
    mov ah, 0x0E ; Teletype output
    mov bh, 0x0 ; Video page 0
    mov bl, 0x7 ; Light grey text
.PRINT_LOOP:
    lodsb
    test al, al
    jz .NULL_HIT
    int 0x10
    jmp .PRINT_LOOP
.NULL_HIT:
    cli
.HALT_LOOP:
    hlt
    jmp .HALT_LOOP



; Mutable data
align 4
disk_packet:
istruc Disk_packet
    at Disk_packet.SIZE, db 16
    at Disk_packet.RESERVED, db 0
    at Disk_packet.sector_count, dw 0 ; Initialize at runtime
    at Disk_packet.dest_offset, dw 0x0
    at Disk_packet.dest_segment, dw STAGE2_SEGMENT
    at Disk_packet.start_sector, dq 1
iend

; Constant data
MSG_EDD11_NOT_SUPPORTED: db "Err: EDD 1.1 not supported", 0
MSG_DRIVE_PARAMS_FAILED: db "Err: Failed to get drive parameters", 0
MSG_STAGE2_READ_FAILED: db "Err: Failed to read stage 2", 0



; MBR Partition Table (values must be set by an external tool)
times 446-($-$$) db 0
PARTITION_TABLE: times (Partition_entry_size * 4) db 0

; MBR bootloader signature
dw 0xAA55

; Uninitialized data ouside the 512 bytes
BEGIN_UDATA equ $
boot_drive equ $
drive_params equ boot_drive +1 +3 ; extra +3 for alginment
ball equ drive_params +Drive_params26_size
AFTER_UDATA equ ball +S1t2Ball_size

; Make the assembler check if arbitrary BSS capacity is enough
%if (AFTER_UDATA - BEGIN_UDATA) > UDATA_CAPACITY
    %error "BSS size is above defined capacity limit"
%endif

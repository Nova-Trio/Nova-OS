
; Uninitialized data capacity (data outside the file and the 512 bytes)
UDATA_CAPACITY equ 128
STAGE2_ADDRESS equ 0x7C00 +512 +UDATA_CAPACITY
STAGE2_SEGMENT equ STAGE2_ADDRESS /16

struc Partition_entry
    .status: resb 1
    .start_hsc: resb 3
    .type: resb 1
    .end_hsc: resb 3
    .start_lba: resd 1
    .sector_count: resd 1
endstruc

struc Drive_params26
    .SIZE: resw 1
    .info_flags: resw 1
    .cylinder_count: resd 1
    .head_count: resd 1
    .sectors_per_track: resd 1
    .sector_count: resq 1
    .bytes_per_sector: resw 1
endstruc

; Disk address packet for LBA read operations
struc Disk_packet
    .SIZE: resb 1
    .RESERVED: resb 1 ; Reserved
    .sector_count: resw 1
    .dest_offset: resw 1
    .dest_segment: resw 1
    .start_sector: resq 1
endstruc

struc S1t2Ball
    .p_partition_table: resw 1
    .p_drive_params: resw 1
    .boot_drive: resb 1
endstruc

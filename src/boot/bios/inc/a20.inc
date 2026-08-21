bits 16

; Returns in AL: 1 if it succeeds, 0 otherwise
; AH may be modified
enableA20:
    ; Push registers
    push bx

    ; A20 enabled?
    call _checkA20
    test al, al
    jnz .RETURN

    ; BIOS enable A20 gate
    mov ax, 0x2401
    int 0x15
    jc .BIOS_INT_FAILED

    ; A20 enabled?
    call _checkA20
    test al, al
    jnz .RETURN

.BIOS_INT_FAILED:
    ; Reading 0xEE IO port (VEEEEEERY OLD METHOD FOR SPECIFIC MOTHERBOARDS)
    in al, 0xEE ; AL value is irrelevant

    ; A20 enabled?
    call _checkA20
    cmp al, 1
    je .RETURN

    ; Keyboard controller method
    ; Disable interrupts
    cli

    ; Disable keyboard
    call _waitBoardReadyToRecv
    mov al, 0xAD
    out 0x64, al

    ; Signal a read
    call _waitBoardReadyToRecv
    mov al, 0xD0
    out 0x64, al
    ; Receive keyboard controller configuration byte
    call _waitBoardReadyToSend
    in al, 0x60
    mov bl, al

    ; Signal a write
    call _waitBoardReadyToRecv
    mov al, 0xD1
    out 0x64, al
    ; Send keyboard controller configuration byte with A20 enabled
    call _waitBoardReadyToRecv
    mov al, bl
    or al, 2 ; Set A20 enabled bit
    out 0x60, al

    ; Enable keyboard
    call _waitBoardReadyToRecv
    mov al, 0xAE
    out 0x64, al

    ; Enable interrupts
    sti

    ; A20 enabled?
    call _checkA20
    cmp al, 1
    je .RETURN
    
    ; System Control Port A (Fast A20 method)
    in al, 0x92
    or al, 2 ; A20 bit
    and al, 0xFE ; Set the reboot bit to 0 (just in case the hardware gave us 1)
    out 0x92, al

    ; A20 enabled?
    call _checkA20
    ;cmp al, 1
    ;je .RETURN

.RETURN:
    ; Pop registers and return
    pop bx
    ret



; Returns in AL: 1 if it succeeds, 0 otherwise
; AH may be modified
_checkA20:
    ; Stop interrupts
    cli
    ; Push registers
    push fs
    push si
    push gs
    push di

    ; Set FS:SI to 0x0:0x500 (0x500)
    xor ax, ax
    mov fs, ax
    mov si, 0x500

    ; Set GS:DI to 0xFFFF:0x510 (0x100500)
    not ax
    mov gs, ax
    mov di, 0x510

    ; Save data there just in case there is
    mov byte al, [fs:si]
    mov byte ah, [gs:di]
    push ax

    ; Edit data then check if the address wraps around
    mov byte [fs:si], 0x00
    mov byte [gs:di], 0xFF
    cmp byte [fs:si], 0xFF

    ; Put original data back
    pop ax
    mov byte [gs:di], ah
    mov byte [fs:si], al

    ; Use check result
    mov ax, 0
    je .RETURN
    inc al ; AX = 1

.RETURN:
    ; Pop registers
    pop di
    pop gs
    pop si
    pop fs
    ; Enable interrupts and return
    sti
    ret

; Waits until input buffer is clear
_waitBoardReadyToRecv:
    in al, 0x64
    test al, 10b
    jnz _waitBoardReadyToRecv
    ret

; Waits until response byte arrives
_waitBoardReadyToSend:
    in al, 0x64
    test al, 01b
    jz _waitBoardReadyToSend
    ret

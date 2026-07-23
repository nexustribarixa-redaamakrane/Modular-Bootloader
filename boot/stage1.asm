BITS 16
ORG 0x7C00

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    mov [boot_drive], dl    ; Store BIOS boot drive number

    ; Display early boot message
    mov si, msg_loading
    call print_string

    ; Load Stage 2 from disk (LBA 1, Sector 2) using INT 13h AH=0x02
    mov ah, 0x02
    mov al, 32              ; Read 32 sectors (16 KiB)
    mov ch, 0               ; Cylinder 0
    mov cl, 2               ; Sector 2 (1-based index)
    mov dh, 0               ; Head 0
    mov dl, [boot_drive]
    mov bx, 0x8000          ; Load to 0x0000:0x8000
    int 0x13
    jc disk_error

    ; Jump to Stage 2 entry point at 0x8000
    mov dl, [boot_drive]
    jmp 0x0000:0x8000

disk_error:
    mov si, msg_error
    call print_string
    cli
    hlt

print_string:
    lodsb
    or al, al
    jz .done
    mov ah, 0x0E
    mov bh, 0x00
    mov bl, 0x07
    int 0x10
    jmp print_string
.done:
    ret

boot_drive:  db 0
msg_loading: db "MBL Stage 1...", 0x0D, 0x0A, 0
msg_error:   db "Disk Read Error!", 0x0D, 0x0A, 0

TIMES 510 - ($ - $$) db 0
DW 0xAA55

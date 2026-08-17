;=============================================================================
; MBL Stage 1 - 512-byte MBR boot sector
; Loads Stage 2 from LBA 1 to 0x0000:0x8000 and jumps to it.
;
; Layout contract (must match tools/build_image.py):
;   LBA 0   : stage1 (this file, 512 bytes)
;   LBA 1..15: stage2 (15 sectors, up to 7680 bytes)
;   LBA 16+ : boot image (loaded by stage2 to 0x80000)
;   LBA 128+: OpenWindows OWFS volume (byte offset 0x10000)
;=============================================================================

BITS 16
ORG 0x7C00

%define STAGE2_LBA      1
%define STAGE2_SECTORS  15
%define STAGE2_SEG      0x0000
%define STAGE2_OFF      0x8000

%define BOOTINFO        0x0500      ; mbl_bootinfo_t written here
%define BOOTINFO_MAGIC  0x314C424D  ; 'MBL1'
%define DAP_ADDR        0x0510

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    mov [boot_drive], dl

    ; ---- publish boot info block (stage2 + C read this) ----
    mov word [BOOTINFO + 0], BOOTINFO_MAGIC & 0xFFFF
    mov word [BOOTINFO + 2], BOOTINFO_MAGIC >> 16
    mov [BOOTINFO + 4], dl
    mov byte [BOOTINFO + 5], 0
    mov word [BOOTINFO + 6], 0

    mov si, msg_stage1
    call print_string

    ; ---- load stage2 ----
    call check_ext
    jc .chs

    mov byte [dap + 0], 0x10        ; DAP size
    mov byte [dap + 1], 0           ; reserved
    mov word [dap + 2], STAGE2_SECTORS
    mov word [dap + 4], STAGE2_OFF
    mov word [dap + 6], STAGE2_SEG
    mov dword [dap + 8], STAGE2_LBA ; lba low
    mov dword [dap + 12], 0         ; lba high

    mov ah, 0x42
    mov dl, [boot_drive]
    mov si, dap
    int 0x13
    jc disk_error
    jmp .done

.chs:
    mov ah, 0x02
    mov al, STAGE2_SECTORS
    mov ch, 0                       ; cylinder 0
    mov cl, STAGE2_LBA + 1          ; sector (1-based)
    mov dh, 0                       ; head 0
    mov dl, [boot_drive]
    mov bx, STAGE2_OFF
    int 0x13
    jc disk_error

.done:
    mov dl, [boot_drive]
    jmp STAGE2_SEG:STAGE2_OFF

;------------------------------------------------------------------------------
; check_ext() - CF=1 if INT 13h extended disk functions are unavailable
;------------------------------------------------------------------------------
check_ext:
    mov ax, 0x4100
    mov bx, 0x55AA
    mov dl, [boot_drive]
    int 0x13
    jc .bad
    cmp bx, 0xAA55
    jne .bad
    test cx, 0x0001                 ; bit 0: extended disk access
    jz .bad
    clc
    ret
.bad:
    stc
    ret

disk_error:
    mov si, msg_error
    call print_string
.hang:
    cli
    hlt
    jmp .hang

;------------------------------------------------------------------------------
; print_string() - teletype output (AL = terminator: 0)
;------------------------------------------------------------------------------
print_string:
    lodsb
    or al, al
    jz .done
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    jmp print_string
.done:
    ret

boot_drive: db 0

dap:
    db 0x10, 0
    dw STAGE2_SECTORS
    dw STAGE2_OFF
    dw STAGE2_SEG
    dd STAGE2_LBA
    dd 0

msg_stage1: db "MBL Stage 1: booting...", 0x0D, 0x0A, 0
msg_error:  db "MBL Stage 1: disk read failed!", 0x0D, 0x0A, 0

; Boot signature
TIMES 510 - ($ - $$) db 0
DW 0xAA55

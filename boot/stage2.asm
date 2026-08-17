;=============================================================================
; MBL Stage 2 - 16-bit boot image loader
; Loaded by stage1 at 0x0000:0x8000. Loads the linked boot image (16-bit
; entry stub + BIOS trampoline + 32-bit C bootloader) from LBA 16 to
; 0x8000:0x0000 (linear 0x80000), then far-jumps to its real-mode entry.
;
; The boot image's own entry stub performs the A20/GDT/protected-mode
; transition, so stage2 stays a trivial flat-binary loader.
;=============================================================================

BITS 16
ORG 0x8000

%define BOOTIMG_LBA     16
%define BOOTIMG_SECTORS 96          ; capacity 49152 bytes (must fit the blob)
%define BOOTIMG_SEG     0x8000
%define BOOTIMG_OFF     0x0000

%define BOOTINFO        0x0500
%define BOOTINFO_MAGIC  0x314C424D  ; 'MBL1'

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    mov [boot_drive], dl

    mov si, msg_stage2
    call print_string

    ; ---- refresh boot info block ----
    mov word [BOOTINFO + 0], BOOTINFO_MAGIC & 0xFFFF
    mov word [BOOTINFO + 2], BOOTINFO_MAGIC >> 16
    mov [BOOTINFO + 4], dl
    mov byte [BOOTINFO + 5], 0
    mov word [BOOTINFO + 6], 0

    ; ---- load boot image ----
    call check_ext
    jc .use_chs

    mov byte [dap + 0], 0x10
    mov byte [dap + 1], 0
    mov word [dap + 2], BOOTIMG_SECTORS
    mov word [dap + 4], BOOTIMG_OFF
    mov word [dap + 6], BOOTIMG_SEG
    mov dword [dap + 8], BOOTIMG_LBA
    mov dword [dap + 12], 0

    mov ah, 0x42
    mov dl, [boot_drive]
    mov si, dap
    int 0x13
    jnc .after_read

.use_chs:
    ; Query drive geometry via INT 13h AH=08h
    mov ah, 0x08
    mov dl, [boot_drive]
    xor di, di
    mov es, di
    int 0x13
    jc .chs_defaults

    ; Sectors per track in CL bits 0..5
    and cx, 0x3F
    mov [sectors_per_track], cx

    ; Number of heads in DH (0-based max head number, so count is DH + 1)
    movzx ax, dh
    inc ax
    mov [num_heads], ax
    jmp .chs_start

.chs_defaults:
    mov word [sectors_per_track], 18
    mov word [num_heads], 2

.chs_start:
    mov word [cur_lba], BOOTIMG_LBA
    mov word [cur_seg], BOOTIMG_SEG
    mov word [rem_sectors], BOOTIMG_SECTORS

.chs_loop:
    cmp word [rem_sectors], 0
    je .after_read

    ; Convert cur_lba to CHS:
    ; LBA / sectors_per_track -> quotient in AX, remainder (0-based sector) in DX
    mov ax, [cur_lba]
    xor dx, dx
    div word [sectors_per_track]
    inc dx                          ; 1-based sector
    mov cl, dl                      ; CL = sector (bits 0-5)

    ; AX = track index = (cylinder * num_heads) + head
    ; AX / num_heads -> quotient (cylinder) in AX, remainder (head) in DX
    xor dx, dx
    div word [num_heads]
    mov dh, dl                      ; DH = head
    mov ch, al                      ; CH = cylinder (low 8 bits)
    shl ah, 6
    or cl, ah                       ; CL bits 6-7 = cylinder high 2 bits

    ; Read 1 sector into cur_seg:0x0000
    mov ax, [cur_seg]
    mov es, ax
    xor bx, bx                      ; ES:BX = cur_seg:0x0000

    mov byte [retry_count], 3
.retry_read:
    mov ah, 0x02
    mov al, 1                       ; 1 sector
    mov dl, [boot_drive]
    int 0x13
    jnc .read_ok

    ; Reset disk system on error and retry
    xor ax, ax
    mov dl, [boot_drive]
    int 0x13
    dec byte [retry_count]
    jnz .retry_read
    jmp disk_error

.read_ok:
    inc word [cur_lba]
    add word [cur_seg], 0x0020      ; 512 bytes = 0x20 paragraphs
    dec word [rem_sectors]
    jmp .chs_loop

.after_read:
    ; ---- jump to boot image real-mode entry ----
    mov dl, [boot_drive]
    jmp BOOTIMG_SEG:BOOTIMG_OFF

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

boot_drive:         db 0
retry_count:        db 0
sectors_per_track:  dw 18
num_heads:          dw 2
cur_lba:            dw 0
cur_seg:            dw 0
rem_sectors:        dw 0

dap:
    db 0x10, 0
    dw BOOTIMG_SECTORS
    dw BOOTIMG_OFF
    dw BOOTIMG_SEG
    dd BOOTIMG_LBA
    dd 0

msg_stage2: db "MBL Stage 2: loading boot image...", 0x0D, 0x0A, 0
msg_error:  db "MBL Stage 2: disk read failed!", 0x0D, 0x0A, 0

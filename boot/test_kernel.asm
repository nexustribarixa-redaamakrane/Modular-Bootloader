;=============================================================================
; MBL Test Kernel - a tiny flat 32-bit program loaded to 0x200000.
; Prints the boot configuration passed by the bootloader and halts.
;
; cdecl handoff:  kernel(mbl_boot_config_t *cfg)  [cfg on the stack]
;   mbl_boot_config_t: +0 magic, +4 boot_drive, +8 kernel_size,
;                      +12 sucs_cfg.active_mode
;=============================================================================

BITS 32
ORG 0x200000

%define VGA       0xB8000
%define ATTR      0x07

start:
    cli
    mov ebp, esp
    mov eax, [ebp + 4]           ; cfg pointer
    mov [cfg_ptr], eax

    ; own segments and stack (well above the bootloader's 0x90000 area)
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov esp, 0x180000

    ; clear screen
    mov edi, VGA
    mov ecx, 80 * 25
    mov eax, 0x0720
    rep stosw

    ; banner
    mov edi, VGA + 1 * 160 + 14 * 2
    mov esi, msg_banner
    call putstr

    ; boot drive
    mov eax, [cfg_ptr]
    movzx eax, byte [eax + 4]
    mov edi, VGA + 3 * 160 + 14 * 2
    mov esi, msg_drive
    call putstr
    call puthex32

    ; kernel size
    mov eax, [cfg_ptr]
    mov eax, [eax + 8]
    mov edi, VGA + 4 * 160 + 14 * 2
    mov esi, msg_size
    call putstr
    call puthex32

    ; SUCS active mode (SuperUnicode boot config)
    mov eax, [cfg_ptr]
    mov eax, [eax + 12]
    mov edi, VGA + 5 * 160 + 14 * 2
    mov esi, msg_sucs
    call putstr
    call puthex32

    ; magic
    mov eax, [cfg_ptr]
    mov eax, [eax]
    mov edi, VGA + 6 * 160 + 14 * 2
    mov esi, msg_magic
    call putstr
    call puthex32

.halt:
    cli
    hlt
    jmp .halt

;------------------------------------------------------------------------------
; putstr - print null-terminated string at [edi]
;------------------------------------------------------------------------------
putstr:
.loop:
    lodsb
    test al, al
    jz .done
    mov ah, ATTR
    mov [edi], ax
    add edi, 2
    jmp .loop
.done:
    ret

;------------------------------------------------------------------------------
; puthex32 - print eax as 8 uppercase hex digits at [edi]
;------------------------------------------------------------------------------
puthex32:
    mov ecx, 8
.loop:
    rol eax, 4
    mov edx, eax
    and edx, 0x0F
    cmp dl, 10
    jb .num
    add dl, 'A' - 10
    jmp .put
.num:
    add dl, '0'
.put:
    mov dh, ATTR
    mov [edi], dx
    add edi, 2
    loop .loop
    ret

section .data
cfg_ptr:    dd 0
msg_banner: db "MBL TEST KERNEL", 0
msg_drive:  db "boot drive : 0x", 0
msg_size:   db "kernel size: 0x", 0
msg_sucs:   db "SUCS mode  : 0x", 0
msg_magic:  db "config magic:0x", 0

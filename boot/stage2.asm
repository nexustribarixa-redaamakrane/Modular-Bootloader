BITS 16
ORG 0x8000

stage2_entry:
    cli
    mov ax, cs
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    ; 1. Query VBE Mode Info for Mode 0x0118 (1024x768x32bpp)
    mov ax, 0x4F01
    mov cx, 0x4118          ; Mode 0x0118 with Linear Framebuffer Bit (0x4000)
    mov di, 0x9000          ; VBE Mode Info Block destination
    int 0x10
    cmp ax, 0x004F
    jne try_vbe_800

    mov word [vbe_width], 1024
    mov word [vbe_height], 768
    mov byte [vbe_bpp], 32
    jmp set_vbe_mode

try_vbe_800:
    mov ax, 0x4F01
    mov cx, 0x4115          ; Mode 0x0115 (800x600x32bpp)
    mov di, 0x9000
    int 0x10
    mov word [vbe_width], 800
    mov word [vbe_height], 600
    mov byte [vbe_bpp], 32

set_vbe_mode:
    ; Set VBE graphics mode
    mov ax, 0x4F02
    mov bx, 0x4118          ; 1024x768x32 LFB
    int 0x10

    ; Extract LFB Physical Address from VBE Info Block (offset 40)
    mov eax, [0x9000 + 40]
    mov [vbe_lfb], eax
    mov ax, [0x9000 + 16]   ; Bytes Per Scanline (Pitch)
    mov [vbe_pitch], ax

    ; Populate Boot Parameter Block at 0x7000
    mov eax, [vbe_lfb]
    mov [0x7000], eax       ; fb->pixels
    mov edx, 0
    mov dx, [vbe_width]
    mov [0x7004], edx       ; fb->width
    mov dx, [vbe_height]
    mov [0x7008], edx       ; fb->height
    mov dx, [vbe_pitch]
    mov [0x700C], edx       ; fb->pitch
    mov dl, [vbe_bpp]
    mov [0x7010], edx       ; fb->bpp

    ; 2. Enable Fast A20 Line via Port 0x92
    in al, 0x92
    or al, 2
    out 0x92, al

    ; 3. Load Global Descriptor Table (GDT)
    cli
    lgdt [gdt_descriptor]

    ; 4. Protected Mode Transition
    mov eax, cr0
    or eax, 1
    mov cr0, eax

    ; Far jump to 32-bit Code Segment (0x08)
    jmp 0x08:pm_start

BITS 32
pm_start:
    ; Reload Protected Mode Segment Registers
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000

    ; Build mbl_boot_state_t at 0x7100
    mov dword [0x7100], 0x7000   ; state->framebuffer = &fb_struct (0x7000)
    mov dword [0x7104], 0x000F   ; state->boot_flags = VBE | KBD | MOUSE | EXTSUCS
    mov dword [0x7108], 0        ; state->selected_entry = 0
    mov dword [0x710C], 0        ; state->mode = SUCS_MODE_BASE

    ; Pass pointer to mbl_boot_state_t at 0x7100 and jump to C stage
    push 0x7100
    mov eax, 0x100000            ; C kernel stage entry address
    call eax

    cli
    hlt

; Data Definitions
vbe_width:  dw 1024
vbe_height: dw 768
vbe_pitch:  dw 4096
vbe_bpp:    db 32
vbe_lfb:    dd 0xE0000000

; GDT Table Definition
align 8
gdt_start:
    ; Null Descriptor
    dd 0x00000000
    dd 0x00000000

    ; 32-bit Code Segment Descriptor (Base: 0, Limit: 4GB, Access: 0x9A, Flags: 0xCF)
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 0x9A
    db 0xCF
    db 0x00

    ; 32-bit Data Segment Descriptor (Base: 0, Limit: 4GB, Access: 0x92, Flags: 0xCF)
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 0x92
    db 0xCF
    db 0x00
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

;=============================================================================
; MBL Stage 2 - Disk Loader + VBE Loader + Protected Mode Bridge
; NASM flat binary, loaded by stage1 at 0x0000:0x8000 (16-bit, then 32-bit).
;
; Flow:
;   1. Load stage3 (boot GUI) from disk to 0x80000  (INT 13h AH=0x42, chunked)
;   2. Load the kernel image from disk to staging 0x10000 (INT 13h AH=0x42,
;      chunked) - the kernel's real destination (0x200000) is above the 1 MiB
;      real-mode boundary, so it is staged low and copied in protected mode.
;   3. Query + set a VBE linear framebuffer mode (1024x768x32, falling back to
;      800x600x32 on failure - checking BOTH the 0x4F01 query AND the 0x4F02
;      set return, which the previous stage2 got wrong).
;   4. Enable A20, load the GDT, switch to 32-bit protected mode.
;   5. Copy the kernel from staging (0x10000) to its load address (0x200000).
;   6. Populate mbl_boot_state_t at 0x7100 and cdecl-call stage3 at 0x80000.
;
; Memory map (matches the documented MBL map):
;   0x7000  mbl_framebuffer_t (C layout - see include/mbl_framebuffer.h)
;   0x7100  mbl_boot_state_t  (+ MBL extension fields consumed by stage3)
;   0x8000  stage2 code
;   0x9000  VBE ModeInfoBlock scratch
;   0x10000 kernel staging buffer (256 KiB max)
;   0x80000 stage3 code
;   0x90000 protected mode stack
;   0x100000 kernel entry address (populated by stage3 handoff)
;   0x200000 kernel load address (source for stage3 handoff copy)
;=============================================================================

BITS 16
ORG 0x8000

; Disk layout (must match tools/build_image.py)
%define STAGE1_SECTORS    1
%define STAGE2_LBA        1
%define STAGE3_LBA        33
%define STAGE3_SECTORS    64          ; 32 KiB capacity
%define KERNEL_LBA        97
%define KERNEL_SECTORS    512         ; 256 KiB max kernel image
%define KERNEL_IMAGE_SIZE (KERNEL_SECTORS * 512)

; Memory map constants
%define FB_BLOCK         0x7000
%define STATE_BLOCK      0x7100
%define VBE_MODE_INFO    0x9000
%define VBE_INFO_BLOCK   0x9200     ; 0x4F00 buffer (mode list lives inside it)
%define STACK_TOP        0x7C00
%define STAGE3_ADDR      0x80000
%define KERNEL_STAGING   0x10000
%define KERNEL_LOAD_ADDR 0x200000
%define PM_STACK_TOP     0x90000

; mbl_framebuffer_t field offsets (include/mbl_framebuffer.h)
%define FB_OFF_PIXELS     0
%define FB_OFF_BACK       4
%define FB_OFF_WIDTH      8
%define FB_OFF_HEIGHT     12
%define FB_OFF_PITCH      16
%define FB_OFF_BPP        20
%define FB_OFF_BACK_ALLOC 24

; mbl_boot_state_t field offsets (include/mbl_core.h), then MBL extension
%define ST_OFF_FRAMEBUFFER  0
%define ST_OFF_BOOT_FLAGS   4
%define ST_OFF_SEL_ENTRY    8
%define ST_OFF_MODE         12
%define ST_OFF_MMAP_PTR     16
%define ST_OFF_MMAP_CNT     20
%define ST_OFF_ACTIVE_MODE  24      ; sucs_kernel_boot_config_t.active_mode
%define ST_OFF_PENDING_MODE 28      ; .pending_mode
%define ST_OFF_REBOOT_REQ   32      ; .reboot_required (bool)
%define ST_OFF_MODE_CHANGES 36      ; .mode_change_count
%define ST_OFF_KERNEL_LOAD  40      ; stage3 handoff: kernel source address
%define ST_OFF_KERNEL_SIZE  44      ; stage3 handoff: kernel byte count

; Boot flags (mirror MBL_BOOT_FLAG_* in mbl_core.h)
%define MBL_BOOT_FLAG_VBE_ACTIVE      (1 << 0)
%define MBL_BOOT_FLAG_KBD_READY       (1 << 1)
%define MBL_BOOT_FLAG_MOUSE_READY     (1 << 2)
%define MBL_BOOT_FLAG_EXTSUCS_ENABLED (1 << 3)

section .text

stage2_entry:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, STACK_TOP
    sti

    mov [boot_drive], dl

    mov si, msg_stage2
    call print_string

    ; ---- Load stage3 to 0x80000 (segment 0x8000:0x0000) ----
    mov ax, STAGE3_LBA
    mov cx, STAGE3_SECTORS
    mov dx, 0x8000
    xor bx, bx
    call read_sectors
    jc fatal_disk

    ; ---- Load kernel image to staging 0x10000 (segment 0x1000:0x0000) ----
    mov ax, KERNEL_LBA
    mov cx, KERNEL_SECTORS
    mov dx, 0x1000
    xor bx, bx
    call read_sectors
    jc fatal_disk

    ; ---- VBE init (query + set, with 800x600 fallback) ----
    call vbe_init
    jc fatal_vbe

    ; ---- Enable Fast A20 via port 0x92 ----
    in al, 0x92
    or al, 0x02
    out 0x92, al

    ; ---- Load GDT and enter protected mode ----
    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:pm_entry

;=============================================================================
; read_sectors(ax=LBA, cx=count, dx=buffer_segment, bx=buffer_offset)
; Chunked INT 13h AH=0x42 reads (64 sectors max per call) so the DAP buffer
; never crosses a 64 KiB real-mode segment boundary. DS/ES restored to 0.
;=============================================================================
read_sectors:
    push ax
    push cx
    push dx
    push bx
    push si
    push bp

    mov [read_lba], ax
    mov [read_count], cx
    mov [read_seg], dx
    mov [read_off], bx

    ; ---- Verify INT 13h extensions are available ----
    mov ax, 0x4100
    mov bx, 0x55AA
    mov dl, [boot_drive]
    int 0x13
    jc .fail
    cmp bx, 0xAA55
    jne .fail
    test cx, 0x0001             ; bit 0: extended disk access functions
    jz .fail
    xor ax, ax
    mov ds, ax
    mov es, ax

.ext_loop:
    ; chunk = min(remaining, 64)
    mov ax, [read_count]
    test ax, ax
    jz .done
    cmp ax, 64
    jbe .chunk_ok
    mov ax, 64
.chunk_ok:
    mov [dap_count], ax
    sub [read_count], ax

    ; populate DAP
    mov ax, [read_lba]
    mov [dap_lba], ax
    mov dword [dap_lba + 4], 0
    mov ax, [read_seg]
    mov [dap_seg], ax
    mov ax, [read_off]
    mov [dap_off], ax

    mov ah, 0x42
    mov dl, [boot_drive]
    mov si, dap
    int 0x13
    xor ax, ax
    mov ds, ax
    mov es, ax
    jc .fail

    ; advance LBA by chunk
    mov ax, [dap_count]
    add [read_lba], ax
    ; advance segment by chunk*32 paragraphs (chunk*512 bytes / 16)
    mov ax, [dap_count]
    shl ax, 5
    add [read_seg], ax
    jmp .ext_loop

.done:
    clc
    jmp .out
.fail:
    stc
.out:
    pop bp
    pop si
    pop bx
    pop dx
    pop cx
    pop ax
    ret

;=============================================================================
; vbe_init() - select a 32bpp linear framebuffer mode and fill the fb struct
; at 0x7000 using the documented mbl_framebuffer_t layout.
;
;   VBE mode numbers are vendor-defined, so no fixed mode can be trusted.
;   Walk the BIOS-provided VBE mode list (0x4F00), keep the highest-resolution
;   mode that is 32bpp + LFB, then set that exact mode. Both the 0x4F01 query
;   and the 0x4F02 set returns are checked (the old stage2 set 0x4118
;   unconditionally, so the fallback could never actually be selected).
;   A fixed candidate list is used only if 0x4F00 is unavailable.
;=============================================================================
vbe_init:
    xor ebp, ebp                    ; best area so far
    xor bx, bx                      ; best mode so far
    mov [mlist_ok], word 0

    ; ---- obtain the BIOS mode list ----
    mov dword [VBE_INFO_BLOCK], 'VBE2'
    mov ax, 0x4F00
    mov di, VBE_INFO_BLOCK           ; separate from VBE_MODE_INFO: the mode
                                     ; list lives inside this buffer, so a
                                     ; 0x4F01 write to 0x9000 must not clobber it
    int 0x10
    mov [0x0500], ax                 ; diag: 0x4F00 result
    cmp ax, 0x004F
    jne .fallback
    xor ax, ax
    mov ds, ax
    cmp dword [VBE_INFO_BLOCK], 'VESA'
    jne .fallback
    mov ax, [VBE_INFO_BLOCK + 0x10] ; VideoModePtr segment
    mov [mlist_seg], ax
    mov [0x0504], ax                 ; diag
    mov ax, [VBE_INFO_BLOCK + 0x0E] ; VideoModePtr offset
    mov [mlist_off], ax
    mov [0x0506], ax                 ; diag
    mov [mlist_ok], word 1
    mov [0x0508], word 1             ; diag
.mloop:
    mov es, [mlist_seg]             ; reload - int 0x10 may clobber es
    mov si, [mlist_off]
    movzx cx, word [es:si]
    add word [mlist_off], 2
    cmp cx, 0xFFFF
    je .pick
    call vbe_query_mode
    jc .mloop
    movzx eax, word [VBE_MODE_INFO + 0x12]
    movzx edx, word [VBE_MODE_INFO + 0x14]
    imul eax, edx
    cmp eax, ebp
    jbe .mloop
    mov ebp, eax
    mov bx, cx
    jmp .mloop

    ; ---- defensive fallback: fixed candidate list ----
.fallback:
    mov si, vbe_candidates
.fall_loop:
    movzx cx, word [si]
    cmp cx, 0xFFFF
    je .pick
    call vbe_query_mode
    jc .fall_skip
    movzx eax, word [VBE_MODE_INFO + 0x12]
    movzx edx, word [VBE_MODE_INFO + 0x14]
    imul eax, edx
    cmp eax, ebp
    jbe .fall_skip
    mov ebp, eax
    mov bx, cx
.fall_skip:
    add si, 2
    jmp .fall_loop

.pick:
    test bx, bx
    jz .fail
    mov [vbe_mode], bx
    mov cx, bx
    call vbe_query_mode             ; refresh 0x9000 for the chosen mode
    jc .fail
.set:
    mov ax, 0x4F02
    mov bx, [vbe_mode]
    or bx, 0x4000                    ; bit 14 = enable linear framebuffer model
    int 0x10
    cmp ax, 0x004F
    jne .fail
    xor ax, ax
    mov ds, ax
    call fb_store
    clc
    ret
.fail:
    stc
    ret

; vbe_query_mode(cx=mode) - fills VBE_MODE_INFO, CF=1 if unusable
;
; Some VBE implementations (including QEMU's std-VGA BIOS) do not set the
; "linear framebuffer" ModeAttributes bit (bit 7), so do not require it.
; Instead require: mode supported + graphics, 32 bpp (ARGB dword for the
; whole rendering stack), and a non-null PhysBasePtr (the real LFB proof).
vbe_query_mode:
    mov ax, 0x4F01
    mov di, VBE_MODE_INFO
    int 0x10
    cmp ax, 0x004F
    jne .bad
    xor ax, ax
    mov ds, ax
    test word [VBE_MODE_INFO], 0x0011   ; mode supported + graphics mode
    jz .bad
    cmp byte [VBE_MODE_INFO + 0x19], 32 ; bits per pixel (ARGB dword)
    jne .bad
    cmp dword [VBE_MODE_INFO + 0x28], 0 ; must expose a linear framebuffer
    je .bad
    clc
    ret
.bad:
    xor ax, ax
    mov ds, ax
    mov [0x050A], cx                 ; diag: last queried mode
    mov ax, [VBE_MODE_INFO]
    mov [0x050C], ax                 ; diag: attrs
    movzx ax, byte [VBE_MODE_INFO + 0x19]
    mov [0x050E], ax                 ; diag: bpp
    mov eax, [VBE_MODE_INFO + 0x28]
    mov [0x0510], eax                ; diag: PhysBasePtr
    stc
    ret

; fb_store() - write mbl_framebuffer_t at FB_BLOCK from VBE_MODE_INFO
fb_store:
    mov eax, [VBE_MODE_INFO + 0x28]     ; PhysBasePtr -> pixels
    mov [FB_BLOCK + FB_OFF_PIXELS], eax
    mov dword [FB_BLOCK + FB_OFF_BACK], 0
    movzx eax, word [VBE_MODE_INFO + 0x12]  ; XResolution -> width
    mov [FB_BLOCK + FB_OFF_WIDTH], eax
    movzx eax, word [VBE_MODE_INFO + 0x14]  ; YResolution -> height
    mov [FB_BLOCK + FB_OFF_HEIGHT], eax
    movzx eax, word [VBE_MODE_INFO + 0x10]  ; BytesPerScanLine -> pitch
    mov [FB_BLOCK + FB_OFF_PITCH], eax
    movzx eax, byte [VBE_MODE_INFO + 0x19]  ; BitsPerPixel -> bpp
    mov [FB_BLOCK + FB_OFF_BPP], eax
    mov dword [FB_BLOCK + FB_OFF_BACK_ALLOC], 0
    ret

;=============================================================================
; Error helpers / teletype output (text mode only - used before VBE graphics)
;=============================================================================
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

fatal_disk:
    mov si, msg_disk
    call print_string
    cli
    hlt

fatal_vbe:
    mov si, msg_vbe
    call print_string
    cli
    hlt

;=============================================================================
; 32-bit protected mode
;=============================================================================
BITS 32
pm_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, PM_STACK_TOP

    ; ---- Copy kernel from staging buffer to 0x200000 ----
    mov esi, KERNEL_STAGING
    mov edi, KERNEL_LOAD_ADDR
    mov ecx, KERNEL_IMAGE_SIZE / 4
    cld
    rep movsd

    ; ---- Populate mbl_boot_state_t at 0x7100 ----
    mov dword [STATE_BLOCK + ST_OFF_FRAMEBUFFER], FB_BLOCK
    mov dword [STATE_BLOCK + ST_OFF_BOOT_FLAGS], MBL_BOOT_FLAG_VBE_ACTIVE | MBL_BOOT_FLAG_KBD_READY
    mov dword [STATE_BLOCK + ST_OFF_SEL_ENTRY], 0
    mov dword [STATE_BLOCK + ST_OFF_MODE], 0
    mov dword [STATE_BLOCK + ST_OFF_MMAP_PTR], 0
    mov dword [STATE_BLOCK + ST_OFF_MMAP_CNT], 0
    mov dword [STATE_BLOCK + ST_OFF_ACTIVE_MODE], 0
    mov dword [STATE_BLOCK + ST_OFF_PENDING_MODE], 0
    mov dword [STATE_BLOCK + ST_OFF_REBOOT_REQ], 0
    mov dword [STATE_BLOCK + ST_OFF_MODE_CHANGES], 0
    mov dword [STATE_BLOCK + ST_OFF_KERNEL_LOAD], KERNEL_LOAD_ADDR
    mov dword [STATE_BLOCK + ST_OFF_KERNEL_SIZE], KERNEL_IMAGE_SIZE

    ; ---- cdecl call into stage3: push state, call 0x80000 ----
    mov eax, STAGE3_ADDR
    push dword STATE_BLOCK
    call eax

    cli
    hlt

;=============================================================================
; Data (real-mode accessible until the PM transition)
;=============================================================================
align 4
dap:
    db 0x10                         ; DAP size
    db 0                            ; reserved
dap_count:
    dw 0
dap_off:
    dw 0
dap_seg:
    dw 0
dap_lba:
    dd 0
    dd 0

read_lba:   dw 0
read_count: dw 0
read_seg:   dw 0
read_off:   dw 0

boot_drive: db 0
vbe_mode:   dw 0
mlist_seg:  dw 0
mlist_off:  dw 0
mlist_ok:   dw 0

; Candidate VBE modes, best resolution first. Covers the QEMU std-VGA table
; (0x11C = 1024x768x32, 0x118 = 800x600x32, 0x115 = 800x600x24) as well as
; the common BIOS convention (0x118 = 1024x768x32). 24bpp modes are skipped
; by the bpp check since the whole stack assumes ARGB dwords.
vbe_candidates:
    dw 0x411C, 0x4118, 0x411D, 0x4119, 0x4114, 0x4116, 0x4111, 0x4115, 0xFFFF

msg_stage2: db "MBL Stage 2: VBE + disk loader", 0x0D, 0x0A, 0
msg_disk:   db "Stage 2 Disk Error!", 0x0D, 0x0A, 0
msg_vbe:    db "Stage 2 VBE Error!", 0x0D, 0x0A, 0

; GDT: null, flat 32-bit code (0x08), flat 32-bit data (0x10)
align 8
gdt_start:
    dd 0x00000000
    dd 0x00000000

    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 0x9A
    db 0xCF
    db 0x00

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

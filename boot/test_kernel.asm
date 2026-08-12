;=============================================================================
; MBL Test Kernel - end-to-end boot chain verification.
; NASM flat binary, ORG 0x100000. Entered by stage3 handoff exactly like a
; real kernel:
;       push dword <state_ptr>; call 0x100000
;   so [esp + 4] = mbl_boot_state_t*.
;
; Fills the framebuffer dark violet and draws a neon-cyan midline, proving
; that stage1 -> stage2 (VBE + disk) -> stage3 (GUI) -> handoff all worked,
; then halts. Replace this file / pass --kernel to build_image.py to boot a
; real kernel image.
;=============================================================================
BITS 32
ORG 0x100000

_start:
    mov eax, [esp + 4]
    test eax, eax
    jz .halt
    mov ebx, [eax + 0]              ; state->framebuffer
    test ebx, ebx
    jz .halt
    mov edi, [ebx + 0]              ; fb->pixels
    test edi, edi
    jz .halt
    mov ecx, [ebx + 8]              ; fb->width
    mov edx, [ebx + 12]             ; fb->height
    mov esi, [ebx + 16]             ; fb->pitch (bytes)

    mov [k_pixels], edi
    mov [k_pitch], esi
    mov [k_width], ecx
    mov eax, edx
    mov [k_height], eax
    shr eax, 1
    mov [k_half], eax

    ; ---- fill every row with the dark violet theme background ----
    mov ebp, [k_pixels]             ; current row base
    mov ebx, [k_height]             ; rows remaining
.fill_loop:
    test ebx, ebx
    jz .line
    mov edi, ebp
    mov ecx, [k_width]
    mov eax, 0xFF0F0C20
    cld
    rep stosd
    add ebp, [k_pitch]
    dec ebx
    jmp .fill_loop

    ; ---- neon-cyan midline at height/2 ----
.line:
    mov eax, [k_half]
    imul eax, [k_pitch]
    add eax, [k_pixels]
    mov edi, eax
    mov ecx, [k_width]
    mov eax, 0xFF00E5FF
    rep stosd

.halt:
    cli
    hlt
    jmp .halt

section .data
k_pixels: dd 0
k_pitch:  dd 0
k_width:  dd 0
k_height: dd 0
k_half:   dd 0

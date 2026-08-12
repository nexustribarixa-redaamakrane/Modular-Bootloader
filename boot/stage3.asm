;=============================================================================
; MBL Stage 3 - Boot GUI Stage (32-bit Protected Mode)
; NASM flat binary, loaded at physical 0x80000.
;
; Entry: invoked as a cdecl call from stage2:
;         push dword <state_ptr>   ; mbl_boot_state_t*  (0x7100)
;         call 0x80000
;       => [esp+4] = mbl_boot_state_t*
;
; Renders the documented OpenWindows boot menu (dark-violet theme, neon-cyan
; accents, geometric 'M' emblem, 4 entries, software mouse cursor), handles
; PS/2 keyboard + mouse input, and performs the final kernel handoff by
; copying the kernel image to 0x100000 and jumping into it.
;=============================================================================

BITS 32
[ORG 0x80000]

; Boot flags (mirror MBL_BOOT_FLAG_* in mbl_core.h)
%define MBL_BOOT_FLAG_VBE_ACTIVE      (1 << 0)
%define MBL_BOOT_FLAG_KBD_READY       (1 << 1)
%define MBL_BOOT_FLAG_MOUSE_READY     (1 << 2)
%define MBL_BOOT_FLAG_EXTSUCS_ENABLED (1 << 3)
%define MBL_BOOT_FLAG_GUI_READY       (1 << 4)

%define KERNEL_ENTRY_ADDR 0x100000
%define BACKBUFFER_ADDR   0x400000
%define BACKBUFFER_DWORDS (1024 * 768)

%define MBL_ACTION_BOOT_KERNEL 0
%define MBL_ACTION_BOOT_DEVICE 1
%define MBL_ACTION_SYS_CONFIG  2
%define MBL_ACTION_SHUTDOWN    3

%define MENU_BTN_COUNT 4
%define MENU_START_Y   280
%define MENU_BTN_W     360
%define MENU_BTN_H     44
%define MENU_BTN_GAP   56

section .text

global _start
_start:
    mov eax, [esp + 4]
    mov [state_ptr], eax

    ; ---- Resolve framebuffer info from the boot state passed by stage2 ----
    test eax, eax
    jz .fallback_fb
    mov ebx, [eax + 0]              ; state->framebuffer
    test ebx, ebx
    jz .fallback_fb

    mov ecx, [ebx + 0]
    mov [fb_pixels], ecx
    mov dword [fb_back], BACKBUFFER_ADDR
    mov [ebx + 4], dword BACKBUFFER_ADDR   ; fb->back_pixels (kernel sees it)
    mov ecx, [ebx + 8]
    mov [fb_width], ecx
    mov ecx, [ebx + 12]
    mov [fb_height], ecx
    mov ecx, [ebx + 16]
    mov [fb_pitch], ecx
    mov ecx, [ebx + 20]
    mov [fb_bpp], ecx
    jmp .fb_done
.fallback_fb:
    mov dword [fb_pixels], 0xE0000000
    mov dword [fb_back], BACKBUFFER_ADDR
    mov dword [fb_width], 1024
    mov dword [fb_height], 768
    mov dword [fb_pitch], 4096
    mov dword [fb_bpp], 32
.fb_done:

    ; ---- Mark GUI ready so the kernel can prove this stage executed ----
    mov eax, [state_ptr]
    test eax, eax
    jz .no_state
    mov ebx, [eax + 4]              ; state->boot_flags
    or ebx, MBL_BOOT_FLAG_GUI_READY
    mov [eax + 4], ebx
.no_state:

    ; ---- PS/2 mouse initialization (best effort) ----
    call ps2_mouse_init

    ; ---- Initial render + double-buffer swap ----
    call render_menu
    call fb_swap

    ; ---- Main bootloader event loop ----
.event_loop:
    mov dx, 0x64
    in al, dx
    test al, 0x01                   ; output buffer full?
    jz .event_loop
    push eax                        ; save status
    in al, 0x60                     ; consume data byte
    movzx ebx, al
    pop eax
    test eax, 0x20                  ; bit 5: aux (mouse) data?
    jnz .mouse

    ; ---------------- keyboard ----------------
    test bl, 0x80                   ; ignore break codes
    jnz .event_loop
    cmp bl, 0x1C                    ; ENTER
    je .enter
    cmp bl, 0x48                    ; Up
    je .up
    cmp bl, 0x11                    ; W
    je .up
    cmp bl, 0x50                    ; Down
    je .down
    cmp bl, 0x1F                    ; S
    je .down
    cmp bl, 0x02                    ; '1'..'4'
    jb .event_loop
    cmp bl, 0x05
    ja .event_loop
    movzx eax, bl
    sub eax, 0x02
    mov [sel_entry], eax
    call rerender
    jmp .event_loop
.up:
    mov eax, [sel_entry]
    test eax, eax
    jz .event_loop
    dec eax
    mov [sel_entry], eax
    call rerender
    jmp .event_loop
.down:
    mov eax, [sel_entry]
    cmp eax, MENU_BTN_COUNT - 1
    jae .event_loop
    inc eax
    mov [sel_entry], eax
    call rerender
    jmp .event_loop
.enter:
    call do_selected_action
    cmp eax, 1                      ; 1 => boot kernel
    je .boot_done
    jmp .event_loop

    ; ---------------- mouse packet (3 bytes) ----------------
.mouse:
    movzx eax, byte [mouse_cycle]
    mov [mouse_bytes + eax], bl
    inc byte [mouse_cycle]
    cmp byte [mouse_cycle], 3
    jne .event_loop
    mov byte [mouse_cycle], 0
    movzx eax, byte [mouse_bytes]
    movzx ecx, byte [mouse_bytes + 1]
    movzx edx, byte [mouse_bytes + 2]
    push edx
    push ecx
    push eax
    call handle_mouse
    add esp, 12
    cmp eax, 1                      ; 1 => left click selected action to boot kernel
    je .boot_done
    call rerender
    jmp .event_loop

.boot_done:
    call do_kernel_handoff
    cli
    hlt                             ; should never return

rerender:
    call render_menu
    call fb_swap
    ret

;=============================================================================
; do_selected_action() -> eax: 1 if we should boot the kernel, else 0
;=============================================================================
do_selected_action:
    mov eax, [sel_entry]
    cmp eax, MBL_ACTION_BOOT_KERNEL
    je .boot_kernel
    cmp eax, MBL_ACTION_BOOT_DEVICE
    je .boot_device
    cmp eax, MBL_ACTION_SYS_CONFIG
    je .sys_config
    cmp eax, MBL_ACTION_SHUTDOWN
    je .shutdown
    xor eax, eax
    ret
.boot_kernel:
    mov eax, 1
    ret
.boot_device:
    ; Warm reboot via keyboard controller => BIOS boots next device
    mov dx, 0x64
    mov al, 0xFE
    out dx, al
    cli
    hlt
.sys_config:
    mov eax, [cur_mode]
    xor eax, 1
    mov [cur_mode], eax
    ; persist into the boot state handed to the kernel
    mov ebx, [state_ptr]
    test ebx, ebx
    jz .cfg_done
    mov [ebx + 0x0C], eax           ; state->mode
    mov [ebx + 0x18], eax           ; mode_cfg.active_mode
    mov [ebx + 0x1C], eax           ; mode_cfg.pending_mode
    mov dword [ebx + 0x20], 0       ; mode_cfg.reboot_required
.cfg_done:
    call rerender
    xor eax, eax
    ret
.shutdown:
    call poweroff
    xor eax, eax
    ret

;=============================================================================
; do_kernel_handoff() - copy kernel to its entry address and jump
;   src  = state->kernel_load_addr  (+0x28)
;   size = state->kernel_image_size (+0x2C)
;   dst  = KERNEL_ENTRY_ADDR
; Stage3 code lives at 0x80000, below both, so the forward copy is safe.
;=============================================================================
do_kernel_handoff:
    push ebp
    mov ebp, esp
    push esi
    push edi
    push ecx
    push edx

    mov eax, [state_ptr]
    test eax, eax
    jz .done
    mov esi, [eax + 0x28]           ; src
    mov ecx, [eax + 0x2C]           ; byte count
    test ecx, ecx
    jz .done
    mov edi, KERNEL_ENTRY_ADDR

    mov edx, ecx
    shr ecx, 2
    cld
    rep movsd
    mov ecx, edx
    and ecx, 3
    rep movsb

    cli
    push dword [state_ptr]
    mov eax, KERNEL_ENTRY_ADDR
    call eax                        ; kernel entry; state at [esp+4]
.done:
    pop edx
    pop ecx
    pop edi
    pop esi
    pop ebp
    ret

;=============================================================================
; poweroff() - QEMU / Bochs / VirtualBox / Cloud Hypervisor ports
;=============================================================================
poweroff:
    mov dx, 0x604
    mov ax, 0x2000
    out dx, ax
    mov dx, 0xB004
    mov ax, 0x2000
    out dx, ax
    mov dx, 0x600
    mov ax, 0x34
    out dx, ax
    cli
    hlt
    ret

;=============================================================================
; PS/2 mouse init (polling mode - no IRQ needed)
;=============================================================================
ps2_mouse_init:
    pushad

    ; flush any stale output bytes
    mov ecx, 16
.flush_loop:
    mov dx, 0x64
    in al, dx
    test al, 0x01
    jz .flush_done
    in al, 0x60
    dec ecx
    jnz .flush_loop
.flush_done:

    call ps2_wait_input
    jc .no_mouse
    mov al, 0xA8                   ; enable auxiliary port
    out 0x64, al

    call ps2_wait_input
    jc .no_mouse
    mov al, 0xD4                   ; write-to-aux command
    out 0x64, al

    call ps2_wait_input
    jc .no_mouse
    mov al, 0xF4                   ; enable data reporting
    out 0x60, al

    call ps2_wait_output
    jc .no_mouse
    in al, 0x60
    cmp al, 0xFA                   ; ACK?
    jne .no_mouse

    mov dword [mouse_visible], 1
    mov ebx, [state_ptr]
    test ebx, ebx
    jz .ok
    or dword [ebx + 4], MBL_BOOT_FLAG_MOUSE_READY
.ok:
    popad
    ret
.no_mouse:
    mov dword [mouse_visible], 0
    popad
    ret

; ps2_wait_input: input buffer empty? CF=1 on timeout.
ps2_wait_input:
    mov ecx, 0xFFFFF
.wi:
    mov dx, 0x64
    in al, dx
    test al, 0x02
    jz .wi_ok
    dec ecx
    jnz .wi
    stc
    ret
.wi_ok:
    clc
    ret

; ps2_wait_output: output buffer has data? CF=1 on timeout.
ps2_wait_output:
    mov ecx, 0xFFFFF
.wo:
    mov dx, 0x64
    in al, dx
    test al, 0x01
    jnz .wo_ok
    dec ecx
    jnz .wo
    stc
    ret
.wo_ok:
    clc
    ret

;=============================================================================
; handle_mouse(b0, b1, b2) - relative packet, hover selection
;=============================================================================
handle_mouse:
    push ebp
    mov ebp, esp
    pushad

    movsx eax, byte [ebp + 12]      ; rel_x = (int8)b1
    add [mouse_x], eax
    movsx eax, byte [ebp + 16]      ; rel_y = (int8)b2
    sub [mouse_y], eax              ; Y axis inverted in PS/2

    ; clamp
    mov eax, [mouse_x]
    test eax, eax
    jns .x_hi
    mov dword [mouse_x], 0
    jmp .y_clamp
.x_hi:
    mov ecx, [fb_width]
    dec ecx
    cmp eax, ecx
    jle .y_clamp
    mov [mouse_x], ecx
.y_clamp:
    mov eax, [mouse_y]
    test eax, eax
    jns .y_hi
    mov dword [mouse_y], 0
    jmp .hover
.y_hi:
    mov ecx, [fb_height]
    dec ecx
    cmp eax, ecx
    jle .hover
    mov [mouse_y], ecx

.hover:
    xor edi, edi                    ; button index
.hloop:
    cmp edi, MENU_BTN_COUNT
    jae .done
    mov eax, [fb_width]
    sub eax, MENU_BTN_W
    shr eax, 1                      ; btn_x
    mov ecx, [mouse_x]
    cmp ecx, eax
    jb .hnext
    add eax, MENU_BTN_W
    cmp ecx, eax
    ja .hnext
    mov eax, MENU_START_Y
    mov edx, edi
    imul edx, MENU_BTN_GAP
    add eax, edx                    ; btn_y
    mov ecx, [mouse_y]
    cmp ecx, eax
    jb .hnext
    add eax, MENU_BTN_H
    cmp ecx, eax
    ja .hnext
    mov [sel_entry], edi
    ; check left mouse button (b0 bit 0)
    mov al, [ebp + 8]
    test al, 0x01
    jz .done
    call do_selected_action
    popad
    pop ebp
    ret
.hnext:
    inc edi
    jmp .hloop
.done:
    popad
    pop ebp
    xor eax, eax
    ret

;=============================================================================
; render_menu()
;=============================================================================
render_menu:
    pushad

    call fb_clear_back

    ; geometric emblem, horizontally centered, top at y=110
    mov eax, [fb_width]
    shr eax, 1
    push dword 110                  ; top_y
    push eax                        ; cx
    call draw_emblem
    add esp, 8

    ; title centered at y=225
    mov esi, str_title
    call str_len
    shl eax, 3                      ; length * 8
    mov ecx, [fb_width]
    sub ecx, eax
    shr ecx, 1
    push ecx                        ; x
    push dword 225                  ; y
    push dword [theme_text]         ; color
    push dword 1                    ; bold
    mov esi, str_title
    push esi                        ; str
    call text_puts
    add esp, 20

    ; boot entries
    xor edi, edi
.btn_loop:
    cmp edi, MENU_BTN_COUNT
    jae .btn_done
    mov eax, [fb_width]
    sub eax, MENU_BTN_W
    shr eax, 1
    push eax                        ; x
    mov eax, MENU_START_Y
    mov ecx, edi
    imul ecx, MENU_BTN_GAP
    add eax, ecx
    push eax                        ; y
    push edi                        ; index
    call draw_button
    add esp, 12
    inc edi
    jmp .btn_loop
.btn_done:

    ; status line showing current kernel mode
    cmp dword [cur_mode], 0
    jne .mode_ext
    mov esi, str_mode_base
    jmp .mode_show
.mode_ext:
    mov esi, str_mode_ext
.mode_show:
    mov eax, [fb_width]
    shr eax, 1
    sub eax, 110
    push eax                        ; x
    mov eax, [fb_height]
    sub eax, 48
    push eax                        ; y
    push dword [theme_dim]
    push dword 0
    push esi
    call text_puts
    add esp, 20

    ; software mouse cursor
    cmp dword [mouse_visible], 0
    je .no_cursor
    mov eax, [mouse_x]
    push eax
    mov eax, [mouse_y]
    push eax
    call draw_cursor
    add esp, 8
.no_cursor:

    popad
    ret

; draw_button(x, y, index)
draw_button:
    push ebp
    mov ebp, esp
    push esi
    push edi

    mov edi, [ebp + 16]             ; index
    mov eax, [sel_entry]
    cmp eax, edi
    je .selected

    ; ---- inactive: plain dim text, no box ----
    mov edi, [ebp + 16]
    call btn_label                  ; eax = label
    mov esi, eax
    push esi
    call str_len
    add esp, 4
    shl eax, 3
    mov ecx, [ebp + 8]              ; x
    add ecx, MENU_BTN_W
    sub ecx, eax
    shr ecx, 1                      ; centered text_x
    push ecx                        ; x
    mov eax, [ebp + 12]
    add eax, 14
    push eax                        ; y
    push dword [theme_dim]          ; color
    push dword 1                    ; bold
    push esi
    call text_puts
    add esp, 20
    jmp .done

.selected:
    ; glowing rounded selection box
    push dword [theme_panel]        ; fill
    push dword [theme_hl]           ; border
    push dword MENU_BTN_H
    push dword MENU_BTN_W
    mov eax, [ebp + 12]
    push eax                        ; y
    mov eax, [ebp + 8]
    push eax                        ; x
    call draw_round_rect
    add esp, 24

    ; chevron '>' at (x-24, y+14)
    mov eax, [ebp + 8]
    sub eax, 24
    push eax                        ; x
    mov eax, [ebp + 12]
    add eax, 14
    push eax                        ; y
    push dword [theme_accent]
    push dword 1
    mov esi, str_chevron
    push esi
    call text_puts
    add esp, 20

    ; label in white
    mov edi, [ebp + 16]
    call btn_label
    mov esi, eax
    push esi
    call str_len
    add esp, 4
    shl eax, 3
    mov ecx, [ebp + 8]
    add ecx, MENU_BTN_W
    sub ecx, eax
    shr ecx, 1
    push ecx                        ; x
    mov eax, [ebp + 12]
    add eax, 14
    push eax                        ; y
    push dword [theme_text]
    push dword 1
    push esi
    call text_puts
    add esp, 20
.done:
    pop edi
    pop esi
    pop ebp
    ret

; btn_label: edi = index, returns label pointer in eax
btn_label:
    cmp edi, 0
    je .l0
    cmp edi, 1
    je .l1
    cmp edi, 2
    je .l2
    mov eax, str_l3
    ret
.l2:
    mov eax, str_l2
    ret
.l1:
    mov eax, str_l1
    ret
.l0:
    mov eax, str_l0
    ret

;=============================================================================
; text_puts(str, bold, color, y, x)
;=============================================================================
text_puts:
    push ebp
    mov ebp, esp
    push esi

    mov esi, [ebp + 8]              ; str
    mov eax, [ebp + 12]
    mov [txt_bold], eax
    mov eax, [ebp + 16]
    mov [txt_color], eax
    mov eax, [ebp + 20]
    mov [cursor_y], eax
    mov eax, [ebp + 24]
    mov [cursor_x], eax
.next:
    lodsb
    test al, al
    jz .done
    movzx eax, al
    call putchar
    jmp .next
.done:
    pop esi
    pop ebp
    ret

; putchar(eax = ASCII codepoint)
putchar:
    cmp al, 0x0A
    je .newline
    cmp al, 0x0D
    je .carriage
    cmp al, 0x09
    je .tab
    cmp al, 0x20
    jb .advance
    cmp al, 0x7E
    ja .qmark

    movzx eax, al
    mov ebx, eax
    shl ebx, 4
    sub ebx, 0x200
    add ebx, font8x16               ; glyph ptr

    ; primary blit
    push dword 0                    ; bg (transparent)
    push dword [txt_color]          ; fg
    push ebx                        ; glyph
    mov ecx, [cursor_y]
    push ecx
    mov ecx, [cursor_x]
    push ecx
    call blit_glyph
    add esp, 20

    cmp dword [txt_bold], 0
    je .advance
    ; bold: secondary blit one pixel right
    push dword 0
    push dword [txt_color]
    push ebx
    mov ecx, [cursor_y]
    push ecx
    mov ecx, [cursor_x]
    inc ecx
    push ecx
    call blit_glyph
    add esp, 20
    jmp .advance
.qmark:
    mov eax, '?'
    call putchar
    jmp .advance
.newline:
    mov eax, [cursor_y]
    add eax, 16
    mov [cursor_y], eax
    mov [cursor_x], dword 0
    ret
.carriage:
    mov [cursor_x], dword 0
    ret
.tab:
    mov eax, [cursor_x]
    add eax, 32
    mov [cursor_x], eax
    ret
.advance:
    mov eax, [cursor_x]
    add eax, 8
    mov [cursor_x], eax
    ret

;=============================================================================
; Framebuffer primitives (double-buffered)
;=============================================================================
; fb_clear_back() - fill backbuffer with theme background
fb_clear_back:
    push eax
    push ecx
    push edi
    mov edi, [fb_back]
    mov eax, [theme_bg]
    mov ecx, [fb_width]
    imul ecx, [fb_height]
    cld
    rep stosd
    pop edi
    pop ecx
    pop eax
    ret

; put_pixel(x, y, color) - writes to backbuffer (or pixels if none)
put_pixel:
    push ebp
    mov ebp, esp
    push eax
    push ebx
    push edx

    mov eax, [ebp + 8]              ; x
    mov ebx, [ebp + 12]             ; y
    mov ecx, [ebp + 16]             ; color

    cmp eax, [fb_width]
    jae .done
    cmp ebx, [fb_height]
    jae .done

    mov eax, [fb_back]
    test eax, eax
    jz .use_front
    mov edx, [fb_width]
    imul edx, ebx                   ; y * width (dense dwords in backbuffer)
    add edx, [ebp + 8]              ; + x
    mov [eax + edx * 4], ecx
    jmp .done
.use_front:
    mov edx, [fb_pitch]
    shr edx, 2                      ; stride in dwords
    imul edx, ebx                   ; y * pitch_dwords
    add edx, [ebp + 8]              ; + x
    mov eax, [fb_pixels]
    mov [eax + edx * 4], ecx
.done:
    pop edx
    pop ebx
    pop eax
    pop ebp
    ret

; fill_rect(x, y, w, h, color)
fill_rect:
    push ebp
    mov ebp, esp
    pushad

    mov ebx, [ebp + 12]             ; y
.outer:
    mov eax, [ebp + 8]              ; x
    mov edx, [ebp + 8]
    add edx, [ebp + 16]             ; xend = x + w
.inner:
    push dword [ebp + 24]           ; color
    push ebx
    push eax
    call put_pixel
    add esp, 12
    inc eax
    cmp eax, edx
    jb .inner
    inc ebx
    mov eax, [ebp + 12]
    add eax, [ebp + 20]             ; yend = y + h
    cmp ebx, eax
    jb .outer
    popad
    pop ebp
    ret

; draw_line(x0, y0, x1, y1, color) - Bresenham
draw_line:
    push ebp
    mov ebp, esp
    sub esp, 16                     ; locals: [ebp-4]=err, [ebp-8]=cy, [ebp-12]=e2, [ebp-16]=|dx|
    pushad

    ; |dx|, sx
    mov eax, [ebp + 16]
    sub eax, [ebp + 8]
    jns .dx_pos
    neg eax
    mov edi, -1
    jmp .dx_done
.dx_pos:
    mov edi, 1
.dx_done:
    mov [ebp - 16], eax             ; |dx| (in local; put_pixel clobbers ecx)

    ; |dy|, sy
    mov eax, [ebp + 20]
    sub eax, [ebp + 12]
    jns .dy_pos
    neg eax
    mov edx, -1
    jmp .dy_done
.dy_pos:
    mov edx, 1
.dy_done:
    mov esi, eax                    ; esi = |dy| (put_pixel preserves esi/edi)

    mov eax, [ebp - 16]
    sub eax, esi
    mov [ebp - 4], eax              ; err = |dx| - |dy|

    mov ebx, [ebp + 8]              ; cx = x0
    mov eax, [ebp + 12]
    mov [ebp - 8], eax              ; cy = y0

.loop:
    push dword [ebp + 24]           ; color
    push dword [ebp - 8]            ; y
    push ebx                        ; x
    call put_pixel
    add esp, 12

    cmp ebx, [ebp + 16]
    jne .cont
    mov eax, [ebp - 8]
    cmp eax, [ebp + 20]
    je .done
.cont:
    mov eax, [ebp - 4]
    add eax, eax
    mov [ebp - 12], eax             ; e2 = 2*err

    ; if (e2 > -|dy|) { err -= |dy|; cx += sx; }
    mov eax, [ebp - 12]
    add eax, esi
    jle .no_x
    mov eax, [ebp - 4]
    sub eax, esi
    mov [ebp - 4], eax
    add ebx, edi
.no_x:
    ; if (e2 < |dx|) { err += |dx|; cy += sy; }
    mov eax, [ebp - 16]
    sub eax, [ebp - 12]
    jle .no_y
    mov eax, [ebp - 4]
    add eax, [ebp - 16]
    mov [ebp - 4], eax
    mov eax, [ebp - 8]
    add eax, edx
    mov [ebp - 8], eax
.no_y:
    jmp .loop
.done:
    add esp, 16
    popad
    pop ebp
    ret

; draw_round_rect(x, y, w, h, border, fill)
draw_round_rect:
    push ebp
    mov ebp, esp
    pushad

    ; body
    mov eax, [ebp + 8]
    add eax, 2
    mov ebx, [ebp + 12]
    add ebx, 2
    mov ecx, [ebp + 16]
    sub ecx, 4
    mov edx, [ebp + 20]
    sub edx, 4
    push dword [ebp + 28]           ; fill
    push edx                        ; h
    push ecx                        ; w
    push ebx                        ; y
    push eax                        ; x
    call fill_rect
    add esp, 20

    ; top border
    push dword [ebp + 24]
    push dword 2
    push dword [ebp + 16]
    push dword [ebp + 12]
    push dword [ebp + 8]
    call fill_rect
    add esp, 20
    ; bottom border
    mov eax, [ebp + 12]
    add eax, [ebp + 20]
    sub eax, 2
    push dword [ebp + 24]
    push dword 2
    push dword [ebp + 16]
    push eax
    push dword [ebp + 8]
    call fill_rect
    add esp, 20
    ; left border
    push dword [ebp + 24]
    push dword [ebp + 20]
    push dword 2
    push dword [ebp + 12]
    push dword [ebp + 8]
    call fill_rect
    add esp, 20
    ; right border
    mov eax, [ebp + 8]
    add eax, [ebp + 16]
    sub eax, 2
    push dword [ebp + 24]
    push dword [ebp + 20]
    push dword 2
    push dword [ebp + 12]
    push eax
    call fill_rect
    add esp, 20

    ; round the four radius-2 corners (inner corner pixel = fill)
    mov eax, [ebp + 8]
    inc eax
    mov ebx, [ebp + 12]
    inc ebx
    push dword [ebp + 28]
    push ebx
    push eax
    call put_pixel
    add esp, 12
    mov eax, [ebp + 8]
    add eax, [ebp + 16]
    sub eax, 2
    mov ebx, [ebp + 12]
    inc ebx
    push dword [ebp + 28]
    push ebx
    push eax
    call put_pixel
    add esp, 12
    mov eax, [ebp + 8]
    inc eax
    mov ebx, [ebp + 12]
    add ebx, [ebp + 20]
    sub ebx, 2
    push dword [ebp + 28]
    push ebx
    push eax
    call put_pixel
    add esp, 12
    mov eax, [ebp + 8]
    add eax, [ebp + 16]
    sub eax, 2
    mov ebx, [ebp + 12]
    add ebx, [ebp + 20]
    sub ebx, 2
    push dword [ebp + 28]
    push ebx
    push eax
    call put_pixel
    add esp, 12

    popad
    pop ebp
    ret

; blit_glyph(x, y, glyph, fg, bg)
blit_glyph:
    push ebp
    mov ebp, esp
    pushad

    mov esi, [ebp + 16]             ; glyph
    xor ebx, ebx                    ; row
.outer:
    cmp ebx, 16
    jae .done
    mov al, [esi + ebx]             ; row bits
    xor edx, edx                    ; col
.inner:
    cmp edx, 8
    jae .row_next
    shl al, 1
    jnc .col_next
    push eax                        ; preserve AL row byte
    push dword [ebp + 20]           ; fg
    mov eax, [ebp + 12]
    add eax, ebx
    push eax                        ; y
    mov eax, [ebp + 8]
    add eax, edx
    push eax                        ; x
    call put_pixel
    add esp, 12
    pop eax                         ; restore AL row byte
.col_next:
    inc edx
    jmp .inner
.row_next:
    inc ebx
    jmp .outer
.done:
    popad
    pop ebp
    ret

; draw_emblem(cx, top_y) - geometric 'M' logo (white-inner/cyan-outer)
draw_emblem:
    push ebp
    mov ebp, esp
    push ebx
    push esi

    mov ebx, [ebp + 8]              ; cx
    mov esi, [ebp + 12]             ; top

    ; line1: (cx-40,top) -> (cx, top+40)
    mov eax, esi
    add eax, 40
    push eax                        ; y1
    push ebx                        ; x1
    push esi                        ; y0
    mov eax, ebx
    sub eax, 40
    push eax                        ; x0
    call emblem_line
    add esp, 16

    ; line2: (cx, top+40) -> (cx+40, top)
    push esi                        ; y1
    mov eax, ebx
    add eax, 40
    push eax                        ; x1
    mov eax, esi
    add eax, 40
    push eax                        ; y0
    push ebx                        ; x0
    call emblem_line
    add esp, 16

    ; line3: (cx-40,top) -> (cx-40, top+50)
    mov eax, esi
    add eax, 50
    push eax
    mov eax, ebx
    sub eax, 40
    push eax
    push esi
    mov eax, ebx
    sub eax, 40
    push eax
    call emblem_line
    add esp, 16

    ; line4: (cx+40,top) -> (cx+40, top+50)
    mov eax, esi
    add eax, 50
    push eax
    mov eax, ebx
    add eax, 40
    push eax
    push esi
    mov eax, ebx
    add eax, 40
    push eax
    call emblem_line
    add esp, 16

    ; line5: (cx-40, top+50) -> (cx, top+90)
    mov eax, esi
    add eax, 90
    push eax
    push ebx
    mov eax, esi
    add eax, 50
    push eax
    mov eax, ebx
    sub eax, 40
    push eax
    call emblem_line
    add esp, 16

    ; line6: (cx+40, top+50) -> (cx, top+90)
    mov eax, esi
    add eax, 90
    push eax
    push ebx
    mov eax, esi
    add eax, 50
    push eax
    mov eax, ebx
    add eax, 40
    push eax
    call emblem_line
    add esp, 16

    pop esi
    pop ebx
    pop ebp
    ret

; emblem_line(x0, y0, x1, y1) - accent-colored line
emblem_line:
    push ebp
    mov ebp, esp
    push dword [theme_accent]       ; color
    push dword [ebp + 20]           ; y1
    push dword [ebp + 16]           ; x1
    push dword [ebp + 12]           ; y0
    push dword [ebp + 8]            ; x0
    call draw_line
    add esp, 20
    pop ebp
    ret

; draw_cursor(x, y) - 12x16 neon-cyan arrow
draw_cursor:
    push ebp
    mov ebp, esp
    pushad

    xor ebx, ebx                    ; row
.outer:
    cmp ebx, 16
    jae .done
    movzx eax, word [g_mouse_sprite + ebx * 2]
    xor edx, edx                    ; col
.inner:
    cmp edx, 12
    jae .row_next
    test ax, 0x8000
    jz .col_next
    push eax                        ; preserve AX sprite row mask
    push dword [theme_accent]
    mov eax, [ebp + 12]
    add eax, ebx
    push eax
    mov eax, [ebp + 8]
    add eax, edx
    push eax
    call put_pixel
    add esp, 12
    pop eax                         ; restore AX sprite row mask
.col_next:
    shl ax, 1
    inc edx
    jmp .inner
.row_next:
    inc ebx
    jmp .outer
.done:
    popad
    pop ebp
    ret

;=============================================================================
; fb_swap() - copy backbuffer to front pixels (respecting pitch)
;=============================================================================
fb_swap:
    pushad
    mov esi, [fb_back]
    test esi, esi
    jz .done
    mov edi, [fb_pixels]
    test edi, edi
    jz .done
    xor ebx, ebx                    ; row
.row:
    mov eax, [fb_height]
    cmp ebx, eax
    jae .done
    ; bytes per row = width * 4
    mov ecx, [fb_width]
    shl ecx, 2
    push ecx                        ; count
    ; src offset = row * (width * 4)
    mov edx, ebx
    imul edx, ecx
    mov eax, esi
    add eax, edx
    push eax                        ; src
    ; dst offset = row * pitch
    mov edx, ebx
    imul edx, [fb_pitch]
    mov eax, edi
    add eax, edx
    push eax                        ; dst
    call memcpy_bytes
    add esp, 12
    inc ebx
    jmp .row
.done:
    popad
    ret

; memcpy_bytes(dst, src, n)
memcpy_bytes:
    push ebp
    mov ebp, esp
    push esi
    push edi
    push ecx
    mov edi, [ebp + 8]
    mov esi, [ebp + 12]
    mov ecx, [ebp + 16]
    cld
    rep movsb
    pop ecx
    pop edi
    pop esi
    pop ebp
    ret

;=============================================================================
; str_len(str) -> eax
;=============================================================================
str_len:
    push ebx
    mov ebx, [esp + 8]
    xor eax, eax
.l:
    cmp byte [ebx + eax], 0
    je .done
    inc eax
    jmp .l
.done:
    pop ebx
    ret

;=============================================================================
section .data
;=============================================================================
state_ptr:    dd 0
fb_pixels:    dd 0
fb_back:      dd 0
fb_width:     dd 0
fb_height:    dd 0
fb_pitch:     dd 0
fb_bpp:       dd 0

mouse_x:      dd 512
mouse_y:      dd 384
mouse_visible: dd 1
mouse_cycle:  db 0
mouse_bytes:  db 0, 0, 0

sel_entry:    dd 0
cur_mode:     dd 0

txt_color:    dd 0xFFFFFFFF
txt_bold:     dd 0
cursor_x:     dd 0
cursor_y:     dd 0

theme_bg:     dd 0xFF0F0C20
theme_panel:  dd 0xFF1B152E
theme_accent: dd 0xFF00E5FF
theme_text:   dd 0xFFFFFFFF
theme_dim:    dd 0xFFA0A0C0
theme_hl:     dd 0xFF4DEEEA

str_title:    db "Secure System Select", 0
str_chevron:  db ">", 0
str_l0:       db "1. OpenWindows Kernel (Secured)", 0
str_l1:       db "2. Boot from Device", 0
str_l2:       db "3. System Configuration", 0
str_l3:       db "4. Shutdown", 0
str_mode_base: db "Kernel Mode: BASE (SUCS)", 0
str_mode_ext:  db "Kernel Mode: EXTENDED (ExtSUCS)", 0

%include "font8x16.inc"

; 12x16 mouse arrow sprite
g_mouse_sprite:
    dw 0b1000000000000000
    dw 0b1100000000000000
    dw 0b1110000000000000
    dw 0b1111000000000000
    dw 0b1111100000000000
    dw 0b1111110000000000
    dw 0b1111111000000000
    dw 0b1111111100000000
    dw 0b1111111110000000
    dw 0b1111110000000000
    dw 0b1110111000000000
    dw 0b1100011100000000
    dw 0b1000001110000000
    dw 0b0000000111000000
    dw 0b0000000011000000
    dw 0b0000000000000000

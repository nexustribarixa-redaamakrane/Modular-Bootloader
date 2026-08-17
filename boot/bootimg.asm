;=============================================================================
; MBL Boot Image (linked flat binary at 0x80000)
; =============================================================================
; Single self-contained image containing:
;   .text     - real-mode entry (_rm_entry) first, then INT13h BIOS trampoline
;              (_bios_tramp), chainload launcher (_rm_boot), the GDT, the
;              32-bit entry (_start), BIOS call wrappers (bios_read /
;              bios_boot), port I/O helpers, and every translated C module
;              (the GRUB-style black & white TUI bootloader).
;
; Flow:
;   stage2 (real mode) far-jumps to 0x8000:0x0000 -> _rm_entry.
;   _rm_entry sets video mode 3, enables A20, loads the GDT, switches to
;   protected mode and jumps to _start (flat 0x08:0x800xx).
;   _start sets up segments/stack and calls kmain().
;
;   The C bootloader reads the OpenWindows OWFS volume through bios_read(),
;   which drops to real mode, runs INT 13h AH=0x42 and returns.
;
; Memory layout (must match tools/build_image.py):
;   0x0500  mbl_bootinfo_t
;   0x6000  BIOS call frame (bios_call_t + DAP + RM IDT operand)
;   0x7C00  stage1 MBR
;   0x8000  stage2 (dead after _rm_entry)
;   0x10000 kernel read staging buffer (64 KiB)
;   0x80000 this boot image
;   0x90000 protected mode stack (grows down)
;   0x200000 kernel load address
;=============================================================================

BITS 16
ORG 0x80000

%define ORG_BASE        0x80000
%define CALL_FRAME      0x6000
%define PM_STACK_TOP    0x90000
%define KERNEL_ADDR     0x200000

;=============================================================================
; .text - all code, real-mode entry first (NASM -f bin always emits the
; .text section before any other section, so the entry stub must live here).
;=============================================================================
_rm_entry:
    cli
    mov ax, ORG_BASE >> 4            ; segment of this image
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    ; remember the BIOS boot drive (DL from stage2) for the INT13h trampoline.
    ; 0x475 is the BDA *hard-disk count*, not the boot drive, so we must not
    ; use it here.  (ds = 0x8000 can't reach 0x601C, so use an es:0 override.)
    push ax
    xor ax, ax
    mov es, ax
    mov [es:CALL_FRAME + 0x1C], dl
    pop ax
    mov ax, ORG_BASE >> 4
    mov es, ax

    ; 80x25 colour text mode (the black & white GRUB-style TUI)
    mov ax, 0x0003
    int 0x10

    ; fast A20 gate
    in al, 0x92
    or al, 0x02
    out 0x92, al

    ; load the GDT (ds = 0x8000, so offset is gdt_desc - base)
    lgdt [gdt_desc - ORG_BASE]

    ; enter protected mode
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp dword 0x08:_start

;-----------------------------------------------------------------------------
; _bios_tramp - real-mode INT 13h AH=0x42 service.  Entered from bios_read()
; by clearing CR0.PE and far-jumping to 0x8000:(_bios_tramp - ORG_BASE).
; Reads the BIOS call frame at 0x6000, runs the extended disk read, stores the
; BIOS AX result back, then returns to protected mode through the frame.
;-----------------------------------------------------------------------------
_bios_tramp:
    ; DEBUG marker: 0xAA in 0x6030 means we reached the trampoline
    push ax
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov byte [0x6030], 0xAA
    pop ax

    ; populate the DAP at 0x6020 from the call frame at 0x6000
    mov byte [0x6020], 0x10          ; DAP size
    mov byte [0x6021], 0             ; reserved
    mov ax, [0x600C]                 ; sector count
    mov [0x6022], ax
    mov ax, [0x600A]                 ; buffer offset
    mov [0x6024], ax
    mov ax, [0x6008]                 ; buffer segment
    mov [0x6026], ax
    mov eax, [0x6000]                ; LBA low
    mov [0x6028], eax
    mov eax, [0x6004]                ; LBA high
    mov [0x602C], eax

    mov dl, [CALL_FRAME + 0x1C]      ; BIOS boot drive (saved by _rm_entry)
    mov si, 0x6020
    mov ah, 0x42
    sti
    int 0x13
    cli
    mov [0x600E], ax                 ; result

    ; back to protected mode: ds = 0x8000 to reach the GDT by 16-bit offset.
    ; The far jump target lives in the call frame at linear 0x6010, which
    ; ds=0x8000 cannot reach, so read it via es (still flat 0).
    mov ax, ORG_BASE >> 4
    mov ds, ax
    lgdt [gdt_desc - ORG_BASE]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp far dword [es:0x6010]        ; off32 @0x6010, seg16 @0x6014

;-----------------------------------------------------------------------------
; _rm_boot - real-mode chainload launcher.  Entered from bios_boot() after
; clearing CR0.PE.  Reads the target seg:off from the call frame and jumps to
; it with DL = boot drive (used to boot an MBR from another device).
;-----------------------------------------------------------------------------
_rm_boot:
    xor ax, ax
    mov ds, ax
    mov dl, [0x6016]                 ; drive to pass in DL
    jmp far [0x6018]                 ; off16 @0x6018, seg16 @0x601A

;-----------------------------------------------------------------------------
; GDT - null, flat 32-bit code (0x08), flat 32-bit data (0x10)
;-----------------------------------------------------------------------------
align 8
gdt_start:
    dq 0
    dw 0xFFFF, 0x0000
    db 0x00, 0x9A, 0xCF, 0x00
    dw 0xFFFF, 0x0000
    db 0x00, 0x92, 0xCF, 0x00
gdt_end:
gdt_desc:
    dw gdt_end - gdt_start - 1
    dd gdt_start

;=============================================================================
; .text - 32-bit protected mode
;=============================================================================
section .text
BITS 32

;-----------------------------------------------------------------------------
; _start - 32-bit entry, jumped to from _rm_entry (0x08:offset)
;-----------------------------------------------------------------------------
_start:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, PM_STACK_TOP

    ; zero the .bss region (NASM -f bin writes no BSS bytes to the file)
    mov edi, __bss_start
    xor eax, eax
    mov ecx, (__bss_end - __bss_start + 3) >> 2
    rep stosd

    call kmain
.halt:
    cli
    hlt
    jmp .halt

;-----------------------------------------------------------------------------
; bios_read - int bios_read(uint32_t lba, uint32_t lba_hi, uint16_t seg,
;                           uint16_t off, uint16_t count)
; Reads `count` 512-byte sectors starting at `lba` into seg:off via BIOS
; INT 13h AH=0x42 (real-mode trampoline).  Returns the BIOS AX result
; (0x0000 = success, else CF|AH error code).
;-----------------------------------------------------------------------------
global bios_read
bios_read:
    push ebp
    mov ebp, esp
    cli

    mov eax, [ebp + 8]
    mov [CALL_FRAME + 0x00], eax     ; lba low
    mov eax, [ebp + 12]
    mov [CALL_FRAME + 0x04], eax     ; lba high
    mov ax, [ebp + 16]
    mov [CALL_FRAME + 0x08], ax      ; buffer segment
    mov ax, [ebp + 20]
    mov [CALL_FRAME + 0x0A], ax      ; buffer offset
    mov ax, [ebp + 24]
    mov [CALL_FRAME + 0x0C], ax      ; sector count
    mov word [CALL_FRAME + 0x0E], 0  ; result
    mov eax, .ret
    mov [CALL_FRAME + 0x10], eax     ; pm_return offset
    mov word [CALL_FRAME + 0x14], 0x08 ; pm_return segment

    ; point the IDTR at the real-mode IVT (base 0, limit 0x3FF)
    mov word [0x6040], 0x3FF
    mov dword [0x6042], 0
    lidt [0x6040]

    ; drop to real mode and enter the trampoline
    mov eax, cr0
    and eax, 0xFFFFFFFE
    mov cr0, eax
BITS 16
    jmp 0x8000:(_bios_tramp - ORG_BASE)
BITS 32

.ret:
    ; back in protected mode; restore flat selectors (stack untouched)
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    movzx eax, word [CALL_FRAME + 0x0E]
    pop ebp
    ret

;-----------------------------------------------------------------------------
; bios_boot - void bios_boot(uint16_t seg, uint16_t off, uint8_t drive)
; Switches to real mode and far-jumps to seg:off with DL = drive.
; Used for chainloading a boot sector (e.g. MBR of another disk).
; Does NOT return.
;-----------------------------------------------------------------------------
global bios_boot
bios_boot:
    push ebp
    mov ebp, esp
    cli

    mov ax, [ebp + 8]
    mov [CALL_FRAME + 0x18], ax      ; rm_off
    mov ax, [ebp + 12]
    mov [CALL_FRAME + 0x1A], ax      ; rm_seg
    mov al, [ebp + 16]
    mov [CALL_FRAME + 0x16], al      ; rm_drive

    mov word [0x6040], 0x3FF
    mov dword [0x6042], 0
    lidt [0x6040]

    mov eax, cr0
    and eax, 0xFFFFFFFE
    mov cr0, eax
BITS 16
    jmp 0x8000:(_rm_boot - ORG_BASE)
BITS 32

;-----------------------------------------------------------------------------
; Port I/O helpers (freestanding C has no way to emit in/out)
;-----------------------------------------------------------------------------
global inb
inb:
    mov dx, [esp + 4]
    in al, dx
    ret

global outb
outb:
    mov dx, [esp + 4]
    mov al, [esp + 8]
    out dx, al
    ret

global inw
inw:
    mov dx, [esp + 4]
    in ax, dx
    ret

global outw
outw:
    mov dx, [esp + 4]
    mov ax, [esp + 8]
    out dx, ax
    ret

;=============================================================================
; Translated C modules (generated by tools/build_boot.py from src/*.c)
;=============================================================================
section .bss
__bss_start:
%include "main.gen.asm"
%include "vga.gen.asm"
%include "kbd.gen.asm"
%include "menu.gen.asm"
%include "owfs.gen.asm"
%include "bios_disk.gen.asm"
%include "sutf8.gen.asm"
%include "sucs_mode.gen.asm"
section .bss
alignb 4
__bss_end:

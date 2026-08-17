# PERSISTENT MEMORY ARTIFACT: MODULAR BOOTLOADER (MBL) & SUPERUNICODE ARCHITECTURE

> **SYSTEM ARCHITECTURE & STATE TRACKER**
> **Workspace:** `C:\Users\KARIMABENDA\Documents\Modular-Bootloader`
> **Project:** Modular Bootloader (`MBL`) — GRUB-style black-and-white TUI bootloader for the OpenWindows Operating System Kernel (`OpenWindows-Kernel`).

---

## 1. Current Build Status

| Component | Status | Notes |
| :--- | :--- | :--- |
| GCC → NASM translator (`gcc_intel_to_nasm.py`) | DONE | Strips `.cfi_*`, `.loc`, `.size`, `.type` directives; translates Intel-suffix GAS to NASM syntax. |
| All 8 C modules compile + translate | DONE | `main.c`, `menu.c`, `vga.c`, `kbd.c`, `bios_disk.c`, `owfs.c`, `sutf8.c`, `sucs_mode.c` — clean build. |
| Stage 1 MBR (`stage1.asm`) | DONE | 512B MBR, LBA+CHS fallback, publishes bootinfo at 0x500. |
| Stage 2 loader (`stage2.asm`) | DONE | Loads 96-sector blob from LBA 16 to 0x80000. **Includes INT13h extension check + CHS fallback.** |
| Boot blob (`bootimg.asm`) | DONE | A20 → GDT → PM → C entry. Contains `_rm_entry`, `_bios_tramp`, `_rm_boot`, BIOS call frame. **16-bit far-jump operand fix applied to bios_read & bios_boot.** |
| Disk image builder (`build_image.py`) | DONE | Assembles all pieces, creates OWFS volume, writes `mbl_test.img`. |
| OWFS filesystem (`owfs_mkfs.py`) | DONE | Creates OpenWindows FS volume with `kernel.bin` seed. |
| Automated test (`test_qemu.py`) | DONE | Boots QEMU headless, checks VGA text buffer for menu title, sends keys, verifies kernel. (`parse_xp` supports 0x-prefixed monitor output). |
| **Bios_read PM→RM trampoline** | **RESOLVED** | 16-bit far jump encoding (`BITS 16` before `jmp 0x8000:...`) ensures real-mode 5-byte decode. |
| **Menu rendering** | **IN TEST** | Menu renders in VGA text buffer. |
| **Kernel boot** | **IN TEST** | Boots test kernel from OWFS volume. |

---

## 2. Toolchain

| Tool | Path | Version |
| :--- | :--- | :--- |
| GCC (32-bit cross) | `C:\w64devkit\bin\gcc.exe` | w64devkit |
| NASM | `C:\Program Files\NASM\nasm.exe` | Latest |
| QEMU | `C:\Program Files\qemu\qemu-system-i386.exe` | v11.0.50 |
| Python | System Python 3.13 / 3.14 | For build scripts |
| GDB | `C:\w64devkit\bin\gdb.exe` | For QEMU debugging |

### GCC flags (for C → NASM)
```
-m32 -S -masm=intel -ffreestanding -nostdlib -fno-pic -fno-stack-protector
-fno-builtin -fno-asynchronous-unwind-tables -fno-unwind-tables -fno-ident
-fno-common -mno-sse -mno-80387 -O2 -I include -I include/sutf
```

### NASM flags (boot blob)
```
-f bin -w-number-overflow -I build -o build/boot_blob.bin boot/bootimg.asm
```

### Build command
```
python tools\build_image.py
```

### Test command
```
python tools\test_qemu.py
```

---

## 3. Disk Image Layout (`mbl_test.img`)

| LBA | Size | Content |
| :--- | :--- | :--- |
| 0 | 512 B | Stage 1 MBR boot sector |
| 1..15 | 7680 B | Stage 2 (427 bytes with CHS fallback) |
| 16..111 | 49152 B | Boot blob (10592 bytes, 21 sectors used) |
| 112..127 | 8192 B | Reserved |
| 128+ | ~8 MB | OWFS volume (`kernel.bin` at offset 0x10000 in volume) |

---

## 4. Memory Map

| Address | Size | Usage |
| :--- | :--- | :--- |
| `0x0500` | 8 B | Boot info block (`MBL1` magic, boot drive, size) |
| `0x0510` | 16 B | DAP copy (stage1 writes its DAP here) |
| `0x6000` | 64 B | BIOS call frame (used by bios_read PM→RM transition) |
| `0x6020` | 16 B | DAP for trampoline INT13h |
| `0x6030` | 1 B | Debug marker (0xAA = trampoline reached) |
| `0x6040` | 6 B | RM IDT operand for lidt (base=0, limit=0x3FF) |
| `0x7C00` | 512 B | Stage 1 MBR (loaded by BIOS) |
| `0x8000` | ~7680 B | Stage 2 (overwritten by blob load) |
| `0x80000` | 49152 B | Boot blob (loaded by stage2, max 96 sectors) |
| `0x90000` | — | Stack top (grows down from 0x90000) |
| `0x200000` | — | Kernel load address |

---

## 5. BIOS Call Frame Format (at 0x6000)

```
Offset  Size  Field
0x00    4     LBA low (uint32_t)
0x04    4     LBA high (uint32_t)
0x08    2     Buffer segment
0x0A    2     Buffer offset
0x0C    2     Sector count
0x0E    2     BIOS AX result (return)
0x10    4     Return EIP (offset)
0x14    2     Return CS (selector, 0x08)
0x16    2     RM boot drive (for _rm_boot)
0x18    4     RM boot address (for _rm_boot)
0x1A    2     RM boot segment
0x1C    1     BIOS boot drive (saved by _rm_entry)
```

---

## 6. Resolved Root Cause: PM→RM Trampoline Far-Jump Mismatch

### Root Cause
After `mov cr0, eax` clears PE (disabling protected mode), the CPU enters Real-Address Mode. In Real Mode, the CPU instruction decoder decodes standard far jumps (`0xEA`) using 16-bit operand size (5 bytes: `EA <imm16 offset> <imm16 segment>`).
Because the jumps in `bios_read` and `bios_boot` were assembled within a `BITS 32` block, NASM emitted 7-byte 32-bit far jumps (`EA <imm32 offset> <imm16 segment>`).
When executed in Real Mode:
- Byte 1..2: offset = `0x003F` (low 16 bits of 32-bit offset)
- Byte 3..4: segment = `0x0000` (high 16 bits of 32-bit offset)
- Result: CPU jumped to `0x0000:0x003F` (the Real Mode IVT in low RAM) instead of `0x8000:0x003F`.

### Fix
Wrapped the far jumps in `BITS 16` directive:
```nasm
    mov eax, cr0
    and eax, 0xFFFFFFFE
    mov cr0, eax
BITS 16
    jmp 0x8000:(_bios_tramp - ORG_BASE)  ; Emits 5 bytes: EA 3F 00 00 80
BITS 32
```
And identically for `bios_boot`:
```nasm
    mov eax, cr0
    and eax, 0xFFFFFFFE
    mov cr0, eax
BITS 16
    jmp 0x8000:(_rm_boot - ORG_BASE)
BITS 32
```

---

## 7. Stage 2 INT 13h Extension Check & CHS Fallback

`boot/stage2.asm` checks for INT 13h extensions via `AH=0x41, BX=0x55AA`.
- If extensions are present (`CF=0`), it executes `AH=0x42` (LBA extended read).
- If absent or failed, it queries geometry with `AH=0x08`, computes `(Cylinder, Head, Sector)` for LBA 16..111, and reads sector-by-sector via `AH=0x02` into `cur_seg:0x0000` (incrementing `cur_seg` by `0x0020` = 512B per sector) with 3 retries on error.

---

## 8. GDT Layout (at blob offset ~0xB0)

| Offset | Size | Content |
| :--- | :--- | :--- |
| 0xB0+0 | 8 B | Null descriptor |
| 0xB0+8 | 8 B | Code: base 0, limit 4GB, execute/read, D=1 |
| 0xB0+16 | 8 B | Data: base 0, limit 4GB, read/write |
| 0xB0+24 | 2+4 B | GDTR: limit 0x17 (24 bytes), base = gdt_start |

---

## 9. Translated C Module Locations

All translated C code is `%include`d into `boot/bootimg.asm`. Their `.text` sections merge into the blob after `_rm_boot`.

| Module | Source | Key symbols |
| :--- | :--- | :--- |
| `bios_disk` | `src/bios_disk.c` | `bios_read`, `bios_boot`, `disk_read_sectors`, `disk_read_block` |
| `main` | `src/main.c` | `kmain`, `fail` |
| `menu` | `src/menu.c` | `menu_init`, `menu_render`, `menu_update` |
| `vga` | `src/vga.c` | `vga_init`, `vga_clear`, `vga_write` |
| `kbd` | `src/kbd.c` | `kbd_init`, `kbd_read` |
| `owfs` | `src/owfs.c` | `owfs_probe`, `owfs_enumerate`, `owfs_load` |
| `sutf8` | `src/sutf/sutf8.c` | SUTF-8 encode/decode |
| `sucs_mode` | `src/sutf/sucs_mode.c` | `sucs_commit_mode_on_boot` |

---

## 10. Key Headers

| Header | Purpose |
| :--- | :--- |
| `include/mbl.h` | Main bootloader types: `mbl_bootinfo_t`, `mbl_config_t`, constants (`MBL_BOOT_MAGIC`, `MBL_CFG_MAGIC`, `OWFS_BLOCK_SIZE`, etc.) |
| `include/sutf/sutf8.h` | SUTF-8 encode/decode prototypes |
| `include/sutf/sucs_mode.h` | SUCS mode constants (`SUCS_MODE_BASE`, `SUCS_MODE_EXTENDED`) |

---

## 11. NASM Flat Binary Section Ordering

With `ORG 0x80000` and `-f bin`:
1. `.text` is ALWAYS first (at ORG base)
2. Other **progbits** sections follow in declaration order (`.rodata`, `.data`, etc.)
3. **`.bss` (nobits) is ALWAYS placed LAST** regardless of declaration order

---

## 12. SUCS/ExtSUTF Architecture Reference

- **Base SUCS**: 31-bit bounded codepoint space (0x00000000–0x7FFFFFFF). Type `sucs_char_t` (uint32_t).
- **ExtSUCS**: Unbounded codepoint space. Type `sucs_ex_char_t` (uint64_t).
- **Base SUTF**: Stream framing — SUTF-8 (1-6B), SUTF-16 (1-2 words).
- **extSUTF**: Multi-byte, SIMD vector aligned serialization.
- **SUCS Mode**: `SUCS_MODE_BASE` (0), `SUCS_MODE_EXTENDED` (1). Committed via `sucs_commit_mode_on_boot()` at kernel handoff.

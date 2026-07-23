# PERSISTENT MEMORY ARTIFACT: MODULAR BOOTLOADER (MBL) & SUPERUNICODE ARCHITECTURE

> **SYSTEM ARCHITECTURE & SYSTEM STATE TRACKER**
> **Workspace:** `c:\Users\HIGH SKILLS\Documents\Modular Bootloader`
> **Project:** Modular Bootloader (`MBL`) for OpenWindows Operating System Kernel (`OpenWindows-Kernel`).

---

## 1. Absolute Nomenclature & Terminology Matrix

| System Term | Classification | Definition / Boundaries | Key Types & Constants |
| :--- | :--- | :--- | :--- |
| **Base SUCS** | **Character Encoding** | 31-bit bounded codepoint numerical address space ($0x00000000$ to $0x7FFFFFFF$). | `sucs_char_t` (`uint32_t`), Sentinel `SUCS_INVALID_CODEPOINT` ($0x7FFFFFFF$), Trap Range $0x7FFFFFF0$–$0x7FFFFFFE$. |
| **ExtSUCS** | **Character Encoding** | Unbounded codepoint address space ($0 \rightarrow \infty$). Implemented via 64-bit container in C99. | `sucs_ex_char_t` (`uint64_t`), No in-band sentinel ($0x7FFFFFFF$ is valid in ExtSUCS), Inherits Trap Range. |
| **Base SUTF** | **Text Formatting / Serialization Transport** | Stream framing & byte packing rules for Base SUCS codepoints across memory & buses. | SUTF-8 (1-6B), SUTF-16 (1-2 words), SUTF-4 (4-bit nibbles), SUTF-2 (2-bit frames). |
| **extSUTF** | **Text Formatting / Serialization Transport** | Multi-byte, SIMD vector aligned, and page-mapped virtual coordinate serialization. | SUTF-32/64/128/256/512/N (fixed vector slots), vSUTF (0xFE prefix 9B framing), e-SUTF (6B IPC frame). |

---

## 2. GUI Reference Layout & Default Menu Options

- **Default Bootloader Entries**:
  1. `1. OpenWindows Kernel (Secured)` — Main kernel image load and jump.
  2. `2. Boot from Device` — Chainload secondary drive / partition boot sector.
  3. `3. System Configuration` — Configure SUCS/ExtSUCS mode, display resolution, & boot flags.
  4. `4. Shutdown` — System power-off via ACPI / QEMU port `0x604` / APM fallback.
- **Visual Styling**:
  - Dark violet background (`0x000F0C20`) with neon cyan accent glow (`0x0000E5FF`).
  - Top geometric logo emblem ('M') in cyan-white lines.
  - Subtitle section header: "Secure System Select".
  - Glowing cyan rounded border outline box (`0x0000E5FF`) surrounding the currently selected option.
  - Glowing cyan chevron indicator `>` to the left of the active box.
  - Inactive items rendered as plain white text without outline box.
  - PS/2 keyboard navigation & software mouse cursor overlay matching cyan theme.

---

## 3. Kernel Mode-Switching Subsystem (`sucs_mode.h`) Integration

The Modular Bootloader manages mode initialization and passes kernel mode state to `OpenWindows-Kernel` during boot handoff:
- `SUCS_MODE_BASE` (0): Active Base SUCS character encoding & Base SUTF text formatting transports.
- `SUCS_MODE_EXTENDED` (1): Active ExtSUCS character encoding & extSUTF text formatting transports.
- **Bootloader Mode Commit Rule**: During boot initialization, MBL calls `sucs_commit_mode_on_boot()` to process any pending kernel mode switch before jumping to kernel entry point.

---

## 4. Completed Files & Verified Build Targets

### Built Artifacts (`mbl/build/`)
- `mbl_core.bin`: 512 bytes 16-bit Stage 1 MBR bootsector (ends in `0xAA55`). Verified.
- `mbl_stage2.sys`: 294 bytes 32-bit Stage 2 VBE 0x0118 Protected Mode loader. Verified.
- `libmbl.a`: 33,732 bytes freestanding C99 static library. Verified.

### Directory & File Architecture
```text
mbl/
├── boot/
│   ├── stage1.asm           # [COMPLETED] 16-bit MBR/VBR boot sector (INT 13h sector loading, 0xAA55)
│   └── stage2.asm           # [COMPLETED] VBE mode 0x0118 (1024x768x32bpp) set, A20, GDT, 32-bit PM jump
├── include/
│   ├── io.h                 # [COMPLETED] Port I/O inline assembly (inb, outb, inw, outw, cli, sti, io_wait, shutdown)
│   ├── mbl_core.h           # [COMPLETED] MBL state struct, memory map, boot flags, mode handoff, boot actions
│   ├── mbl_framebuffer.h    # [COMPLETED] Double-buffered 32-bit ARGB8888 linear framebuffer routines
│   ├── mbl_gui.h            # [COMPLETED] Modern dark GUI boot menu, buttons, mouse cursor & event handler
│   ├── mbl_sutf_gui.h       # [COMPLETED] SUCS codepoint pixel glyph blitter & SUTF-8 stream console
│   ├── sucs_types.h         # [COMPLETED] Base SUCS (sucs_char_t) & ExtSUCS (sucs_ex_char_t) definitions
│   ├── sucs_mode.h          # [COMPLETED] Kernel Mode-Switching Controller & boot config block
│   ├── sutf.h               # [COMPLETED] Master SUTF transport header (SUTF-8/16/4/2, vSUTF, extSUTF)
│   ├── sutf8.h              # [COMPLETED] SUTF-8 transport stream encoder/decoder prototypes
│   ├── sutf16.h             # [COMPLETED] SUTF-16 word transport encoder/decoder prototypes
│   ├── extsucs_types.h      # [COMPLETED] ExtSUCS 64-bit encoding & upcast/downcast inline helpers
│   ├── vsutf.h              # [COMPLETED] vSUTF variable-length 9-byte transport prototypes
│   ├── extsutf_fixed.h      # [COMPLETED] SUTF-32/64/128/256/512/N vector slot transport prototypes
│   └── esutf.h              # [COMPLETED] e-SUTF hypervisor virtual page translation & IPC frame
├── src/
│   ├── mbl_core.c           # [COMPLETED] Main C entry (mbl_main), PS/2 controller init, boot loop, action dispatcher
│   ├── mbl_framebuffer.c    # [COMPLETED] Pixel plotting, rect fill, line draw, double-buffer swap
│   ├── mbl_gui.c            # [COMPLETED] Boot menu rendering, theme palette, mouse packet decoder
│   ├── mbl_sutf_gui.c       # [COMPLETED] Embedded 8x16 font blitter, SUCS formatting & SUTF-8 text console
│   ├── mbl_loader.c         # [COMPLETED] Executable image relocation, memory map validation & kernel jump
│   ├── sucs_mode.c          # [COMPLETED] Mode switch commit logic
│   ├── sutf8.c              # [COMPLETED] Canonical SUTF-8 encoding/decoding routines
│   ├── sutf16.c             # [COMPLETED] Canonical SUTF-16 encoding/decoding routines
│   ├── extsutf_fixed.c      # [COMPLETED] Fixed-width vector slot serialization
│   ├── vsutf.c              # [COMPLETED] vSUTF 9-byte stream serialization
│   └── esutf.c              # [COMPLETED] e-SUTF IPC page serialization
└── CMakeLists.txt           # [COMPLETED] Cross-compilation bare-metal GCC/Clang + NASM setup
```

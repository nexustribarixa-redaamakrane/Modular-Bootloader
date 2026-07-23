# Modular Bootloader (MBL)

A modular, freestanding x86 bootloader designed for the **OpenWindows Operating System Kernel**. Built with C99, NASM assembly, and CMake.

## Overview

MBL is a multi-stage bootloader featuring a custom GUI boot menu, framebuffer rendering, and support for the **SuperUnicode (SUCS)** character encoding and text formatting transport systems. It loads the OpenWindows kernel with configurable display modes and system settings.

## Features

- **Multi-Stage Boot Architecture** — Stage 1 (MBR) and Stage 2 (VBE Protected Mode loader)
- **Modern GUI Boot Menu** — Dark theme with neon cyan accents, mouse cursor, and keyboard navigation
- **Double-Buffered Framebuffer** — ARGB8888 linear framebuffer with smooth rendering
- **SuperUnicode Support** — Base SUCS, ExtSUCS, SUTF-8/16/4/2, vSUTF, and extSUTF transports
- **Freestanding C99** — Zero standard library dependencies
- **Dual License** — MIT and Apache 2.0

## Building

### Prerequisites

- CMake >= 3.10
- GCC or Clang (cross-compilation capable)
- NASM assembler

### Build

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

### Build Artifacts

| File | Description |
|------|-------------|
| `mbl_core.bin` | 512-byte Stage 1 MBR bootsector |
| `mbl_stage2.sys` | Stage 2 VBE 0x0118 (1024x768x32bpp) loader |
| `libmbl.a` | Freestanding C99 static library |

## Project Structure

```
mbl/
├── boot/
│   ├── stage1.asm           # 16-bit MBR boot sector
│   └── stage2.asm           # 32-bit VBE protected mode loader
├── include/
│   ├── io.h                 # Port I/O inline assembly
│   ├── mbl_core.h           # Core state, memory map, boot flags
│   ├── mbl_framebuffer.h    # Double-buffered framebuffer routines
│   ├── mbl_gui.h            # GUI boot menu and event handling
│   ├── mbl_sutf_gui.h       # SUCS glyph blitter & SUTF-8 console
│   ├── sucs_types.h         # SUCS type definitions
│   ├── sucs_mode.h          # Kernel mode-switching controller
│   ├── sutf.h               # Master SUTF transport header
│   ├── sutf8.h              # SUTF-8 encoder/decoder
│   ├── sutf16.h             # SUTF-16 encoder/decoder
│   ├── extsucs_types.h      # ExtSUCS 64-bit encoding
│   ├── vsutf.h              # vSUTF variable-length transport
│   ├── extsutf_fixed.h      # Fixed-width vector slot transport
│   └── esutf.h              # e-SUTF IPC page translation
├── src/                     # Source implementations
├── CMakeLists.txt           # Build configuration
└── memory.md                # Architecture documentation
```

## Boot Menu

1. **OpenWindows Kernel (Secured)** — Load and jump to the main kernel
2. **Boot from Device** — Chainload secondary drive/partition
3. **System Configuration** — Configure SUCS/ExtSUCS mode, display resolution, boot flags
4. **Shutdown** — System power-off via ACPI/APM

## License

This project is dual-licensed under the [MIT License](license-mit) and [Apache License 2.0](license-apache). You may choose either license for your use.

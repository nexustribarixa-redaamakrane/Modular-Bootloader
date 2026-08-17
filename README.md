# Modular Bootloader (MBL)

A modular, freestanding x86 BIOS bootloader designed for the **OpenWindows Operating System Kernel**. Built with C99, NASM assembly, and automated Python toolchain scripts.

---

## Overview

MBL is a multi-stage x86 BIOS bootloader featuring a GRUB-style text-mode (80x25 VGA) boot menu, OpenWindows File System (OWFS) driver, BIOS disk read/chainload trampolines, and **SuperUnicode (SUCS / SUTF)** character encoding transport support. It loads and transfers control to the 32-bit OpenWindows kernel at `0x200000` with a standardized boot configuration block.

---

## Architecture & Boot Stages

```
   +-------------------------------------------------------------------+
   | Stage 1: MBR Boot Sector (512 Bytes @ 0x7C00, LBA 0)              |
   | - Initializes segments & stack (0x7C00)                           |
   | - Publishes mbl_bootinfo_t at 0x0500                              |
   | - Loads Stage 2 from LBA 1..15 via INT 13h (LBA Ext / CHS) to     |
   |   0x0000:0x8000                                                   |
   +---------------------------------+---------------------------------+
                                     |
                                     v
   +---------------------------------+---------------------------------+
   | Stage 2: Boot Image Loader (LBA 1..15 @ 0x8000)                   |
   | - Checks INT 13h Extensions (AH=0x41) with geometry CHS fallback |
   | - Loads 96-sector boot image from LBA 16..111 to 0x8000:0x0000    |
   |   (linear 0x80000)                                                |
   | - Far jumps to 0x8000:0x0000 (_rm_entry)                          |
   +---------------------------------+---------------------------------+
                                     |
                                     v
   +---------------------------------+---------------------------------+
   | Boot Image Blob (Flat binary @ 0x80000, LBA 16..111)              |
   | - Real-Mode Entry: Sets mode 3, enables A20, loads GDT, enters PM |
   | - Protected-Mode Entry: Sets flat segments, stack at 0x90000,     |
   |   clears .bss, calls kmain()                                      |
   | - C Bootloader (main, vga, kbd, menu, owfs, bios_disk, sutf)      |
   | - BIOS Trampoline (_bios_tramp): Transitions PM -> RM for         |
   |   INT 13h sector reads, then returns to 32-bit PM                 |
   | - Kernel Handoff: Streams kernel from OWFS volume to 0x200000,    |
   |   sets mbl_boot_config_t, and jumps to kernel                     |
   +-------------------------------------------------------------------+
```

---

## Features

- **Multi-Stage Loading** — Stage 1 (512-byte MBR) & Stage 2 with INT 13h Extensions and CHS fallback (floppy and hard drive support).
- **Protected Mode / Real Mode BIOS Trampoline** — Seamless 32-bit Protected Mode to 16-bit Real Mode transitions for BIOS disk services and chainloading.
- **OWFS Filesystem Driver** — Read-only catalog enumeration, block mapping (direct and indirect blocks), and file loading with CRC32c verification.
- **VGA Text-Mode GUI Menu** — GRUB-style black-and-white 80x25 text interface with keyboard navigation and timeout.
- **SuperUnicode (SUCS / SUTF)** — Integrated Base SUCS and SUTF-8 text decoder for file catalog and kernel boot config.
- **Freestanding C99** — Zero standard library dependencies (`-ffreestanding -nostdlib`).
- **Dual License** — MIT and Apache 2.0.

---

## Memory Layout

| Address Range | Size | Usage |
| :--- | :--- | :--- |
| `0x00000 - 0x003FF` | 1 KiB | Real Mode Interrupt Vector Table (IVT) |
| `0x00500 - 0x00507` | 8 B | Boot info block (`mbl_bootinfo_t`) |
| `0x00510 - 0x0051F` | 16 B | Stage 1 DAP buffer |
| `0x06000 - 0x0603F` | 64 B | BIOS call frame (`bios_call_t`, DAP, RM IDT) |
| `0x07C00 - 0x07DFF` | 512 B | Stage 1 MBR |
| `0x08000 - 0x09FFF` | ~8 KiB | Stage 2 loader |
| `0x10000 - 0x1FFFF` | 64 KiB | OWFS file staging buffer |
| `0x80000 - 0x8BFFF` | 48 KiB | MBL Boot Image Blob (code, data, trampoline) |
| `0x8C000 - 0x8FFFF` | 16 KiB | Bootloader stack (grows down from `0x90000`) |
| `0xB8000 - 0xB8FA0` | 4 KiB | 80x25 VGA Color Text Buffer |
| `0x200000+` | Variable | Kernel load address |

---

## Disk Image Layout (`mbl_test.img`)

| LBA Sector | Byte Offset | Size | Content |
| :--- | :--- | :--- | :--- |
| **0** | `0x0000` | 512 B | Stage 1 MBR |
| **1 .. 15** | `0x0200` | 7.5 KiB | Stage 2 Boot Loader |
| **16 .. 111** | `0x2000` | 48 KiB | Boot Image Blob (MBL Core + C modules) |
| **112 .. 127** | `0xE000` | 8 KiB | Reserved |
| **128+** | `0x10000` | ~8 MiB | OpenWindows OWFS volume (`kernel.bin`) |

---

## Building & Testing

### Prerequisites

- **GCC** (32-bit cross-compilation capable, e.g. `w64devkit` or `gcc -m32`)
- **NASM** (>= 2.14)
- **QEMU** (`qemu-system-i386`)
- **Python 3** (>= 3.8)

### Build Bootloader & Disk Image

```bash
python tools/build_image.py
```

This pipeline automatically:
1. Compiles C sources (`src/*.c`) to Intel-syntax assembly (`-m32 -S -masm=intel`).
2. Translates assembly to NASM format via `tools/gcc_intel_to_nasm.py`.
3. Assembles standalone ELF32 object files and creates the static library **`build/libmbl.a`** via `ar rcs` (for linking directly into the OpenWindows kernel or other targets).
4. Assembles `boot/stage1.asm`, `boot/stage2.asm`, `boot/bootimg.asm`, and `boot/test_kernel.asm` with NASM.
5. Formats the OWFS partition using `tools/owfs_mkfs.py` and seeds `kernel.bin`.
6. Emits the bootable raw disk image `mbl_test.img`.

### Run Automated QEMU Test

```bash
python tools/test_qemu.py
```

Runs QEMU in headless mode, connects to the TCP monitor, validates VGA text rendering, sends keystrokes, and verifies kernel handoff.

### Run in QEMU Interactively

```bash
qemu-system-i386 -drive file=mbl_test.img,format=raw -m 32
```

---

## Project Structure

```text
Modular-Bootloader/
├── boot/
│   ├── stage1.asm           # 16-bit MBR boot sector (LBA + CHS fallback)
│   ├── stage2.asm           # 16-bit stage 2 loader (INT 13h ext check + CHS)
│   ├── bootimg.asm          # Flat boot image (A20, GDT, PM entry, BIOS tramp)
│   └── test_kernel.asm      # 32-bit test kernel payload
├── include/
│   ├── mbl.h                # Master MBL definitions & structures
│   └── sutf/
│       ├── sucs_mode.h      # Kernel SUCS mode configuration
│       └── sutf8.h          # SUTF-8 stream encoder / decoder
├── src/
│   ├── main.c               # kmain entry, volume probe, kernel handoff
│   ├── menu.c               # GRUB-style text menu rendering and input
│   ├── vga.c                # 80x25 VGA text buffer driver
│   ├── kbd.c                # PS/2 keyboard poller and scan code handler
│   ├── owfs.c               # Read-only OpenWindows File System driver
│   ├── bios_disk.c          # Disk block I/O wrapper
│   └── sutf/
│       ├── sutf8.c          # SUTF-8 character decode routines
│       └── sucs_mode.c      # SUCS mode initialization
├── tools/
│   ├── build_boot.py        # C compile & NASM assemble pipeline
│   ├── build_image.py       # Full raw disk image builder
│   ├── gcc_intel_to_nasm.py # GCC Intel assembly to NASM translator
│   ├── owfs_mkfs.py         # OWFS partition formatter and seeder
│   └── test_qemu.py         # Automated QEMU verification harness
├── memory.md                # Persistent system architecture tracker
└── README.md                # Project documentation
```

---

## License

Dual-licensed under either:
- **[MIT License](LICENSE-MIT)**
- **[Apache License 2.0](LICENSE-APACHE)**

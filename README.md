# Modular Bootloader (MBL)

A modular, freestanding 64-bit UEFI bootloader with GOP framebuffer rendering designed for the **OpenWindows Operating System Kernel**. Built with C99, UEFI protocols, and automated Python toolchain scripts.

---

## Overview

MBL is a 64-bit UEFI application (`BOOTX64.EFI`) featuring a GRUB-style text-mode interface rendered onto the UEFI **Graphics Output Protocol (GOP)** framebuffer, an integrated **OpenWindows File System (OWFS)** driver, UEFI **Block I/O** sector access, and **SuperUnicode (SUCS / SUTF)** character encoding transport support. It loads and transfers control to the 32-bit OpenWindows kernel at `0x200000` with a standardized boot configuration block after cleanly invoking `ExitBootServices`.

---

## Architecture & Boot Flow

```text
   +-------------------------------------------------------------------+
   | UEFI Firmware (EDK II / OVMF / Motherboard Firmware)             |
   | - Initializes CPU (x86_64 Long Mode), memory, and PCI devices     |
   | - Mounts FAT32 EFI System Partition (ESP) on GPT disk             |
   | - Loads and launches \EFI\BOOT\BOOTX64.EFI                        |
   +---------------------------------+---------------------------------+
                                     |
                                     v
   +---------------------------------+---------------------------------+
   | UEFI Application Entry (src/efi_entry.c: EfiMain)                 |
   | - Stores EFI System Table, Boot Services, and Runtime Services    |
   | - Locates Graphics Output Protocol (GOP) & selects highest resolution mode     |
   | - Locates Block I/O Protocol for the root fixed storage drive     |
   | - Initializes ConIn / ConOut and calls kmain()                    |
   +---------------------------------+---------------------------------+
                                     |
                                     v
   +---------------------------------+---------------------------------+
   | MBL Core & GOP Text Renderer (src/gop.c, src/menu.c, src/owfs.c)  |
   | - GOP Text Engine: Renders 8x16 font glyphs directly to FB       |
   | - OWFS Driver: Reads GPT partition 2 at LBA 131200 via Block I/O  |
   | - Menu & Input: Displays GRUB-style boot menu with countdown      |
   | - Streams selected kernel payload from OWFS volume to 0x200000    |
   +---------------------------------+---------------------------------+
                                     |
                                     v
   +---------------------------------+---------------------------------+
   | Kernel Handoff (src/main.c)                                       |
   | - Writes mbl_boot_config_t block to 0x00000510 (MBL2 magic)       |
   | - Retrieves final memory map & calls ExitBootServices             |
   | - Sets up flat 32-bit segment registers & stack at 0x180000       |
   | - Calls 32-bit OpenWindows kernel entry point                     |
   +-------------------------------------------------------------------+
```

---

## Features

- **Native 64-bit UEFI** — Pure UEFI application (`EFI_APPLICATION`, subsystem 10) targeting Windows x64 ABI (`ms_abi`).
- **GOP Framebuffer Text Rendering** — Hardware-independent graphical text renderer utilizing an embedded 8x16 CP437 font with support for both BGRA and RGBA linear pixel buffers.
- **GPT & FAT32 Support** — Boots from standard GPT partition layouts with an EFI System Partition (ESP).
- **OWFS Filesystem Driver** — Read-only catalog enumeration, block mapping (direct and indirect blocks), and file loading with full CRC32c verification from GPT data partitions.
- **GRUB-Style Text Menu** — Responsive keyboard-driven boot menu with automatic countdown, timeout fallback, reboot, and shutdown.
- **SuperUnicode (SUCS / SUTF)** — Integrated Base SUCS and SUTF-8 text decoder for catalog names and kernel boot configuration transport.
- **BANcode Boot Diagnostics** — Every boot fault path raises a `uint32_t` codepoint from the canonical C+/W+/S+/B+ registry blocks (provisional MBL placements), recorded in an in-memory ring buffer and rendered on fail screens together with their resolved Kernel Security Trap codepoints.
- **Freestanding C99** — Zero standard library dependencies (`-ffreestanding -nostdlib -mno-red-zone`).
- **Dual License** — MIT and Apache 2.0.

---

## Ecosystem Compatibility

MBL is designed to share translation units with its sibling OpenWindows
projects. A single-TU smoke test (`tests/compat_smoke.c`, run via
`python tools/compat_smoke.py`) compiles `mbl.h` together with all four
sibling headers and verifies every integration surface at runtime:

| Project | Integration |
|---|---|
| **SuperUnicode** | `sutf/sutf8.{h,c}`, `sutf/sucs_mode.{h,c}`, and `sutf/sucs_types.h` are vendored byte-identical copies of libsutf; catalog names decode through the same overlong-rejecting, trap-range-excluding codec, and the handoff block embeds `sucs_kernel_boot_config_t` directly. |
| **BANcode** | `<bancode/bancode_all.h>` mirrors the upstream generated master header (shared include guard: whichever comes first wins); driver APIs return `bancode_t` codes from the canonical blocks, with trap geometry matching `bancode_to_trap()`. Concrete slots are provisional placements in `<bancode/mbl_bancode.h>`. |
| **vip** | Volume addressing uses absolute 512-byte LBAs (`OWFS_PARTITION_LBA` = 131200 = `VIP_OWFS_PARTITION_LBA`); the MBL reserved region (LBAs 0–127) is honored as `VIP_MBL_RESERVED_LBA_COUNT`; runtime parity proven against the real libvip. |
| **OpenWindows-Storage** | On-disk structures mirror libowfs byte-for-byte — including the full 4096-byte superblock with ChaCha20 key slots. When storage headers are on the include path they are adopted via `__has_include` (zero redefinitions). Encrypted volumes are refused cleanly (`MBL_SOFT_ENCRYPTED_VOLUME`), USFS media is detected and never misread, and dirty/error/locked volume states raise W+ WARNcode telemetry while still allowing read-only boot. |

---

## Memory & Handoff Layout

| Address Range | Size | Usage |
| :--- | :--- | :--- |
| `0x00000510 - 0x0000052B` | 28 B | Boot configuration block (`mbl_boot_config_t`) |
| `0x00030000 - 0x00030FFF` | 4 KiB | OWFS staging buffer (`MBL_FS_BUF`) |
| `0x00180000` | — | Initial 32-bit stack pointer for kernel handoff |
| `0x00200000+` | Variable | Target kernel load address (`MBL_KERNEL_ADDR`, up to 16 MiB) |
| Dynamic Firmware Alloc | Variable | GOP Linear Framebuffer (`FrameBufferBase`) |

---

## Disk Image Layout (`mbl_test.img`)

Total Size: 96 MiB (196,608 sectors @ 512 B/sector)

| LBA Sector | Byte Offset | Size | Content |
| :--- | :--- | :--- | :--- |
| **0** | `0x000000` | 512 B | Protective MBR (Type `0xEE`) |
| **1** | `0x000200` | 512 B | Primary GPT Header (`EFI PART`) |
| **2 .. 33** | `0x000400` | 16 KiB | Primary GPT Partition Table (128 entries) |
| **128 .. 131199** | `0x010000` | 64 MiB | **Partition 1: FAT32 EFI System Partition (ESP)** (`\EFI\BOOT\BOOTX64.EFI`) |
| **131200 .. 196735** | `0x4010000` | 32 MiB | **Partition 2: OpenWindows OWFS Data Partition** (`kernel.bin`) |
| **196736 .. 198622** | `0x6010000` | ~943 KiB | Unallocated / alignment space |
| **198623 .. 198654** | `0x60EFE00` | 16 KiB | Backup GPT Partition Table (128 entries) |
| **198655** | `0x60FFE00` | 512 B | Backup GPT Header |

---

## Building & Testing

### Prerequisites

- **x86_64 MinGW-w64 GCC** (e.g. from `w64devkit` or `x86_64-w64-mingw32-gcc`)
- **NASM** (for assembling the test kernel payload)
- **QEMU** (`qemu-system-x86_64`)
- **OVMF Firmware** (`OVMF.fd` included at repository root)
- **Python 3** (>= 3.8)

### Build Bootloader & Disk Image

```bash
python tools/build_image.py
```

This pipeline automatically:
1. Compiles all freestanding C sources with `x86_64-w64-mingw32-gcc`.
2. Packages standalone 64-bit static library **`build/libmbl.a`** via `ar rcs`.
3. Links `build/BOOTX64.EFI` with `-Wl,--subsystem,10 -Wl,-e,EfiMain`.
4. Assembles `boot/test_kernel.asm` to `build/test_kernel.bin`.
5. Creates a 96 MiB GPT disk image with a FAT32 ESP containing `\EFI\BOOT\BOOTX64.EFI`.
6. Formats the OWFS data partition starting at LBA 131200 via `tools/owfs_mkfs.py` and seeds `kernel.bin`.
7. Writes the final bootable raw disk image `mbl_test.img`.

### Run Automated QEMU Test

```bash
python tools/test_qemu.py
```

Launches QEMU in headless mode with `OVMF.fd`, verifies UEFI boot discovery, GOP framebuffer initialization, menu responsiveness, and test kernel execution.

### Run Ecosystem Compatibility Suite

```bash
python tools/compat_smoke.py
```

Compiles `tests/compat_smoke.c` as a single translation unit together with the public headers **and** implementations of `../BANcode`, `../superunicode`, `../OpenWindows-Storage`, and `../vip`, then executes ~770 runtime checks covering on-disk layout parity, absolute-LBA addressing, SUTF codec agreement, mode-controller handoff semantics, and BANcode trap geometry. The sibling repositories must be checked out next to this one.

### Run in QEMU Interactively

```bash
qemu-system-x86_64 -accel tcg -bios OVMF.fd -drive file=mbl_test.img,format=raw -m 256
```

---

## Project Structure

```text
Modular-Bootloader/
├── .clangd                  # Clangd include and target configuration
├── OVMF.fd                  # EDK II OVMF x86_64 UEFI firmware for QEMU
├── boot/
│   └── test_kernel.asm      # 32-bit test kernel payload
├── include/
│   ├── efi.h                # Minimal UEFI 2.x types and protocol definitions
│   ├── efi_font.h           # 8x16 CP437 bitmap font glyph table
│   ├── mbl.h                # Master MBL definitions, OWFS layout, prototypes
│   ├── bancode/
│   │   ├── bancode_all.h    # BANcode framework mirror (upstream-guarded)
│   │   └── mbl_bancode.h    # Provisional MBL boot diagnostic placements
│   └── sutf/
│       ├── sucs_mode.h      # Kernel SUCS mode configuration
│       ├── sucs_types.h     # Base SUCS codepoint space & sentinels
│       └── sutf8.h          # SUTF-8 stream encoder / decoder
├── src/
│   ├── efi_entry.c          # UEFI entry point (EfiMain), protocol discovery
│   ├── gop.c                # GOP linear framebuffer text renderer
│   ├── kbd.c                # UEFI Simple Text Input & RTC timer
│   ├── bios_disk.c          # UEFI Block I/O disk sector reader
│   ├── owfs.c               # Read-only OpenWindows File System driver
│   ├── bancode/
│   │   └── bancode_boot.c   # Diagnostic ring buffer, code names, formatter
│   ├── menu.c               # GRUB-style boot menu logic
│   ├── main.c               # kmain, volume probe, kernel handoff
│   └── sutf/
│       ├── sutf8.c          # SUTF-8 character decoder routines
│       └── sucs_mode.c      # SUCS boot configuration init
├── tests/
│   └── compat_smoke.c       # Single-TU ecosystem compatibility suite
├── tools/
│   ├── build_efi.py         # UEFI C compiler and linker script
│   ├── build_image.py       # GPT + FAT32 ESP + OWFS disk image builder
│   ├── owfs_mkfs.py         # OWFS partition formatter and seeder
│   ├── compat_smoke.py      # Ecosystem compatibility test runner
│   └── test_qemu.py         # Automated QEMU UEFI test harness
├── memory.md                # Persistent system architecture tracker
└── README.md                # Project documentation
```

---

## License

Dual-licensed under either:
- **[MIT License](LICENSE-MIT)**
- **[Apache License 2.0](LICENSE-APACHE)**

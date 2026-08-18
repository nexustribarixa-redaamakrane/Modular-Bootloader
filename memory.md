# PERSISTENT MEMORY ARTIFACT: MODULAR BOOTLOADER (MBL) & SUPERUNICODE ARCHITECTURE

> **SYSTEM ARCHITECTURE & STATE TRACKER**
> **Workspace:** `C:\Users\KARIMABENDA\Documents\Modular-Bootloader`
> **Project:** Modular Bootloader (`MBL`) — 64-bit UEFI bootloader with GOP framebuffer text rendering and OpenWindows File System (`OWFS`) driver for the OpenWindows Operating System Kernel (`OpenWindows-Kernel`).

---

## 1. Current Build Status

| Component | Status | Notes |
| :--- | :--- | :--- |
| UEFI Entry (`efi_entry.c`) | DONE | `EfiMain` initializes UEFI globals (`gST`, `gBS`, `gRT`, `gImageHandle`), locates GOP (selects highest resolution from first working handle), Block I/O (fixed disk), ConIn/ConOut, and Loaded Image Protocol. |
| GOP Framebuffer Renderer (`gop.c`) | DONE | Renders 8x16 CP437 bitmap font glyphs into linear framebuffer (`PixelBlueGreenRedReserved8BitPerColor` and `PixelRedGreenBlueReserved8BitPerColor`), matching `vga_*` API. |
| UEFI Keyboard & RTC (`kbd.c`) | DONE | `EFI_SIMPLE_TEXT_INPUT_PROTOCOL` key polling (ScanCode / UnicodeChar) and `rtc_get_seconds()` for RTC timeout via `GetTime`. |
| UEFI Block I/O Driver (`bios_disk.c`) | DONE | `EFI_BLOCK_IO_PROTOCOL->ReadBlocks` for sector reads, `disk_read_block` reads from `OWFS_PARTITION_LBA` (131200). |
| OWFS Filesystem Driver (`owfs.c`) | DONE | Read-only volume probe, superblock / catalog / inode CRC32c verification, block mapping (direct + indirect), and file streaming. |
| GRUB-style Menu (`menu.c`) | DONE | 80x25 text layout on GOP framebuffer with countdown timer, keyboard navigation, reboot, and shutdown. |
| Boot Entry & Handoff (`main.c`) | DONE | Probes OWFS, runs menu, loads kernel to `0x200000`, publishes `mbl_boot_config_t` at `0x00000510`, calls `GetMemoryMap` + `ExitBootServices`, sets segments and jumps to kernel. |
| SuperUnicode / SUTF-8 (`sutf/`) | DONE | Decodes SUTF-8 volume / catalog filenames and initializes kernel SUCS boot configuration. |
| EFI Application Builder (`build_efi.py`) | DONE | Compiles all C modules with `x86_64-w64-mingw32-gcc` (`-m64 -ffreestanding -mno-red-zone -mno-sse -mcmodel=large`), generates `build/libmbl.a` and links `build/BOOTX64.EFI` (`-Wl,--subsystem,10 -Wl,-e,EfiMain`). |
| Static Library (`libmbl.a`) | DONE | Standalone 64-bit static archive in `build/libmbl.a` (29,250 bytes) packaging all 9 UEFI C modules. |
| GPT & FAT32 Image Builder (`build_image.py`) | DONE | Assembles protective MBR, primary/backup GPT headers and partition tables, 64 MiB FAT32 ESP with `\EFI\BOOT\BOOTX64.EFI`, and 32 MiB OWFS partition with `kernel.bin`. |
| OWFS Volume Formatter (`owfs_mkfs.py`) | DONE | Formats OWFS volume at arbitrary partition offsets (`block_start=OWFS_LBA * SECTOR`) with CRC32c and Fletcher-64 checksums. |
| Automated QEMU Test (`test_qemu.py`) | DONE | Boots `mbl_test.img` under QEMU with `OVMF.fd`, connects via monitor, verifies menu rendering and kernel handoff. |

---

## 2. Toolchain

| Tool | Path | Version | Purpose |
| :--- | :--- | :--- | :--- |
| MinGW-w64 GCC | `C:\w64devkit\bin\x86_64-w64-mingw32-gcc.exe` | w64devkit x86_64 | Compiles freestanding UEFI PE/COFF modules (Windows x64 ABI / `ms_abi`) |
| NASM | `C:\Program Files\NASM\nasm.exe` | Latest | Assembles 32-bit test kernel (`boot/test_kernel.asm`) |
| QEMU | `C:\Program Files\qemu\qemu-system-x86_64.exe` | Latest | Emulates x86_64 UEFI machine with OVMF firmware |
| OVMF Firmware | `OVMF.fd` (project root) | EDK II OVMF | UEFI x86_64 firmware for QEMU testing |
| Python | System Python 3.14 | 3.14+ | Build scripts and automated testing |

### GCC Compilation Flags
```text
-m64 -c -ffreestanding -nostdlib -fno-pic -fno-stack-protector
-fno-builtin -fno-asynchronous-unwind-tables -fno-unwind-tables -fno-ident
-fno-common -mno-red-zone -mno-sse -mno-80387 -mcmodel=large -O2
-I include -I include/sutf
```

### GCC Linker Flags (PE/COFF EFI Application)
```text
-shared -Bsymbolic -Wl,--subsystem,10 -Wl,-e,EfiMain -Wl,--no-se -nostdlib -mno-red-zone
```

### Build & Test Commands
```bash
# Build EFI binary and assemble disk image
python tools/build_image.py

# Run automated QEMU UEFI test
python tools/test_qemu.py
```

---

## 3. Disk Image Layout (`mbl_test.img`)

Total Size: 96 MiB (196,608 sectors @ 512 B/sector)

| LBA Sector | Byte Offset | Size | Content |
| :--- | :--- | :--- | :--- |
| **0** | `0x000000` | 512 B | Protective MBR (Partition Type `0xEE`) |
| **1** | `0x000200` | 512 B | Primary GPT Header (`EFI PART`) |
| **2 .. 33** | `0x000400` | 16 KiB | Primary GPT Partition Table (128 entries of 128 bytes) |
| **128 .. 131199** | `0x010000` | 64 MiB | **Partition 1: FAT32 EFI System Partition (ESP)**<br>Contains `\EFI\BOOT\BOOTX64.EFI` |
| **131200 .. 196735** | `0x4010000` | 32 MiB | **Partition 2: OpenWindows OWFS Data Partition**<br>Contains `kernel.bin` and catalog |
| **196736 .. 198622** | `0x6010000` | ~943 KiB | Unallocated / alignment space |
| **198623 .. 198654** | `0x60EFE00` | 16 KiB | Backup GPT Partition Table (128 entries) |
| **198655** | `0x60FFE00` | 512 B | Backup GPT Header (LBA `DISK_SECTORS - 1`) |

### Partition GUIDs
- **ESP Type GUID:** `C12A7328-F81F-11D2-BA4B-00A0C93EC93B`
- **OWFS Type GUID (Linux Data):** `E6D6D379-F507-44C2-A23C-238F2A3DF928`
- **ESP Partition GUID:** `12345678-1234-1234-1234-123456789ABC`
- **OWFS Partition GUID:** `ABCDEF01-2345-6789-ABCD-EF0123456789`

---

## 4. Memory Map & Handoff Protocol

| Address / Range | Size | Component / Purpose |
| :--- | :--- | :--- |
| `0x00000510` | 28 B | Boot configuration block (`mbl_boot_config_t`) passed to kernel |
| `0x00030000` | 4 KiB | OWFS file staging block buffer (`MBL_FS_BUF`) |
| `0x00180000` | — | Initial 32-bit stack top for kernel execution |
| `0x00200000` | Variable | Target kernel load address (`MBL_KERNEL_ADDR`, up to 16 MiB) |
| Dynamic (UEFI pool) | Variable | EFI memory map buffer allocated via `AllocatePages` |
| Firmware-assigned | Variable | Linear GOP Framebuffer (`FrameBufferBase`) |

### Kernel Boot Configuration (`mbl_boot_config_t`)
```c
typedef struct {
    uint32_t magic;                      // MBL_MAGIC_BOOTCFG ('MBL2' / 0x324C424D)
    uint8_t  boot_drive;                 // Drive index (0)
    uint8_t  pad[3];
    uint32_t kernel_size;                // Kernel payload size in bytes
    sucs_kernel_boot_config_t sucs_cfg;  // SuperUnicode kernel boot mode config
} mbl_boot_config_t;
```

---

## 5. UEFI Subsystem Architecture

### Protocol Usage
1. **EFI Graphics Output Protocol (`EFI_GRAPHICS_OUTPUT_PROTOCOL`)**
   - Discovered in `efi_entry.c` via `LocateHandleBuffer`.
   - Highest available resolution mode selected automatically (from the first working GOP handle).
   - `gop.c` maps character cells into glyph pixel coordinates using an 8x16 font table.
2. **EFI Block I/O Protocol (`EFI_BLOCK_IO_PROTOCOL`)**
   - Discovered in `efi_entry.c` (filters for non-removable, media-present block device).
   - Used by `bios_disk.c` to read 512-byte sectors and 4096-byte OWFS blocks directly from disk.
3. **EFI Simple Text Input Protocol (`EFI_SIMPLE_TEXT_INPUT_PROTOCOL`)**
   - Handled via `gST->ConIn` in `kbd.c`.
   - Translates arrow keys, PageUp/Down, Home/End scan codes, and Enter/Esc ASCII characters.
4. **EFI Runtime Services (`EFI_RUNTIME_SERVICES`)**
   - `GetTime`: Queries RTC second count for menu countdown timeout.
   - `ResetSystem`: Triggers `EfiResetCold` (reboot) or `EfiResetShutdown` (power off).
5. **EFI Boot Services (`EFI_BOOT_SERVICES`)**
   - `GetMemoryMap` / `AllocatePages`: Retrieves memory map and allocates buffer for map key.
   - `ExitBootServices`: Called before transferring control to the bare-metal kernel.

---

## 6. Key Headers & Source Files

| File | Purpose |
| :--- | :--- |
| `include/efi.h` | Minimal UEFI 2.x type definitions, protocol structs, system table wrappers, and globals |
| `include/efi_font.h` | 8x16 CP437 bitmap font glyph array (95 printable ASCII characters) |
| `include/mbl.h` | Master bootloader constants, OWFS layout structs, menu entry structures, and C function prototypes |
| `include/sutf/sutf8.h` | SUTF-8 stream encoder / decoder prototypes |
| `include/sutf/sucs_mode.h` | SuperUnicode mode definitions (`SUCS_MODE_BASE`, `SUCS_MODE_EXTENDED`) |
| `src/efi_entry.c` | UEFI entry point (`EfiMain`), locates GOP, Block I/O, ConIn, ConOut |
| `src/gop.c` | GOP framebuffer text-mode emulation layer (implements `vga_*` API) |
| `src/kbd.c` | UEFI keyboard polling and `rtc_get_seconds()` RTC wrapper |
| `src/bios_disk.c` | Block I/O read wrappers for sector and OWFS block access |
| `src/owfs.c` | Read-only OpenWindows File System driver |
| `src/menu.c` | GRUB-style text menu rendering and input handling |
| `src/main.c` | `kmain` workflow: probe OWFS -> show menu -> load kernel -> `ExitBootServices` -> handoff |
| `src/sutf/sutf8.c` | SUTF-8 decoding implementation |
| `src/sutf/sucs_mode.c` | Kernel SUCS configuration initialization |

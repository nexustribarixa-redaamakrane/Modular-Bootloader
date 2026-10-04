# Modular Bootloader (MBL)

MBL is a freestanding x86-64 UEFI application for the OpenWindows boot path.
Its C entry point is `EfiMain` in `src/efi_entry.c`; the implementation uses
UEFI Boot Services, GOP, and Block I/O. CMake also defines a `mbl_static`
archive target, but the repeatable UEFI test image is built by the Python
scripts below.

## Current boot path

`src/main.c`, `src/menu.c`, `src/gop.c`, `src/kbd.c`, and `src/owfs.c` implement
the menu, framebuffer text output, keyboard input, and OWFS kernel-file read
path. The image builder places `BOOTX64.EFI` at
`\EFI\BOOT\BOOTX64.EFI` in a FAT32 EFI System Partition and includes the
repository's `boot/test_kernel.asm` payload as `kernel.bin` in an OWFS
partition. The test payload is not the OpenWindows production kernel.

The image layout in `tools/build_image.py` uses 512-byte sectors:

| Partition | Start LBA | Size | Contents |
| --- | ---: | ---: | --- |
| EFI System Partition | 128 | 64 MiB | FAT32 and `BOOTX64.EFI` |
| OWFS | 131200 | 32 MiB | `kernel.bin` |

The image also contains a protective MBR and primary and backup GPT metadata.
The disk GUID is generated at image-build time, so two builds are not
byte-identical.

## Build the test image

Requirements:

- Python 3 (the scripts use the standard library).
- NASM, used for `boot/test_kernel.asm`.
- GCC capable of emitting x86-64 PE/COFF EFI binaries. The script prefers
  `x86_64-w64-mingw32-gcc`; set `MBL_GCC` to the compiler path if needed.
- `ar` or `x86_64-w64-mingw32-ar`; set `MBL_AR` to override it.

From the repository root:

```powershell
py tools/build_image.py
```

This compiles the EFI app, creates `build/libmbl.a` and
`build/BOOTX64.EFI`, assembles the test kernel, and writes `mbl_test.img` in
the repository root. CMake requires 3.10 or newer and also defines the
`mbl_static` and `mbl_disk_img` targets; the direct script is the unambiguous
image-building entry point.

## Checks

The compatibility check compiles `tests/compat_smoke.c` against sibling
checkouts of `BANcode`, `OpenWindows-Storage`, `superunicode`, and `vip` placed
beside this repository:

```powershell
py tools/compat_smoke.py
```

To boot the generated test disk under QEMU/OVMF and drive the menu:

```powershell
py tools/test_qemu.py
```

Install `qemu-system-x86_64` and provide OVMF firmware. The default firmware
path is `OVMF.fd` in the repository root. `MBL_QEMU`, `MBL_OVMF`,
`MBL_MON_PORT`, and `MBL_NASM` can override the corresponding tool/path
settings. `--no-build` runs the QEMU test against an already-built image.

## Limits

The supplied end-to-end image exercises this bootloader with a deliberately
small test kernel. It does not establish compatibility with arbitrary kernels,
UEFI implementations, physical hardware, or production Secure Boot. The
OWFS reader and handoff structures are project-specific interfaces; see
`include/mbl.h` and `src/owfs.c` before integrating another kernel.

#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""build_boot.py - compile the C bootloader modules, build the static library
(libmbl.a), and assemble the flat boot image.

Pipeline:
  gcc -m32 -S -masm=intel (freestanding)  ->  Intel-syntax .s
  tools/gcc_intel_to_nasm.py              ->  NASM .gen.asm
  NASM (flat binary, ORG 0x80000)         ->  boot/bootimg.asm includes the
                                             .gen.asm modules and helpers.
  gcc -m32 -c + ar rcs                    ->  build/libmbl.a

Also assembles boot/stage1.asm (MBR) and boot/stage2.asm.
Outputs land in build/: stage1.bin, stage2.bin, boot_blob.bin, libmbl.a.
"""

import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOLS = os.path.join(ROOT, "tools")
BOOT = os.path.join(ROOT, "boot")
BUILD = os.path.join(ROOT, "build")

GCC = os.environ.get("MBL_GCC", r"C:\w64devkit\bin\gcc.exe")
AR = os.environ.get("MBL_AR", os.path.join(os.path.dirname(GCC), "ar.exe"))
NASM = os.environ.get("MBL_NASM", r"C:\Program Files\NASM\nasm.exe")
PY = sys.executable

CFLAGS = [
    "-m32", "-S", "-masm=intel",
    "-ffreestanding", "-nostdlib",
    "-fno-pic", "-fno-stack-protector", "-fno-builtin",
    "-fno-asynchronous-unwind-tables", "-fno-unwind-tables",
    "-fno-ident", "-fno-common",
    "-mno-sse", "-mno-80387",
    "-O2",
    "-I", os.path.join(ROOT, "include"),
    "-I", os.path.join(ROOT, "include", "sutf"),
]

CFLAGS_OBJ = [
    "-m32", "-c",
    "-ffreestanding", "-nostdlib",
    "-fno-pic", "-fno-stack-protector", "-fno-builtin",
    "-fno-asynchronous-unwind-tables", "-fno-unwind-tables",
    "-fno-ident", "-fno-common",
    "-mno-sse", "-mno-80387",
    "-O2",
    "-I", os.path.join(ROOT, "include"),
    "-I", os.path.join(ROOT, "include", "sutf"),
]

# Order matters only for readability; symbols resolve across modules.
SRCS = [
    "main.c",
    "vga.c",
    "kbd.c",
    "menu.c",
    "owfs.c",
    "bios_disk.c",
    "sutf/sutf8.c",
    "sutf/sucs_mode.c",
]

SIZE_STAGE1 = 512          # exactly one sector
SIZE_STAGE2_MAX = 15 * 512 # LBA 1..15
SIZE_BLOB_MAX = 96 * 512   # LBA 16..111, loaded to 0x80000


def run(cmd, **kw):
    print("+ " + " ".join(cmd))
    return subprocess.run(cmd, check=True, **kw)


def build_c_modules():
    os.makedirs(BUILD, exist_ok=True)
    for rel in SRCS:
        src = os.path.join(ROOT, "src", rel)
        base = os.path.basename(rel).replace(".c", "")
        s_file = os.path.join(BUILD, base + ".s")
        gen_file = os.path.join(BUILD, base + ".gen.asm")
        run([GCC, *CFLAGS, "-o", s_file, src])
        run([PY, os.path.join(TOOLS, "gcc_intel_to_nasm.py"), s_file, gen_file])


def build_static_lib():
    obj_files = []
    for rel in SRCS:
        src = os.path.join(ROOT, "src", rel)
        base = os.path.basename(rel).replace(".c", "")
        obj_file = os.path.join(BUILD, base + ".o")
        run([GCC, *CFLAGS_OBJ, "-o", obj_file, src])
        obj_files.append(obj_file)
    lib_file = os.path.join(BUILD, "libmbl.a")
    run([AR, "rcs", lib_file, *obj_files])


def assemble():
    # stage 1 (MBR, exactly 512 bytes)
    run([NASM, "-f", "bin", os.path.join(BOOT, "stage1.asm"),
         "-o", os.path.join(BUILD, "stage1.bin")])
    # stage 2
    run([NASM, "-f", "bin", os.path.join(BOOT, "stage2.asm"),
         "-o", os.path.join(BUILD, "stage2.bin")])
    # boot image (includes the translated C modules)
    # -w-number-overflow: NASM warns spuriously on `lgdt [gdt_desc - ORG_BASE]`
    # (16-bit BITS 16 displacement) during multipass; the encoding is correct.
    run([NASM, "-f", "bin", os.path.join(BOOT, "bootimg.asm"),
         "-w-number-overflow", "-I", BUILD, "-o", os.path.join(BUILD, "boot_blob.bin")])


def check_sizes():
    s1 = os.path.getsize(os.path.join(BUILD, "stage1.bin"))
    s2 = os.path.getsize(os.path.join(BUILD, "stage2.bin"))
    sb = os.path.getsize(os.path.join(BUILD, "boot_blob.bin"))
    sl = os.path.getsize(os.path.join(BUILD, "libmbl.a"))
    if s1 != SIZE_STAGE1:
        raise SystemExit("stage1 is %d bytes (must be exactly %d)" % (s1, SIZE_STAGE1))
    if s2 > SIZE_STAGE2_MAX:
        raise SystemExit("stage2 is %d bytes (max %d)" % (s2, SIZE_STAGE2_MAX))
    if sb > SIZE_BLOB_MAX:
        raise SystemExit("boot blob is %d bytes (max %d)" % (sb, SIZE_BLOB_MAX))
    print("  stage1 : %6d bytes" % s1)
    print("  stage2 : %6d bytes" % s2)
    print("  blob   : %6d bytes" % sb)
    print("  libmbl : %6d bytes" % sl)


def main():
    build_c_modules()
    build_static_lib()
    assemble()
    check_sizes()
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""build_efi.py - compile UEFI x86_64 PE/COFF EFI application (BOOTX64.EFI).

Uses w64devkit's x86_64-w64-mingw32-gcc (or bare gcc.exe) to compile all
C source files and link them into a UEFI application. The output PE has
subsystem 10 (EFI_APPLICATION).

Pipeline:
  x86_64-w64-mingw32-gcc -c  (freestanding, -mno-red-zone, -mno-sse, ...)
      -> .o object files
  x86_64-w64-mingw32-gcc -shared -Wl,--subsystem,10 -Wl,-e,EfiMain ...
      -> build/BOOTX64.EFI
"""

import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOLS = os.path.join(ROOT, "tools")
BUILD = os.path.join(ROOT, "build")
SRC = os.path.join(ROOT, "src")
INCLUDE = os.path.join(ROOT, "include")

# Use mingw-w64 gcc for PE/COFF output (targets Windows ABI = ms_abi)
GCC = os.environ.get(
    "MBL_GCC",
    r"C:\w64devkit\bin\x86_64-w64-mingw32-gcc.exe"
)

CFLAGS = [
    "-m64", "-c",
    "-ffreestanding", "-nostdlib",
    "-fno-pic", "-fno-stack-protector", "-fno-builtin",
    "-fno-asynchronous-unwind-tables", "-fno-unwind-tables",
    "-fno-ident", "-fno-common",
    "-mno-red-zone", "-mno-sse", "-mno-80387",
    "-mcmodel=large",
    "-O2",
    "-I", INCLUDE,
    "-I", os.path.join(INCLUDE, "sutf"),
]

LINKFLAGS = [
    "-shared", "-Bsymbolic",
    "-Wl,--subsystem,10",           # EFI_APPLICATION
    "-Wl,-e,EfiMain",              # entry point
    "-Wl,--no-se",                  # no PE section entries (smaller header)
    "-nostdlib",
    "-mno-red-zone",
]

# Source files in link order (entry first for clarity)
SRCS = [
    "efi_entry.c",   # must link first so EfiMain is at the PE entry
    "gop.c",
    "kbd.c",
    "bios_disk.c",
    "menu.c",
    "owfs.c",
    "main.c",
    "sutf/sutf8.c",
    "sutf/sucs_mode.c",
]

EFI_OUT = os.path.join(BUILD, "BOOTX64.EFI")


def run(cmd):
    print("+ " + " ".join(cmd))
    subprocess.run(cmd, check=True)


def compile_sources():
    os.makedirs(BUILD, exist_ok=True)
    obj_files = []
    for rel in SRCS:
        src_path = os.path.join(SRC, rel)
        base = rel.replace("/", "_").replace(".c", "")
        obj_path = os.path.join(BUILD, base + ".o")
        run([GCC, *CFLAGS, "-o", obj_path, src_path])
        obj_files.append(obj_path)
    return obj_files


def link(obj_files):
    run([GCC, *LINKFLAGS, "-o", EFI_OUT, *obj_files])
    size = os.path.getsize(EFI_OUT)
    print("  BOOTX64.EFI: %d bytes" % size)


def main():
    objs = compile_sources()
    link(objs)
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""build_image.py - assemble the complete test disk image.

Layout (must match boot/stage1.asm / stage2.asm / bootimg.asm):
    LBA 0      stage1 (512-byte MBR)
    LBA 1..15  stage2
    LBA 16..111 boot image (flat blob at 0x80000, max 48 KiB)
    LBA 112..127 reserved
    LBA 128+   OpenWindows OWFS volume (byte offset 0x10000)

The OWFS volume is formatted by owfs_mkfs.py and seeded with the test
kernel as "kernel.bin".
"""

import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOLS = os.path.join(ROOT, "tools")
BUILD = os.path.join(ROOT, "build")
BOOT = os.path.join(ROOT, "boot")
NASM = os.environ.get("MBL_NASM", r"C:\Program Files\NASM\nasm.exe")

sys.path.insert(0, TOOLS)
from owfs_mkfs import format_owfs, BLOCK_START

OUT_IMAGE = os.path.join(ROOT, "mbl_test.img")
TOTAL_BLOCKS = 2048

BLOB_SECTORS = 96
BLOB_LBA = 16
STAGE2_LBA = 1
STAGE2_SECTORS = 15


def run(cmd):
    print("+ " + " ".join(cmd))
    subprocess.run(cmd, check=True)


def main():
    # 1. compile the bootloader pieces
    run([sys.executable, os.path.join(TOOLS, "build_boot.py")])
    # 2. assemble the test kernel
    run([NASM, "-f", "bin", os.path.join(BOOT, "test_kernel.asm"),
         "-o", os.path.join(BUILD, "test_kernel.bin")])

    with open(os.path.join(BUILD, "stage1.bin"), "rb") as f:
        stage1 = f.read()
    with open(os.path.join(BUILD, "stage2.bin"), "rb") as f:
        stage2 = f.read()
    with open(os.path.join(BUILD, "boot_blob.bin"), "rb") as f:
        blob = f.read()
    with open(os.path.join(BUILD, "test_kernel.bin"), "rb") as f:
        kernel = f.read()

    if len(stage1) != 512:
        raise SystemExit("stage1 must be exactly 512 bytes")
    if len(stage2) > STAGE2_SECTORS * 512:
        raise SystemExit("stage2 too large")
    if len(blob) > BLOB_SECTORS * 512:
        raise SystemExit("boot blob too large")

    image = bytearray(BLOCK_START + TOTAL_BLOCKS * 0x1000)

    image[0:len(stage1)] = stage1
    image[STAGE2_LBA * 512:STAGE2_LBA * 512 + len(stage2)] = stage2
    image[BLOB_LBA * 512:BLOB_LBA * 512 + len(blob)] = blob

    format_owfs(image, TOTAL_BLOCKS, "MBL", [(b"kernel.bin", kernel)])

    with open(OUT_IMAGE, "wb") as f:
        f.write(image)

    print("wrote %s (%d bytes)" % (OUT_IMAGE, len(image)))
    return 0


if __name__ == "__main__":
    sys.exit(main())

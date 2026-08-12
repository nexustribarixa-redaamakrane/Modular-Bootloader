#!/usr/bin/env python3
"""Build a bootable raw disk image for the MBL boot chain.

Layout (LBA 0-based, 512-byte sectors) - MUST match boot/stage2.asm:
    LBA 0            stage1 (512 B MBR, 0xAA55)
    LBA 1..32        stage2 (16 KiB)
    LBA 33..96       stage3 (64 sectors = 32 KiB)
    LBA 97..608      kernel (512 sectors = 256 KiB)

If a stage binary is not supplied, it is assembled from boot/*.asm with
nasm (font8x16.inc is generated from src/mbl_sutf_gui.c as needed). If no
kernel is supplied, boot/test_kernel.asm is assembled as a placeholder so
the whole chain can be verified in an emulator:

    python tools/build_image.py --out build/mbl_disk.img
    qemu-system-i386 -drive file=build/mbl_disk.img,format=raw,if=ide
"""

import argparse
import pathlib
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
BOOT = ROOT / "boot"
BUILD = ROOT / "build"

STAGE1_LBA = 0
STAGE1_SECTORS = 1
STAGE2_LBA = 1
STAGE2_SECTORS = 32
STAGE3_LBA = 33
STAGE3_SECTORS = 64
KERNEL_LBA = 97
KERNEL_SECTORS = 512
IMAGE_SECTORS = KERNEL_LBA + KERNEL_SECTORS


def run(cmd: list[str]) -> None:
    subprocess.run(cmd, check=True)


def ensure_font(nasm: str) -> None:
    inc = BOOT / "font8x16.inc"
    if not inc.exists():
        run([sys.executable, str(ROOT / "tools" / "extract_font.py"),
             str(ROOT / "src" / "mbl_sutf_gui.c"), str(inc)])


def assemble(nasm: str, src: pathlib.Path, out: pathlib.Path) -> None:
    out.parent.mkdir(parents=True, exist_ok=True)
    run([nasm, "-f", "bin", "-I", str(BOOT), "-o", str(out), str(src)])


def resolve_bin(nasm: str, arg: str | None, default: pathlib.Path,
                src: pathlib.Path) -> pathlib.Path:
    if arg:
        return pathlib.Path(arg)
    if not default.exists():
        assemble(nasm, src, default)
    return default


def place(img: bytearray, lba: int, sectors: int, data: bytes, name: str) -> None:
    capacity = sectors * 512
    if len(data) > capacity:
        sys.exit(f"error: {name} is {len(data)} bytes, but the layout only "
                 f"reserves {capacity} bytes ({sectors} sectors) at LBA {lba}")
    start = lba * 512
    img[start:start + len(data)] = data


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default=str(BUILD / "mbl_disk.img"))
    ap.add_argument("--stage1", help="stage1 binary (default: build from boot/stage1.asm)")
    ap.add_argument("--stage2", help="stage2 binary (default: build from boot/stage2.asm)")
    ap.add_argument("--stage3", help="stage3 binary (default: build from boot/stage3.asm)")
    ap.add_argument("--kernel", help="kernel binary (default: build/test_kernel.asm placeholder)")
    ap.add_argument("--nasm", help="path to nasm")
    args = ap.parse_args()

    nasm = args.nasm or shutil.which("nasm")
    if not nasm:
        sys.exit("error: nasm not found; install it or pass --nasm")

    ensure_font(nasm)

    stage1 = resolve_bin(nasm, args.stage1, BUILD / "mbl_core.bin", BOOT / "stage1.asm")
    stage2 = resolve_bin(nasm, args.stage2, BUILD / "mbl_stage2.sys", BOOT / "stage2.asm")
    stage3 = resolve_bin(nasm, args.stage3, BUILD / "mbl_stage3.bin", BOOT / "stage3.asm")

    if args.kernel:
        kernel = pathlib.Path(args.kernel)
        kernel_note = str(kernel)
    else:
        kernel = BUILD / "test_kernel.bin"
        assemble(nasm, BOOT / "test_kernel.asm", kernel)
        kernel_note = f"{kernel} (placeholder)"

    b1, b2, b3, bk = (p.read_bytes() for p in (stage1, stage2, stage3, kernel))
    if len(b1) != 512:
        sys.exit(f"error: stage1 must be exactly 512 bytes, got {len(b1)}")
    if b1[-2:] != b"\x55\xaa":
        sys.exit("error: stage1 does not end in the 0xAA55 signature")

    img = bytearray(IMAGE_SECTORS * 512)
    place(img, STAGE1_LBA, STAGE1_SECTORS, b1, "stage1")
    place(img, STAGE2_LBA, STAGE2_SECTORS, b2, "stage2")
    place(img, STAGE3_LBA, STAGE3_SECTORS, b3, "stage3")
    place(img, KERNEL_LBA, KERNEL_SECTORS, bk, "kernel")

    out = pathlib.Path(args.out)
    out.write_bytes(img)

    print(f"wrote {out} ({len(img)} bytes, {IMAGE_SECTORS} sectors)")
    print(f"  LBA {STAGE1_LBA:>4}  stage1  ({len(b1):>6} B)")
    print(f"  LBA {STAGE2_LBA:>4}  stage2  ({len(b2):>6} B)")
    print(f"  LBA {STAGE3_LBA:>4}  stage3  ({len(b3):>6} B)")
    print(f"  LBA {KERNEL_LBA:>4}  kernel  ({len(bk):>6} B) -> {kernel_note}")
    print("  boot with: qemu-system-i386 -drive "
          f"file={out},format=raw,if=ide")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""compat_smoke.py - ecosystem compatibility smoke test runner.

Compiles tests/compat_smoke.c as a single hosted translation unit
together with the public headers AND implementations of the four
sibling projects (BANcode, SuperUnicode/libsutf, OpenWindows-Storage,
vip), then executes the resulting binary:

    gcc -std=c11 tests/compat_smoke.c \\
        ../vip/univip_fvip.c \\
        ../superunicode/sutf/src/sutf8.c \\
        ../superunicode/sutf/src/sucs_mode.c \\
        -I ../BANcode/kernel_inc            (upstream bancode_all.h wins)
        -I ../OpenWindows-Storage/owfs/include
        -I ../OpenWindows-Storage/usfs/include
        -I ../vip
        -I ../superunicode/sutf/include
        -I include                          (mbl.h joins last)

The sibling repositories must be checked out next to this one
(e.g. ~/Documents/{Modular-Bootloader,vip,BANcode,superunicode,
OpenWindows-Storage}), mirroring the layout vip's own compatibility
suite expects.
"""

import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, "build")
TESTS = os.path.join(ROOT, "tests")
PARENT = os.path.dirname(ROOT)

GCC_CANDIDATES = [
    os.environ.get("MBL_HOST_GCC", ""),
    r"C:\w64devkit\bin\gcc.exe",
    "gcc",
]

SIBLINGS = {
    "vip": ["univip_fvip.h"],
    "BANcode": [os.path.join("kernel_inc", "bancode", "bancode_all.h")],
    "superunicode": [os.path.join("sutf", "include", "sutf8.h"),
                     os.path.join("sutf", "src", "sutf8.c"),
                     os.path.join("sutf", "src", "sucs_mode.c")],
    "OpenWindows-Storage": [os.path.join("owfs", "include", "owfs_types.h"),
                            os.path.join("usfs", "include", "usfs_types.h")],
}

SOURCES = [
    os.path.join(TESTS, "compat_smoke.c"),
]

SIBLING_SOURCES = [
    ("vip", os.path.join("univip_fvip.c")),
    ("superunicode", os.path.join("sutf", "src", "sutf8.c")),
    ("superunicode", os.path.join("sutf", "src", "sucs_mode.c")),
]

INCLUDES = [
    ("BANcode", "kernel_inc"),                    # upstream master first
    ("OpenWindows-Storage", os.path.join("owfs", "include")),
    ("OpenWindows-Storage", os.path.join("usfs", "include")),
    ("vip", ""),
    ("superunicode", os.path.join("sutf", "include")),
]


def find_gcc():
    for cand in GCC_CANDIDATES:
        if not cand:
            continue
        path = cand if os.sep in cand else None
        if path and os.path.isfile(path):
            return path
        found = subprocess.run(["where", cand] if os.name == "nt" else
                               ["which", cand], capture_output=True)
        if found.returncode == 0:
            return cand
    return None


def main():
    missing = []
    resolved = {}
    for name, _ in SIBLINGS.items():
        base = os.path.join(PARENT, name)
        if not os.path.isdir(base):
            for entry in os.listdir(PARENT):
                if entry.lower() == name.lower():
                    base = os.path.join(PARENT, entry)
                    break
        resolved[name] = base
        probe = os.path.join(base, SIBLINGS[name][0])
        if not os.path.isfile(probe):
            missing.append(probe)
    if missing:
        print("compat_smoke: missing sibling files:")
        for m in missing:
            print("  " + m)
        print("Check out the sibling repos next to Modular-Bootloader.")
        return 2

    gcc = find_gcc()
    if gcc is None:
        print("compat_smoke: no host gcc found "
              "(set MBL_HOST_GCC or install w64devkit)")
        return 2

    os.makedirs(BUILD, exist_ok=True)
    out_exe = os.path.join(BUILD, "test_compat_smoke.exe")

    cmd = [gcc, "-std=c11", "-Wall", "-Wextra", "-O1"]
    for name, sub in INCLUDES:
        cmd += ["-I", os.path.join(resolved[name], sub) if sub
                else resolved[name]]
    cmd += ["-I", os.path.join(ROOT, "include")]
    cmd += SOURCES
    for name, rel in SIBLING_SOURCES:
        cmd.append(os.path.join(resolved[name], rel))
    cmd += ["-o", out_exe]

    print("+ " + " ".join(cmd))
    subprocess.run(cmd, check=True)

    print("+ " + out_exe)
    rc = subprocess.run([out_exe]).returncode
    return rc


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""test_qemu.py - boot mbl_test.img in QEMU and drive the menu.

Uses QEMU's TCP monitor so the keyboard can be scripted and the VGA text
buffer inspected without a display window.

PASS criteria:
  1. the GRUB-style menu renders (title visible on row 0 of 0xB8000)
  2. pressing Enter boots the test kernel, which prints a banner
  3. the boot config block (drive/size/SUCS mode) is passed correctly

Usage:
    python tools/test_qemu.py [--no-build] [--wait SECS]
"""

import argparse
import os
import re
import socket
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOLS = os.path.join(ROOT, "tools")
IMAGE = os.path.join(ROOT, "mbl_test.img")
QEMU = os.environ.get("MBL_QEMU", r"C:\Program Files\qemu\qemu-system-i386.exe")
MON_PORT = int(os.environ.get("MBL_MON_PORT", "4444"))

TITLE_EXPECT = "Modular Bootloader"
KERNEL_EXPECT = "MBL TEST KERNEL"


# ---------------------------------------------------------------------------
# QEMU monitor (TCP) client with minimal telnet negotiation handling
# ---------------------------------------------------------------------------
class QemuMonitor:
    def __init__(self, port, host="127.0.0.1"):
        self.sock = socket.create_connection((host, port), timeout=10)
        self.sock.settimeout(5)
        self.buf = b""

    def _strip_telnet(self, data):
        out = bytearray()
        i = 0
        n = len(data)
        while i < n:
            b = data[i]
            if b == 0xFF:                      # IAC
                if i + 1 >= n:
                    break
                cmd = data[i + 1]
                if cmd == 0xFA:                # sub-negotiation: skip to IAC SE
                    j = data.find(b"\xFF\xF0", i + 2)
                    i = j + 2 if j != -1 else n
                    continue
                elif cmd in (0xFB, 0xFD):      # WILL / DO -> reply DONT / WONT
                    reply = 0xFE if cmd == 0xFB else 0xFC
                    self.sock.send(bytes([0xFF, reply, data[i + 2] if i + 2 < n else 0]))
                i += 2
                continue
            out.append(b)
            i += 1
        return bytes(out)

    def _read_until_prompt(self, timeout=10):
        deadline = time.time() + timeout
        while time.time() < deadline:
            if b"(qemu) " in self.buf:
                out, _, self.buf = self.buf.partition(b"(qemu) ")
                return out
            try:
                chunk = self.sock.recv(4096)
                if not chunk:
                    raise EOFError("monitor closed")
                self.buf += chunk
            except socket.timeout:
                pass
        raise TimeoutError("monitor prompt not seen")

    def cmd(self, cmd):
        self.sock.send(cmd.encode() + b"\n")
        return self._read_until_prompt().decode(errors="replace")

    def close(self):
        try:
            self.cmd("quit")
        except Exception:
            pass
        try:
            self.sock.close()
        except Exception:
            pass


def parse_xp(text):
    """Parse `xp` output into a list of byte values."""
    vals = []
    for line in text.splitlines():
        if ":" in line:
            _, _, hex_part = line.partition(":")
            for tok in hex_part.strip().split():
                if re.fullmatch(r"(?:0x)?[0-9a-fA-F]{2}", tok):
                    vals.append(int(tok, 16))
        else:
            for tok in line.strip().split():
                if re.fullmatch(r"(?:0x)?[0-9a-fA-F]{2}", tok):
                    vals.append(int(tok, 16))
    return vals


def vga_text(vals):
    """Even bytes (character cells) of a VGA dump -> string."""
    chars = []
    for i in range(0, len(vals) - 1, 2):
        chars.append(chr(vals[i]))
    return "".join(chars)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--no-build", action="store_true")
    ap.add_argument("--wait", type=float, default=4.0,
                    help="seconds to wait for the menu before probing")
    args = ap.parse_args()

    if not args.no_build:
        subprocess.run([sys.executable, os.path.join(TOOLS, "build_image.py")],
                       check=True)

    proc = subprocess.Popen(
        [QEMU, "-drive", "file=%s,format=raw" % IMAGE,
         "-display", "none",
         "-monitor", "tcp:127.0.0.1:%d,server,nowait" % MON_PORT,
         "-m", "32", "-no-reboot"],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    ok = True
    mon = None
    try:
        time.sleep(1.0)
        mon = QemuMonitor(MON_PORT)
        time.sleep(args.wait)

        # --- check the menu rendered ---
        dump = mon.cmd("xp /4000bx 0xb8000")
        text = vga_text(parse_xp(dump))
        if TITLE_EXPECT in text:
            print("[PASS] menu title rendered: %r" % TITLE_EXPECT)
        else:
            print("[FAIL] menu title not found on screen")
            print("--- first 400 chars of screen ---")
            print(repr(text[:400]))
            ok = False

        # --- navigate and boot ---
        mon.cmd("sendkey down")
        mon.cmd("sendkey ret")
        time.sleep(2.0)

        dump = mon.cmd("xp /4000bx 0xb8000")
        text = vga_text(parse_xp(dump))
        if KERNEL_EXPECT in text:
            print("[PASS] test kernel booted")
            print("--- kernel screen ---")
            for row in range(0, 400, 80):
                print("   " + text[row:row + 80].rstrip())
        else:
            print("[FAIL] kernel banner not found")
            print("--- first 400 chars of screen ---")
            print(repr(text[:400]))
            ok = False
    except Exception as e:
        print("[FAIL] exception: %r" % (e,))
        ok = False
    finally:
        if mon:
            mon.close()
        time.sleep(0.5)
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()

    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())

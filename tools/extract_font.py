#!/usr/bin/env python3
"""Extract the embedded 8x16 ASCII font table from src/mbl_sutf_gui.c
and emit it as NASM include data (label `font8x16`, 95 x 16 bytes).

Usage:
    python3 tools/extract_font.py src/mbl_sutf_gui.c <out.inc>
"""

import re
import sys


def main() -> None:
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(2)

    src_path, out_path = sys.argv[1], sys.argv[2]
    with open(src_path, "r", encoding="utf-8") as fh:
        text = fh.read()

    marker = "g_font8x16"
    idx = text.find(marker)
    if idx < 0:
        print(f"error: '{marker}' not found in {src_path}", file=sys.stderr)
        sys.exit(1)

    rows = []
    for line in text[idx:].splitlines():
        m = re.match(r"\s*\{([^}]*)\}\s*,?\s*(?:/\*.*?\*/)?\s*$", line)
        if m:
            values = re.findall(r"\b\d+\b", m.group(1))
            if len(values) == 16:
                rows.append(values)
            else:
                break
        elif line.strip().startswith("};"):
            break
        elif len(rows) > 0 and not line.strip().startswith("static"):
            # stop scanning once the table body has ended
            if not line.strip().startswith("{"):
                break

    if len(rows) != 95:
        print(f"error: expected 95 glyph rows, parsed {len(rows)}", file=sys.stderr)
        sys.exit(1)

    with open(out_path, "w", encoding="utf-8") as fh:
        fh.write("; Auto-generated from %s - do not edit.\n" % src_path)
        fh.write("font8x16:\n")
        for r in rows:
            fh.write("    db " + ",".join("0x%02X" % int(v) for v in r) + "\n")

    print(f"wrote {out_path}: 95 glyphs x 16 bytes")
    sys.exit(0)


if __name__ == "__main__":
    main()

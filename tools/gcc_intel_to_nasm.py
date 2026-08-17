#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""gcc_intel_to_nasm.py - translate `gcc -m32 -S -masm=intel` output to NASM.

The bootloader is built as one flat NASM binary linked at ORG 0x80000.
The C modules are translated here and then %include'd into boot/bootimg.asm.

Semantics handled (verified against NASM 2.x flat binary output):
  * GCC/PE symbols use a leading underscore: `_kmain` -> `kmain`.
  * File-local labels and static symbols (L4, LC0, _s_counter, _msg.0)
    are prefixed with a per-file token so they do not collide across the
    modules that share one assembly.
  * Memory operands that GCC writes WITHOUT brackets (`mov eax, _g_flag`,
    `cmp DWORD PTR [..]`) are wrapped in brackets (NASM treats a bare
    symbol as an immediate).
  * `OFFSET FLAT:_sym` immediates become plain `sym`.
  * BSS is collected and flushed at the end (NASM -f bin does not emit
    .bss, so every BSS byte is RAM, not file space).
"""

import re
import sys
import os

# ---------------------------------------------------------------------------
# per-file identity used to namespace local symbols
# ---------------------------------------------------------------------------
_file_id = 0

def _next_file_id():
    global _file_id
    _file_id += 1
    return _file_id

# ---------------------------------------------------------------------------
# instruction / register / keyword sets used to classify bare operands
# ---------------------------------------------------------------------------
REGISTERS = set("""
    eax ebx ecx edx esi edi ebp esp eip ax bx cx dx si di bp sp
    al bl cl dl ah bh ch dh spl bpl sil dil eax ebx ecx edx
    xmm0 xmm1 xmm2 xmm3 xmm4 xmm5 xmm6 xmm7 st0 st1 st2 st3 st4 st5 st6 st7
    cs ds es fs gs ss cr0 cr1 cr2 cr3 cr4 dr0 dr1 dr2 dr3 dr6 dr7
""".split())

CONTROL_FLOW = set("""
    call jmp ja jae jb jbe jc je jg jge jl jle jna jnae jnb jnbe jnc jne
    jng jnge jnl jnle jno jnp jns jnz jo jp jpe jpo js jz jcxz jecxz
    loop loope loopne ret iret
""".split())

INSTRUCTIONS = set("""
    mov movzx movsx lea push pop add sub adc sbb and or xor not neg inc dec
    cmp test imul mul idiv div cdq cqo cld std cli sti hlt nop leave pause
    xchg bswap shl shr sar rol ror rcl rcr seta setae setb setbe setc sete
    setg setge setl setle setna setnae setnb setnbe setnc setne setng setnge
    setnl setnle setno setnp setns setnz seto setp setpe setpo sets setz
    int in out rep repe repz repne repnz scasb stosb movsb lodsb cmpsb
    pushad popad pushf popf cpuid rdtsc rdtscp rdmsr wrmsr ud2
""".split())

SIZE_WORDS = set("byte word dword qword xmmword oword tword fword".split())

KEYWORDS = REGISTERS | INSTRUCTIONS | SIZE_WORDS | set("""
    section global extern bits align times resb resw resd resq db dw dd dq
    do dt org equ absolute
""".split())

# ---------------------------------------------------------------------------
# C string (gcc .ascii) decoding
# ---------------------------------------------------------------------------
_ESCAPES = {
    'a': 0x07, 'b': 0x08, 'f': 0x0C, 'n': 0x0A, 'r': 0x0D,
    't': 0x09, 'v': 0x0B, '0': 0x00, 'e': 0x1B, '\\': 0x5C,
    '"': 0x22, "'": 0x27,
}

def decode_c_string(txt):
    """Decode a C-style string literal (GCC emits octal escapes)."""
    out = bytearray()
    i = 0
    n = len(txt)
    while i < n:
        c = txt[i]
        if c == '\\':
            i += 1
            if i >= n:
                break
            e = txt[i]
            if e in _ESCAPES:
                out.append(_ESCAPES[e])
                i += 1
            elif e == 'x':
                m = re.match(r'[0-9a-fA-F]{1,2}', txt[i + 1:])
                if not m:
                    out.append(0)
                    i += 1
                else:
                    out.append(int(m.group(0), 16))
                    i += 1 + len(m.group(0))
            elif e in '01234567':
                m = re.match(r'[0-7]{1,3}', txt[i:])
                out.append(int(m.group(0), 8))
                i += len(m.group(0))
            else:
                out.append(ord(e))
                i += 1
        else:
            out.append(ord(c))
            i += 1
    return bytes(out)

def nasm_escape_bytes(data):
    """Emit bytes as a NASM backquoted string (printable raw, else \\ddd)."""
    parts = []
    for b in data:
        if 0x20 <= b <= 0x7E and b not in (0x60, 0x5C, 0x27):
            parts.append(chr(b))
        else:
            parts.append('\\%03d' % b)
    return '`%s`' % ''.join(parts)

# ---------------------------------------------------------------------------
# symbol rewriting
# ---------------------------------------------------------------------------
class Rewriter:
    def __init__(self, file_token, globals_, locals_):
        self.token = file_token
        self.globals = globals_
        self.locals = locals_

    def rewrite(self, ident):
        bare = ident[1:] if (ident[:1] == '_' and len(ident) > 1) else ident
        if bare in self.globals:
            return bare
        if bare in self.locals:
            return self.token + bare
        return bare

# ---------------------------------------------------------------------------
# operand handling
# ---------------------------------------------------------------------------
_NUM_RE = re.compile(r'^[+-]?(0[xX][0-9a-fA-F]+|\d+)$')
_IDENT_RE = re.compile(r'[A-Za-z_.$][A-Za-z0-9_.$]*')
_SEG_PREFIX_RE = re.compile(r'^\s*(?:cs|ds|es|fs|gs|ss)\s*:\s*', re.I)

def _is_number(tok):
    return bool(_NUM_RE.match(tok.strip()))

def _strip_segment(expr):
    """Drop gcc's explicit segment prefix (flat model: all bases are 0)."""
    return _SEG_PREFIX_RE.sub('', expr)

def _rename_in_expr(expr, rw):
    """Rename symbol tokens inside a bracketed or plain expression.

    Registers and NASM keywords are left alone (gcc often mixes them into
    memory expressions, e.g. `_g_entries[eax]` or `[esp+52+ecx]`).
    """
    def repl(m):
        tok = m.group(0)
        if tok in REGISTERS or tok in KEYWORDS:
            return tok
        return rw.rewrite(tok)
    return _IDENT_RE.sub(repl, expr)

_INDEX_RE = re.compile(r'^([A-Za-z_.$][A-Za-z0-9_.$]*)\s*\[(.*)\]\s*$')

def _normalize_index(expr):
    """Turn gcc's `sym[reg]` / `sym[reg*4+disp]` indexing into NASM's
    `sym+reg*4+disp` form (NASM rejects `[sym[reg]]`)."""
    m = _INDEX_RE.match(expr)
    if m:
        return m.group(1) + '+' + m.group(2)
    return expr

def _wrap_if_unbracketed(expr):
    e = _normalize_index(expr.strip())
    if e.startswith('['):
        return e
    return '[' + e + ']'

def _transform_operand(op, mnem, rw):
    op = op.strip()

    # OFFSET FLAT:... -> immediate symbol
    m = re.match(r'OFFSET\s+(FLAT:\s*)?(.*)$', op, re.I)
    if m:
        return _rename_in_expr(m.group(2), rw)

    # [size PTR expr] -> size [expr]  (gcc jump tables: `jmp [DWORD PTR _tab[0+ebx*4]]`)
    m = re.match(r'(?i)^\[\s*(byte|word|dword|qword|xmmword|oword|tword|fword)\s+PTR\s+(.+)\]\s*$', op)
    if m:
        expr = _strip_segment(m.group(2))
        expr = _rename_in_expr(expr, rw)
        return '%s %s' % (m.group(1).lower(), _wrap_if_unbracketed(expr))

    # size PTR expr -> size [expr]
    m = re.match(r'(?i)^(byte|word|dword|qword|xmmword|oword|tword|fword)\s+PTR\s+(.+)$', op)
    if m:
        expr = _strip_segment(m.group(2))
        expr = _rename_in_expr(expr, rw)
        return '%s %s' % (m.group(1).lower(), _wrap_if_unbracketed(expr))

    if op.startswith('['):
        return _rename_in_expr(_strip_segment(op), rw)

    if _is_number(op):
        return op

    if op in REGISTERS:
        return op

    # a bare identifier (possibly with +/- terms, e.g. `_var+4`)
    # leave control-flow targets alone
    if mnem in CONTROL_FLOW:
        return _rename_in_expr(op, rw)

    # lea / data address: wrap in brackets
    return _wrap_if_unbracketed(_rename_in_expr(op, rw))

def _split_operands(s):
    """Split an operand list on top-level commas."""
    ops = []
    depth = 0
    cur = []
    for ch in s:
        if ch == '[':
            depth += 1
        elif ch == ']':
            depth -= 1
        elif ch == ',' and depth == 0:
            ops.append(''.join(cur))
            cur = []
            continue
        cur.append(ch)
    if cur:
        ops.append(''.join(cur))
    return ops

def _transform_instruction(line, rw):
    """Rewrite a single instruction line (Intel syntax, no size/PTR)."""
    line = line.strip()
    if not line:
        return ''
    m = re.match(r'^([A-Za-z][A-Za-z0-9]*)\b\s*(.*)$', line)
    if not m:
        return line
    mnem = m.group(1).lower()
    rest = m.group(2).strip()

    # some pseudo ops look like instructions
    if mnem not in INSTRUCTIONS and mnem not in CONTROL_FLOW and mnem not in SIZE_WORDS:
        # e.g. `rep movsd` is handled by the simple pass-through below
        pass

    if not rest:
        return mnem

    # handle rep/repe/repne prefixes (mnem becomes the first token again)
    if mnem in ('rep', 'repe', 'repne'):
        inner = _transform_instruction(rest, rw)
        return mnem + ' ' + inner

    ops = _split_operands(rest)
    new_ops = [_transform_operand(o, mnem, rw) for o in ops]

    # gcc omits the shift count when it is 1 (`shr eax`); NASM requires it.
    if (mnem in ('sal', 'shl', 'sar', 'shr', 'rol', 'ror', 'rcl', 'rcr')
            and len(new_ops) == 1):
        new_ops.append('1')

    return mnem + ' ' + ', '.join(new_ops)

# ---------------------------------------------------------------------------
# directives
# ---------------------------------------------------------------------------
_DIRECTIVE_STRIP = re.compile(r'^\.(file|ident|intel_syntax|def|scl|type|size|'
                              r'endef|cfi[a-z0-9_]*|seh[a-z0-9_]*|hidden|'
                              r'weak|set)\b', re.I)

def _bare(name):
    return name[1:] if (name[:1] == '_' and len(name) > 1) else name

def _scan_symbols(text):
    """Collect globals (exported) and locals (defined in this file).

    Two passes are needed because gcc emits `.globl` before the defining
    label; symbols referenced but defined in another module are neither.
    """
    globals_ = set()
    defined = set()
    for line in text.splitlines():
        line = line.strip()
        m = re.match(r'^\.globl\s+(.+)$', line, re.I)
        if m:
            for name in m.group(1).split(','):
                globals_.add(_bare(name.strip()))
            continue
        m = re.match(r'^\.lcomm\s+([A-Za-z_.$][\w.$]*)', line, re.I)
        if m:
            defined.add(_bare(m.group(1)))
            continue
        m = re.match(r'^([A-Za-z_.$][\w.$]*):$', line)
        if m:
            defined.add(_bare(m.group(1)))
    return globals_, defined

def translate(text, file_token):
    globals_, defined = _scan_symbols(text)
    rw = Rewriter(file_token, globals_, defined)
    out = []
    bss = []
    in_bss = False
    seen_bss_header = False

    def flush_bss():
        nonlocal seen_bss_header
        if not bss:
            return
        if not seen_bss_header:
            out.append('')
            out.append('section .bss')
            out.append('alignb 4')
            seen_bss_header = True
        out.extend(bss)
        bss.clear()

    for raw in text.splitlines():
        line = raw.strip()
        if not line:
            continue

        # ---- global declarations ----
        m = re.match(r'^\.globl\s+(.+)$', line, re.I)
        if m:
            for name in m.group(1).split(','):
                name = name.strip()
                bare = _bare(name)
                rw.globals.add(bare)
                out.append('global ' + bare)
            continue

        m = re.match(r'^\.comm\s+([A-Za-z_.$][\w.$]*)\s*,\s*(\d+)(?:\s*,\s*\d+)?$', line)
        if m:
            name = m.group(1)
            size = int(m.group(2))
            bare = _bare(name)
            rw.globals.add(bare)
            out.append('global ' + bare)
            bss.append('alignb 4')
            bss.append(bare + ':')
            bss.append('resb %d' % size)
            continue

        m = re.match(r'^\.lcomm\s+([A-Za-z_.$][\w.$]*)\s*,\s*(\d+)(?:\s*,\s*(\d+))?$', line, re.I)
        if m:
            name = rw.rewrite(m.group(1))
            size = int(m.group(2))
            align = int(m.group(3)) if m.group(3) else 4
            if align > 1:
                bss.append('alignb %d' % align)
            bss.append(name + ':')
            bss.append('resb %d' % size)
            continue

        if _DIRECTIVE_STRIP.match(line):
            continue

        # ---- sections ----
        m = re.match(r'^\.text\b', line)
        if m:
            flush_bss()
            in_bss = False
            out.append('section .text')
            continue
        m = re.match(r'^\.data\b', line)
        if m:
            flush_bss()
            in_bss = False
            out.append('section .data')
            continue
        m = re.match(r'^\.bss\b', line)
        if m:
            in_bss = True
            continue
        m = re.match(r'^\.section\s+\.rdata', line, re.I)
        if m:
            flush_bss()
            in_bss = False
            out.append('section .rodata')
            continue
        m = re.match(r'^\.section\s+\.data', line, re.I)
        if m:
            flush_bss()
            in_bss = False
            out.append('section .data')
            continue
        m = re.match(r'^\.section\s+\.bss', line, re.I)
        if m:
            in_bss = True
            continue
        m = re.match(r'^\.section\s+', line, re.I)
        if m:
            flush_bss()
            in_bss = False
            out.append('section .text')
            continue

        # ---- alignment / zero-fill ----
        m = re.match(r'^\.p2align\s+(\d+)', line, re.I)
        if m:
            a = 1 << int(m.group(1))
            (bss if in_bss else out).append('align %d' % a)
            continue
        m = re.match(r'^\.align\s+(\d+)', line, re.I)
        if m:
            (bss if in_bss else out).append('align %d' % int(m.group(1)))
            continue
        m = re.match(r'^\.(space|zero)\s+(\d+)', line, re.I)
        if m:
            n = int(m.group(2))
            if in_bss:
                bss.append('resb %d' % n)
            else:
                out.append('times %d db 0' % n)
            continue

        # ---- raw data ----
        m = re.match(r'^\.(byte|short|word|long|quad)\s+(.+)$', line, re.I)
        if m:
            kind = m.group(1).lower()
            dw = {'byte': 'db', 'short': 'dw', 'word': 'dw',
                  'long': 'dd', 'quad': 'dq'}[kind]
            operands = [o.strip() for o in m.group(2).split(',')]
            operands = [_rename_in_expr(o, rw) for o in operands]
            (bss if in_bss else out).append('%s %s' % (dw, ', '.join(operands)))
            continue

        m = re.match(r'^\.(ascii|asciz|string)\s+(.+)$', line, re.I)
        if m:
            kind = m.group(1).lower()
            data = decode_c_string(m.group(2).strip())
            if kind != 'ascii':
                data += b'\x00'
            (bss if in_bss else out).append('db ' + nasm_escape_bytes(data))
            continue

        # ---- label definitions ----
        m = re.match(r'^([A-Za-z_.$][\w.$]*):$', line)
        if m:
            label = rw.rewrite(m.group(1))
            (bss if in_bss else out).append(label + ':')
            continue

        # ---- instructions ----
        target = bss if in_bss else out
        target.append(_transform_instruction(line, rw))

    flush_bss()
    return '\n'.join(out) + '\n'

def main(argv):
    if len(argv) != 3:
        print('usage: gcc_intel_to_nasm.py <input.s> <output.asm>')
        return 1
    src, dst = argv[1], argv[2]
    with open(src, 'r', encoding='utf-8', errors='replace') as f:
        text = f.read()
    base = os.path.splitext(os.path.basename(src))[0]
    token = '_f%s_' % _next_file_id()
    # keep the token stable and readable: use file name
    token = '_f' + re.sub(r'[^A-Za-z0-9]', '_', base) + '_'
    result = translate(text, token)
    with open(dst, 'w', encoding='utf-8', newline='\n') as f:
        f.write(result)
    return 0

if __name__ == '__main__':
    sys.exit(main(sys.argv))

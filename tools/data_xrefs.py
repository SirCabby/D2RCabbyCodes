#!/usr/bin/env python3
"""Code references to data addresses in the dumped image: every RIP-relative
operand (lea/mov/cmp/... of the forms [REX] op modrm disp32 [imm]) in .text that
lands on one of the given RVAs. Heuristic (no full decode), so confirm with
disasm.py; it lists the referencing instruction's RVA and the enclosing function.

    tools/data_xrefs.py D2R-93847.dumped.exe 0x1D1A930 0x1D3D5E0 ...
"""
import struct
import sys

sys.path.insert(0, __import__('os').path.dirname(__file__))
from pdata import load, containing  # noqa: E402  (same folder)


def text_range(data):
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    nsec = struct.unpack_from("<H", data, pe + 6)[0]
    opt = struct.unpack_from("<H", data, pe + 20)[0]
    sec = pe + 24 + opt
    for i in range(nsec):
        name = data[sec + i * 40:sec + i * 40 + 8].rstrip(b"\0")
        vsize, va = struct.unpack_from("<II", data, sec + i * 40 + 8)
        if name == b".text":
            return va, va + vsize
    raise SystemExit("no .text")


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return
    data = open(sys.argv[1], "rb").read()
    targets = {int(a, 0) for a in sys.argv[2:]}
    lo, hi = text_range(data)
    entries = load(data)
    hits = []
    # modrm with mod=00, rm=101 => RIP-relative. Try the instruction shapes:
    #   [REX] op modrm disp32 [imm0/1/4]  and  [REX] 0F op modrm disp32 [imm0/1]
    for p in range(lo, hi - 6):
        m = data[p]
        if (m & 0xC7) != 0x05:
            continue
        disp = struct.unpack_from("<i", data, p + 1)[0]
        for pre in (1, 2, 3):  # bytes before modrm: op | REX op | REX 0F op / 0F op ...
            start = p - pre
            if start < lo:
                continue
            for imm in (0, 1, 4):
                end = p + 5 + imm
                if start + 0 <= 0 or end > hi:
                    continue
                if end + disp in targets:
                    hits.append((start, end + disp, pre, imm))
    seen = set()
    for start, tgt, pre, imm in sorted(hits):
        key = (start, tgt)
        if key in seen:
            continue
        seen.add(key)
        f = containing(entries, start, data)
        fs = f"in 0x{f[0]:x}..0x{f[1]:x}" if f else "(no function)"
        print(f"0x{start:06x} -> 0x{tgt:x}  {fs}  (prefix {pre}, imm {imm})")
    print(f"{len(seen)} reference(s)")


if __name__ == "__main__":
    main()

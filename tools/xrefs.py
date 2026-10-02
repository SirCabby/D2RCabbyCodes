#!/usr/bin/env python3
"""Callers of an RVA: every direct call or jump (E8/E9 rel32) in .text that lands
there, with the calling function's bounds.

    tools/xrefs.py D2R-93847.dumped.exe 0x550C20 [more RVAs...]
"""
import struct
import sys

import pdata


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    data = open(sys.argv[1], "rb").read()
    funcs = pdata.load(data)
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    nsec = struct.unpack_from("<H", data, pe + 6)[0]
    opt_size = struct.unpack_from("<H", data, pe + 20)[0]
    sec0 = pe + 24 + opt_size
    va = vsz = 0
    for i in range(nsec):
        o = sec0 + i * 40
        if data[o:o + 8].rstrip(b"\0") == b".text":
            vsz, va = struct.unpack_from("<II", data, o + 8)
    targets = {int(a, 0) for a in sys.argv[2:]}
    text = data[va:va + vsz]
    hits = {t: [] for t in targets}
    for i in range(len(text) - 5):
        op = text[i]
        if op != 0xE8 and op != 0xE9:
            continue
        rel = struct.unpack_from("<i", text, i + 1)[0]
        site = va + i
        target = site + 5 + rel
        if target in hits:
            hits[target].append((site, op))
    for t in targets:
        print(f"== {t:#x}: {len(hits[t])} reference(s)")
        for site, op in hits[t]:
            f = pdata.containing(funcs, site, data)
            print(f"  {'call' if op == 0xE8 else 'jmp '} at {site:#x}  in {hex(f[0]) + '..' + hex(f[1]) if f else '?'}")


if __name__ == "__main__":
    main()

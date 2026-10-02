#!/usr/bin/env python3
"""List every direct call to the stat helpers in the image, with the stat id the
caller loads into edx when it is a constant, and the calling function's bounds.

    tools/find_stat_sites.py D2R-93847.dumped.exe [--stat 13] [--target 0x2F7D10,0x2F34F0]

Defaults: SetUnitStat (0x2F7D10) and AddUnitStat (0x2F34F0), every stat.
"""
import argparse
import struct

from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_OP_REG, X86_REG_EDX, X86_REG_RDX

import pdata

BASE = 0x140000000


def text_section(data):
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    nsec = struct.unpack_from("<H", data, pe + 6)[0]
    opt_size = struct.unpack_from("<H", data, pe + 20)[0]
    sec0 = pe + 24 + opt_size
    for i in range(nsec):
        o = sec0 + i * 40
        if data[o:o + 8].rstrip(b"\0") == b".text":
            vsz, va = struct.unpack_from("<II", data, o + 8)
            return va, vsz
    raise SystemExit("no .text")


def stat_before(md, data, call_rva, start):
    """The constant edx gets before the call, scanning back from a safe start."""
    stat = None
    for insn in md.disasm(data[start:call_rva], BASE + start):
        if insn.address - BASE >= call_rva:
            break
        ops = insn.operands
        if not ops or ops[0].type != X86_OP_REG or ops[0].reg not in (X86_REG_EDX, X86_REG_RDX):
            continue
        if insn.mnemonic == "mov" and len(ops) == 2 and ops[1].type == X86_OP_IMM:
            stat = ops[1].imm
        elif insn.mnemonic == "lea" and len(ops) == 2 and ops[1].type == X86_OP_MEM:
            stat = ("+", ops[1].mem.disp)  # lea edx, [reg + disp]: disp when reg is zero
        elif insn.mnemonic == "xor" and len(ops) == 2 and ops[1].type == X86_OP_REG and ops[1].reg == ops[0].reg:
            stat = 0
        elif insn.mnemonic in ("mov", "movzx", "movsxd"):
            stat = "?"
    return stat


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dump")
    ap.add_argument("--stat", type=lambda s: int(s, 0))
    ap.add_argument("--target", default="0x2F7D10,0x2F34F0")
    a = ap.parse_args()
    data = open(a.dump, "rb").read()
    targets = {int(t, 0) for t in a.target.split(",")}
    funcs = pdata.load(data)
    va, vsz = text_section(data)
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = True
    text = data[va:va + vsz]
    i = 0
    n = 0
    while True:
        i = text.find(b"\xe8", i)
        if i < 0 or i + 5 > len(text):
            break
        rel = struct.unpack_from("<i", text, i + 1)[0]
        call_rva = va + i
        target = call_rva + 5 + rel
        if target in targets:
            f = pdata.containing(funcs, call_rva, data)
            start = max(f[0], call_rva - 48) if f else call_rva - 48
            stat = stat_before(md, data, call_rva, start)
            if isinstance(stat, tuple):
                stat_txt = f"reg+{stat[1]}"
                match = a.stat is None or stat[1] == a.stat
            else:
                stat_txt = "?" if stat is None or stat == "?" else str(stat)
                match = a.stat is None or stat == a.stat
            if match:
                n += 1
                print(f"call {call_rva:#x} -> {target:#x}  stat {stat_txt:<8} in {hex(f[0]) + '..' + hex(f[1]) if f else '?'}")
        i += 1
    print(f"{n} call sites")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Print a byte signature for the routine at an RVA of the dumped image, with the
operands that move between builds wildcarded: RIP-relative displacements and
relative call/jump targets. Other immediates (stat ids, sizes) stay.

    tools/make_sig.py D2R-93847.dumped.exe 0x42E600 [bytes=32]

Then check it with tools/check_sigs.py, or just watch the plugin's log: a
usable signature matches exactly once.
"""
import sys
from capstone import Cs, CS_ARCH_X86, CS_MODE_64, CS_GRP_JUMP, CS_GRP_CALL
from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_REG_RIP

BASE = 0x140000000


def signature(data, rva, n):
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = True
    out = []
    for insn in md.disasm(data[rva:rva + n + 16], BASE + rva):
        if len(out) >= n:
            break
        raw = list(insn.bytes)
        wild = [False] * len(raw)
        x86 = insn._detail.arch.x86 if hasattr(insn, "_detail") else None
        branch = (insn.group(CS_GRP_JUMP) or insn.group(CS_GRP_CALL)) and insn.operands and insn.operands[0].type == X86_OP_IMM
        rip = any(op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP for op in insn.operands)
        if x86 is not None:
            if branch and x86.encoding.imm_offset:
                for i in range(x86.encoding.imm_offset, x86.encoding.imm_offset + x86.encoding.imm_size):
                    wild[i] = True
            if rip and x86.encoding.disp_offset:
                for i in range(x86.encoding.disp_offset, x86.encoding.disp_offset + x86.encoding.disp_size):
                    wild[i] = True
        for b, w in zip(raw, wild):
            out.append("??" if w else f"{b:02X}")
    return " ".join(out[:n])


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    data = open(sys.argv[1], "rb").read()
    rva = int(sys.argv[2], 0)
    n = int(sys.argv[3], 0) if len(sys.argv) > 3 else 32
    print(signature(data, rva, n))


if __name__ == "__main__":
    main()

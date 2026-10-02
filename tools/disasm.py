#!/usr/bin/env python3
"""Disassemble a range of the image the plugin dumps (memory layout: file offset == RVA).

    tools/disasm.py D2R-93847.dumped.exe 0x42E600 [bytes=96]

Needs capstone (pip install capstone). Addresses print as RVAs; call/jump
targets are shown as RVAs too, so they can be looked up in the research notes.
"""
import sys
from capstone import Cs, CS_ARCH_X86, CS_MODE_64, CS_GRP_JUMP, CS_GRP_CALL
from capstone.x86 import X86_OP_IMM

BASE = 0x140000000


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    data = open(sys.argv[1], "rb").read()
    rva = int(sys.argv[2], 0)
    n = int(sys.argv[3], 0) if len(sys.argv) > 3 else 96
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = True
    for insn in md.disasm(data[rva:rva + n], BASE + rva):
        note = ""
        if (insn.group(CS_GRP_JUMP) or insn.group(CS_GRP_CALL)) and insn.operands and insn.operands[0].type == X86_OP_IMM:
            note = f"   ; -> rva 0x{insn.operands[0].imm - BASE:X}"
        print(f"{insn.address - BASE:08X}  {insn.bytes.hex(' '):<30} {insn.mnemonic} {insn.op_str}{note}")


if __name__ == "__main__":
    main()

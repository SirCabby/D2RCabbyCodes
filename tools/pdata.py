#!/usr/bin/env python3
"""Function bounds from the image's .pdata (the x64 exception directory), so an
address inside a routine can be mapped to the routine's entry without Ghidra.

    tools/pdata.py D2R-93847.dumped.exe 0x44D083 [more RVAs...]

Prints "rva -> function start..end". Also usable as a module: load(), containing().
"""
import bisect
import struct
import sys


def load(data):
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    nsec = struct.unpack_from("<H", data, pe + 6)[0]
    opt_size = struct.unpack_from("<H", data, pe + 20)[0]
    sec0 = pe + 24 + opt_size
    for i in range(nsec):
        o = sec0 + i * 40
        name = data[o:o + 8].rstrip(b"\0")
        vsz, va = struct.unpack_from("<II", data, o + 8)
        if name == b".pdata":
            entries = []
            for off in range(va, va + vsz, 12):
                begin, end, unwind = struct.unpack_from("<III", data, off)
                if begin == 0 and end == 0:
                    break
                entries.append((begin, end, unwind))
            entries.sort()
            return entries
    raise SystemExit("no .pdata section")


def primary(data, entries, entry):
    """Follow chained unwind info (a chunk of a larger function) to the function's own range."""
    begin, end, unwind = entry
    for _ in range(8):
        flags = data[unwind] >> 3
        if not flags & 4:  # UNW_FLAG_CHAININFO
            return begin, end
        count = data[unwind + 2]
        chained = unwind + 4 + ((count + 1) & ~1) * 2
        begin, end, unwind = struct.unpack_from("<III", data, chained)
    return begin, end


def containing(entries, rva, data=None):
    starts = [e[0] for e in entries]
    i = bisect.bisect_right(starts, rva) - 1
    if i >= 0 and entries[i][0] <= rva < entries[i][1]:
        e = entries[i]
        return primary(data, entries, e) if data is not None else (e[0], e[1])
    return None


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    data = open(sys.argv[1], "rb").read()
    entries = load(data)
    for arg in sys.argv[2:]:
        rva = int(arg, 0)
        chunk = containing(entries, rva)
        f = containing(entries, rva, data)
        chained = "" if not chunk or chunk == f else f"  (chunk {chunk[0]:#x}..{chunk[1]:#x})"
        print(f"{rva:#x} -> {'function ' + hex(f[0]) + '..' + hex(f[1]) if f else 'no function'}{chained}")


if __name__ == "__main__":
    main()

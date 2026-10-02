#!/usr/bin/env python3
"""Make the image the plugin dumps loadable by Ghidra (or any PE tool).

    tools/fix_dump.py D2R-93847.dumped.exe [D2R-93847.fixed.exe]

The plugin writes the decrypted image exactly as it sits in memory (file
offset == RVA), headers included, so every section's raw pointer and size are
still the on-disk ones. This rewrites them to the memory layout. Pure stdlib.
"""
import struct
import sys


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    src = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else src.replace(".dumped.exe", ".fixed.exe")
    d = bytearray(open(src, "rb").read())
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    if d[pe:pe + 4] != b"PE\0\0":
        raise SystemExit("not a PE image")
    nsec = struct.unpack_from("<H", d, pe + 6)[0]
    opt_size = struct.unpack_from("<H", d, pe + 20)[0]
    opt = pe + 24
    magic = struct.unpack_from("<H", d, opt)[0]
    if magic != 0x20B:
        raise SystemExit(f"expected a PE32+ image, magic {magic:#x}")
    size_of_image = struct.unpack_from("<I", d, opt + 56)[0]
    print(f"{nsec} sections, SizeOfImage {size_of_image:#x}, dump {len(d):#x} bytes")
    sec0 = opt + opt_size
    for i in range(nsec):
        o = sec0 + i * 40
        name = d[o:o + 8].rstrip(b"\0").decode(errors="replace")
        vsz, va, rsz, rptr = struct.unpack_from("<IIII", d, o + 8)
        keep = min(vsz, max(0, len(d) - va))
        struct.pack_into("<IIII", d, o + 8, vsz, va, keep, va)
        print(f"  {name:<8} va={va:#010x} vsize={vsz:#010x}  (was raw={rptr:#x} rsize={rsz:#x})")
    open(out, "wb").write(d)
    print(f"wrote {out}")


if __name__ == "__main__":
    main()

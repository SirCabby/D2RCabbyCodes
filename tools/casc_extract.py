#!/usr/bin/env python3
"""Read game files out of the installed game's CASC storage (read only), through
the CascLib D2RMM ships in its tools folder (CascLib.so on Linux, CascLib.dll on
Windows with a Windows Python). D2RMM's mods read the same files.

    tools/casc_extract.py <CascLib> <game dir> <out dir> <data path>...
    tools/casc_extract.py ".../D2RMM 1.9.2/tools/CascLib.so" "$GAME_DIR" re/casc \\
        local/lng/strings/item-runes.json hd/items/items.json global/excel/misc.txt

A data path is relative to the storage's data/ folder, the way D2RMM names files;
each file lands at <out dir>/<data path>. HD sprites (.sprite): "SpA1", u16
version, u32 width at +8, u32 height at +0xC, RGBA rows from +0x28.
"""
import ctypes
import os
import sys


def main():
    if len(sys.argv) < 5:
        raise SystemExit(__doc__)
    lib = ctypes.CDLL(sys.argv[1])
    game, out = sys.argv[2], sys.argv[3]
    handle = ctypes.c_void_p
    lib.CascOpenStorage.argtypes = [ctypes.c_char_p, ctypes.c_uint32, ctypes.POINTER(handle)]
    lib.CascOpenStorage.restype = ctypes.c_bool
    lib.CascOpenFile.argtypes = [handle, ctypes.c_char_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.POINTER(handle)]
    lib.CascOpenFile.restype = ctypes.c_bool
    lib.CascGetFileSize.argtypes = [handle, ctypes.POINTER(ctypes.c_uint32)]
    lib.CascGetFileSize.restype = ctypes.c_uint32
    lib.CascReadFile.argtypes = [handle, ctypes.c_void_p, ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint32)]
    lib.CascReadFile.restype = ctypes.c_bool
    lib.CascCloseFile.argtypes = [handle]
    lib.CascCloseStorage.argtypes = [handle]

    storage = handle()
    if not lib.CascOpenStorage(game.encode(), 0, ctypes.byref(storage)):
        raise SystemExit(f"cannot open the CASC storage in {game}")
    missing = 0
    for name in sys.argv[4:]:
        f = handle()
        if not lib.CascOpenFile(storage, f"data:data/{name}".encode(), 0, 0, ctypes.byref(f)):
            print(f"not in the storage: {name}")
            missing += 1
            continue
        high = ctypes.c_uint32(0)
        size = lib.CascGetFileSize(f, ctypes.byref(high))
        buf = ctypes.create_string_buffer(size)
        got = ctypes.c_uint32(0)
        ok = lib.CascReadFile(f, buf, size, ctypes.byref(got))
        lib.CascCloseFile(f)
        if not ok or got.value != size:
            print(f"short read: {name} ({got.value} of {size} bytes)")
            missing += 1
            continue
        dst = os.path.join(out, name)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with open(dst, "wb") as w:
            w.write(buf.raw)
        print(f"{name}: {size} bytes")
    lib.CascCloseStorage(storage)
    sys.exit(1 if missing else 0)


if __name__ == "__main__":
    main()

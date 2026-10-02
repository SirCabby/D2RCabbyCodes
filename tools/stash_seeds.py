#!/usr/bin/env python3
"""The seed a save keeps with an item, against the item: read the offline stash files and the characters, say
which items have a seed that does not roll what the item has, and (with --fix) give those a seed that does. Pure
stdlib.

    tools/stash_seeds.py [--excel DIR] [--also FILE]... FILE...
    tools/stash_seeds.py --fix --backup DIR [--copies BEFORE AFTER]... [--excel DIR] [--also FILE]... FILE...

    S="$HOME/Games/battlenet/drive_c/users/steamuser/Saved Games/Diablo II Resurrected/mods/D2RMM"
    tools/stash_seeds.py "$S/ModernSharedStashSoftCoreV2.d2i" "$S/SharedStash.d2x" "$S/Xeen.d2s"

FILE: a shared stash (.d2i: sections of a 64-byte header, "JM", a count, the items), a PlugY-format stash
(.d2x / .sss: "SSS\\0", pages of "ST", flags, a name, "JM", a count, items of the 1.10 format) or a character
(.d2s: the items after the skills, a corpse's, the mercenary's after "jf", the golem's after "kf"). --also names
files that are only read for the seeds they hold, so that no new seed is one of theirs. --excel is the folder of the
game's tables as text (the default: the loader's copies under GAME_DIR of config.mk, the rotw bank).

What is compared (src/itemseed.h has the rule, CLAUDE.md "Item seeds" the record). A save keeps one seed with an
item, the unit's (32 bits after the count of socketed items); a compact item (potions, runes, gems) has none. At
an item's creation item init (0x43EF10) rolls on that seed's stream, in this order:
  armor     the durability (half the base's plus a roll of that half), then the base defense (minac plus a roll
            of maxac - minac + 1)
  weapons   a stack's quantity, then the durability
  others    a stack's quantity
  then      the picture, for a type with pictures (rings, amulets, charms, jewels)
Of these a save keeps the base defense and the picture as they were rolled; the durability and the quantity change
in play. The generator changes the base defense after the roll: enhanced defense among the properties makes it the
top plus one whatever was rolled, an ethereal item has half as much again, a low quality one three quarters. So an
item's seed is judged
  ok        the defense and the picture in the save are what the seed rolls
  BAD       one of them is not
  -         nothing the seed rolls is kept (weapons, keys, tomes, ...)
--fix gives a BAD item the first seed after its own, an odd stride (0x9E3779B1) apart, that rolls the item's
defense and picture, and its durability too when the save has one item init can have rolled (so the item is as
the game makes it from that seed), and that no other item has. Copies of one item (the same seed, base and
defense) get the same new seed; --copies names a file as it was before an earlier fix and as it is since, and a
copy of an item that got a new seed there gets that seed. Only the 32 bits of the seed are written, and in a
character the header's checksum (+12: every byte added to the sum turned left by one bit); the file is read again
afterwards and must hold the same items, the new seeds rolling them. The originals go to --backup first.

A character's sidecar (<name>.d2rl, D2RLoader's record of the mods and plugins the character was played with) is
not written: it names the .d2s by checksum, size and SHA-256 and is sealed with a signature (D2RCore, "D2RL-SEAL-
V2"). With a .d2s that is no longer the one it names, the loader shows the character without its mod and plugin
badges and asks nothing at its launch (D2RCore's summary reader and launch check, read 2026-09-28), and writes a
new sidecar at the character's next save. A copy of the old one goes to --backup with the .d2s.

The item layout is the game's own reader's (0x374BF0 -> 0x378860, compact items 0x3780D0), formats 96 (1.10 to
1.14, what a PlugY file holds) and 105 (D2R 3.3).
"""
import argparse
import csv
import datetime
import hashlib
import os
import shutil
import struct
import sys

# ---------------------------------------------------------------------------------------------------------------
# tables

# The item code's Huffman tree (the game's, 0x1D03330 in the image: 8-byte nodes, the root 510), as the bits
# read for each character. The tree has all 256 byte values; item codes use these.
HUFFMAN = {
    " ": "10", "0": "11111011", "1": "1111100", "2": "001100", "3": "1101101", "4": "11111010", "5": "00010110",
    "6": "1101111", "7": "01111", "8": "000100", "9": "01110", "a": "11110", "b": "0101", "c": "01000",
    "d": "110001", "e": "110000", "f": "010011", "g": "11010", "h": "00011", "i": "1111110", "j": "000101110",
    "k": "010010", "l": "11101", "m": "01101", "n": "001101", "o": "1111111", "p": "11001", "q": "11011001",
    "r": "11100", "s": "0010", "t": "01100", "u": "00001", "v": "1101110", "w": "00000", "x": "00111",
    "y": "0001010", "z": "11011000",
}
HUFFMAN_BY_BITS = {bits: ch for ch, bits in HUFFMAN.items()}
HUFFMAN_LONGEST = max(len(b) for b in HUFFMAN.values())

QUALITY = {1: "low", 2: "normal", 3: "superior", 4: "magic", 5: "set", 6: "rare", 7: "unique", 8: "crafted",
           9: "tempered"}

# Item flags (item data +0x18).
F_SOCKETED = 0x800
F_EAR = 0x10000
F_COMPACT = 0x200000
F_ETHEREAL = 0x400000
F_PERSONALIZED = 0x1000000
F_SIMPLE_LOW = 0x2000000  # the reader takes it off the flags: nothing but the code follows
F_RUNEWORD = 0x4000000
F_FOUND = 0x10000000  # 3.3: where and when the item was found
F_FOUND_SHORT = 0x20000000

STAT_DEFENSE, STAT_DURABILITY, STAT_MAX_DURABILITY, STAT_SOCKETS = 31, 72, 73, 194
STAT_ENHANCED_DEFENSE, STAT_QUEST_DIFFICULTY = 16, 356
# The stats saved in a row with the one before them: read after the first one's id.
STAT_GROUPS = {17: 2, 48: 2, 50: 2, 52: 2, 54: 3, 57: 3}
SHARD_CODES = ("xa1", "xa2", "xa3", "xa4", "xa5")  # formats below 105 read a quantity for these by code


class ParseError(Exception):
    pass


def _int(s, default=0):
    try:
        return int(s)
    except (TypeError, ValueError):
        return default


def _rows(path):
    with open(path, newline="", encoding="latin-1") as f:
        r = csv.reader(f, delimiter="\t", quoting=csv.QUOTE_NONE)
        head = [h.strip() for h in next(r)]
        for row in r:
            row = row + [""] * (len(head) - len(row))
            yield dict(zip(head, (c.strip() for c in row)))


class Tables:
    """The game's tables of one bank, from its txt files."""

    def __init__(self, excel):
        self.items = {}
        for kind in ("armor", "weapons", "misc"):
            for row in _rows(os.path.join(excel, kind + ".txt")):
                if row.get("code"):
                    row["_kind"] = kind
                    self.items[row["code"]] = row
        self.types = {row["Code"]: row for row in _rows(os.path.join(excel, "itemtypes.txt")) if row.get("Code")}
        self.stats = {int(row["*ID"]): row for row in _rows(os.path.join(excel, "itemstatcost.txt"))
                      if row.get("*ID", "") != ""}

    def is_type(self, code, want, depth=0):
        """The game's item-type test (0x373890): the type or one of its Equiv1 / Equiv2, to any depth."""
        if not code or depth > 10:
            return False
        if code == want:
            return True
        row = self.types.get(code)
        return row is not None and (self.is_type(row.get("Equiv1", ""), want, depth + 1) or
                                    self.is_type(row.get("Equiv2", ""), want, depth + 1))

    def item_is(self, row, want):
        return self.is_type(row.get("type", ""), want) or self.is_type(row.get("type2", ""), want)

    def save_bits(self, stat):
        row = self.stats.get(stat)
        if row is None:
            raise ParseError(f"stat {stat} is not in ItemStatCost")
        return _int(row.get("Save Bits")), _int(row.get("Save Add")), _int(row.get("Save Param Bits"))


def default_excel():
    here = os.path.dirname(os.path.abspath(__file__))
    game = None
    try:
        for line in open(os.path.join(here, "..", "config.mk"), encoding="utf-8"):
            if line.split(":=")[0].strip() == "GAME_DIR" and ":=" in line:
                game = line.split(":=", 1)[1].strip()
    except OSError:
        pass
    if not game:
        return None
    root = os.path.join(game, "d2rloader", "data", "compiler")
    try:
        versions = sorted(v for v in os.listdir(root) if os.path.isdir(os.path.join(root, v)))
    except OSError:
        return None
    return os.path.join(root, versions[-1], "data", "global", "excel") if versions else None


# ---------------------------------------------------------------------------------------------------------------
# items

class Bits:
    """The game's bit stream: bits are taken from each byte's low end first."""

    def __init__(self, data, byte_pos=0):
        self.d = data
        self.pos = byte_pos * 8

    def read(self, n):
        if self.pos + n > len(self.d) * 8:
            raise ParseError(f"read of {n} bits past the end (bit {self.pos})")
        v = 0
        for i in range(n):
            p = self.pos + i
            v |= ((self.d[p >> 3] >> (p & 7)) & 1) << i
        self.pos += n
        return v

    def align(self):
        self.pos = (self.pos + 7) & ~7

    @property
    def byte(self):
        return self.pos >> 3


def write_bits(buf, bit, n, value):
    for i in range(n):
        p = bit + i
        if (value >> i) & 1:
            buf[p >> 3] |= 1 << (p & 7)
        else:
            buf[p >> 3] &= ~(1 << (p & 7)) & 0xFF


def read_code(b):
    out = ""
    for _ in range(4):
        path = ""
        while path not in HUFFMAN_BY_BITS:
            path += "1" if b.read(1) else "0"
            if len(path) > HUFFMAN_LONGEST:
                raise ParseError(f"no character of an item code for the bits at {b.pos}")
        out += HUFFMAN_BY_BITS[path]
    return out


def read_name(b, char_bits):
    s = bytearray()
    while True:
        c = b.read(char_bits)
        if c == 0:
            return s.decode("utf-8", "replace")
        s.append(c)


def read_stat_list(b, t):
    out = []
    while True:
        at = b.pos
        sid = b.read(9)
        if sid == 0x1FF:
            return out
        for j in range(STAT_GROUPS.get(sid, 1)):
            bits, add, pbits = t.save_bits(sid + j)
            if bits == 0:
                raise ParseError(f"stat {sid + j} has no save bits (bit {at})")
            param = b.read(pbits) if pbits else 0
            out.append((sid + j, param, b.read(bits) - add))


def read_item(b, t, fmt):
    """One item at the stream's position, the save form, in the save format `fmt` (a .d2s header's version).
    Leaves the stream after it, byte aligned."""
    if fmt not in (0x60, 0x69):
        raise ParseError(f"item format {fmt} is not read (96 and 105 are)")
    it = {"at": b.byte, "format": fmt}
    old = fmt < 0x61
    if old and b.read(16) != 0x4D4A:
        raise ParseError(f"no JM at byte {it['at']}")
    flags = b.read(32)
    simple_low = bool(flags & F_SIMPLE_LOW)
    flags &= ~F_SIMPLE_LOW
    it["flags"] = flags
    compact = it["compact"] = bool(flags & F_COMPACT)
    if old:
        it["version"] = b.read(10)
    else:
        it["version"] = b.read(2) + 99 if b.read(1) else b.read(2)
    char_bits = 8 if fmt > 0x61 else 7
    it["mode"] = b.read(3)
    if it["mode"] in (3, 5):
        it["x"], it["y"] = b.read(16), b.read(16)
    else:
        it["body"], it["x"], it["y"] = b.read(4), b.read(4), b.read(4)
        it["page"] = b.read(3) - 1
        if compact and flags & F_EAR:  # the compact reader has the ear before the code
            it["ear"] = (b.read(3), b.read(7), read_name(b, char_bits))
    if old:
        raw = b.read(32)
        code = bytes((raw >> (8 * i)) & 0xFF for i in range(4)).decode("latin-1")
    else:
        code = read_code(b)
    it["code"] = code.rstrip(" ")
    row = t.items.get(it["code"])
    if row is None:
        raise ParseError(f"item code {code!r} is in no table (byte {it['at']})")
    it["name"] = row.get("name", "")
    it["socketed_items"] = 0

    def realm():  # the id the game's duplicate test at a load reads (0x4229C0); offline items may have none
        if b.read(1):
            it["realm"] = b.read(96 if old else 128)

    def stack():  # 3.3: the count of an item stacked in the stash's materials tab
        if fmt >= 0x68 and b.read(1):
            it["stash_count"] = b.read(8)

    def gold():
        it["gold"] = b.read(32 if b.read(1) else 12)
        b.read(1)

    def finish():
        b.align()
        it["end"] = b.byte
        return it

    if compact:  # no seed, level or quality: the game gives it seed 0, level 1, normal
        if t.item_is(row, "gold"):
            gold()
        if _int(row.get("quest")) and _int(row.get("questdiffcheck")):
            bits, add, _ = t.save_bits(STAT_QUEST_DIFFICULTY)
            it["quest_difficulty"] = b.read(bits) - add
        realm()
        stack()
        return finish()
    if simple_low:
        it["level"], it["quality"] = 1, 1
        return finish()
    it["socketed_items"] = b.read(3)
    it["seed_bit"] = b.pos
    it["seed"] = b.read(32)
    it["level"] = b.read(7)
    q = it["quality"] = b.read(4)
    if b.read(1):
        it["picture"] = b.read(3)
    if b.read(1):
        it["auto_affix"] = b.read(11)
    if q in (1, 3):
        it["file_index"] = b.read(3)
    elif q == 2:
        if t.item_is(row, "char"):
            it["charm_affix"] = (b.read(1), b.read(11))
        if t.item_is(row, "body") and not t.item_is(row, "play"):
            it["monster"] = b.read(10)
        if t.item_is(row, "scro") or t.item_is(row, "book"):
            it["spell"] = b.read(5)
    elif q == 4:
        it["affixes"] = (b.read(11), b.read(11))
    elif q in (5, 7):
        it["file_index"] = b.read(12)
    elif q in (6, 8):
        it["rare_name"] = (b.read(8), b.read(8))
        it["affixes"] = tuple(b.read(11) if b.read(1) else 0 for _ in range(6))
    elif q == 9:
        it["rare_name"] = (b.read(8), b.read(8))
    else:
        raise ParseError(f"quality {q} (byte {it['at']})")
    if flags & F_RUNEWORD:
        it["runeword"] = b.read(16)
    if flags & F_EAR:
        it["ear"] = (b.read(3), b.read(7), read_name(b, char_bits))
    elif flags & F_PERSONALIZED:
        it["owner"] = read_name(b, char_bits)
    realm()
    if t.item_is(row, "armo"):
        bits, add, _ = t.save_bits(STAT_DEFENSE)
        it["defense"] = b.read(bits) - add
    if t.item_is(row, "armo") or t.item_is(row, "weap"):
        bits, add, _ = t.save_bits(STAT_MAX_DURABILITY)
        it["max_durability"] = b.read(bits) - add
        if it["max_durability"] != 0:
            bits, add, _ = t.save_bits(STAT_DURABILITY)
            it["durability"] = b.read(bits) - add
    elif t.item_is(row, "gold"):
        gold()
    if fmt >= 0x69:
        if b.read(1):
            it["quantity"] = b.read(9)
    elif _int(row.get("stackable")) or it["code"] in SHARD_CODES:
        it["quantity"] = b.read(9)
    if flags & F_SOCKETED:
        bits, add, _ = t.save_bits(STAT_SOCKETS)
        it["sockets"] = b.read(bits) - add
    set_mask = b.read(5) if q == 5 else 0
    it["set_mask"] = set_mask
    lists = 1 + bin(set_mask).count("1") + (1 if flags & F_RUNEWORD else 0)
    it["stats"] = tuple(tuple(read_stat_list(b, t)) for _ in range(lists))
    if fmt > 99 and flags & F_FOUND:
        found = [b.read(16)]
        n = 1
        if not flags & F_FOUND_SHORT:
            found.append(b.read(32))
            n = min(b.read(4), 8)
        it["found"] = tuple(found + [b.read(64) for _ in range(n)])
    stack()
    return finish()


def read_list(data, start, count, t, fmt):
    """`count` items from byte `start`, each followed by the items in its sockets."""
    b = Bits(data, start)
    items = []
    for i in range(count):
        it = read_item(b, t, fmt)
        it["index"] = i
        items.append(it)
        for _ in range(it["socketed_items"]):
            s = read_item(b, t, fmt)
            s["index"], s["socket_of"] = i, it["code"]
            items.append(s)
    return items, b.byte


# ---------------------------------------------------------------------------------------------------------------
# files

def read_d2i(data, t):
    out = []
    off = n = 0
    while off < len(data):
        magic, _kind, fmt, _gold, size = struct.unpack_from("<IIIII", data, off)
        if magic != 0xAA55AA55 or size < 68 or off + size > len(data):
            raise ParseError(f"no stash section at {off:#x}")
        n += 1
        if data[off + 64:off + 66] == b"JM":  # a tab of items; the last section of a 3.3 stash is another thing
            count = struct.unpack_from("<H", data, off + 66)[0]
            items, end = read_list(data, off + 68, count, t, fmt)
            if end != off + size:
                raise ParseError(f"tab {n}: the items end at {end:#x}, the section at {off + size:#x}")
            for it in items:
                it["where"] = f"tab {n}"
            out += items
        off += size
    return out


def read_plugy(data, t):
    shared = data[:4] == b"SSS\0"
    off = 6
    if shared and data[4:6] == b"02":
        off += 4  # the shared gold
    if not shared:
        off += 4
    pages = struct.unpack_from("<I", data, off)[0]
    off += 4
    out = []
    for p in range(1, pages + 1):
        if data[off:off + 2] != b"ST":
            raise ParseError(f"page {p}: no ST at {off:#x}")
        off += 2
        z = data.index(b"\0", off)
        if data[z + 1:z + 3] != b"JM":  # the page's flags come before its name
            off += 4
            z = data.index(b"\0", off)
        name = data[off:z].decode("latin-1")
        off = z + 1
        if data[off:off + 2] != b"JM":
            raise ParseError(f"page {p}: no JM at {off:#x}")
        count = struct.unpack_from("<H", data, off + 2)[0]
        items, off = read_list(data, off + 4, count, t, 0x60)
        for it in items:
            it["where"] = f"page {p} '{name}'"
        out += items
    if off != len(data):
        raise ParseError(f"the pages end at {off:#x}, the file at {len(data):#x}")
    return out


def d2s_checksum(data):
    """A character file's checksum (+12), the 1.10 games': each byte added to the sum turned left by one bit, the
    checksum's own four bytes taken as 0."""
    total = 0
    for i, c in enumerate(data):
        total = ((total << 1) | (total >> 31)) & 0xFFFFFFFF
        total = (total + (0 if 12 <= i < 16 else c)) & 0xFFFFFFFF
    return total


def d2s_lists(data, at, t, fmt):
    """The lists of a character from its own at `at`: "JM" count items, "JM" corpses (12 bytes and a list each),
    "jf" and the mercenary's list when there is one, "kf", whether there is a golem, its item."""
    def word(p):
        if p + 2 > len(data):
            raise ParseError("the file ends in a list's head")
        return struct.unpack_from("<H", data, p)[0]

    def tag(p, want):
        if data[p:p + 2] != want:
            raise ParseError(f"no {want.decode()} at {p:#x}")
        return p + 2

    def lst(p, count, where):
        items, end = read_list(data, p, count, t, fmt)
        for it in items:
            it["where"] = where
        return items, end

    out, p = lst(tag(at, b"JM") + 2, word(at + 2), "the character")
    corpses = word(tag(p, b"JM"))
    p += 4
    for _ in range(corpses):
        p = tag(p + 12, b"JM")
        items, p = lst(p + 2, word(p), "the corpse")
        out += items
    p = tag(p, b"jf")
    if data[p:p + 2] == b"JM":
        items, p = lst(p + 4, word(p + 2), "the mercenary")
        out += items
    p = tag(p, b"kf")
    if p >= len(data):
        raise ParseError("the file ends before the golem")
    if data[p]:
        items, p = lst(p + 1, 1, "the golem")
        out += items
    return out


def read_d2s(data, t):
    _magic, fmt, size, stored = struct.unpack_from("<IIII", data, 0)
    if size != len(data):
        raise ParseError(f"the header says {size} bytes, the file has {len(data)}")
    if d2s_checksum(data) != stored:
        raise ParseError(f"the header's checksum {stored:#010x} is not the file's, {d2s_checksum(data):#010x}")
    pos = 0
    while True:  # the character's list is the first "JM" that the whole row of lists follows
        at = data.find(b"JM", pos)
        if at < 0:
            raise ParseError("no list of items that a corpse's, jf and kf follow")
        pos = at + 1
        try:
            return d2s_lists(data, at, t, fmt)
        except ParseError:
            continue


def read_file(path, t):
    data = open(path, "rb").read()
    if data[:4] in (b"SSS\0", b"CSTM"):
        return data, "plugy", read_plugy(data, t)
    if data[:4] != b"\x55\xaa\x55\xaa":
        raise ParseError("neither a stash nor a character file")
    if path.lower().endswith(".d2s"):
        return data, "d2s", read_d2s(data, t)
    return data, "d2i", read_d2i(data, t)


# ---------------------------------------------------------------------------------------------------------------
# what a seed rolls

MULTIPLIER = 0x6AC690C5
STRIDE = 0x9E3779B1  # the plugin's (src/itemseed.h)


class Rng:
    """The game's random stream: multiply with carry on {lo, hi}, the carry seeded 666."""

    def __init__(self, seed):
        self.lo, self.hi = seed & 0xFFFFFFFF, 666

    def roll(self, n):
        if n <= 0:
            return 0  # without a step
        v = self.lo * MULTIPLIER + self.hi
        self.lo, self.hi = v & 0xFFFFFFFF, v >> 32
        return self.lo & (n - 1) if n & (n - 1) == 0 else self.lo % n


def init_rolls(t, row, seed):
    """What item init rolls for a new item of this base from the unit seed, in its order."""
    r = Rng(seed)
    out = {}
    ty = t.types.get(row.get("type", ""))
    if row.get("type") == "gold":
        return out
    mins, maxs, spawn = _int(row.get("minstack")), min(_int(row.get("maxstack")), 0x1FF), _int(row.get("spawnstack"))
    stackable = _int(row.get("stackable")) != 0
    half = (_int(row.get("durability")) & 0xFF) >> 1
    if ty is not None and ty.get("Quiver"):  # arrows and bolts
        out["quantity"] = mins + r.roll(maxs - mins)
    elif t.item_is(row, "armo"):
        out["durability"] = min(r.roll(half) + half, 0xFF)
        lo, hi = _int(row.get("minac")), _int(row.get("maxac"))
        out["defense"] = lo + r.roll(hi - lo + 1)
    elif t.item_is(row, "weap"):
        if stackable:
            out["quantity"] = mins + r.roll(maxs - mins)
        out["durability"] = min(r.roll(half) + half, 0xFF)
    elif stackable:
        top = spawn if spawn >= mins and spawn != 0 else max(maxs, mins)
        out["quantity"] = mins + r.roll(top - mins)
    pictures = _int(ty.get("VarInvGfx")) if ty is not None else 0
    if pictures:
        out["picture"] = r.roll(pictures)
    return out


def has_stat(it, stat):
    return any(s[0] == stat for lst in it.get("stats", ()) for s in lst)


def kept_defense(row, it, rolled):
    """The base defense the save has for a base defense rolled at item init."""
    top = _int(row.get("maxac"))
    d = rolled
    if it.get("quality") == 1:
        d = max(1, d * 75 // 100)
    elif has_stat(it, STAT_ENHANCED_DEFENSE) and top != 0 and d <= top:
        d = top + 1  # 0x3D5AC0, before the property's roll
    return d * 3 // 2 if it["flags"] & F_ETHEREAL else d


def rolls_the_item(t, row, it, seed, durability=None):
    r = init_rolls(t, row, seed)
    if "defense" in r and "defense" in it and kept_defense(row, it, r["defense"]) != it["defense"]:
        return False
    if "picture" in r and r["picture"] != it.get("picture"):
        return False
    return durability is None or r.get("durability") == durability


def judge(t, it):
    """(verdict, what was compared)."""
    if "seed" not in it:
        return "-", "no seed is saved with it"
    row = t.items[it["code"]]
    r = init_rolls(t, row, it["seed"])
    said = []
    bad = False
    if "defense" in r and "defense" in it:
        lo, hi = _int(row.get("minac")), _int(row.get("maxac"))
        want = kept_defense(row, it, r["defense"])
        how = "".join(w for w, on in ((", three quarters for low quality", it.get("quality") == 1),
                                      (", half as much again for ethereal", it["flags"] & F_ETHEREAL)) if on)
        if want != it["defense"]:
            bad = True
            said.append(f"base defense {it['defense']} in the save, the seed rolls {r['defense']} of {lo}..{hi}"
                        + (f"{how}: {want}" if how else ""))
        elif has_stat(it, STAT_ENHANCED_DEFENSE) and it.get("quality") != 1 and hi != 0:
            said.append(f"base defense {it['defense']}: the top of {lo}..{hi} plus one for enhanced defense{how},"
                        " whatever the seed rolls")
        else:
            said.append(f"base defense {it['defense']} as the seed rolls it ({r['defense']} of {lo}..{hi}{how})")
    if "picture" in r:
        if r["picture"] != it.get("picture"):
            bad = True
            said.append(f"picture {it.get('picture')} in the save, the seed rolls {r['picture']}")
        else:
            said.append(f"picture {it['picture']} as the seed rolls it")
    if not said:
        return "-", "nothing the seed rolls is kept"
    return ("BAD" if bad else "ok"), "; ".join(said)


def can_be_rolled(t, row, it):
    """Whether some roll of item init gives what the save has."""
    if "defense" in it:
        lo, hi = _int(row.get("minac")), _int(row.get("maxac"))
        if it["defense"] not in {kept_defense(row, it, d) for d in range(lo, hi + 1)}:
            return False
    ty = t.types.get(row.get("type", ""))
    pictures = _int(ty.get("VarInvGfx")) if ty is not None else 0
    return not pictures or (it.get("picture") is not None and it["picture"] < pictures)


def rollable_durability(t, row, it):
    """The durability in the save when item init can have rolled it, else None."""
    if it["flags"] & F_ETHEREAL or it.get("quality") == 1 or "durability" not in it:
        return None
    base = _int(row.get("durability")) & 0xFF
    if it.get("max_durability") != base or not base >> 1 <= it["durability"] < 2 * (base >> 1):
        return None
    return it["durability"]


def describe(it):
    q = QUALITY.get(it.get("quality"), "")
    eth = " ethereal" if it["flags"] & F_ETHEREAL else ""
    rw = " runeword" if it["flags"] & F_RUNEWORD else ""
    sock = f", in the sockets of {it['socket_of']}" if "socket_of" in it else ""
    return f"{it['where']} #{it['index'] + 1} {it['code']} {it['name']} ({q}{eth}{rw}, level {it.get('level')}{sock})"


# ---------------------------------------------------------------------------------------------------------------

def sha(data):
    return hashlib.sha256(data).hexdigest()


def game_running():
    try:
        for pid in os.listdir("/proc"):
            if pid.isdigit():
                try:
                    cmd = open(f"/proc/{pid}/cmdline", "rb").read().split(b"\0")[0]
                except OSError:
                    continue
                if cmd.replace(b"\\", b"/").split(b"/")[-1].lower() in (b"d2r.exe", b"d2rloader.exe"):
                    return True
    except OSError:
        pass
    return False


def group_of(it):
    """What makes two items copies of one: the seed, the base and what the seed is to roll."""
    return (it["seed"], it["code"], it.get("defense"), it.get("picture"), it["flags"] & F_ETHEREAL,
            it.get("quality") == 1, has_stat(it, STAT_ENHANCED_DEFENSE))


def main():
    ap = argparse.ArgumentParser(description="Item seeds in the offline stashes and characters, against the items.",
                                 epilog="See the head of the file.")
    ap.add_argument("files", nargs="+", metavar="FILE")
    ap.add_argument("--excel", help="the folder of the game's tables as text")
    ap.add_argument("--also", action="append", default=[], metavar="FILE",
                    help="a file only read for the seeds it holds")
    ap.add_argument("--fix", action="store_true", help="give the items judged BAD a seed that rolls them")
    ap.add_argument("--backup", metavar="DIR", help="where the originals go before a file is written")
    ap.add_argument("--copies", nargs=2, action="append", default=[], metavar=("BEFORE", "AFTER"),
                    help="a file before and after an earlier fix: copies of its items get the seeds given there")
    ap.add_argument("--all", action="store_true", help="list every item with a seed, not only the BAD ones")
    args = ap.parse_args()
    excel = args.excel or default_excel()
    if not excel or not os.path.isfile(os.path.join(excel, "armor.txt")):
        raise SystemExit("the game's tables were not found: name their folder with --excel")
    if args.fix and not args.backup:
        raise SystemExit("--fix needs --backup DIR")
    if args.fix and game_running():
        raise SystemExit("the game is running: it would write its own saves over the change. Close it first.")
    t = Tables(excel)
    print(f"tables: {excel} ({len(t.items)} items)")

    def read(path):
        try:
            return read_file(path, t)
        except (ParseError, OSError, struct.error) as e:
            raise SystemExit(f"{path}: {e}")

    files = []
    used = set()
    for path in args.files + args.also:
        data, kind, items = read(path)
        used.update(it["seed"] for it in items if "seed" in it)
        files.append({"path": path, "data": data, "kind": kind, "items": items, "bad": [],
                      "write": path in args.files})

    given = {}  # the seeds an earlier fix gave, by what they were given to
    for before, after in args.copies:
        was, now = read(before)[2], read(after)[2]
        if len(was) != len(now) or any(a["code"] != b["code"] for a, b in zip(was, now)):
            raise SystemExit(f"{after} does not hold the items of {before}")
        for a, b in zip(was, now):
            if a.get("seed") != b.get("seed"):
                given[group_of(a)] = (b["seed"], os.path.basename(after))
        used.update(it["seed"] for it in was + now if "seed" in it)

    for f in files:
        tally = {"ok": 0, "BAD": 0, "-": 0}
        for it in f["items"]:
            if "seed" not in it:
                continue
            verdict, said = judge(t, it)
            tally[verdict] += 1
            if verdict == "BAD":
                f["bad"].append(it)
            if verdict == "BAD" or args.all:
                print(f"  {verdict:<3} {it['seed']:08X}  {describe(it)}: {said}")
        role = "" if f["write"] else " (only read)"
        print(f"{f['path']}{role}: {len(f['items'])} items, {sum(tally.values())} with a seed: {tally['ok']} ok,"
              f" {tally['BAD']} BAD, {tally['-']} with nothing to compare")
    if not args.fix:
        return 0

    # The copies of an item (the same seed, base and defense) get one seed, and the durability is asked of it
    # when every copy has the same one that item init can have rolled.
    groups = {}
    for f in files:
        for it in f["bad"] if f["write"] else ():
            it["group"] = group_of(it)
            groups.setdefault(it["group"], []).append(it)
    chosen = {}
    for key, members in groups.items():
        row = t.items[key[1]]
        if not can_be_rolled(t, row, members[0]):
            print(f"  no roll gives what {describe(members[0])} has: left as it is")
            continue
        if key in given and rolls_the_item(t, row, members[0], given[key][0]):
            chosen[key] = (given[key][0], f"its copy's in {given[key][1]}", None)
            continue
        durabilities = {rollable_durability(t, row, it) for it in members}
        durability = durabilities.pop() if len(durabilities) == 1 else None
        s = key[0]
        for tries in range(1, 1 << 24):
            s = (s + STRIDE) & 0xFFFFFFFF
            if s not in used and rolls_the_item(t, row, members[0], s, durability):
                chosen[key] = (s, f"seed {tries} of the walk", durability)
                used.add(s)
                break
        else:
            print(f"  no seed found that rolls {describe(members[0])}: left as it is")

    wrote = 0
    for f in files:
        if not f["write"] or not f["bad"]:
            continue
        new = bytearray(f["data"])
        changed = []
        for it in f["bad"]:
            if it["group"] not in chosen:
                continue
            seed, how, durability = chosen[it["group"]]
            write_bits(new, it["seed_bit"], 32, seed)
            changed.append((it, seed))
            rolled = init_rolls(t, t.items[it["code"]], seed)
            print(f"  {it['seed']:08X} -> {seed:08X}  {describe(it)}: the new seed rolls base defense"
                  f" {rolled.get('defense')}" + (f" and durability {durability}" if durability is not None else "")
                  + f" ({how})")
        if not changed:
            continue
        allowed = set()
        for it, _ in changed:
            allowed.update(range(it["seed_bit"] >> 3, (it["seed_bit"] + 31 >> 3) + 1))
        if f["kind"] == "d2s":
            struct.pack_into("<I", new, 12, d2s_checksum(new))
            allowed.update(range(12, 16))

        # Read what was made: the same items, only the seeds new, and each of those rolling its item.
        try:
            again = {"plugy": read_plugy, "d2i": read_d2i, "d2s": read_d2s}[f["kind"]](bytes(new), t)
        except (ParseError, struct.error) as e:
            raise SystemExit(f"{f['path']}: after the change: {e}. Not written.")
        seeds = {id(it): s for it, s in changed}
        if len(again) != len(f["items"]):
            raise SystemExit(f"{f['path']}: {len(again)} items after the change, {len(f['items'])} before."
                             " Not written.")
        for was, now in zip(f["items"], again):
            want = {k: v for k, v in was.items() if k != "group"}
            if id(was) in seeds:
                want["seed"] = seeds[id(was)]
                if judge(t, now)[0] != "ok":
                    raise SystemExit(f"{f['path']}: {describe(now)} is not rolled by its new seed. Not written.")
            if want != now:
                raise SystemExit(f"{f['path']}: {describe(was)} reads otherwise after the change. Not written.")
        differ = {i for i in range(len(new)) if new[i] != f["data"][i]}
        if len(new) != len(f["data"]) or not differ <= allowed:
            raise SystemExit(f"{f['path']}: bytes outside the seeds differ. Not written.")

        os.makedirs(args.backup, exist_ok=True)
        with open(os.path.join(args.backup, "SHA256SUMS.txt"), "a", encoding="utf-8") as log:
            def keep(path, data):
                kept = os.path.join(args.backup, os.path.basename(path))
                if os.path.exists(kept):
                    if open(kept, "rb").read() != data:
                        raise SystemExit(f"{kept} is there already and is another file. Not written.")
                else:
                    shutil.copy2(path, kept)
                if open(kept, "rb").read() != data or open(path, "rb").read() != data:
                    raise SystemExit(f"{path} changed while it was read. Not written.")
                now = datetime.datetime.now().astimezone().isoformat(timespec="seconds")
                log.write(f"{sha(data)}  {os.path.basename(path)}  (as it was, {now}; from {path})\n")
                return kept

            kept = keep(f["path"], f["data"])
            sidecar = os.path.splitext(f["path"])[0] + ".d2rl"
            if f["kind"] == "d2s" and os.path.isfile(sidecar):
                keep(sidecar, open(sidecar, "rb").read())
            else:
                sidecar = None
            log.write(f"{sha(bytes(new))}  {os.path.basename(f['path'])}  (with {len(changed)} seeds new)\n")
        tmp = f["path"] + ".seeds-tmp"
        with open(tmp, "wb") as w:
            w.write(new)
            w.flush()
            os.fsync(w.fileno())
        shutil.copymode(f["path"], tmp)
        os.replace(tmp, f["path"])
        if open(f["path"], "rb").read() != bytes(new):
            raise SystemExit(f"{f['path']}: what was written does not read back. The original is {kept}.")
        wrote += 1
        print(f"{f['path']}: written, {len(changed)} seeds in {len(differ)} bytes"
              + (" (the checksum among them)" if f["kind"] == "d2s" else "") + f"; the original is {kept}")
        if sidecar:
            print(f"  {os.path.basename(sidecar)} is left as it is: it names the .d2s as it was, so the loader shows"
                  " the character without its badges until its next save writes a new one")
    print(f"{wrote} file(s) written" if wrote else "nothing written")
    return 0


if __name__ == "__main__":
    sys.exit(main())

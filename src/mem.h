#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

// Small memory helpers, in the spirit of the sibling mods' mem.h. Every read of
// game memory is guarded: mingw has no __try, so a "guarded" read is a copy whose
// faults a vectored exception handler turns into a false return (mem.cpp). It
// costs a copy; asking VirtualQuery first would scan Wine's page table and cost
// far more (RE2 measured the game at 30 fps instead of 120).
namespace d2rcc::mem {

struct Range {
  uintptr_t begin = 0;
  uintptr_t end = 0;
  bool empty() const { return end <= begin; }
  size_t size() const { return empty() ? 0 : end - begin; }
  bool contains(uintptr_t a) const { return a >= begin && a < end; }
};

// The user-mode address space of a 64-bit process: anything outside it is not
// a pointer, whatever it looks like.
inline bool plausible(uintptr_t a) { return a >= 0x10000 && a < 0x00007FFFFFFF0000ull; }

// --- guarded copies (mem.cpp) -------------------------------------------------
bool guard_install();                                    // once; the first copy does it too
long guard_faults();                                     // faults caught so far (the log)
bool copy_from(void* dst, uintptr_t src, size_t n);      // game memory -> ours; false = it faulted
bool copy_to(uintptr_t dst, const void* src, size_t n);  // ours -> game memory (no unprotecting)
bool readable(uintptr_t a, size_t n);                    // one guarded byte per page

template <typename T>
inline bool read_safe(uintptr_t target, T* out) { return copy_from(out, target, sizeof(T)); }

template <typename T>
inline T read(uintptr_t target) {
  T v{};
  std::memcpy(&v, reinterpret_cast<const void*>(target), sizeof(T));
  return v;
}

// A plain store into game data (a heap object's field, a global byte). A page
// that is not writable faults, which fails the store before its first byte.
template <typename T>
inline bool store(uintptr_t target, const T& value) { return copy_to(target, &value, sizeof(T)); }

inline uintptr_t read_ptr(uintptr_t target) {
  uintptr_t v = 0;
  return read_safe(target, &v) && plausible(v) ? v : 0;
}

// The absolute target of a RIP-relative operand: the disp32 at insn+disp_at,
// counted from the end of the instruction (insn+len).
inline uintptr_t rip_target(uintptr_t insn, int disp_at, int len) {
  return insn + static_cast<uintptr_t>(len) + static_cast<intptr_t>(read<int32_t>(insn + disp_at));
}

// --- PE sections -------------------------------------------------------------
inline IMAGE_NT_HEADERS* nt_headers(uintptr_t base) {
  auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
  if (!base || !readable(base, sizeof(IMAGE_DOS_HEADER)) || dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
  auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
  if (!readable(reinterpret_cast<uintptr_t>(nt), sizeof(IMAGE_NT_HEADERS))) return nullptr;
  return nt->Signature == IMAGE_NT_SIGNATURE ? nt : nullptr;
}

inline Range section_range(uintptr_t base, const IMAGE_SECTION_HEADER& s) {
  DWORD size = s.Misc.VirtualSize ? s.Misc.VirtualSize : s.SizeOfRawData;
  return Range{base + s.VirtualAddress, base + s.VirtualAddress + size};
}

// The section with this (up to 8 character) name, or an empty range.
inline Range section(uintptr_t base, const char* name) {
  auto nt = nt_headers(base);
  if (!nt) return {};
  auto* s = IMAGE_FIRST_SECTION(nt);
  for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i)
    if (std::strncmp(reinterpret_cast<const char*>(s[i].Name), name, 8) == 0) return section_range(base, s[i]);
  return {};
}

inline Range module_range(uintptr_t base) {
  auto nt = nt_headers(base);
  if (!nt) return {};
  return Range{base, base + nt->OptionalHeader.SizeOfImage};
}

// --- pattern scanning --------------------------------------------------------
// Patterns are IDA/CE style: "48 8B 0D ?? ?? ?? ?? BA FF FF FF FF". "??" is a wildcard.
struct Pattern {
  std::vector<int16_t> bytes;  // -1 = wildcard
};

inline Pattern parse_pattern(const char* s) {
  Pattern p;
  for (const char* c = s; *c;) {
    while (*c == ' ') ++c;
    if (!*c) break;
    if (c[0] == '?') {
      p.bytes.push_back(-1);
      while (*c == '?') ++c;
      continue;
    }
    auto hex = [](char h) -> int {
      if (h >= '0' && h <= '9') return h - '0';
      if (h >= 'a' && h <= 'f') return h - 'a' + 10;
      if (h >= 'A' && h <= 'F') return h - 'A' + 10;
      return -1;
    };
    const int hi = hex(c[0]), lo = c[1] ? hex(c[1]) : -1;
    if (hi < 0 || lo < 0) break;  // malformed; stop where it stops making sense
    p.bytes.push_back(static_cast<int16_t>(hi * 16 + lo));
    c += 2;
  }
  return p;
}

// Every match of `p` in the buffer [data, data+size), as offsets, up to `limit`.
inline std::vector<size_t> find_all(const uint8_t* data, size_t size, const Pattern& p, size_t limit = 64) {
  std::vector<size_t> out;
  const size_t n = p.bytes.size();
  if (!n || size < n) return out;
  const bool first_wild = p.bytes[0] < 0;
  const uint8_t first = static_cast<uint8_t>(p.bytes[0]);
  for (size_t a = 0; a + n <= size && out.size() < limit; ++a) {
    if (!first_wild) {
      const void* hit = std::memchr(data + a, first, size - n + 1 - a);
      if (!hit) break;
      a = static_cast<size_t>(static_cast<const uint8_t*>(hit) - data);
    }
    size_t i = 1;
    for (; i < n; ++i)
      if (p.bytes[i] >= 0 && data[a + i] != static_cast<uint8_t>(p.bytes[i])) break;
    if (i == n) out.push_back(a);
  }
  return out;
}

// A private copy of a range of game memory, taken in 64 KiB pieces so a page
// that cannot be read leaves a hole (zeros) instead of failing the whole copy.
// Returns how many bytes were readable.
inline size_t snapshot(uintptr_t begin, size_t size, std::vector<uint8_t>& out) {
  out.assign(size, 0);
  size_t got = 0;
  constexpr size_t kPiece = 64 * 1024;
  for (size_t off = 0; off < size; off += kPiece) {
    const size_t n = size - off < kPiece ? size - off : kPiece;
    if (copy_from(out.data() + off, begin + off, n)) {
      got += n;
    } else {
      // Fall back to pages, keeping what can be read.
      for (size_t po = 0; po < n; po += 0x1000) {
        const size_t pn = n - po < 0x1000 ? n - po : 0x1000;
        if (copy_from(out.data() + off + po, begin + off + po, pn)) got += pn;
      }
    }
  }
  return got;
}

}  // namespace d2rcc::mem

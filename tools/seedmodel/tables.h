// The loader's compiled tables (D2RLBIN) as the seed model's Tables: the rows, and the text tables where the compiled
// ones keep less. For the tools and tests that run over the game's data (D2RCC_EXCEL = the loader's compiled excel
// folder).
#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "itemgen.h"

namespace tables {

using namespace d2rcc::itemgen;

inline bool read_file(const std::string& path, std::vector<uint8_t>* out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  out->assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
  return true;
}
template <typename T>
inline T rd(const std::vector<uint8_t>& d, size_t at) {
  T v{};
  if (at + sizeof(T) <= d.size()) std::memcpy(&v, d.data() + at, sizeof(T));
  return v;
}
inline bool rows_of(const std::string& dir, const char* name, uint32_t row_size, std::vector<uint8_t>* rows,
                    uint32_t* count) {
  std::vector<uint8_t> d;
  if (!read_file(dir + "/" + name + ".bin", &d) || d.size() < 0x60 || std::memcmp(d.data(), "D2RLBIN", 7) != 0)
    return false;
  const uint32_t desc = rd<uint32_t>(d, 0x3C);
  const uint32_t n = rd<uint32_t>(d, desc + 12), size = rd<uint32_t>(d, desc + 16);
  const uint32_t at = rd<uint32_t>(d, desc + 36);
  if (size != row_size || static_cast<uint64_t>(at) + static_cast<uint64_t>(n) * size > d.size()) return false;
  rows->insert(rows->end(), d.begin() + at, d.begin() + at + static_cast<size_t>(n) * size);
  *count += n;
  return true;
}
inline std::vector<std::string> split(const std::string& line) {
  std::vector<std::string> out;
  std::string cell;
  std::stringstream s(line);
  while (std::getline(s, cell, '\t')) out.push_back(cell);
  return out;
}
inline bool load(const std::string& dir, Tables* t) {
  *t = Tables{};
  t->bank = 3;
  uint32_t suffixes = 0, prefixes = 0, autos = 0;
  if (!rows_of(dir, "magicsuffix", kAffixRow, &t->affix, &suffixes) ||
      !rows_of(dir, "magicprefix", kAffixRow, &t->affix, &prefixes) ||
      !rows_of(dir, "automagic", kAffixRow, &t->affix, &autos))
    return false;
  t->suffix_at = 0;
  t->prefix_at = suffixes;
  t->auto_at = suffixes + prefixes;
  t->affix_count = suffixes + prefixes + autos;
  if (!rows_of(dir, "properties", kPropertyRow, &t->props, &t->props_count) ||
      !rows_of(dir, "weapons", kItemsRow, &t->items, &t->items_count) ||
      !rows_of(dir, "armor", kItemsRow, &t->items, &t->items_count) ||
      !rows_of(dir, "misc", kItemsRow, &t->items, &t->items_count) ||
      !rows_of(dir, "itemtypes", kTypesRow, &t->types, &t->types_count) ||
      !rows_of(dir, "uniqueitems", kUniqueRow, &t->unique, &t->unique_count) ||
      !rows_of(dir, "setitems", kSetItemRow, &t->sets, &t->sets_count) ||
      !rows_of(dir, "qualityitems", kQualityRow, &t->quality, &t->quality_count) ||
      !rows_of(dir, "raresuffix", kRareNameRow, &t->rare_names, &t->rare_suffix_count) ||
      !rows_of(dir, "rareprefix", kRareNameRow, &t->rare_names, &t->rare_prefix_count))
    return false;
  // The skills: class, levels and item type from the text (the compiled file keeps the ids only).
  std::map<std::string, int> type_of;
  for (uint32_t i = 0; i < t->types_count; ++i) {
    char code[5] = {};
    std::memcpy(code, t->types.data() + static_cast<size_t>(i) * kTypesRow, 4);
    std::string c(code);
    while (!c.empty() && (c.back() == ' ' || c.back() == 0)) c.pop_back();
    if (!c.empty()) type_of[c] = static_cast<int>(i);
  }
  std::ifstream f(dir + "/skills.txt");
  std::string line;
  if (!std::getline(f, line)) return false;
  const std::vector<std::string> head = split(line);
  int cc = -1, rl = -1, ml = -1, it = -1;
  for (size_t i = 0; i < head.size(); ++i) {
    if (head[i] == "charclass") cc = static_cast<int>(i);
    if (head[i] == "reqlevel") rl = static_cast<int>(i);
    if (head[i] == "maxlvl") ml = static_cast<int>(i);
    if (head[i] == "itypea1") it = static_cast<int>(i);
  }
  if (cc < 0 || rl < 0 || ml < 0 || it < 0) return false;
  static const char* const kClasses[8] = {"ama", "sor", "nec", "pal", "bar", "dru", "ass", "war"};
  while (std::getline(f, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    std::vector<std::string> c = split(line);
    c.resize(head.size());
    Skill s;
    for (int k = 0; k < 8; ++k)
      if (c[cc] == kClasses[k]) s.char_class = static_cast<int8_t>(k);
    s.req_level = static_cast<int16_t>(std::atoi(c[rl].c_str()));
    s.max_level = static_cast<int16_t>(std::atoi(c[ml].c_str()));
    const auto found = type_of.find(c[it]);
    s.item_type = c[it].empty() || found == type_of.end() ? 0 : static_cast<int16_t>(found->second);
    t->skills.push_back(s);
  }
  return t->complete();
}

}  // namespace tables

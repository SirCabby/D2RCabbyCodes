// Enemies without immunities or affixes (src/enemies.h): the two rules, and, when the game's tables are at hand
// (D2RCC_EXCEL = the loader's compiled excel folder, where its txt copies lie too), the MonUMod rows: the compiled rows
// are laid out as the plugin reads them, and the affixes it finds are exactly the mods monumod.txt gives a unique pick
// weight to (and are enabled).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "enemies.h"

using namespace d2rcc;

static int g_failures = 0;
#define CHECK(cond)                                                     \
  do {                                                                  \
    if (!(cond)) {                                                      \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
      ++g_failures;                                                     \
    }                                                                   \
  } while (0)

template <typename T>
static T at(const std::vector<uint8_t>& d, size_t offset) {
  T v{};
  if (offset + sizeof(T) <= d.size()) std::memcpy(&v, d.data() + offset, sizeof(T));
  return v;
}

// The rows of a compiled table (a D2RLBIN file has one or more tables: the one with rows of this size).
static bool rows_of(const std::string& path, uint32_t row_size, std::vector<uint8_t>* rows, uint32_t* count) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  const std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  if (d.size() < 0x60 || std::memcmp(d.data(), "D2RLBIN", 7) != 0) return false;
  const uint32_t tables = at<uint32_t>(d, 0x3C), size = at<uint32_t>(d, 0x40);
  for (uint32_t t = tables; t + 0x50 <= tables + size; t += 0x50) {
    const uint32_t n = at<uint32_t>(d, t + 12), each = at<uint32_t>(d, t + 16), where = at<uint32_t>(d, t + 36);
    if (each != row_size || static_cast<uint64_t>(where) + static_cast<uint64_t>(n) * each > d.size()) continue;
    rows->assign(d.begin() + where, d.begin() + where + static_cast<size_t>(n) * each);
    *count = n;
    return true;
  }
  return false;
}

// A txt table: its rows split on tabs (the header first), the trailing "\r" taken off.
static std::vector<std::vector<std::string>> tsv(const std::string& path) {
  std::vector<std::vector<std::string>> out;
  std::ifstream f(path, std::ios::binary);
  std::string line;
  while (std::getline(f, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    std::vector<std::string> cells;
    std::stringstream ss(line);
    std::string cell;
    while (std::getline(ss, cell, '\t')) cells.push_back(cell);
    if (!line.empty() && line.back() == '\t') cells.emplace_back();
    out.push_back(cells);
  }
  return out;
}

static int column(const std::vector<std::string>& header, const char* name) {
  for (size_t i = 0; i < header.size(); ++i)
    if (header[i] == name) return static_cast<int>(i);
  return -1;
}

int main() {
  // Immunities: an immunity (the total, or the base, at 100 or more) of an enemy under the switch is at most 0.
  CHECK(enemies::resistance(true, true, 100, 100, 100) == 0);
  CHECK(enemies::resistance(true, true, 150, 75, 150) == 0);   // an immunity by a mod (Cold Enchanted on 75)
  CHECK(enemies::resistance(true, true, 60, 110, 60) == 0);    // an immunity a curse brought below 100
  CHECK(enemies::resistance(true, true, -40, 110, -40) == -40);  // ... below 0: what the curse took it to stays
  CHECK(enemies::resistance(true, true, 99, 99, 99) == 99);    // no immunity: the game's
  CHECK(enemies::resistance(true, true, 75, 50, 75) == 75);
  CHECK(enemies::resistance(true, true, 0, 0, 0) == 0);
  CHECK(enemies::resistance(false, true, 100, 100, 100) == 100);  // the switch off: the game's
  CHECK(enemies::resistance(true, false, 100, 100, 100) == 100);  // a pet of yours, a player: the game's
  CHECK(enemies::resistance(true, true, 120, 120, 95) == 0);   // whatever the routine made of it
  for (int r = -200; r <= 300; r += 7)
    CHECK(enemies::resistance(true, true, r, r, r) == (r >= enemies::kImmune ? 0 : r));

  // The hover: the client's copy of an enemy's immunity becomes 0 under the switch, nothing else changes.
  CHECK(enemies::displayed(true, true, 100) == 0);
  CHECK(enemies::displayed(true, true, 150) == 0);
  CHECK(enemies::displayed(true, true, 99) == 99);
  CHECK(enemies::displayed(true, true, -25) == -25);
  CHECK(enemies::displayed(false, true, 150) == 150);
  CHECK(enemies::displayed(true, false, 150) == 150);
  CHECK(enemies::kResistStats[0] == 36 && enemies::kResistStats[1] == 37 && enemies::kResistStats[2] == 39 &&
        enemies::kResistStats[3] == 41 && enemies::kResistStats[4] == 43 && enemies::kResistStats[5] == 45);

  // Affixes: only an affix, only an enemy's, only under the switch.
  CHECK(enemies::skips(true, true, true));
  CHECK(!enemies::skips(false, true, true));
  CHECK(!enemies::skips(true, false, true));
  CHECK(!enemies::skips(true, true, false));
  uint8_t row[enemies::kMonUModRowSize] = {};
  CHECK(!enemies::row_is_affix(row));
  CHECK(!enemies::row_is_affix(nullptr));
  row[enemies::kMonUModUniquePick + 4] = 6;  // a Hell weight alone (Magic Resistant's: none in Normal)
  CHECK(!enemies::row_is_affix(row));        // ... not enabled
  row[enemies::kMonUModEnabled] = 1;
  CHECK(enemies::row_is_affix(row));

  // A monster's list with its affixes taken out: the rest in their order, the tail zeroed, a 0 ending the list.
  {
    constexpr uint64_t kAffixes = (1ull << 5) | (1ull << 9) | (1ull << 30);
    uint8_t mods[9] = {1, 5, 2, 9, 3, 4, 30, 21, 0};
    CHECK(enemies::strip_affixes(mods, kAffixes) == 3);
    const uint8_t want[9] = {1, 2, 3, 4, 21, 0, 0, 0, 0};
    CHECK(std::memcmp(mods, want, 9) == 0);
    uint8_t none[9] = {1, 2, 3, 4, 16, 0, 9, 0, 0};  // past the 0 is not the list's
    CHECK(enemies::strip_affixes(none, kAffixes) == 0);
    const uint8_t same[9] = {1, 2, 3, 4, 16, 0, 0, 0, 0};
    CHECK(std::memcmp(none, same, 9) == 0);
    uint8_t full[9] = {5, 9, 30, 5, 9, 30, 5, 9, 30};
    CHECK(enemies::strip_affixes(full, kAffixes) == 9);
    const uint8_t empty[9] = {};
    CHECK(std::memcmp(full, empty, 9) == 0);
    uint8_t high[9] = {200, 5, 0};  // an id past the table's is no affix
    CHECK(enemies::strip_affixes(high, kAffixes) == 1 && high[0] == 200 && high[1] == 0);
  }

  // A list left empty keeps the placeholder only when it had affixes or a roller was refused one for the monster.
  {
    uint8_t emptied[9] = {};
    CHECK(enemies::keep_placeholder(emptied, 2, false) && emptied[0] == enemies::kPlaceholderMod && emptied[1] == 0);
    uint8_t refused[9] = {};
    CHECK(enemies::keep_placeholder(refused, 0, true) && refused[0] == enemies::kPlaceholderMod);
    uint8_t vanilla[9] = {};  // the Ancients in Normal: no mods of the game's own, nothing refused
    CHECK(!enemies::keep_placeholder(vanilla, 0, false) && vanilla[0] == 0);
    uint8_t kept[9] = {16, 0};  // a champion keeps its type: nothing to add
    CHECK(!enemies::keep_placeholder(kept, 1, true) && kept[0] == 16 && kept[1] == 0);
  }

  // Over the game's tables.
  const char* dir = std::getenv("D2RCC_EXCEL");
  std::vector<uint8_t> rows;
  uint32_t count = 0;
  if (!dir || !*dir || !rows_of(std::string(dir) + "/monumod.bin", enemies::kMonUModRowSize, &rows, &count)) {
    std::printf("the game's tables are not at hand (D2RCC_EXCEL) - its MonUMod rows were not checked\n");
  } else {
    const auto txt = tsv(std::string(dir) + "/monumod.txt");
    CHECK(txt.size() > 1);
    const int id_col = txt.empty() ? -1 : column(txt[0], "id");
    const int on_col = txt.empty() ? -1 : column(txt[0], "enabled");
    const int name_col = txt.empty() ? -1 : column(txt[0], "uniquemod");
    const int pick_col[3] = {txt.empty() ? -1 : column(txt[0], "upick"), txt.empty() ? -1 : column(txt[0], "upick (N)"),
                             txt.empty() ? -1 : column(txt[0], "upick (H)")};
    CHECK(id_col >= 0 && on_col >= 0 && name_col >= 0 && pick_col[0] >= 0 && pick_col[1] >= 0 && pick_col[2] >= 0);
    CHECK(count > 0 && count <= static_cast<uint32_t>(enemies::kMaxMods));
    std::set<int> wanted, found;
    std::string names;
    for (size_t r = 1; r < txt.size(); ++r) {
      const auto& cells = txt[r];
      const auto cell = [&](int c) { return c >= 0 && c < static_cast<int>(cells.size()) ? cells[c] : std::string(); };
      if (cell(id_col).empty()) continue;
      const int id = std::atoi(cell(id_col).c_str());
      bool picked = false;
      for (int c : pick_col) picked = picked || std::atoi(cell(c).c_str()) > 0;
      if (picked && std::atoi(cell(on_col).c_str()) != 0) {
        wanted.insert(id);
        names += (names.empty() ? "" : ", ") + cell(name_col);
      }
    }
    for (uint32_t i = 0; i < count; ++i) {
      const uint8_t* p = rows.data() + static_cast<size_t>(i) * enemies::kMonUModRowSize;
      uint32_t id = 0;
      std::memcpy(&id, p, sizeof(id));
      CHECK(id == i);  // a row is its mod's id
      if (enemies::row_is_affix(p)) found.insert(static_cast<int>(i));
    }
    CHECK(found == wanted);
    // 3.3: the thirteen, and no champion type, killself, a summon's own or a unique's base mods among them.
    CHECK(found == (std::set<int>{5, 6, 7, 8, 9, 17, 18, 25, 26, 27, 28, 29, 30}));
    // The placeholder is an enabled mod that is no affix, no champion type (cpick) and in 3.3 "rage".
    CHECK(enemies::kPlaceholderMod < count && !found.count(enemies::kPlaceholderMod));
    if (enemies::kPlaceholderMod < count) {
      const uint8_t* p = rows.data() + static_cast<size_t>(enemies::kPlaceholderMod) * enemies::kMonUModRowSize;
      CHECK(p[enemies::kMonUModEnabled] == 1);
      CHECK(p[0x0E] == 0 && p[0x0F] == 0 && p[0x10] == 0 && p[0x11] == 0 && p[0x12] == 0 && p[0x13] == 0);
    }
    bool rage = false;
    for (size_t r = 1; r < txt.size(); ++r)
      if (txt[r].size() > static_cast<size_t>(name_col) && txt[r].size() > static_cast<size_t>(id_col) &&
          std::atoi(txt[r][id_col].c_str()) == enemies::kPlaceholderMod)
        rage = txt[r][name_col] == "rage";
    CHECK(rage);
    // No super unique has it among its own mods.
    const auto su = tsv(std::string(dir) + "/superuniques.txt");
    int with = 0;
    if (!su.empty()) {
      const int mods[3] = {column(su[0], "Mod1"), column(su[0], "Mod2"), column(su[0], "Mod3")};
      for (size_t r = 1; r < su.size(); ++r)
        for (int c : mods)
          if (c >= 0 && c < static_cast<int>(su[r].size()) && std::atoi(su[r][c].c_str()) == enemies::kPlaceholderMod)
            ++with;
    }
    CHECK(!su.empty() && with == 0);
    std::printf("the game's MonUMod rows: %u mods; %zu affixes (%s)\n", count, found.size(), names.c_str());
  }

  if (g_failures) {
    std::printf("%d failure(s)\n", g_failures);
    return 1;
  }
  std::printf("test_enemies: ok\n");
  return 0;
}

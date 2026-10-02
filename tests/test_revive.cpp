// Permanent revives (src/revive.h): whose killself timer is left alone. The rule, and, when the game's tables are at
// hand (D2RCC_EXCEL = the loader's compiled excel folder, where its txt copies lie too), the Skills rows' pet type
// column against skills.txt and pettype.txt: every summoning skill's compiled row names its pet type there, Revive's
// "revive", which no other skill's pets are kept under but Revive's.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include "revive.h"

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
  // The rule: only a monster the local player owns, under the switch, in its pet list of Revive's pet type.
  constexpr int kRevive = 6;  // 3.3's (pettype.txt "revive")
  CHECK(revive::keeps(true, true, kRevive, kRevive));
  CHECK(!revive::keeps(false, true, kRevive, kRevive));  // the switch off: the game's
  CHECK(!revive::keeps(true, false, kRevive, kRevive));  // someone else's monster, a hostile one
  CHECK(!revive::keeps(true, true, 0, kRevive));         // in no pet list: an item's Reanimate As
  CHECK(!revive::keeps(true, true, 8, kRevive));         // a Decoy (dopplezon)
  CHECK(!revive::keeps(true, true, 4, kRevive));         // a skeleton
  CHECK(!revive::keeps(true, true, kRevive, -1));        // Revive's pet type not read yet
  CHECK(!revive::keeps(true, true, 0, 0));               // ... nor a type 0 taken for it
  CHECK(!revive::keeps(true, true, revive::kMaxPetTypes, revive::kMaxPetTypes));
  for (int type = -1; type < revive::kMaxPetTypes + 2; ++type)
    CHECK(revive::keeps(true, true, type, kRevive) == (type == kRevive));

  // Over the game's tables.
  const char* dir = std::getenv("D2RCC_EXCEL");
  std::vector<uint8_t> skills;
  uint32_t skill_count = 0;
  if (!dir || !*dir || !rows_of(std::string(dir) + "/skills.bin", revive::kSkillsRowSize, &skills, &skill_count)) {
    std::printf("the game's tables are not at hand (D2RCC_EXCEL) - its Skills rows were not checked\n");
  } else {
    const auto skill_txt = tsv(std::string(dir) + "/skills.txt");
    const auto pet_txt = tsv(std::string(dir) + "/pettype.txt");
    CHECK(skill_txt.size() > 1 && pet_txt.size() > 1);
    const int id_col = skill_txt.empty() ? -1 : column(skill_txt[0], "*Id");
    const int type_col = skill_txt.empty() ? -1 : column(skill_txt[0], "pettype");
    const int name_col = skill_txt.empty() ? -1 : column(skill_txt[0], "skill");
    CHECK(id_col >= 0 && type_col >= 0 && name_col >= 0);
    // pettype.txt's rows, in order: a pet type is its row.
    std::vector<std::string> pet_types;
    for (size_t r = 1; r < pet_txt.size(); ++r)
      if (!pet_txt[r].empty() && !pet_txt[r][0].empty()) pet_types.push_back(pet_txt[r][0]);
    CHECK(pet_types.size() > static_cast<size_t>(kRevive) && pet_types.size() < static_cast<size_t>(revive::kMaxPetTypes));
    int summons = 0, revive_kind = 0, revive_row = -1;
    if (id_col >= 0 && type_col >= 0 && name_col >= 0) {
      for (size_t r = 1; r < skill_txt.size(); ++r) {
        const auto& row = skill_txt[r];
        if (static_cast<int>(row.size()) <= type_col || row[type_col].empty() || row[id_col].empty()) continue;
        const int id = std::atoi(row[id_col].c_str());
        CHECK(id >= 0 && static_cast<uint32_t>(id) < skill_count);
        if (id < 0 || static_cast<uint32_t>(id) >= skill_count) continue;
        const size_t base = static_cast<size_t>(id) * revive::kSkillsRowSize;
        int type = -1;
        for (size_t t = 0; t < pet_types.size(); ++t)
          if (pet_types[t] == row[type_col]) type = static_cast<int>(t);
        // The compiled row is the skill's (its id first), and names the pet type the txt does where Revive reads it
        // (Bone Wall's and Bone Prison's "none" too: row 0).
        CHECK(at<uint16_t>(skills, base) == id);
        CHECK(type >= 0 && at<uint8_t>(skills, base + revive::kSkillsPetType) == type);
        if (type > 0) ++summons;
        if (row[type_col] == "revive") {
          ++revive_kind;
          revive_row = id;
        }
      }
    }
    // Revive alone keeps its pets under "revive", and its row (95) says so.
    CHECK(revive_kind == 1 && revive_row == revive::kReviveSkill);
    CHECK(at<uint8_t>(skills, static_cast<size_t>(revive::kReviveSkill) * revive::kSkillsRowSize + revive::kSkillsPetType) ==
          kRevive);
    CHECK(pet_types.size() > static_cast<size_t>(kRevive) && pet_types[kRevive] == "revive");
    std::printf("the game's Skills rows: %u skills, %d with a pet type; Revive's pets are pet type %d\n", skill_count,
                summons, kRevive);
  }

  if (g_failures) {
    std::printf("%d failure(s)\n", g_failures);
    return 1;
  }
  std::printf("test_revive: ok\n");
  return 0;
}

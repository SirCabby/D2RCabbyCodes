// Ignore item requirements (src/requirements.h): what the hook answers, the restricted-socket test as the game's
// requirement test takes it, and the class test it asks after level, strength and dexterity. When the game's tables are
// at hand (D2RCC_EXCEL = the loader's compiled excel folder, where its txt copies lie too), the rows the class test reads
// against their txt: an ItemTypes row's class and body locations, a Hireling row's monster class, act and the class whose
// items it may use, and the mercenary's pet type.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include "requirements.h"

using namespace d2rcc;
using requirements::Later;
using requirements::Who;

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

static std::string cell(const std::vector<std::string>& row, int c) {
  return c >= 0 && static_cast<size_t>(c) < row.size() ? row[c] : std::string();
}

// What the routine read in 3.3 (sites.cpp reads them from its code; the test holds the compiled rows to them).
constexpr int kNone = 8;                 // a class at or above it is none
constexpr uint32_t kTypeRow = 0xE8;      // an ItemTypes row: its two body locations, its class
constexpr size_t kBody1 = 0x0A, kBody2 = 0x0B, kClass = 0x20;
constexpr uint32_t kHirelingRow = 0x150;  // a Hireling row: its version, monster class, act, the class it may use
constexpr size_t kVersion = 0x00, kMonster = 0x08, kAct = 0x0C, kEquivalent = 0x8C;
constexpr int kMercenaryPet = 7;         // the client's mercenary check asks its pet list for this pet type
// The classes, in the compiled order (charstats.txt less its "Expansion" divider): the codes the txt columns use.
const char* const kClasses[] = {"ama", "sor", "nec", "pal", "bar", "dru", "ass", "war"};
// The body locations (bodylocs.txt's rows, by their codes).
const char* const kBodyLocs[] = {"none", "head", "neck", "tors", "rarm", "larm", "rrin", "lrin", "belt", "feet", "glov"};

static int class_of(const std::string& code) {
  for (int c = 0; c < 8; ++c)
    if (code == kClasses[c]) return c;
  return -1;
}

static int body_loc_of(const std::string& code) {
  if (code.empty()) return 0;
  for (int b = 0; b < static_cast<int>(sizeof(kBodyLocs) / sizeof(kBodyLocs[0])); ++b)
    if (code == kBodyLocs[b]) return b;
  return -1;
}

int main() {
  // What the hook answers.
  const Later usable{true, false, false, true};
  CHECK(!requirements::answer(false, false, false, false, false, usable));  // not ignoring: the game's no
  CHECK(requirements::answer(false, true, true, true, true, usable));       // ... and its yes
  CHECK(requirements::answer(true, true, false, false, false, Later{}));     // a yes stays a yes
  CHECK(!requirements::answer(true, false, true, true, true, usable));       // all three met: a later test said no
  for (int unmet = 1; unmet < 8; ++unmet) {
    const bool s = !(unmet & 1), d = !(unmet & 2), l = !(unmet & 4);
    CHECK(requirements::answer(true, false, s, d, l, usable));
    CHECK(!requirements::answer(true, false, s, d, l, Later{false, false, false, true}));  // unidentified
    CHECK(!requirements::answer(true, false, s, d, l, Later{true, true, false, true}));    // an empty tome
    CHECK(!requirements::answer(true, false, s, d, l, Later{true, false, true, true}));    // restricted sockets
    CHECK(!requirements::answer(true, false, s, d, l, Later{true, false, false, false}));  // another class's item
  }

  // The restricted-socket test: the body location asked about, or both of the type's.
  CHECK(requirements::restricted(true, true, false, false));
  CHECK(!requirements::restricted(true, false, true, true));
  CHECK(requirements::restricted(false, false, true, true));
  CHECK(!requirements::restricted(false, true, true, false));
  CHECK(!requirements::restricted(false, true, false, true));
  CHECK(!requirements::restricted(false, false, false, false));

  // The class test.
  constexpr int kAma = 0, kSor = 1, kBar = 4, kAss = 6;
  for (int type_class : {kNone, 255, -1})  // no class: anyone
    for (Who who : {Who::kPlayer, Who::kMonster, Who::kOther})
      CHECK(requirements::class_ok(type_class, kNone, who, kSor, true, true, -1));
  CHECK(requirements::class_ok(kSor, kNone, Who::kPlayer, kSor, false, false, -1));   // a Sorceress's orb
  CHECK(!requirements::class_ok(kAss, kNone, Who::kPlayer, kSor, false, false, -1));  // ... not a claw
  CHECK(requirements::class_ok(kAma, kNone, Who::kMonster, 271, true, true, kAma));   // the Rogue's Amazon bow
  CHECK(!requirements::class_ok(kSor, kNone, Who::kMonster, 271, true, true, kAma));  // ... not an orb
  CHECK(requirements::class_ok(kBar, kNone, Who::kMonster, 561, true, true, kBar));   // the Barbarian's helm
  CHECK(!requirements::class_ok(kAma, kNone, Who::kMonster, 338, true, true, -1));    // the Desert Mercenary: none
  CHECK(!requirements::class_ok(kAma, kNone, Who::kMonster, 271, true, false, kAma));  // no mercenary flag
  CHECK(requirements::class_ok(kAma, kNone, Who::kMonster, 5, false, false, -1));     // a monster that is no hireling
  CHECK(!requirements::class_ok(kAma, kNone, Who::kOther, kAma, false, false, -1));   // any other unit

  // Over the game's tables.
  const char* dir = std::getenv("D2RCC_EXCEL");
  std::vector<uint8_t> types, hirelings;
  uint32_t type_count = 0, hireling_count = 0;
  if (!dir || !*dir || !rows_of(std::string(dir) + "/itemtypes.bin", kTypeRow, &types, &type_count) ||
      !rows_of(std::string(dir) + "/hireling.bin", kHirelingRow, &hirelings, &hireling_count)) {
    std::printf("the game's tables are not at hand (D2RCC_EXCEL) - its ItemTypes and Hireling rows were not checked\n");
  } else {
    // Each compiled ItemTypes row (by its code) has the txt's class and body locations where the test reads them.
    const auto type_txt = tsv(std::string(dir) + "/itemtypes.txt");
    CHECK(type_txt.size() > 1);
    const int code_col = column(type_txt[0], "Code"), class_col = column(type_txt[0], "Class");
    const int body1_col = column(type_txt[0], "BodyLoc1"), body2_col = column(type_txt[0], "BodyLoc2");
    CHECK(code_col >= 0 && class_col >= 0 && body1_col >= 0 && body2_col >= 0);
    int class_only = 0, worn = 0, matched = 0;
    for (uint32_t r = 0; r < type_count; ++r) {
      const size_t base = static_cast<size_t>(r) * kTypeRow;
      char code[5] = {};
      std::memcpy(code, types.data() + base, 4);
      for (int i = 3; i >= 0 && (code[i] == ' ' || code[i] == 0); --i) code[i] = 0;
      const std::vector<std::string>* row = nullptr;
      for (size_t t = 1; t < type_txt.size(); ++t)
        if (cell(type_txt[t], code_col) == code) row = &type_txt[t];
      if (!row || !*code) continue;
      ++matched;
      const int cls = class_of(cell(*row, class_col));
      const uint8_t compiled = at<uint8_t>(types, base + kClass);
      CHECK(cls >= 0 ? compiled == cls : compiled >= kNone);
      CHECK(at<uint8_t>(types, base + kBody1) == body_loc_of(cell(*row, body1_col)));
      CHECK(at<uint8_t>(types, base + kBody2) == body_loc_of(cell(*row, body2_col)));
      if (cls >= 0) ++class_only;
      if (at<uint8_t>(types, base + kBody1)) ++worn;
    }
    CHECK(matched > 100 && class_only == 20 && worn > 40);
    std::printf("the game's ItemTypes rows: %u (%d by code), %d of one class alone, %d worn somewhere\n", type_count,
                matched, class_only, worn);

    // Each compiled Hireling row, in the txt's order: its version, monster class, act and the class it may use.
    const auto hire_txt = tsv(std::string(dir) + "/hireling.txt");
    CHECK(hire_txt.size() > 1);
    const int version_col = column(hire_txt[0], "Version"), monster_col = column(hire_txt[0], "Class");
    const int act_col = column(hire_txt[0], "Act"), equivalent_col = column(hire_txt[0], "equivalentcharclass");
    CHECK(version_col >= 0 && monster_col >= 0 && act_col >= 0 && equivalent_col >= 0);
    uint32_t row = 0;
    int rogues = 0, barbarians = 0, others = 0;
    for (size_t t = 1; t < hire_txt.size() && row < hireling_count; ++t) {
      const auto& txt = hire_txt[t];
      if (cell(txt, monster_col).empty()) continue;
      const size_t base = static_cast<size_t>(row++) * kHirelingRow;
      CHECK(at<uint16_t>(hirelings, base + kVersion) == std::atoi(cell(txt, version_col).c_str()));
      CHECK(at<int32_t>(hirelings, base + kMonster) == std::atoi(cell(txt, monster_col).c_str()));
      CHECK(at<int32_t>(hirelings, base + kAct) == std::atoi(cell(txt, act_col).c_str()));
      CHECK(at<int32_t>(hirelings, base + kAct) != 0);  // every hireling's act: the class test's "a hireling"
      const int equivalent = class_of(cell(txt, equivalent_col));
      CHECK(at<int8_t>(hirelings, base + kEquivalent) == (equivalent >= 0 ? equivalent : -1));
      if (equivalent == 0) ++rogues;
      else if (equivalent == 4) ++barbarians;
      else ++others;
    }
    CHECK(row == hireling_count && rogues > 0 && barbarians > 0 && others > 0);
    std::printf("the game's Hireling rows: %u (%d may use the Amazon's items, %d the Barbarian's, %d none)\n",
                hireling_count, rogues, barbarians, others);

    // The mercenary's pet type: pettype.txt's row of that number is "hireable".
    const auto pet_txt = tsv(std::string(dir) + "/pettype.txt");
    std::vector<std::string> pet_types;
    for (size_t r = 1; r < pet_txt.size(); ++r)
      if (!pet_txt[r].empty() && !pet_txt[r][0].empty()) pet_types.push_back(pet_txt[r][0]);
    CHECK(pet_types.size() > static_cast<size_t>(kMercenaryPet) && pet_types[kMercenaryPet] == "hireable");
  }

  if (g_failures) {
    std::printf("%d failure(s)\n", g_failures);
    return 1;
  }
  std::printf("test_requirements: ok\n");
  return 0;
}

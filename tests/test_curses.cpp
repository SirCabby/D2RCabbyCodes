// Cannot be cursed (src/curses.h): which states are curses to keep off. The rule over rows made here, and, when
// the game's tables are at hand (D2RCC_EXCEL = the loader's compiled excel folder), over the game's own States
// rows: the thirteen curses of 3.3 and no shrine's bonus, and the state the Cursed monster modifier's skill puts on.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "curses.h"

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

static void set_flags(std::vector<uint8_t>* rows, uint32_t row, uint32_t flags) {
  std::memcpy(rows->data() + static_cast<size_t>(row) * curses::kStatesRowSize + curses::kStatesFlags, &flags,
              sizeof(flags));
}

int main() {
  using curses::kFlagCurable;
  using curses::kFlagCurse;
  // The rule: a curse that can be cured. A shrine's bonus is a curse that cannot; freeze and poison can be cured
  // and are no curses.
  CHECK(curses::keeps_off(kFlagCurse | kFlagCurable));
  CHECK(curses::keeps_off(kFlagCurse | kFlagCurable | 0x80000200u));
  CHECK(!curses::keeps_off(kFlagCurse));
  CHECK(!curses::keeps_off(kFlagCurable));
  CHECK(!curses::keeps_off(0));
  CHECK(!curses::keeps_off(~(kFlagCurse | kFlagCurable)));

  // Rows made here: 140 states, the three that say the flags are where they are taken to be, two curses more and
  // a shrine's bonus.
  {
    std::vector<uint8_t> rows(140 * curses::kStatesRowSize, 0);
    set_flags(&rows, 1, kFlagCurable | 0x8004u);
    set_flags(&rows, 2, kFlagCurable);
    set_flags(&rows, 9, kFlagCurse | kFlagCurable);
    set_flags(&rows, 60, kFlagCurse | kFlagCurable | 0x200u);
    set_flags(&rows, 128, kFlagCurse | 0x100000u);
    set_flags(&rows, 139, kFlagCurse | kFlagCurable);
    curses::Curses found;
    CHECK(curses::rows_as_expected(rows.data(), 140));
    CHECK(curses::curses_of(rows.data(), 140, &found));
    CHECK(found.count == 3 && found.states[0] == 9 && found.states[1] == 60 && found.states[2] == 139);
    CHECK(found.has(9) && found.has(60) && found.has(139) && !found.has(128) && !found.has(2) && !found.has(1));
    CHECK(!found.has(-1) && !found.has(140));
    // Fewer rows than the table has: what lies past them is not read.
    CHECK(curses::curses_of(rows.data(), 100, &found) && found.count == 2);
    // Flags that are somewhere else: poison a curse, or Amplify Damage none - nothing is taken for a curse.
    std::vector<uint8_t> other = rows;
    set_flags(&other, 2, kFlagCurse | kFlagCurable);
    CHECK(!curses::rows_as_expected(other.data(), 140) && !curses::curses_of(other.data(), 140, &found));
    CHECK(found.count == 0);
    other = rows;
    set_flags(&other, 9, kFlagCurse);
    CHECK(!curses::curses_of(other.data(), 140, &found));
    other = rows;
    set_flags(&other, 1, 0);
    CHECK(!curses::curses_of(other.data(), 140, &found));
    CHECK(!curses::curses_of(rows.data(), 9, &found));  // too few rows to say
    CHECK(!curses::curses_of(nullptr, 140, &found));
    // More curses than fit: not half of them.
    std::vector<uint8_t> many(static_cast<size_t>(curses::kMaxStates) * curses::kStatesRowSize, 0);
    for (uint32_t i = 0; i < static_cast<uint32_t>(curses::kMaxStates); ++i) set_flags(&many, i, kFlagCurse | kFlagCurable);
    set_flags(&many, 1, kFlagCurable);
    set_flags(&many, 2, kFlagCurable);
    CHECK(!curses::curses_of(many.data(), curses::kMaxStates, &found));
  }

  // Over the game's tables.
  const char* dir = std::getenv("D2RCC_EXCEL");
  std::vector<uint8_t> states, skills;
  uint32_t state_count = 0, skill_count = 0;
  if (!dir || !*dir || !rows_of(std::string(dir) + "/states.bin", curses::kStatesRowSize, &states, &state_count)) {
    std::printf("the game's tables are not at hand (D2RCC_EXCEL) - its States rows were not checked\n");
  } else {
    curses::Curses found;
    CHECK(curses::curses_of(states.data(), state_count, &found));
    // 3.3: Amplify Damage, Weaken, Dim Vision, Taunt, Iron Maiden, Terror, Attract, Life Tap, Confuse, Decrepify,
    // Lower Resist, Defense Curse, Blood Mana.
    const int expect[] = {9, 19, 23, 27, 55, 56, 57, 58, 59, 60, 61, 113, 114};
    CHECK(found.count == static_cast<int>(sizeof(expect) / sizeof(expect[0])));
    for (size_t i = 0; i < sizeof(expect) / sizeof(expect[0]) && i < static_cast<size_t>(found.count); ++i)
      CHECK(found.states[i] == expect[i]);
    // Never a shrine's bonus (128 .. 137), Battle Cry (89), Bloodlust (141) or Cloak of Shadows' (156): curses
    // by the flag, not to be cured; nor freeze, poison, cold.
    for (int s = 128; s <= 137; ++s) {
      CHECK(!found.has(s));
      CHECK((curses::state_flags(states.data(), static_cast<uint32_t>(s)) & kFlagCurse) != 0);
    }
    for (int s : {1, 2, 11, 89, 141, 156}) CHECK(!found.has(s));
    // The Cursed monster modifier casts Amplify Damage (skill 66): its row names the state it puts on, where the
    // modifier's step reads it (the row's +0xA2).
    if (rows_of(std::string(dir) + "/skills.bin", curses::kSkillsRowSize, &skills, &skill_count)) {
      CHECK(skill_count > 66);
      int16_t state = -1;
      std::memcpy(&state, skills.data() + 66 * static_cast<size_t>(curses::kSkillsRowSize) + 0xA2, sizeof(state));
      CHECK(state == 9 && found.has(state));
    } else {
      std::printf("FAIL: no Skills rows of %u bytes\n", curses::kSkillsRowSize);
      ++g_failures;
    }
    std::printf("the game's States rows: %u states, %d curses kept off\n", state_count, found.count);
  }

  if (g_failures) {
    std::printf("%d failure(s)\n", g_failures);
    return 1;
  }
  std::printf("test_curses: ok\n");
  return 0;
}

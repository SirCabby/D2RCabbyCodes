// The home town (src/hometown.h): each act's town and its waypoint from the game's tables, which town is home (where
// a portal leads and a death wakes), and what a portal's use comes to. Over rows made here, and, when the game's
// tables are at hand (D2RCC_EXCEL = the loader's compiled excel folder), over the game's own ActInfo and Levels rows.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "hometown.h"

using namespace d2rcc;
using hometown::Towns;
using hometown::Use;

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

// A small game: three acts, their towns levels 1, 5 and 9, twelve levels.
struct Small {
  std::vector<uint8_t> actinfo, levels;
  Small() : actinfo(3 * hometown::kActInfoRowSize, 0), levels(12 * hometown::kLevelsRowSize, 0) {
    const int32_t towns[3] = {1, 5, 9};
    for (int a = 0; a < 3; ++a)
      std::memcpy(actinfo.data() + static_cast<size_t>(a) * hometown::kActInfoRowSize, &towns[a], sizeof(int32_t));
    for (int i = 0; i < 12; ++i) {
      level(i)[hometown::kLevelsAct] = static_cast<uint8_t>(i < 5 ? 0 : i < 9 ? 1 : 2);
      level(i)[hometown::kLevelsWaypoint] = hometown::kNoWaypoint;
    }
    level(1)[hometown::kLevelsWaypoint] = 0;
    level(3)[hometown::kLevelsWaypoint] = 1;
    level(5)[hometown::kLevelsWaypoint] = 2;
    level(9)[hometown::kLevelsWaypoint] = 4;
  }
  uint8_t* level(int i) { return levels.data() + static_cast<size_t>(i) * hometown::kLevelsRowSize; }
  bool read(Towns* out) const { return hometown::towns_of(actinfo.data(), 3, levels.data(), 12, out); }
};

int main() {
  // The towns of a small game.
  {
    Small g;
    Towns t;
    CHECK(g.read(&t));
    CHECK(t.acts == 3 && t.levels == 12);
    CHECK(t.level[0] == 1 && t.level[1] == 5 && t.level[2] == 9);
    CHECK(t.waypoint[0] == 0 && t.waypoint[1] == 2 && t.waypoint[2] == 4);
    CHECK(t.act(1) == 0 && t.act(4) == 0 && t.act(5) == 1 && t.act(8) == 1 && t.act(9) == 2 && t.act(11) == 2);
    CHECK(t.act(0) == -1 && t.act(12) == -1 && t.act(-3) == -1);  // no level, past the last, nonsense
    CHECK(t.is_town(1) && t.is_town(5) && t.is_town(9) && !t.is_town(2) && !t.is_town(0) && !t.is_town(12));
  }
  // Tables that do not name a town with a waypoint for every act are not read as ones that do.
  {
    Towns t;
    Small no_waypoint;
    no_waypoint.level(5)[hometown::kLevelsWaypoint] = hometown::kNoWaypoint;
    CHECK(!no_waypoint.read(&t));
    Small other_act;  // a town that is a level of another act
    other_act.level(5)[hometown::kLevelsAct] = 0;
    CHECK(!other_act.read(&t));
    Small falling;  // waypoints that do not rise with the acts
    falling.level(9)[hometown::kLevelsWaypoint] = 1;
    CHECK(!falling.read(&t));
    Small not_first;  // the first act's town has not the first waypoint
    not_first.level(1)[hometown::kLevelsWaypoint] = 1;
    CHECK(!not_first.read(&t));
    Small past;  // a town that is no level of the table
    const int32_t nowhere = 40;
    std::memcpy(past.actinfo.data() + hometown::kActInfoRowSize, &nowhere, sizeof(nowhere));
    CHECK(!past.read(&t));
    Small bad_act;  // a level of an act there is none of
    bad_act.level(7)[hometown::kLevelsAct] = 9;
    CHECK(!bad_act.read(&t));
    Small wide;  // a waypoint past the game's bound
    wide.level(9)[hometown::kLevelsWaypoint] = 0x70;
    CHECK(!wide.read(&t));
    Small g;
    CHECK(!hometown::towns_of(nullptr, 3, g.levels.data(), 12, &t));
    CHECK(!hometown::towns_of(g.actinfo.data(), 3, nullptr, 12, &t));
    CHECK(!hometown::towns_of(g.actinfo.data(), 0, g.levels.data(), 12, &t));
    CHECK(!hometown::towns_of(g.actinfo.data(), 6, g.levels.data(), 12, &t));
    CHECK(!hometown::towns_of(g.actinfo.data(), 3, g.levels.data(), 1, &t));
  }
  // Which act's town is home: where a portal read in act `here` leads, and where a death there wakes.
  {
    using hometown::chosen_act;
    const unsigned all = 0x1F, first_three = 0x07, none = 0;
    // The game's own: the act the player is in, whatever the character has reached.
    for (int here = 0; here < 5; ++here) CHECK(chosen_act(0, here, all, 5) == here);
    // A town chosen and reached.
    for (int here = 0; here < 5; ++here)
      for (int choice = 1; choice <= 5; ++choice) CHECK(chosen_act(choice, here, all, 5) == choice - 1);
    // A town the character has not the waypoint of: the game's own.
    CHECK(chosen_act(5, 1, first_three, 5) == 1);
    CHECK(chosen_act(4, 0, first_three, 5) == 0);
    CHECK(chosen_act(3, 0, first_three, 5) == 2);
    CHECK(chosen_act(1, 2, first_three, 5) == 0);
    for (int choice = 1; choice <= 5; ++choice) CHECK(chosen_act(choice, 3, none, 5) == 3);
    // A classic game has four acts: the fifth cannot be chosen, reached or not.
    CHECK(chosen_act(5, 2, all, 4) == 2);
    CHECK(chosen_act(4, 2, all, 4) == 3);
    // A choice that is none, or an act no player can be in.
    CHECK(chosen_act(-1, 2, all, 5) == 2 && chosen_act(6, 2, all, 5) == 2 && chosen_act(99, 0, all, 5) == 0);
    CHECK(chosen_act(3, -1, all, 5) == -1 && chosen_act(3, 5, all, 5) == 5);
    // A death: the game wakes the player in the town of the act it died in (`here`); it is taken on only when home
    // is another act's town. At home already, or with a home not reached, it stays where the game woke it.
    for (int here = 0; here < 5; ++here) {
      CHECK(chosen_act(here + 1, here, all, 5) == here);
      CHECK(chosen_act(5, here, first_three, 5) == here);
      CHECK(chosen_act(1, here, all, 5) == 0);
    }
  }
  // What a portal's use comes to.
  {
    using hometown::use_of;
    for (int act = 0; act < 5; ++act) {
      CHECK(use_of(act, act, true) == Use::kGameOwn);   // the game's own portals, both ends
      CHECK(use_of(act, act, false) == Use::kGameOwn);
    }
    CHECK(use_of(0, 4, true) == Use::kToTown);    // read in the first act, the home town the fifth's
    CHECK(use_of(4, 0, false) == Use::kToField);  // ... and the way back, from that town
    CHECK(use_of(3, 1, true) == Use::kToTown && use_of(1, 3, false) == Use::kToField);
    // An act that is not known: the game's own use.
    CHECK(use_of(-1, 2, true) == Use::kGameOwn && use_of(2, -1, false) == Use::kGameOwn);
    CHECK(use_of(-1, -1, true) == Use::kGameOwn);
  }

  // Over the game's tables.
  const char* dir = std::getenv("D2RCC_EXCEL");
  std::vector<uint8_t> actinfo, levels;
  uint32_t acts = 0, level_count = 0;
  if (!dir || !*dir || !rows_of(std::string(dir) + "/actinfo.bin", hometown::kActInfoRowSize, &actinfo, &acts) ||
      !rows_of(std::string(dir) + "/levels.bin", hometown::kLevelsRowSize, &levels, &level_count)) {
    std::printf("the game's tables are not at hand (D2RCC_EXCEL) - its ActInfo and Levels rows were not checked\n");
  } else {
    Towns t;
    CHECK(hometown::towns_of(actinfo.data(), acts, levels.data(), level_count, &t));
    // The Rogue Encampment, Lut Gholein, the Kurast Docks, the Pandemonium Fortress, Harrogath: the five levels
    // the game itself takes for towns (its town test, 0x2F0780), and their waypoints.
    const int towns[5] = {1, 40, 75, 103, 109}, waypoints[5] = {0, 9, 18, 27, 30};
    CHECK(t.acts == 5);
    for (int a = 0; a < 5 && a < t.acts; ++a) {
      CHECK(t.level[a] == towns[a]);
      CHECK(t.waypoint[a] == waypoints[a]);
      CHECK(t.is_town(towns[a]) && t.act(towns[a]) == a);
    }
    // A few levels of each act: the Blood Moor, the Moo Moo Farm, the Arcane Sanctuary, Durance of Hate level 3,
    // the Chaos Sanctuary, the Worldstone Chamber.
    CHECK(t.act(2) == 0 && t.act(39) == 0 && t.act(74) == 1 && t.act(102) == 2 && t.act(108) == 3 && t.act(132) == 4);
    CHECK(!t.is_town(2) && !t.is_town(39) && !t.is_town(108));
    int in_towns = 0;
    for (int id = 1; id < t.levels; ++id) in_towns += t.is_town(id) ? 1 : 0;
    CHECK(in_towns == 5);
    std::printf("the game's tables: %d acts, %d levels; the towns %d %d %d %d %d\n", t.acts, t.levels, t.level[0],
                t.level[1], t.level[2], t.level[3], t.level[4]);
  }

  if (g_failures) {
    std::printf("%d failure(s)\n", g_failures);
    return 1;
  }
  std::printf("test_hometown: ok\n");
  return 0;
}

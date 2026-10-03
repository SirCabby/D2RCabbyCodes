// Named enemies and landmarks on the map (src/mapmarks.h): the rules, and, when the game's tables are at hand
// (D2RCC_EXCEL = the loader's compiled excel folder, where its txt copies lie too), the rows they read: which object
// classes have an icon and the waypoints' (the Objects rows' cell where the reveal reads it), which MonStats rows are
// bosses by the rule (the act and quest bosses, the Putrid Defilers not), and in the automap's tile table every
// waypoint row reached as the first fit for its own tiles (the lookup takes the first record that fits).
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "mapmarks.h"

using namespace d2rcc;
using namespace d2rcc::mapmarks;

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

static TileRecord record(int32_t level, int32_t tile, int style, int first, int last, std::vector<int32_t> cells) {
  TileRecord r;
  r.level_type = level;
  r.tile_type = tile;
  r.style = static_cast<uint8_t>(style);
  r.first = static_cast<uint8_t>(first);
  r.last = static_cast<uint8_t>(last);
  for (size_t i = 0; i < cells.size() && i < static_cast<size_t>(kRecordCells); ++i) r.cells[i] = cells[i];
  r.count = static_cast<int32_t>(cells.size());
  return r;
}

int main() {
  // Who is marked: a super unique (2 and 8), a random unique (8 and 1), a champion (0xD: the unique bit too), a
  // Herald, a boss by its row; never a minion of a unique's or a Herald's, nor an ordinary monster.
  CHECK(named(kKindSuperUnique | kKindUnique, false));
  CHECK(named(kKindUnique | 0x1, false));
  CHECK(named(kKindUnique | kKindChampion | 0x1, false));
  CHECK(champion(kKindUnique | kKindChampion | 0x1));
  CHECK(!champion(kKindUnique | 0x1) && !champion(kKindSuperUnique | kKindUnique) && !champion(kKindHerald));
  CHECK(named(kKindHerald, false));
  CHECK(!named(kKindMinion, false));
  CHECK(!named(kKindMinion, true));
  CHECK(!named(kKindHeraldMinion, false));
  CHECK(!named(kKindHeraldMinion | kKindHerald, false));
  CHECK(named(0, true));
  CHECK(!named(0, false));
  // A boss by its row: the boss flag without Rarity, not an NPC.
  CHECK(boss_row(kMonBoss, 0));
  CHECK(!boss_row(kMonBoss, 1));
  CHECK(!boss_row(kMonBoss | kMonNpc, 0));
  CHECK(!boss_row(0, 0));

  // A preset monster's class: a MonStats row, a super unique past them, then the layout's own places.
  constexpr int kMonsters = 751, kSupers = 66;
  CHECK(preset_class(156, kMonsters, kSupers).kind == PresetKind::kMonster && preset_class(156, kMonsters, kSupers).row == 156);
  CHECK(preset_class(kMonsters, kMonsters, kSupers).kind == PresetKind::kSuperUnique);
  CHECK(preset_class(kMonsters, kMonsters, kSupers).row == 0);
  CHECK(preset_class(kMonsters + kSupers - 1, kMonsters, kSupers).row == kSupers - 1);
  CHECK(preset_class(kMonsters + kSupers, kMonsters, kSupers).kind == PresetKind::kNone);
  CHECK(preset_class(kMonsters + kSupers + 0x21, kMonsters, kSupers).kind == PresetKind::kNone);
  CHECK(preset_class(5, 0, kSupers).kind == PresetKind::kNone);

  // Where the automap puts a unit's icon: its pixels / 10 (toward zero), plus 1 and less 3.
  CHECK(pixel_x(10, 4) == 96 && pixel_y(10, 4) == 112);
  CHECK(cell_x(96) == 10 && cell_y(112) == 8);
  CHECK(pixel_x(4, 10) == -96 && cell_x(-96) == -8);
  CHECK(cell_y(-5) == -3 && cell_y(-15) == -4);
  CHECK(cell_x(9) == 1 && cell_x(-9) == 1);

  // The reveal's rules for an icon (3.3's: the stash in Acts 3 and 4, the sewer stairs open, the Arcane Sanctuary's
  // waypoint there).
  const ObjectRules rules{267, 2, 2, 366, 2, 402, 74};
  CHECK(icon_shows(rules, 2, 310, 0, 3, -1));      // a shrine, anywhere
  CHECK(!icon_shows(rules, 2, 0, 0, 3, -1));       // no icon
  CHECK(!icon_shows(rules, 267, 319, 0, 1, -1));   // the stash in the Rogue Encampment
  CHECK(icon_shows(rules, 267, 319, 2, 75, -1));   // ... the Kurast Docks
  CHECK(icon_shows(rules, 267, 319, 3, 103, -1));  // ... the Pandemonium Fortress
  CHECK(!icon_shows(rules, 267, 319, 4, 109, -1)); // ... not Harrogath
  CHECK(icon_shows(rules, 366, 223, 2, 92, -1));   // the sewer stairs of a layout: ahead, closed as they are
  CHECK(icon_shows(rules, 366, 223, 2, 92, 2));    // ... open
  CHECK(!icon_shows(rules, 366, 223, 2, 92, 0));   // ... the game's own unit pass: not while closed
  CHECK(!icon_shows(rules, 402, 307, 1, 46, -1));  // the valley waypoint object elsewhere
  CHECK(icon_shows(rules, 402, 307, 1, 74, -1));   // ... in the Arcane Sanctuary

  // The tile lookup: the first record that fits wins, a style or first sequence of 0xFF fits any.
  {
    const TileRecord recs[] = {
        record(1, 0, 0, 1, 46, {0, 1, 2, 3}),  // plain floor, sequences 1..46 of style 0
        record(1, 0, 0, 54, 54, {307}),        // the waypoint
        record(1, 0, 0xFF, 0xFF, 0, {9}),      // any floor of the level type
        record(2, 0, 0, 54, 54, {307}),        // another level type's
        record(1, 4, 0, 54, 54, {307}),        // another tile type
    };
    const int n = static_cast<int>(sizeof(recs) / sizeof(recs[0]));
    CHECK(first_fit(recs, n, 1, 0, 0, 54) == 1);
    CHECK(record_is(recs[1], 307));
    CHECK(first_fit(recs, n, 1, 0, 0, 10) == 0);
    CHECK(!record_is(recs[0], 307));
    CHECK(first_fit(recs, n, 1, 0, 3, 54) == 2);   // another style: the catch-all
    CHECK(first_fit(recs, n, 1, 0, 0, 55) == 2);
    CHECK(first_fit(recs, n, 1, 4, 0, 54) == 4);
    CHECK(first_fit(recs, n, 3, 0, 0, 54) == -1);
    // A waypoint shadowed by an earlier record that fits is no waypoint tile.
    const TileRecord shadowed[] = {record(1, 0, 0xFF, 0xFF, 0, {9}), record(1, 0, 0, 54, 54, {307})};
    CHECK(first_fit(shadowed, 2, 1, 0, 0, 54) == 0);
    // A record with no count gives its first cell; with several, any of them.
    TileRecord one = record(1, 0, 0, 1, 1, {307});
    one.count = 0;
    CHECK(record_is(one, 307));
    CHECK(!record_is(record(1, 0, 0, 1, 1, {307, 308}), 307));
    CHECK(record_is(record(1, 0, 0, 1, 1, {307, 307}), 307));
    CHECK(!record_is(record(1, 0, 0, 1, 1, {0}), 0));
  }
  CHECK(waypoint_name("WaypointBaal") && waypoint_name("TempleWaypoint") && waypoint_name("InnerHellWaypoint"));
  CHECK(!waypoint_name("DeerShrine") && !waypoint_name("Waypoin") && !waypoint_name(""));

  // A room's own preset units: places in subtiles from the room's corner (a tile is five).
  CHECK(room_subtile(100, 7) == 507 && room_subtile(0, 0) == 0 && room_subtile(-2, 3) == -7);

  // Where two rooms of different areas meet: the line on the first room's side, along what both have.
  {
    const Rect a{10, 20, 8, 8};
    Edge e;
    CHECK(shared_edge(a, Rect{18, 24, 8, 8}, &e));  // to the right, overlapping rows 24..27
    CHECK(e.vertical && e.line == 89 && e.from == 120 && e.to == 140 && e.side == 1);
    CHECK(shared_edge(a, Rect{2, 16, 8, 8}, &e));   // to the left, rows 20..23
    CHECK(e.vertical && e.line == 50 && e.from == 100 && e.to == 120 && e.side == -1);
    CHECK(shared_edge(a, Rect{12, 28, 8, 8}, &e));  // below, columns 12..17
    CHECK(!e.vertical && e.line == 139 && e.from == 60 && e.to == 90 && e.side == 1);
    CHECK(shared_edge(a, Rect{4, 12, 8, 8}, &e));   // above, columns 10..11
    CHECK(!e.vertical && e.line == 100 && e.from == 50 && e.to == 60 && e.side == -1);
    CHECK(!shared_edge(a, Rect{18, 28, 8, 8}, &e));  // a corner only
    CHECK(!shared_edge(a, Rect{19, 20, 8, 8}, &e));  // a tile apart
    CHECK(!shared_edge(a, Rect{12, 22, 2, 2}, &e));  // inside
    CHECK(!shared_edge(a, Rect{18, 20, 0, 8}, &e));  // no size
  }
  // A player walks where neither of the walk mask's two lasting bits is: not a wall (1), not a player-only block (8);
  // objects, doors and units come and go. The blocking default of a subtile in no room (0x27) blocks.
  CHECK(walkable(0) && walkable(0x10) && walkable(0x400) && walkable(0x800) && walkable(0x1000) && walkable(0x2));
  CHECK(!walkable(1) && !walkable(8) && !walkable(0x27) && !walkable(0xFFFF));
  // The openings along a line: walkable subtiles closer than kOpeningJoin apart are one, fewer than kOpeningLeast none.
  {
    const std::vector<Opening> o = openings({40, 41, 42, 43, 7, 8, 9, 10, 11, 12, 60, 70, 71, 9});
    CHECK(o.size() == 2);
    CHECK(o.size() == 2 && o[0].from == 7 && o[0].to == 12 && o[0].cells == 6);
    CHECK(o.size() == 2 && o[1].from == 40 && o[1].to == 43 && o[1].cells == 4);
    const std::vector<Opening> joined = openings({0, 1, 5, 9, 10});  // gaps of 4 join
    CHECK(joined.size() == 1 && joined[0].from == 0 && joined[0].to == 10 && joined[0].cells == 5);
    CHECK(openings({0, 5, 10}).empty());  // gaps of 5 do not: three pockets of one
    CHECK(openings({}).empty() && openings({3, 4}).empty());
    // Its name: the middle, three subtiles back into the area it was found in.
    const Opening mid{100, 111, 12};
    CHECK(exit_place(true, 89, 1, mid).x == 86 && exit_place(true, 89, 1, mid).y == 105);
    CHECK(exit_place(true, 50, -1, mid).x == 53);
    CHECK(exit_place(false, 139, 1, mid).x == 105 && exit_place(false, 139, 1, mid).y == 136);
    CHECK(exit_place(false, 100, -1, mid).y == 103);
  }

  // Which levels roll an object with an icon as their rooms are filled: a group with a chance that names, with a chance
  // of its own, a class with a cell.
  {
    int16_t cell[16] = {};
    cell[5] = 310;  // a shrine
    cell[6] = 0;    // a chest
    ObjGroup rows[4];
    rows[1].classes[0] = 6;  // chests only
    rows[1].chances[0] = 100;
    rows[2].classes[0] = 6;  // a chest or a shrine
    rows[2].chances[0] = 50;
    rows[2].classes[1] = 5;
    rows[2].chances[1] = 50;
    rows[3].classes[0] = 5;  // a shrine never picked
    rows[3].chances[0] = 0;
    const uint8_t none[kObjGroupSlots] = {};
    uint8_t g[kObjGroupSlots] = {1, 0, 0, 0, 0, 0, 0, 0}, c[kObjGroupSlots] = {50, 0, 0, 0, 0, 0, 0, 0};
    CHECK(!rolls_icons(g, c, rows, 4, cell, 16));
    g[1] = 2;
    CHECK(!rolls_icons(g, c, rows, 4, cell, 16));  // the shrine's group has no chance of its own here
    c[1] = 25;
    CHECK(rolls_icons(g, c, rows, 4, cell, 16));
    uint8_t g3[kObjGroupSlots] = {3, 9, 0, 0, 0, 0, 0, 0}, c3[kObjGroupSlots] = {100, 100, 0, 0, 0, 0, 0, 0};
    CHECK(!rolls_icons(g3, c3, rows, 4, cell, 16));  // a shrine at no chance, and a group past the table
    CHECK(!rolls_icons(none, none, rows, 4, cell, 16));
  }

  // Over the game's tables.
  const char* dir = std::getenv("D2RCC_EXCEL");
  std::vector<uint8_t> objects, monsters, supers;
  uint32_t object_count = 0, monster_count = 0, super_count = 0;
  if (!dir || !*dir || !rows_of(std::string(dir) + "/objects.bin", 0x168, &objects, &object_count) ||
      !rows_of(std::string(dir) + "/monstats.bin", 0x1FC, &monsters, &monster_count) ||
      !rows_of(std::string(dir) + "/superuniques.bin", 0x3C, &supers, &super_count)) {
    std::printf("the game's tables are not at hand (D2RCC_EXCEL) - their rows were not checked\n");
  } else {
    // Objects: the icon where the reveal reads it (+0x164), the waypoints that have one alike.
    int icons = 0, waypoint_cell = 0, waypoint_icons = 0;
    for (uint32_t i = 0; i < object_count; ++i) {
      const size_t base = static_cast<size_t>(i) * 0x168;
      CHECK(at<uint16_t>(objects, base) == i);  // the row is its class
      const int32_t cell = at<int32_t>(objects, base + 0x164);
      if (cell <= 0) continue;
      ++icons;
      char name[0x41] = {};
      std::memcpy(name, objects.data() + base + 2, 0x40);
      if (!waypoint_name(name)) continue;
      ++waypoint_icons;
      CHECK(!waypoint_cell || waypoint_cell == cell);
      waypoint_cell = cell;
    }
    CHECK(icons > 100 && icons < 300);
    CHECK(waypoint_cell == 307 && waypoint_icons >= 4);
    CHECK(at<int32_t>(objects, 267 * 0x168 + 0x164) == 319);  // the stash
    CHECK(at<int32_t>(objects, 366 * 0x168 + 0x164) == 223);  // the sewer stairs
    CHECK(at<int32_t>(objects, 402 * 0x168 + 0x164) == 307);  // the Arcane Sanctuary's waypoint
    CHECK(at<int32_t>(objects, 2 * 0x168 + 0x164) == 310);    // a shrine
    // MonStats: the bosses by the rule.
    std::vector<int> bosses;
    for (uint32_t i = 0; i < monster_count; ++i) {
      const size_t base = static_cast<size_t>(i) * 0x1FC;
      if (boss_row(at<uint32_t>(monsters, base + 0x3C), at<uint8_t>(monsters, base + 0x64))) bosses.push_back(static_cast<int>(i));
    }
    const auto is_boss = [&bosses](int row) {
      for (const int b : bosses)
        if (b == row) return true;
      return false;
    };
    // Andariel, Duriel, Mephisto, Diablo, Baal, Radament, the Summoner, Izual, Blood Raven, Griswold, Nihlathak.
    for (const int row : {156, 211, 242, 243, 544, 229, 250, 256, 267, 365, 526}) CHECK(is_boss(row));
    for (int row = 546; row <= 550; ++row) CHECK(!is_boss(row));  // the Putrid Defilers
    CHECK(bosses.size() >= 15 && bosses.size() <= 40);
    // SuperUniques: a row's monster class is a MonStats row.
    CHECK(super_count >= 60 && super_count < 256);
    for (uint32_t i = 0; i < super_count; ++i) CHECK(at<uint32_t>(supers, static_cast<size_t>(i) * 0x3C + 0x08) < monster_count);
    std::printf("the game's tables: %d object classes with an icon (the waypoints' %d, %d of them), %zu boss rows, %u "
                "super uniques\n",
                icons, waypoint_cell, waypoint_icons, bosses.size(), super_count);

    // The automap's tile table (its txt: the game makes its records from it, a level type's in a row, in order):
    // every waypoint row is the first fit for its own tiles.
    const auto table = tsv(std::string(dir) + "/automap.txt");
    std::map<std::string, int> level_types, tile_types;
    std::map<int, std::vector<TileRecord>> by_level;
    std::vector<std::pair<int, int>> waypoints;  // level type, index
    for (size_t r = 1; r < table.size(); ++r) {
      const auto& row = table[r];
      if (row.size() < 13 || row[0].empty()) continue;
      const int level = level_types.emplace(row[0], static_cast<int>(level_types.size()) + 1).first->second;
      const int tile = tile_types.emplace(row[1], static_cast<int>(tile_types.size())).first->second;
      const int style = std::atoi(row[2].c_str()), first = std::atoi(row[3].c_str()), last = std::atoi(row[4].c_str());
      std::vector<int32_t> cells;
      for (int c = 6; c <= 12; c += 2)
        if (!row[static_cast<size_t>(c)].empty() && std::atoi(row[static_cast<size_t>(c)].c_str()) >= 0)
          cells.push_back(std::atoi(row[static_cast<size_t>(c)].c_str()));
      if (cells.empty()) continue;
      std::vector<TileRecord>& list = by_level[level];
      list.push_back(record(level, tile, style < 0 ? 0xFF : style, first < 0 ? 0xFF : first, last < 0 ? 0 : last, cells));
      if (record_is(list.back(), waypoint_cell)) waypoints.emplace_back(level, static_cast<int>(list.size()) - 1);
    }
    int reached = 0;
    for (const auto& [level, index] : waypoints) {
      const std::vector<TileRecord>& list = by_level[level];
      const TileRecord& w = list[static_cast<size_t>(index)];
      for (int seq = w.first; seq <= w.last; ++seq) {
        const int fit = first_fit(list.data(), static_cast<int>(list.size()), level, w.tile_type, w.style, seq);
        CHECK(fit >= 0 && record_is(list[static_cast<size_t>(fit)], waypoint_cell));
        if (fit >= 0 && record_is(list[static_cast<size_t>(fit)], waypoint_cell)) ++reached;
      }
    }
    CHECK(waypoints.size() >= 15);
    std::printf("the automap's tile table: %zu waypoint rows in %zu kinds of area, %d of their tiles reached as the "
                "first fit\n",
                waypoints.size(), level_types.size(), reached);
  }

  // The Levels rows' name key (the game's 0x18C rows: the loader's compiled table has them as the game does, and once
  // more wider), and the warps' records: the id a warp tile's preset unit has for its class, the warp named by a
  // level's Warp columns.
  std::vector<uint8_t> levels, warps;
  uint32_t level_count = 0, warp_count = 0;
  if (dir && *dir && rows_of(std::string(dir) + "/levels.bin", 0x18C, &levels, &level_count) &&
      rows_of(std::string(dir) + "/lvlwarp.bin", 0x60, &warps, &warp_count)) {
    const auto key = [&](uint32_t row) {
      return row < level_count ? std::string(reinterpret_cast<const char*>(levels.data()) + row * 0x18C + kLevelNameKey,
                                             strnlen(reinterpret_cast<const char*>(levels.data()) + row * 0x18C +
                                                         kLevelNameKey,
                                                     kLevelNameKeyLen))
                               : std::string();
    };
    CHECK(key(1) == "Rogue Encampment" && key(2) == "Blood Moor" && key(8) == "Den of Evil");
    CHECK(key(39) == "Moo Moo Farm" && key(47) == "Sewers Level 1" && key(48) == "Sewers Level 2");
    int named = 0;
    for (uint32_t r = 1; r < level_count; ++r) named += key(r).empty() ? 0 : 1;
    CHECK(named == static_cast<int>(level_count) - 1);
    // Every warp a level names (levels.txt's Warp0..7 beside its Vis0..7) has a record with that id.
    std::map<int, int> ids;
    for (uint32_t r = 0; r < warp_count; ++r) ids[at<int32_t>(warps, r * 0x60 + 0x2C)] = static_cast<int>(r);
    const auto table = tsv(std::string(dir) + "/levels.txt");
    int warp_links = 0, missing = 0;
    if (!table.empty()) {
      std::map<std::string, size_t> col;
      for (size_t c = 0; c < table[0].size(); ++c) col[table[0][c]] = c;
      for (size_t r = 1; r < table.size(); ++r)
        for (int i = 0; i < 8; ++i) {
          const auto v = col.find("Vis" + std::to_string(i)), w = col.find("Warp" + std::to_string(i));
          if (v == col.end() || w == col.end() || table[r].size() <= std::max(v->second, w->second)) continue;
          const int vis = std::atoi(table[r][v->second].c_str()), warp = std::atoi(table[r][w->second].c_str());
          if (vis <= 0 || warp < 0) continue;
          ++warp_links;
          if (!ids.count(warp)) ++missing;
        }
    }
    CHECK(warp_links > 100 && missing == 0);
    std::printf("the Levels rows: %d of %u named (level 2 \"%s\"); %d warps named by the levels, all among the %u "
                "records\n",
                named, level_count - 1, key(2).c_str(), warp_links, warp_count);
  } else {
    std::printf("the Levels and LvlWarp rows are not at hand (D2RCC_EXCEL) - their layout was not checked\n");
  }

  // The object groups the levels name, read where the game's object group step reads them (Levels +0xED / +0xF5,
  // ObjGroup rows of 0x30 bytes), against the txt; and which levels can roll an object with an icon.
  std::vector<uint8_t> groups_raw;
  uint32_t group_count = 0;
  if (dir && *dir && !levels.empty() && !objects.empty() &&
      rows_of(std::string(dir) + "/objgroup.bin", kObjGroupRow, &groups_raw, &group_count)) {
    const auto lv = [&](uint32_t row, size_t at_) { return levels[row * 0x18C + at_]; };
    CHECK(lv(2, kLevelObjGroups) == 4 && lv(2, kLevelObjGroups + 1) == 38 && lv(2, kLevelObjGroups + 3) == 34);
    CHECK(lv(2, kLevelObjChances) == 3 && lv(2, kLevelObjChances + 1) == 17);
    CHECK(lv(101, kLevelObjGroups + 1) == 61 && lv(101, kLevelObjChances + 1) == 25);
    std::vector<ObjGroup> groups(group_count);
    for (uint32_t g = 0; g < group_count; ++g)
      for (int k = 0; k < kObjGroupSlots; ++k) {
        groups[g].classes[k] = at<int32_t>(groups_raw, g * kObjGroupRow + kObjGroupClasses + static_cast<size_t>(k) * 4);
        groups[g].chances[k] = groups_raw[g * kObjGroupRow + kObjGroupChances + static_cast<size_t>(k)];
      }
    CHECK(group_count > 61 && groups[61].classes[0] == 199 && groups[61].classes[3] == 206 &&
          groups[61].classes[4] == 202 && groups[61].chances[3] == 20);
    std::vector<int16_t> cells(object_count);
    for (uint32_t i = 0; i < object_count; ++i) {
      const int32_t c = at<int32_t>(objects, i * 0x168 + 0x164);
      cells[i] = static_cast<int16_t>(c > 0 && c < 0x7FFF ? c : 0);
    }
    std::string rolling;
    int rolls = 0;
    const auto rolls_of = [&](uint32_t row) {
      uint8_t g[kObjGroupSlots], c[kObjGroupSlots];
      for (int k = 0; k < kObjGroupSlots; ++k) {
        g[k] = lv(row, kLevelObjGroups + static_cast<size_t>(k));
        c[k] = lv(row, kLevelObjChances + static_cast<size_t>(k));
      }
      return rolls_icons(g, c, groups.data(), static_cast<int>(group_count), cells.data(), static_cast<int>(object_count));
    };
    for (uint32_t r = 1; r < level_count; ++r)
      if (rolls_of(r)) {
        ++rolls;
        rolling += (rolling.empty() ? "" : " ") + std::to_string(r);
      }
    // Act 1's and Act 2's outdoor areas and every town roll none; Act 3's jungle, Kurast and Travincal, the Durance,
    // Act 4's and Act 5's outdoor areas, the Den of Evil roll some.
    for (uint32_t r : {1u, 2u, 3u, 4u, 5u, 6u, 7u, 40u, 41u, 42u, 43u, 44u, 75u, 103u, 109u}) CHECK(!rolls_of(r));
    for (uint32_t r : {8u, 76u, 77u, 78u, 79u, 80u, 83u, 100u, 101u, 102u, 104u, 110u, 117u}) CHECK(rolls_of(r));
    std::printf("the object groups: %u rows; %d levels roll an object with an icon as their rooms are filled: %s\n",
                group_count, rolls, rolling.c_str());
  } else {
    std::printf("the ObjGroup rows are not at hand (D2RCC_EXCEL) - which levels roll shrines was not checked\n");
  }

  // The names of the named enemies read ahead from the server's side. The game's name formatter (0x3ADCA0): English has
  // no header and no tags; German and French put a header saying which argument agrees with which, and tag their words.
  CHECK(format_name("%0 %1", {"Gloom", "Hack"}) == "Gloom Hack");
  CHECK(format_name("%0 %1 %2", {"Gloom", "Hack", "the Hammer"}) == "Gloom Hack the Hammer");
  CHECK(format_name("a0n1:%0 %1", {"[ms]fanatischer[fs]fanatische[ns]fanatisches[pl]fanatische", "[fs]Kriegerin"}) ==
        "fanatische Kriegerin");
  CHECK(format_name("a0n1:%0 %1", {"[ms]fanatischer[fs]fanatische[ns]fanatisches[pl]fanatische", "Gefallener"}) ==
        "fanatischer Gefallener");  // a noun without a tag: its [ms] form
  CHECK(format_name("a0n1:%0 %1", {"Düsternis -", "[ms]Hack"}) == "Düsternis - Hack");  // no '[': the adjective stays
  CHECK(format_name("a0n1:%0 %1", {"[fs]grise[ms]gris", "[mp]Diables"}) == "[fs]grise[ms]gris Diables");  // no such form
  CHECK(format_name("%0 %1", {"Gloom ", "Hack"}) == "Gloom Hack");  // a space after the mark goes after one
  CHECK(format_name("n0:%0", {"[fs]Hexe"}) == "Hexe");                // a noun alone loses four characters
  CHECK(format_name("a0:%0 %1", {"[fs]rote[ms]roter", "Hund"}) == "roter Hund");  // an adjective alone: its [ms] form
  CHECK(format_name("%0 %3", {"A", "B"}) == "A %3");
  CHECK(format_name("%1 %0", {"A", "B"}) == "B A");
  CHECK(format_name(nullptr, {"A"}).empty());
  {
    // A random unique's stream: the game's RNG from {the name seed, 666}; a pick a step (a power of two takes the low
    // bits, another count the remainder; an empty list no step); the picks in the order prefix, suffix, the coin, the
    // appellation on it.
    constexpr uint64_t kMul = 0x6AC690C5ull;
    NameStream s{1234, 666};
    name_step(&s);
    CHECK(s.lo == static_cast<uint32_t>(1234 * kMul + 666) && s.hi == static_cast<uint32_t>((1234 * kMul + 666) >> 32));
    NameStream a{77, 666}, b{77, 666}, c{5, 666};
    CHECK(name_pick(&a, 64) == ((77 * kMul + 666) & 63));
    CHECK(name_pick(&b, 100) == static_cast<uint32_t>(77 * kMul + 666) % 100);
    CHECK(name_pick(&c, 0) == 0 && c.lo == 5 && c.hi == 666);
    const uint64_t counts[3] = {125, 120, 85};
    int with = 0;
    for (uint32_t seed = 0; seed < 4000; ++seed) {
      const UniquePicks p = unique_picks(static_cast<uint16_t>(seed), 666, counts, 100, 50);
      uint32_t lo = seed, hi = 666;
      const auto step = [&lo, &hi] {
        const uint64_t t = static_cast<uint64_t>(lo) * kMul + hi;
        lo = static_cast<uint32_t>(t);
        hi = static_cast<uint32_t>(t >> 32);
      };
      step();
      const uint64_t prefix = lo % 125;
      step();
      const uint64_t suffix = lo % 120;
      step();
      const bool app = lo % 100 < 50;
      uint64_t appellation = 0;
      if (app) {
        step();
        appellation = lo % 85;
      }
      CHECK(p.prefix == prefix && p.suffix == suffix && p.has_appellation == app && p.appellation == appellation);
      with += app ? 1 : 0;
    }
    CHECK(with > 1800 && with < 2200);  // the coin is an even one
  }
  {
    // A champion's type: its naming mod's entry (16 champion, 36 ghostly, 37 fanatic, 38 possessed), the last (39
    // berserk) for any other naming mod and for none; the last naming mod wins, and a 0 ends the mods.
    const int types[5] = {16, 36, 37, 38, 39};
    const int naming[2] = {1, 12};
    const uint8_t ghostly[9] = {36, 0};
    const uint8_t berserk[9] = {39, 5, 0};
    const uint8_t none[9] = {5, 6, 0};
    const uint8_t two[9] = {16, 7, 38, 0};
    const uint8_t after[9] = {37, 12, 0};
    const uint8_t ended[9] = {0, 36};
    CHECK(champion_type(ghostly, 9, types, 5, naming, 2) == 1);
    CHECK(champion_type(berserk, 9, types, 5, naming, 2) == 4);
    CHECK(champion_type(none, 9, types, 5, naming, 2) == 4);
    CHECK(champion_type(two, 9, types, 5, naming, 2) == 3);
    CHECK(champion_type(after, 9, types, 5, naming, 2) == 4);
    CHECK(champion_type(ended, 9, types, 5, naming, 2) == 4);
    CHECK(champion_type(ghostly, 9, types, 0, naming, 2) == -1);
  }

  if (g_failures) {
    std::printf("test_mapmarks: %d failure(s)\n", g_failures);
    return 1;
  }
  std::printf("test_mapmarks: all checks passed\n");
  return 0;
}

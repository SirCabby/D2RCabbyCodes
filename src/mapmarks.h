#pragma once

#include <D2RLPlugin/api.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace d2rcc::game {
struct Unit;
}

// Named enemies and landmarks on the map: two display switches, on the client's side.
//
// Landmarks. The game puts an object's icon on the automap only once it has drawn the object on the screen (the unit's
// "drawn" flag), and a waypoint of Acts 1 to 4 - a few floor tiles the game's tile lookup table gives the waypoint's
// cell - once that tile has been seen. Under the switch every area the character enters has them put on at once, from
// the area's layout: its rooms built as Reveal the map builds them (the game's CreateActiveRoom, which also makes a room's
// preset units from its DS1 file - the shrines, wells, quest objects and waypoints the layout places - and moves the
// ones that stand in the room into the room's own list), each preset object with an icon (its Objects row's cell,
// under the game's own rules: the stash in Acts 3 and 4 only, the Arcane Sanctuary's waypoint only there; the sewer
// stairs, which the game shows only once the lever opened them, ahead too) put on the layer's units list with the key
// the game gives an object it has seen, and each floor tile whose cell the game's lookup gives as the waypoint's put
// on by the game's own per-tile add. Nothing the game does not put on its map is put on, and nothing is put anywhere
// else: when the object or the tile is seen, the game's own reveal finds it there already. The shrines and wells the
// server rolls when it first fills a room near a player (the object groups levels.txt names: most shrines of Act 3's
// jungle, Kurast and Travincal, of Acts 4 and 5 and of the dungeons; Acts 1 and 2's outdoor areas roll none) are in
// no layout: they keep the game's own rule. What is put on is saved with the map, as what the game reveals itself is
// (and what Reveal the map puts on).
//
// Named enemies. The automap's pass over the units of the rooms near the player draws each through one routine
// (hooked): the game's marker decision says which of its eight markers (colored crosses: blue the player, green the
// party, red another player, white an NPC, yellow a town portal...) and whose name, and draws them. A monster gets none
// but an NPC in town (white, its name in gold). Under the switch the hook draws, after the game's own, a named enemy the
// automap looks at - a unique or super unique monster, a terror zone's Herald, a boss, a champion - with the red cross
// and its name (the one the hover shows) in the color the game gives an NPC's (a champion's in the game's blue, once
// for a pack), and keeps a mark of it; and, at the local
// player's own call (once a draw), every mark on the automap's layer whose monster it does not draw itself now, however
// far: the spawn spots of the super uniques and bosses the layout of the areas entered places (preset units, read as the
// landmarks are), the named monsters drawn before, where they were last drawn - the game stops a monster far from every
// player, so that is where it still is -, and every named monster of an area entered as the server has made it. A random
// unique, a champion or a Herald is made only as its room is first filled (the server's room population, 0x503790), so
// roomfill.cpp has the game fill an area's rooms as the character enters it and reads the monsters in them: the named
// ones are marked where they stand, with the name the client would give them (monsters_found). A mark goes for good once
// its monster is seen dead, and a monster no longer among its area's at the area's next read loses its mark. The game's
// own calls do the drawing: the transform to the automap's screen, the marker, the name. Nothing is written.
//
// Area names at exits. Building a room links it to the rooms of the areas next to it (its near links, 0x3608A0): the
// rooms of another area that its flags say it borders are put among its near rooms, and a warp (a cave's entrance, a
// dungeon's stairs) gets a link node to the room at the warp's other end, with the warp's record; the warp's tile is a
// preset unit of the room's (type 5, the warp's id for its class, 0x3F4670). Under the switch every area the character
// enters is gone over as the landmarks are: each warp's name goes where its tile's preset unit stands, the name of the
// area its link leads to; and where a room meets a room of another area (an outdoor area's border, the walk from one
// area into the next), the subtiles along that edge on the area's own side that a player can walk on (the room's
// collision map, the player's walk mask's two bits that stay) make the openings, and each opening gets the name of the
// area beyond it, a little inside its own area. Drawn by the same hook as the named enemies, at the local player's
// call, for the areas of the automap's layer, with the game's name draw. Nothing is written.
namespace d2rcc::mapmarks {

using game::Unit;

// --- the rules (tests/test_mapmarks.cpp) ---------------------------------------------------------------------------

// A monster's kind (its unit data +0x1A): the bits game::MonsterTypeFlag names.
constexpr uint16_t kKindSuperUnique = 0x02, kKindChampion = 0x04, kKindUnique = 0x08, kKindMinion = 0x10,
                   kKindHerald = 0x200, kKindHeraldMinion = 0x400;
// MonStats: the flags (+0x3C; boss bit 6, npc 8, the bits the game's own tests take) and Rarity (+0x64).
constexpr uint32_t kMonBoss = 1u << 6;
constexpr uint32_t kMonNpc = 1u << 8;

// A boss by its MonStats row: the boss flag, and no Rarity (the Putrid Defilers have the flag and are an area's
// ordinary monsters: the extra drops for elites leave them out the same way), not an NPC.
constexpr bool boss_row(uint32_t flags, uint8_t rarity) {
  return (flags & kMonBoss) != 0 && rarity == 0 && (flags & kMonNpc) == 0;
}

// Whether a monster is marked on the map: a named enemy (one the game gives its own name: a super unique, a random
// unique, a Herald, a boss by its row) or a champion (its monster's name, in the game's champion blue). A minion never
// (a unique's or a Herald's).
constexpr bool named(uint16_t kind, bool is_boss_row) {
  if (kind & (kKindMinion | kKindHeraldMinion)) return false;
  return (kind & (kKindSuperUnique | kKindChampion | kKindUnique | kKindHerald)) != 0 || is_boss_row;
}
// A champion: the client gives it the unique bit as well (its kind is 0xD), so it is asked first.
constexpr bool champion(uint16_t kind) { return (kind & kKindChampion) != 0 && (kind & kKindSuperUnique) == 0; }

// --- The names of named enemies the server has made (read ahead, before the client knows them) ----------------------
//
// The client names a monster in the handler its naming mods run (0x1DE620, sites::NameFacts), into string ids it keeps
// in the monster's data, and composes the text as it shows it (0x985A0): a Herald (kind 0x200) by the string of
// "HeraldName" and its tier's stat; a super unique (kind 2) by its row's string; a champion by the champion format with
// its type (the string its table gives the naming mod) and its monster's name; a random unique by its name seed: a
// stream {lo the seed, hi a constant}, a prefix and a suffix picked, and on a coin an appellation (two formats). The
// server's monster carries what the client is sent for it (its kind, its super unique row, its mods, its name seed), so
// the same name is worked out from it.

// The game's name formatter (0x3ADCA0): a format of "%0" .. "%9", in some languages after a header ending in ':' whose
// 'a' and 'n' each name an argument by the digit after them: an adjective to agree with a noun. A noun that starts with a
// grammar tag ("[fs]", its first four characters; "[ms]" when it has none) loses it, and the adjective's form of that
// tag (the text after the tag up to the next '[') is taken when the adjective starts with a '['. With a noun alone it
// loses its first four characters; with an adjective alone its "[ms]" form is taken. Then each "%i" in turn (the first of
// it) becomes the i-th argument, and a space after it is dropped when what comes before ends in one.
inline std::string format_name(const char* format, const std::vector<std::string>& args) {
  if (!format) return {};
  std::vector<std::string> slot(args);
  const auto valid = [&slot](int i) { return i >= 0 && i < static_cast<int>(slot.size()); };
  const auto drop4 = [](std::string& s) { s = s.size() > 4 ? s.substr(4) : std::string(); };
  const auto take_form = [](std::string& adjective, const std::string& tag) {
    const size_t at = adjective.find(tag);
    if (at == std::string::npos) return;
    const size_t from = at + tag.size(), end = adjective.find('[', from);
    adjective = adjective.substr(from, end == std::string::npos ? std::string::npos : end - from);
  };
  const char* colon = std::strchr(format, ':');
  if (colon) {
    const char* pa = std::strchr(format, 'a');
    const char* pn = std::strchr(format, 'n');
    const int a = pa && pa < colon ? pa[1] - '0' : -1;
    const int n = pn && pn < colon ? pn[1] - '0' : -1;
    if (a == -1) {
      if (valid(n)) drop4(slot[static_cast<size_t>(n)]);
    } else if (n == -1) {
      if (valid(a)) take_form(slot[static_cast<size_t>(a)], "[ms]");
    } else {
      std::string tag = "[ms]";
      if (valid(n) && !slot[static_cast<size_t>(n)].empty() && slot[static_cast<size_t>(n)][0] == '[') {
        tag = slot[static_cast<size_t>(n)].substr(0, 4);
        drop4(slot[static_cast<size_t>(n)]);
      }
      if (valid(a) && !slot[static_cast<size_t>(a)].empty() && slot[static_cast<size_t>(a)][0] == '[')
        take_form(slot[static_cast<size_t>(a)], tag);
    }
  }
  std::string text = colon ? std::string(colon + 1) : std::string(format);
  for (size_t i = 0; i < slot.size() && i < 10; ++i) {
    const char mark[3] = {'%', static_cast<char>('0' + i), 0};
    const size_t at = text.find(mark);
    if (at == std::string::npos) continue;
    std::string out = text.substr(0, at) + slot[i];
    size_t rest = at + 2;
    if (!out.empty() && out.back() == ' ' && rest < text.size() && text[rest] == ' ') ++rest;
    text = out + text.substr(rest);
  }
  return text;
}

// A random unique's name stream: the game's RNG (lo * 0x6AC690C5 + hi: lo the low half, hi the high) from {lo the name
// seed, hi a constant}. A pick (the picker 0x396D50) is a step and the index lo % count (lo & (count - 1) for a power of
// two), and no step for an empty list; the coin for the appellation is a step and lo % coin_mod below coin_below.
struct NameStream {
  uint32_t lo = 0, hi = 0;
};
inline void name_step(NameStream* s) {
  const uint64_t t = static_cast<uint64_t>(s->lo) * 0x6AC690C5ull + s->hi;
  s->lo = static_cast<uint32_t>(t);
  s->hi = static_cast<uint32_t>(t >> 32);
}
inline uint64_t name_pick(NameStream* s, uint64_t count) {
  if (count == 0 || count > 0x7FFFFFFF) return 0;
  name_step(s);
  const uint32_t n = static_cast<uint32_t>(count);
  return (n & (n - 1)) == 0 ? (s->lo & (n - 1)) : (s->lo % n);
}
struct UniquePicks {
  uint64_t prefix = 0, suffix = 0, appellation = 0;
  bool has_appellation = false;
};
// counts: the prefixes', the suffixes' and the appellations' lists.
inline UniquePicks unique_picks(uint16_t seed, uint32_t hi, const uint64_t counts[3], uint32_t coin_mod,
                                uint32_t coin_below) {
  NameStream s{seed, hi};
  UniquePicks p;
  p.prefix = name_pick(&s, counts[0]);
  p.suffix = name_pick(&s, counts[1]);
  name_step(&s);
  p.has_appellation = coin_mod && s.lo % coin_mod < coin_below;
  if (p.has_appellation) p.appellation = name_pick(&s, counts[2]);
  return p;
}

// A champion's type: the handler runs for the base mods first (the first a naming one) and then for each of the
// monster's mods that names it, and the last call names it: the entry of the champion table whose mod it is (all but
// the last entry are compared), else the last entry's. `naming` lists the other naming mods (the base one, Blood
// Raven's), which leave the last entry too. Returns the entry's index.
inline int champion_type(const uint8_t* mods, int mod_count, const int* type_mods, int types, const int* naming,
                         int naming_count) {
  if (types <= 0) return -1;
  int type = types - 1;
  for (int i = 0; i < mod_count && mods[i]; ++i) {
    bool names = false;
    for (int k = 0; k < naming_count && !names; ++k) names = mods[i] == naming[k];
    for (int k = 0; k < types && !names; ++k) names = mods[i] == type_mods[k];
    if (!names) continue;
    type = types - 1;
    for (int k = 0; k + 1 < types; ++k)
      if (mods[i] == type_mods[k]) type = k;
  }
  return type;
}

// A preset unit of a room's layout: +0x20 its type (1 a monster, 2 an object, 5 a warp tile), +0x04 its class, +0x08 /
// +0x24 its place, +0x10 the next. A monster's class is a MonStats row below the table's count; from there on a super
// unique's row (the count added), and past those the layout's own places (a spawn decided at random).
constexpr uintptr_t kPresetClass = 0x04;
constexpr uintptr_t kPresetX = 0x08;
constexpr uintptr_t kPresetNext = 0x10;
constexpr uintptr_t kPresetType = 0x20;
constexpr uintptr_t kPresetY = 0x24;
constexpr uint32_t kPresetMonster = 1;
constexpr uint32_t kPresetObject = 2;
enum class PresetKind : int { kNone = 0, kMonster, kSuperUnique };
struct PresetClass {
  PresetKind kind = PresetKind::kNone;
  int row = -1;  // the MonStats row or the SuperUniques row
};
constexpr PresetClass preset_class(uint32_t cls, int monster_rows, int super_rows) {
  if (monster_rows <= 0) return {};
  if (cls < static_cast<uint32_t>(monster_rows)) return {PresetKind::kMonster, static_cast<int>(cls)};
  const uint32_t su = cls - static_cast<uint32_t>(monster_rows);
  if (super_rows > 0 && su < static_cast<uint32_t>(super_rows)) return {PresetKind::kSuperUnique, static_cast<int>(su)};
  return {};
}

// Where the automap puts a unit's icon (the unit cell add, 0xD52B0): its pixels (a subtile's are (x - y) * 16, (x + y) *
// 8, 0x334E00) / 10, plus 1 and less 3; the division is C's, toward zero.
constexpr int32_t pixel_x(int32_t sx, int32_t sy) { return (sx - sy) * 16; }
constexpr int32_t pixel_y(int32_t sx, int32_t sy) { return (sx + sy) * 8; }
constexpr int32_t cell_x(int32_t px) { return px / 10 + 1; }
constexpr int32_t cell_y(int32_t py) { return py / 10 - 3; }

// The reveal's rules for an object's icon (its unit pass, read from its code: sites::MapFacts): the stash in two acts
// only, the Arcane Sanctuary's waypoint only in its level, the sewer stairs only open - but a layout's (mode -1, not
// known) are put on all the same: the way into Act 3's Sewers Level 2, which the lever opens, is the landmark looked
// for (asked 2026-10-02: "the entrance to sewers level 2 also isn't showing up on the map").
struct ObjectRules {
  int stash_class = -1, stash_act = 0, stash_acts = 0;
  int stairs_class = -1, stairs_mode = 0;
  int arcane_class = -1, arcane_level = 0;
};
constexpr bool icon_shows(const ObjectRules& r, int cls, int cell, int act, int level, int mode) {
  if (cell <= 0) return false;
  if (cls == r.stash_class) return act >= r.stash_act && act < r.stash_act + r.stash_acts;
  if (cls == r.stairs_class) return mode < 0 || mode == r.stairs_mode;
  if (cls == r.arcane_class) return level == r.arcane_level;
  return true;
}

// The tile lookup's records (0x32BDF0): 0x20 bytes, walked from the level type's first to its last; the first that
// fits a tile gives its cell: +0x00 level type, +0x04 tile type, +0x08 style (0xFF any), +0x09 / +0x0A the first and
// last sequence (0xFF at +0x09: any), +0x0C the cells (int32 each), +0x1C how many (one picked at random by the
// automap's own stream; below 1 the first).
constexpr size_t kRecordSize = 0x20;
constexpr int kRecordCells = 4;
struct TileRecord {
  int32_t level_type = 0, tile_type = 0;
  uint8_t style = 0xFF, first = 0xFF, last = 0;
  int32_t cells[kRecordCells] = {};
  int32_t count = 0;
};
constexpr bool record_fits(const TileRecord& r, int32_t level_type, int32_t tile_type, uint32_t style, int32_t sequence) {
  if (r.level_type != level_type || r.tile_type != tile_type) return false;
  if (r.style != 0xFF && r.style != style) return false;
  return r.first == 0xFF || (r.first <= sequence && sequence <= r.last);
}
// Every cell the record can give is this one (the waypoint's).
constexpr bool record_is(const TileRecord& r, int32_t cell) {
  const int n = r.count < 1 ? 1 : r.count > kRecordCells ? kRecordCells : r.count;
  for (int i = 0; i < n; ++i)
    if (r.cells[i] != cell) return false;
  return cell > 0;
}
// The index of the record the lookup takes for a tile among `records` (the level type's range), -1 for none.
inline int first_fit(const TileRecord* records, int count, int32_t level_type, int32_t tile_type, uint32_t style,
                     int32_t sequence) {
  for (int i = 0; i < count; ++i)
    if (record_fits(records[i], level_type, tile_type, style, sequence)) return i;
  return -1;
}

// The waypoint's cell: the icon the Objects rows give their waypoints that have one (Act 5's, the Arcane Sanctuary's;
// the others' are their floor tiles'), all alike, -1 when none or they differ.
constexpr bool waypoint_name(const char* name) {
  for (const char* p = name; p && *p; ++p) {
    const char* w = "Waypoint";
    int i = 0;
    while (w[i] && p[i] == w[i]) ++i;
    if (!w[i]) return true;
  }
  return false;
}

// A Levels row (the game's, 0x18C bytes): the key of its name (LevelName), the string the game's level name getter
// (0xC7A30) looks up.
constexpr size_t kLevelNameKey = 0xFD;
constexpr size_t kLevelNameKeyLen = 40;

// The objects a level rolls as the server fills each of its rooms (the object group step 0x510F00): eight object
// groups (Levels +0xED, ObjGrp, a byte each) with a chance each (+0xF5, ObjPrb, percent), and an ObjGroup row (0x30
// bytes, the row the group's id; 0x38FD90): +0x00 eight object classes (int32, 0 none), +0x20 how many (DENSITY),
// +0x28 the chance of each among them (PROB). A level can roll an object the automap has an icon for when one of its
// groups with a chance names, with a chance of its own, a class whose Objects row has a cell: a shrine (310), a well
// (309), a waypoint (307: Act 5's outdoor areas roll theirs).
constexpr size_t kLevelObjGroups = 0xED, kLevelObjChances = 0xF5;
constexpr int kObjGroupSlots = 8;
constexpr uint32_t kObjGroupRow = 0x30;
constexpr size_t kObjGroupClasses = 0x00, kObjGroupChances = 0x28;
struct ObjGroup {
  int32_t classes[kObjGroupSlots] = {};
  uint8_t chances[kObjGroupSlots] = {};
};
inline bool rolls_icons(const uint8_t (&groups)[kObjGroupSlots], const uint8_t (&chances)[kObjGroupSlots],
                        const ObjGroup* rows, int row_count, const int16_t* object_cell, int objects) {
  for (int s = 0; s < kObjGroupSlots; ++s) {
    if (!groups[s] || !chances[s] || groups[s] >= row_count) continue;
    const ObjGroup& g = rows[groups[s]];
    for (int k = 0; k < kObjGroupSlots; ++k) {
      const int32_t cls = g.classes[k];
      if (cls > 0 && cls < objects && g.chances[k] > 0 && object_cell[cls] > 0) return true;
    }
  }
  return false;
}
// An object the server made in a room ahead (roomfill.cpp): its class and where it stands (its static path's pixels).
struct PlacedObject {
  uint32_t cls = 0;
  int32_t px = 0, py = 0;
};

// A room's own preset units (sites::MapFacts room_presets_at: where CreateActiveRoom's build moves them, 0x3DE420, and
// where the room preset add, 0x360BF0, puts every other): their places are the room's, in subtiles from its corner.
constexpr int32_t kSubtiles = 5;  // a tile is 5 x 5 subtiles (0x334ED0)
constexpr int32_t room_subtile(int32_t room_tile, int32_t own) { return room_tile * kSubtiles + own; }

// Area names at exits.
//
// A DRLG room's place (+0x60 .. +0x6C, tiles). Two rooms of different areas meet on an edge: the subtiles along it on
// the first room's side, and which way the other room lies (+1 where the line's coordinate grows).
struct Rect {
  int32_t x = 0, y = 0, w = 0, h = 0;
};
struct Edge {
  bool vertical = false;     // the edge runs along y: the other room is left or right of the first
  int32_t line = 0;          // the first room's subtile column (vertical) or row next to the edge
  int32_t from = 0, to = 0;  // the subtiles along the edge both rooms have, [from, to)
  int side = 0;              // +1: the other room at line + 1; -1: at line - 1
};
constexpr bool shared_edge(const Rect& a, const Rect& b, Edge* e) {
  const int32_t y0 = std::max(a.y, b.y), y1 = std::min(a.y + a.h, b.y + b.h);
  const int32_t x0 = std::max(a.x, b.x), x1 = std::min(a.x + a.w, b.x + b.w);
  if (a.w <= 0 || a.h <= 0 || b.w <= 0 || b.h <= 0) return false;
  if (y0 < y1 && b.x == a.x + a.w) *e = Edge{true, (a.x + a.w) * kSubtiles - 1, y0 * kSubtiles, y1 * kSubtiles, +1};
  else if (y0 < y1 && b.x + b.w == a.x) *e = Edge{true, a.x * kSubtiles, y0 * kSubtiles, y1 * kSubtiles, -1};
  else if (x0 < x1 && b.y == a.y + a.h) *e = Edge{false, (a.y + a.h) * kSubtiles - 1, x0 * kSubtiles, x1 * kSubtiles, +1};
  else if (x0 < x1 && b.y + b.h == a.y) *e = Edge{false, a.y * kSubtiles, x0 * kSubtiles, x1 * kSubtiles, -1};
  else return false;
  return true;
}

// A subtile a player can walk on: none of the two bits of the player's walk mask (0x1C09, the one the game places a
// player with) that a room's map keeps (1 a wall, 8 one a player does not cross); the others (objects, doors, units)
// come and go.
constexpr uint16_t kWalkBlock = 0x0009;
constexpr bool walkable(uint16_t flags) { return (flags & kWalkBlock) == 0; }

// The walkable subtiles found along one edge line toward one area: those closer than kOpeningJoin apart are one
// opening, and an opening of fewer than kOpeningLeast is none (a pocket of floor beside a wall).
constexpr int32_t kOpeningJoin = 4;
constexpr int kOpeningLeast = 3;
struct Opening {
  int32_t from = 0, to = 0;  // its first and last walkable subtile
  int cells = 0;             // walkable subtiles in it
};
inline std::vector<Opening> openings(std::vector<int32_t> cells) {
  std::sort(cells.begin(), cells.end());
  cells.erase(std::unique(cells.begin(), cells.end()), cells.end());
  std::vector<Opening> out;
  Opening cur;
  for (size_t i = 0; i < cells.size(); ++i) {
    if (cur.cells && cells[i] - cur.to <= kOpeningJoin) {
      cur.to = cells[i];
      ++cur.cells;
      continue;
    }
    if (cur.cells >= kOpeningLeast) out.push_back(cur);
    cur = Opening{cells[i], cells[i], 1};
  }
  if (cur.cells >= kOpeningLeast) out.push_back(cur);
  return out;
}

// Where an opening's name goes: its middle, kExitInward subtiles back into the area it is found in (so the names of
// the two sides of a border stand apart).
constexpr int32_t kExitInward = 3;
struct Subtile {
  int32_t x = 0, y = 0;
};
constexpr Subtile exit_place(bool vertical, int32_t line, int side, const Opening& o) {
  const int32_t along = o.from + (o.to - o.from) / 2;
  const int32_t back = line - side * kExitInward;
  return vertical ? Subtile{back, along} : Subtile{along, back};
}

// --- the plugin ------------------------------------------------------------------------------------------------------

void set_services(const D2RL::PluginContext* ctx, const D2RL::DataTableService* tables,
                  const D2RL::LocalizationService* strings);

// After sites::derive() and game::bind(): sets the two switches' why_not (ui_thread: the loader gives UI callbacks).
// hooks::install says first whether the automap's draw of one unit is hooked.
void set_draw_hooked(bool hooked);
void bind(bool ui_thread);
bool landmarks_ready();  // the landmarks can be put on (bind found what they need)
bool enemies_ready();    // ... the named enemies' spawn spots can be read (their drawing is the hook's)
bool exits_ready();      // ... the exits found and their names drawn

// The objects the game rolls as it fills a room (shrines, wells ...), put on ahead: whether a level rolls any with an
// icon (any thread, once its bank is read), the objects the server made in its rooms handed over (the server thread:
// how many have an icon), and put on the automap's layer of their level once it is the active one (the UI thread, when
// found_waiting() says there are some; tried again a few times a second while the layer is another).
bool level_rolls_icons(uint8_t bank, int level);
int objects_found(uint8_t bank, int level, const PlacedObject* objects, size_t count);
bool found_waiting();
void put_found(uint64_t now_ms);

// The named enemies the server has made in the rooms of an area (the server thread, roomfill.cpp, as the character
// enters it once its rooms are filled: every monster that stands in them, alive): those that are named are marked where
// they stand with the name the client would give them, and the area's earlier marks of the server's monsters that are
// not among them go (they died or left it). Returns how many are named. The marks are put on the automap's layer of their
// level at its next draw (the UI thread).
int monsters_found(uint8_t bank, int level, Unit* const* monsters, size_t count);

// Game thread (the tick): the Objects, MonStats, SuperUniques and Levels rows the two read, and the names, per table
// bank. Once a bank is read it is kept.
void collect();
bool ready(uint8_t bank);

// UI thread, inside the area pass (mapreveal.cpp), for one room of the area the character is in, built for play:
// the landmarks put on the active layer (the area's), the named enemies' spawn spots noted, the exits found. A pass
// begins and ends an area.
void area_begins(int level_id, int layer, uint8_t bank, bool landmarks, bool enemies, bool exits);
void room(void* drlg_room, void* active_room);
void area_ends();

// The hook on the automap's draw of one unit (the thread that draws the automap), after the game's own draw.
void unit_drawn(Unit* unit, const void* view);

void game_left();

// For the panel and the console (any thread).
struct Stats {
  unsigned areas = 0;      // this game: areas whose landmarks were put on
  unsigned icons = 0;      // ... object icons put on
  unsigned waypoints = 0;  // ... waypoint tiles put on
  unsigned spots = 0;      // ... spawn spots of named enemies read
  unsigned drawn = 0;      // named enemies the automap has drawn (each counted once a game)
  unsigned kept = 0;       // ... of them on the map now (where they were last drawn when out of its sight)
  unsigned dead = 0;       // marks let go because their monster was seen dead
  unsigned exit_areas = 0; // areas whose exits were found
  unsigned warps = 0;      // ... their warps named (cave entrances, stairs)
  unsigned borders = 0;    // ... their openings into the next area named
  unsigned exits = 0;      // exits on the map now
  unsigned rolled = 0;     // objects the game rolled ahead put on the map (shrines, wells ...)
  unsigned ahead = 0;      // named enemies read from the server's side as their areas were entered (each once a game)
  unsigned names_off = 0;  // ... whose name worked out was not the one the client gave them when it drew them
};
Stats stats();

}  // namespace d2rcc::mapmarks

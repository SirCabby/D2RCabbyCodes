#include "mapmarks.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <vector>

#include "cheats.h"
#include "game.h"
#include "log.h"
#include "mem.h"
#include "perf.h"
#include "sites.h"

namespace d2rcc::mapmarks {
namespace {

constexpr int kBanks = 4;  // by a unit's data-table bank, 1 classic .. 3 rotw
constexpr int kMaxMonsters = 2048;
constexpr int kMaxSupers = 256;
constexpr int kMaxObjects = 1024;
constexpr int kMaxLevels = 1024;
constexpr int kMaxBosses = 64;
constexpr int kNameLen = 64;
constexpr int kMaxMarks = 4096;   // named enemies and champions on the map in this game: spawn spots, the ones seen
constexpr int kMaxDead = 1024;    // ... the layout's seen dead
constexpr int kMaxDrawn = 512;    // named monsters counted as drawn in this game
constexpr int kMaxPresets = 512;  // a room's preset units are walked no further
constexpr int kMaxNamedLevels = 256;  // levels whose names are kept (3.3 has 137)
constexpr int kMaxExits = 1024;   // exits on the map in this game
constexpr int kMaxLinks = 64;     // a room's warp links are walked no further
constexpr uint64_t kMaxNear = 256;  // ... nor its near rooms
constexpr uint64_t kMaxTiles = 8192;  // ... nor its floor tiles
constexpr int kMaxRecords = 4096;     // a level type's records of the tile lookup
constexpr int kMaxLevelTypes = 1024;
constexpr int kDrawnLogs = 4;
constexpr int kDeadLogs = 4;

// The rows of the loader's tables (the game's own memory): MonStats (its name's string id, flags and Rarity),
// SuperUniques (its name's string id, filled in as the game reads the table), Objects (its class name; the icon's cell
// is where the reveal reads it, sites::MapFacts) and Levels (its act), by row = class, row = id.
constexpr uint32_t kMonStatsRow = 0x1FC;
constexpr size_t kMonName = 0x36, kMonFlags = 0x3C, kMonRarity = 0x64;
constexpr uint32_t kSuperRow = 0x3C;
constexpr size_t kSuperName = 0x02;
constexpr uint32_t kObjectsRow = 0x168;
constexpr size_t kObjectName = 0x02, kObjectNameLen = 0x40;
constexpr uint32_t kLevelsRow = 0x18C;
constexpr size_t kLevelAct = 0x0D;

// A DRLG room as CreateActiveRoom reads it (0x3289A0): its preset units made from its DS1 file into its preset part
// (the preset step 0x3DE0E0), then, as it is built, the ones that stand in it moved into its own list, their places
// made its own (sites::MapFacts room_presets_at; a warp tile's and a maze's are put there too); its place in tiles
// (+0x60 x, +0x64 y, +0x68 width, +0x6C height), its area (+0x90; the area's id +0x1F8), its ActiveRoom once built
// (+0x58). A built room (its ActiveRoom) keeps its floor tiles through +0x08: +0x20 the tiles, +0x28 how many (the
// getter 0x2EFB70); a tile is 0x48 bytes, +0x18 its flags (8: never put on the map; 0x40000: on it), +0x20 its graphics
// record (the reveal 0xD6550 and the per-tile add 0xD5160 read them so). sites.cpp checks these in the routines' code
// (MapFacts: presets, room_presets, tiles, exits).
constexpr uintptr_t kRoomX = 0x60, kRoomY = 0x64, kRoomW = 0x68, kRoomH = 0x6C;
constexpr uintptr_t kRoomArea = 0x90, kAreaId = 0x1F8;
constexpr uintptr_t kRoomActive = 0x58;
constexpr uintptr_t kActiveTiles = 0x08, kTilesArray = 0x20, kTilesCount = 0x28;
// A warp link (the near links' node, 0x361750): +0x00 the room at the other end, +0x08 the next, +0x20 the warp's
// record. A collision map (an ActiveRoom's, 0x366250): +0x00 x, +0x04 y, +0x08 width, +0x0C height (subtiles), +0x20
// the flags, a u16 a subtile, row by row.
constexpr uintptr_t kLinkRoom = 0x00, kLinkNext = 0x08, kLinkWarp = 0x20;
constexpr uintptr_t kCollFlags = 0x20;
constexpr uintptr_t kTileSize = 0x48, kTileFlags = 0x18, kTileGraphics = 0x20;
constexpr uint32_t kTileNever = 0x8, kTileOnMap = 0x40000;

// The red cross: the automap's marker for another player outside your party, and for every monster under the game's
// own reveal-all debug switch (its marker decision, 0xD78F0).
constexpr int kMarkerEnemy = 5;
// The game's text colors (0x9040A0: 13 of them, the classic ones: 0 white, 1 red, 2 green, 3 blue, 4 gold ...): a
// champion's name is blue, as the game writes it. A named enemy's is the color the automap gives an NPC's. An area's
// name at an exit is white.
constexpr int kChampionColor = 3;
constexpr int kExitColor = 0;
// A champion pack's members share their name: it is written once for those within this many of the automap's pixels
// (times its marker scale) of each other in one draw.
constexpr float kLabelApart = 64.0f;
constexpr int kMaxLabels = 64;

struct BossName {
  int row;
  char text[kNameLen];
};
// What the two read of a bank's tables: written once by the game thread, then only read (any thread).
struct Tables {
  volatile LONG ready;
  int monsters, supers, objects, levels;
  int waypoint_cell;  // the waypoints' icon (-1: none alike)
  int icon_kinds;     // object classes with an icon
  uint32_t mon_flags[kMaxMonsters];
  uint8_t mon_rarity[kMaxMonsters];
  int16_t object_cell[kMaxObjects];
  uint8_t level_act[kMaxLevels];
  char super_name[kMaxSupers][kNameLen];
  BossName boss[kMaxBosses];
  int bosses;
  char level_name[kMaxNamedLevels][kNameLen];  // as the game names a level (its LevelName string)
  int level_names;                             // ... how many have one
};

// A named enemy on the map: a spawn spot of an area's layout (a super unique, a boss), or a monster the automap has drawn,
// where it was last drawn (the game stops a monster far from every player, so that is where it still is).
struct Mark {
  int level;
  int layer;
  int32_t px, py;  // where the automap has it: its pixels
  uint32_t unit;   // the monster's id once the automap has drawn it; 0 a layout's spot not seen yet
  ULONGLONG seen;  // the automap draw it was last drawn in itself
  bool layout;     // a layout's spot (a super unique, a boss), or the monster that took one over
  bool champion;
  char name[kNameLen];
};
struct Label {  // a name written in this draw
  int32_t x, y;
  uint32_t hash;
};
struct Dead {
  int level;
  char name[kNameLen];
};
// An exit of an area gone over: where it is (pixels), the area it leads to and its name.
struct Exit {
  int level;
  int layer;
  int32_t px, py;
  int to;
  bool warp;  // a warp's tile (a cave's entrance, stairs); else an opening into the next area
  char name[kNameLen];
};

const D2RL::PluginContext* g_ctx = nullptr;
const D2RL::DataTableService* g_tables = nullptr;
const D2RL::LocalizationService* g_strings = nullptr;
Tables g_banks[kBanks];
volatile LONG g_landmarks_ok = 0, g_enemies_ok = 0, g_draw_hooked = 0;
volatile LONG g_row_warned = 0, g_name_warned = 0, g_drawn_logged = 0, g_dead_logged = 0;
CRITICAL_SECTION g_cs;  // the marks, the dead, the drawn: the area pass and the automap's draw share them
bool g_cs_ready = false;
Mark g_marks[kMaxMarks];
int g_mark_count = 0;
Dead g_dead[kMaxDead];
int g_dead_count = 0;
uint32_t g_drawn[kMaxDrawn];  // the ids of the named monsters drawn this game
int g_drawn_count = 0;
ULONGLONG g_draw = 16;  // automap draws: one each time the local player is drawn
volatile LONG g_areas = 0, g_icons = 0, g_waypoints = 0, g_spot_stat = 0, g_drawn_stat = 0, g_dead_stat = 0;
volatile LONG g_full_warned = 0;
Exit g_exits[kMaxExits];  // under g_cs
int g_exit_count = 0;
volatile LONG g_exits_ok = 0, g_exit_areas = 0, g_warp_stat = 0, g_border_stat = 0;
volatile LONG g_map_warned = 0, g_exits_full_warned = 0;
// The draw thread only: the names written in the current draw.
Label g_labels[kMaxLabels];
int g_label_count = 0;
ULONGLONG g_labels_draw = 0;

// UI thread only: the area the pass walks.
struct Border {  // the walkable subtiles found along one edge line toward one area
  int to = 0;
  bool vertical = false;
  int32_t line = 0;
  int side = 0;
  std::vector<int32_t> cells;
};
struct Area {
  bool on = false;
  int level = -1, layer = -1, act = -1;
  uint8_t bank = 0;
  bool landmarks = false, enemies = false, exits = false;
  std::vector<Exit> warps;      // this area's warps found
  std::vector<Border> borders;  // ... and the edges toward other areas
  unsigned maps_off = 0;        // built rooms whose collision map is not where their place says
  int32_t level_type = -1;
  int waypoint_cell = -1;
  std::vector<TileRecord> records;    // the level type's records, in the lookup's order
  std::vector<int> waypoint_records;  // ... the ones that give the waypoint's cell
  unsigned icons = 0, icons_new = 0, waypoints = 0, spots = 0;
  char names[192] = {};
};
Area g_area;

const Tables* tables_of(uint8_t bank) {
  return bank >= 1 && bank < kBanks && g_banks[bank].ready ? &g_banks[bank] : nullptr;
}

// Some languages start a name with a grammar tag such as "[fs]"; the game drops it (0x1E1CF0) before it shows one.
void strip_tag(char* name) {
  size_t i = std::strlen(name);
  while (i > 0 && name[i] != ']') --i;
  if (name[i] != ']' || i < 3 || name[i - 3] != '[') return;
  std::memmove(name, name + i + 1, std::strlen(name + i + 1) + 1);
}

bool string_by_id(uint16_t id, char* out, size_t cap) {
  out[0] = 0;
  if (!id || !g_ctx || !g_strings || !g_strings->getStringById) return false;
  uint32_t need = 0;
  if (g_strings->getStringById(g_ctx, id, out, static_cast<uint32_t>(cap), &need) != D2RL::Localization::Result::Success)
    out[0] = 0;
  out[cap - 1] = 0;
  strip_tag(out);
  return out[0] != 0;
}

// The game's string of a key (a level's LevelName), as the game shows it.
bool string_by_key(const char* key, char* out, size_t cap) {
  out[0] = 0;
  if (!key[0] || !g_ctx || !g_strings || !g_strings->getStringByKey) return false;
  char full[kLevelNameKeyLen + 8];
  std::snprintf(full, sizeof(full), "d2r:%s", key);
  uint32_t need = 0;
  if (g_strings->getStringByKey(g_ctx, full, out, static_cast<uint32_t>(cap), &need) !=
      D2RL::Localization::Result::Success)
    out[0] = 0;
  out[cap - 1] = 0;
  strip_tag(out);
  return out[0] != 0;
}

// A copy of a table's rows (game thread), empty when it is not there or its rows are of another size.
std::vector<uint8_t> rows_of(D2RL::DataTables::Bank bank, D2RL::DataTables::TableId table, uint32_t row_size,
                             int most, int* count) {
  using namespace D2RL::DataTables;
  std::vector<uint8_t> out;
  *count = 0;
  TableView view{};
  view.structSize = TableViewSize;
  if (!g_tables || !g_tables->getTable || g_tables->getTable(g_ctx, bank, table, &view) != Result::Success ||
      !view.rows || !view.rowCount)
    return out;
  if (view.rowSize != row_size || view.rowCount > static_cast<uint32_t>(most)) {
    if (log_once(&g_row_warned, LogLevel::kWarning))
      log_warn("map: table %u has %u rows of %u bytes, not at most %d rows of %u - no landmark or named enemy is put "
               "on the map ahead",
               static_cast<unsigned>(table), view.rowCount, view.rowSize, most, row_size);
    return out;
  }
  out.resize(static_cast<size_t>(view.rowCount) * row_size);
  if (!mem::copy_from(out.data(), reinterpret_cast<uintptr_t>(view.rows), out.size())) out.clear();
  else *count = static_cast<int>(view.rowCount);
  return out;
}

template <typename T>
T field(const std::vector<uint8_t>& rows, uint32_t row_size, int row, size_t at) {
  T v{};
  std::memcpy(&v, rows.data() + static_cast<size_t>(row) * row_size + at, sizeof(T));
  return v;
}

// One bank's rows (game thread) into its record, which nothing reads until it is marked ready. False when a table is
// not there yet.
bool read_bank(int b, Tables* t) {
  using namespace D2RL::DataTables;
  const auto bank = static_cast<Bank>(b);
  int monsters = 0, supers = 0, objects = 0, levels = 0;
  const std::vector<uint8_t> mon = rows_of(bank, TableId::MonStats, kMonStatsRow, kMaxMonsters, &monsters);
  const std::vector<uint8_t> sup = rows_of(bank, TableId::SuperUniques, kSuperRow, kMaxSupers, &supers);
  const std::vector<uint8_t> obj = rows_of(bank, TableId::Objects, kObjectsRow, kMaxObjects, &objects);
  const std::vector<uint8_t> lev = rows_of(bank, TableId::Levels, kLevelsRow, kMaxLevels, &levels);
  if (!monsters || !supers || !objects || !levels) return false;
  t->monsters = monsters;
  t->supers = supers;
  t->objects = objects;
  t->levels = levels;
  t->bosses = 0;
  for (int i = 0; i < monsters; ++i) {
    t->mon_flags[i] = field<uint32_t>(mon, kMonStatsRow, i, kMonFlags);
    t->mon_rarity[i] = field<uint8_t>(mon, kMonStatsRow, i, kMonRarity);
    if (boss_row(t->mon_flags[i], t->mon_rarity[i]) && t->bosses < kMaxBosses) {
      BossName& n = t->boss[t->bosses++];
      n.row = i;
      string_by_id(field<uint16_t>(mon, kMonStatsRow, i, kMonName), n.text, sizeof(n.text));
    }
  }
  int named = 0;
  for (int i = 0; i < supers; ++i)
    if (string_by_id(field<uint16_t>(sup, kSuperRow, i, kSuperName), t->super_name[i], kNameLen)) ++named;
  if (named < supers && log_once(&g_name_warned, LogLevel::kWarning))
    log_warn("map: %d of %d super uniques of bank %d have no name the loader knows - their spawn spots are drawn "
             "without one",
             supers - named, supers, b);
  // An object's icon is where the reveal reads it; the waypoints that have one have the same (the others are their
  // floor tiles').
  const int cell_at = sites::map_facts().object_cell_at;
  t->waypoint_cell = 0;
  t->icon_kinds = 0;
  for (int i = 0; i < objects; ++i) {
    int32_t cell = cell_at > 0 && static_cast<uint32_t>(cell_at) + 4 <= kObjectsRow
                       ? field<int32_t>(obj, kObjectsRow, i, static_cast<size_t>(cell_at))
                       : 0;
    if (cell < 0 || cell > 0x7FFF) cell = 0;
    t->object_cell[i] = static_cast<int16_t>(cell);
    if (!cell) continue;
    ++t->icon_kinds;
    char name[kObjectNameLen + 1] = {};
    std::memcpy(name, obj.data() + static_cast<size_t>(i) * kObjectsRow + kObjectName, kObjectNameLen);
    if (waypoint_name(name)) t->waypoint_cell = !t->waypoint_cell || t->waypoint_cell == cell ? cell : -1;
  }
  if (t->waypoint_cell <= 0) t->waypoint_cell = -1;
  for (int i = 0; i < levels; ++i) t->level_act[i] = field<uint8_t>(lev, kLevelsRow, i, kLevelAct);
  // A level's name as the game shows it: its LevelName key's string (the key itself when the loader has none).
  t->level_names = 0;
  for (int i = 0; i < levels && i < kMaxNamedLevels; ++i) {
    char key[kLevelNameKeyLen + 1] = {};
    std::memcpy(key, lev.data() + static_cast<size_t>(i) * kLevelsRow + kLevelNameKey, kLevelNameKeyLen);
    t->level_name[i][0] = 0;
    if (!key[0]) continue;
    if (!string_by_key(key, t->level_name[i], kNameLen)) std::snprintf(t->level_name[i], kNameLen, "%s", key);
    ++t->level_names;
  }
  return true;
}

ObjectRules rules() {
  const sites::MapFacts& m = sites::map_facts();
  ObjectRules r;
  r.stash_class = m.stash_class;
  r.stash_act = m.stash_act;
  r.stash_acts = m.stash_acts;
  r.stairs_class = m.stairs_class;
  r.stairs_mode = m.stairs_mode;
  r.arcane_class = m.arcane_class;
  r.arcane_level = m.arcane_level;
  return r;
}

// --- the area pass (UI thread) ----------------------------------------------------------------------------------------

// The lookup's records of the area's level type, and those of them that give the waypoint's cell (none: no waypoint
// of this kind of area is a floor tile).
void read_records(const Tables& t) {
  const sites::MapFacts& m = sites::map_facts();
  g_area.records.clear();
  g_area.waypoint_records.clear();
  g_area.waypoint_cell = t.waypoint_cell;
  g_area.level_type = game::level_type(g_area.bank, g_area.level);
  const int32_t type = g_area.level_type;
  int32_t range[2] = {-1, -1};
  uint64_t count = 0;
  if (t.waypoint_cell <= 0 || type <= 0 || type >= kMaxLevelTypes || !m.lookup_index || !m.lookup_records ||
      !mem::copy_from(range, m.lookup_index + static_cast<uintptr_t>(type) * 8, sizeof(range)) || range[0] < 0 ||
      range[1] <= range[0] || range[1] - range[0] > kMaxRecords ||
      !mem::read_safe(m.lookup_records + 8, &count) || static_cast<uint64_t>(range[1]) > count)
    return;
  const uintptr_t data = mem::read_ptr(m.lookup_records);
  std::vector<uint8_t> raw(static_cast<size_t>(range[1] - range[0]) * kRecordSize);
  if (!data || !mem::copy_from(raw.data(), data + static_cast<uintptr_t>(range[0]) * kRecordSize, raw.size())) return;
  g_area.records.resize(static_cast<size_t>(range[1] - range[0]));
  for (size_t i = 0; i < g_area.records.size(); ++i) {
    const uint8_t* r = raw.data() + i * kRecordSize;
    TileRecord& rec = g_area.records[i];
    std::memcpy(&rec.level_type, r + 0x00, 4);
    std::memcpy(&rec.tile_type, r + 0x04, 4);
    rec.style = r[0x08];
    rec.first = r[0x09];
    rec.last = r[0x0A];
    std::memcpy(rec.cells, r + 0x0C, sizeof(rec.cells));
    std::memcpy(&rec.count, r + 0x1C, 4);
    if (rec.level_type == type && record_is(rec, t.waypoint_cell)) g_area.waypoint_records.push_back(static_cast<int>(i));
  }
}

// The floor tiles of a built room whose cell the lookup gives as the waypoint's, put on as the game's reveal puts on
// a tile it has seen: by its own per-tile add. Read only up to that call.
void waypoint_tiles(void* drlg_room, void* active) {
  const sites::MapFacts& m = sites::map_facts();
  const uintptr_t tiles = mem::read_ptr(reinterpret_cast<uintptr_t>(active) + kActiveTiles);
  const uintptr_t array = tiles ? mem::read_ptr(tiles + kTilesArray) : 0;
  uint64_t count = 0;
  if (!array || !mem::read_safe(tiles + kTilesCount, &count) || count > kMaxTiles) return;
  const int n = static_cast<int>(g_area.records.size());
  for (uint64_t i = 0; i < count; ++i) {
    const uintptr_t tile = array + static_cast<uintptr_t>(i) * kTileSize;
    uint32_t flags = 0;
    if (!mem::read_safe(tile + kTileFlags, &flags) || (flags & (kTileNever | kTileOnMap))) continue;
    const uintptr_t graphics = mem::read_ptr(tile + kTileGraphics);
    int32_t type = 0, sequence = 0;
    uint32_t style = 0;
    if (!graphics || !mem::read_safe(graphics + static_cast<uintptr_t>(m.tile_type_at), &type) ||
        !mem::read_safe(graphics + static_cast<uintptr_t>(m.tile_style_at), &style) ||
        !mem::read_safe(graphics + static_cast<uintptr_t>(m.tile_sequence_at), &sequence))
      continue;
    bool maybe = false;
    for (const int r : g_area.waypoint_records)
      maybe = maybe || record_fits(g_area.records[static_cast<size_t>(r)], g_area.level_type, type, style, sequence);
    if (!maybe) continue;
    // The lookup takes the first record that fits: it must be one that gives the waypoint's cell.
    const int first = first_fit(g_area.records.data(), n, g_area.level_type, type, style, sequence);
    if (first < 0 || !record_is(g_area.records[static_cast<size_t>(first)], g_area.waypoint_cell)) continue;
    if (game::map_put_tile(g_area.bank, reinterpret_cast<void*>(tile), drlg_room)) ++g_area.waypoints;
  }
}

void object_icon(const Tables& t, uint32_t cls, int32_t x, int32_t y) {
  if (cls >= static_cast<uint32_t>(t.objects)) return;
  const int cell = t.object_cell[cls];
  if (!icon_shows(rules(), static_cast<int>(cls), cell, g_area.act, g_area.level, -1)) return;
  ++g_area.icons;
  if (game::map_put_cell(static_cast<uint16_t>(cell), cell_x(pixel_x(x, y)), cell_y(pixel_y(x, y)))) ++g_area.icons_new;
}

bool dead_here(int level, const char* name) {
  for (int i = 0; i < g_dead_count; ++i)
    if (g_dead[i].level == level && std::strcmp(g_dead[i].name, name) == 0) return true;
  return false;
}

const char* boss_name(const Tables& t, int row) {
  for (int i = 0; i < t.bosses; ++i)
    if (t.boss[i].row == row) return t.boss[i].text;
  return "";
}

void spawn_spot(const Tables& t, uint32_t cls, int32_t x, int32_t y) {
  const PresetClass pc = preset_class(cls, t.monsters, t.supers);
  const char* name = nullptr;
  if (pc.kind == PresetKind::kSuperUnique) name = t.super_name[pc.row];
  else if (pc.kind == PresetKind::kMonster && boss_row(t.mon_flags[pc.row], t.mon_rarity[pc.row])) name = boss_name(t, pc.row);
  if (!name) return;
  EnterCriticalSection(&g_cs);
  // Not the spot of one seen dead, nor of one the automap has drawn already (its own mark follows it).
  bool skip = name[0] && dead_here(g_area.level, name);
  for (int i = 0; i < g_mark_count && !skip && name[0]; ++i)
    skip = g_marks[i].unit && g_marks[i].level == g_area.level && std::strcmp(g_marks[i].name, name) == 0;
  if (!skip && g_mark_count < kMaxMarks) {
    Mark& m = g_marks[g_mark_count++];
    m.level = g_area.level;
    m.layer = g_area.layer;
    m.px = pixel_x(x, y);
    m.py = pixel_y(x, y);
    m.unit = 0;
    m.seen = 0;
    m.layout = true;
    m.champion = false;
    std::snprintf(m.name, sizeof(m.name), "%s", name);
  }
  LeaveCriticalSection(&g_cs);
  if (skip) return;
  ++g_area.spots;
  const size_t at = std::strlen(g_area.names);
  if (at + 4 < sizeof(g_area.names))
    std::snprintf(g_area.names + at, sizeof(g_area.names) - at, "%s%s", at ? ", " : "", name[0] ? name : "?");
}

// A DRLG room's place (tiles) and area; false when they cannot be read.
bool room_rect(uintptr_t room, Rect* r) {
  return room && mem::read_safe(room + kRoomX, &r->x) && mem::read_safe(room + kRoomY, &r->y) &&
         mem::read_safe(room + kRoomW, &r->w) && mem::read_safe(room + kRoomH, &r->h) && r->w > 0 && r->h > 0 &&
         r->w < 0x1000 && r->h < 0x1000;
}
int room_area(uintptr_t room) {
  const uintptr_t area = room ? mem::read_ptr(room + kRoomArea) : 0;
  int32_t id = -1;
  return area && mem::read_safe(area + kAreaId, &id) && id > 0 ? id : -1;
}

const char* level_name(const Tables& t, int level) {
  return level > 0 && level < kMaxNamedLevels && level < t.levels ? t.level_name[level] : "";
}

// A warp's tile (a preset unit of the room's): the link whose warp it is says which area it leads to.
void warp_exit(const Tables& t, uintptr_t room, uint32_t cls, int32_t x, int32_t y) {
  const sites::MapFacts& m = sites::map_facts();
  uintptr_t link = mem::read_ptr(room + static_cast<uintptr_t>(m.links_at));
  for (int n = 0; link && n < kMaxLinks; ++n) {
    const uintptr_t warp = mem::read_ptr(link + kLinkWarp);
    int32_t id = -1;
    if (warp && mem::read_safe(warp + static_cast<uintptr_t>(m.warp_id_at), &id) && id == static_cast<int32_t>(cls)) {
      const int to = room_area(mem::read_ptr(link + kLinkRoom));
      const char* name = level_name(t, to);
      if (to <= 0 || to == g_area.level || !name[0]) return;
      for (const Exit& e : g_area.warps)  // a warp's two tiles: one name
        if (e.to == to && std::abs(e.px - pixel_x(x, y)) < 160 && std::abs(e.py - pixel_y(x, y)) < 80) return;
      Exit e{};
      e.level = g_area.level;
      e.layer = g_area.layer;
      e.px = pixel_x(x, y);
      e.py = pixel_y(x, y);
      e.to = to;
      e.warp = true;
      std::snprintf(e.name, sizeof(e.name), "%s", name);
      g_area.warps.push_back(e);
      return;
    }
    link = mem::read_ptr(link + kLinkNext);
  }
}

// A built room's preset units (its own list, places its own): the objects with an icon, the super uniques and bosses,
// the warps' tiles.
void presets(void* drlg_room, const Tables& t) {
  const sites::MapFacts& m = sites::map_facts();
  const uintptr_t room = reinterpret_cast<uintptr_t>(drlg_room);
  Rect r;
  if (!m.room_presets || !room_rect(room, &r)) return;
  uintptr_t unit = mem::read_ptr(room + static_cast<uintptr_t>(m.room_presets_at));
  for (int n = 0; unit && n < kMaxPresets; ++n) {
    uint32_t kind = 0, cls = 0;
    int32_t x = 0, y = 0;
    if (!mem::read_safe(unit + kPresetType, &kind) || !mem::read_safe(unit + kPresetClass, &cls) ||
        !mem::read_safe(unit + kPresetX, &x) || !mem::read_safe(unit + kPresetY, &y))
      break;
    const int32_t sx = room_subtile(r.x, x), sy = room_subtile(r.y, y);
    if (kind == kPresetObject && g_area.landmarks && game::has_map_objects()) object_icon(t, cls, sx, sy);
    else if (kind == kPresetMonster && g_area.enemies) spawn_spot(t, cls, sx, sy);
    else if (g_area.exits && m.exits && kind == static_cast<uint32_t>(m.warp_preset_type)) warp_exit(t, room, cls, sx, sy);
    unit = mem::read_ptr(unit + kPresetNext);
  }
}

// A subtile's collision flags in a map read into `map` (its place first), or the blocking default when it is not
// in it.
struct Map {
  int32_t x = 0, y = 0, w = 0, h = 0;
  uintptr_t flags = 0;
};
bool read_map(uintptr_t active, const Rect& r, Map* out) {
  const uintptr_t map = active ? mem::read_ptr(active + static_cast<uintptr_t>(sites::map_facts().collision_at)) : 0;
  int32_t place[4] = {};
  if (!map || !mem::copy_from(place, map, sizeof(place))) return false;
  out->x = place[0];
  out->y = place[1];
  out->w = place[2];
  out->h = place[3];
  out->flags = mem::read_ptr(map + kCollFlags);
  return out->flags && out->x == r.x * kSubtiles && out->y == r.y * kSubtiles && out->w == r.w * kSubtiles &&
         out->h == r.h * kSubtiles;
}
bool walkable_at(const Map& m, int32_t x, int32_t y) {
  if (x < m.x || y < m.y || x >= m.x + m.w || y >= m.y + m.h) return false;
  uint16_t flags = 0xFFFF;
  return mem::read_safe(m.flags + 2 * (static_cast<uintptr_t>(y - m.y) * static_cast<uintptr_t>(m.w) +
                                       static_cast<uintptr_t>(x - m.x)),
                        &flags) &&
         walkable(flags);
}

// A built room's edges with the rooms of other areas it is linked to (its near rooms of another area): the subtiles
// along each that a player can walk on, on this room's side and, when the room across is built too, on that one's.
void border_cells(void* drlg_room, void* active) {
  const sites::MapFacts& m = sites::map_facts();
  const uintptr_t room = reinterpret_cast<uintptr_t>(drlg_room);
  Rect r;
  Map mine;
  if (!room_rect(room, &r)) return;
  const uintptr_t nearby = mem::read_ptr(room + static_cast<uintptr_t>(m.near_at));
  uint64_t count = 0;
  if (!nearby || !mem::read_safe(room + static_cast<uintptr_t>(m.near_at) + 8, &count) || !count) return;
  bool have_map = false, read = false;
  for (uint64_t i = 0; i < count && i < kMaxNear; ++i) {
    const uintptr_t other = mem::read_ptr(nearby + static_cast<uintptr_t>(i) * 8);
    const int to = room_area(other);
    Rect o;
    Edge e;
    if (to <= 0 || to == g_area.level || !room_rect(other, &o) || !shared_edge(r, o, &e)) continue;
    if (!read) {
      read = true;
      have_map = read_map(reinterpret_cast<uintptr_t>(active), r, &mine);
      if (!have_map) ++g_area.maps_off;
    }
    if (!have_map) return;
    Map theirs;
    const bool across = read_map(mem::read_ptr(other + kRoomActive), o, &theirs);
    Border* b = nullptr;
    for (Border& k : g_area.borders)
      if (k.to == to && k.vertical == e.vertical && k.line == e.line && k.side == e.side) b = &k;
    if (!b) {
      g_area.borders.push_back(Border{to, e.vertical, e.line, e.side, {}});
      b = &g_area.borders.back();
    }
    for (int32_t s = e.from; s < e.to; ++s) {
      const int32_t x = e.vertical ? e.line : s, y = e.vertical ? s : e.line;
      if (!walkable_at(mine, x, y)) continue;
      if (across && !walkable_at(theirs, e.vertical ? x + e.side : x, e.vertical ? y : y + e.side)) continue;
      b->cells.push_back(s);
    }
  }
}

// The area's exits once all its rooms are gone over: its warps, and the openings along each edge line toward another
// area. This area's exits of an earlier pass give way to them.
void exits_found() {
  const Tables* t = tables_of(g_area.bank);
  if (!t) return;
  std::vector<Exit> found = g_area.warps;
  const unsigned warps = static_cast<unsigned>(found.size());
  char names[256] = {};
  size_t at = 0;
  for (const Exit& e : found)
    if (at + 4 < sizeof(names))
      at += static_cast<size_t>(std::snprintf(names + at, sizeof(names) - at, "%s%s (a warp)", at ? ", " : "", e.name));
  for (const Border& b : g_area.borders) {
    const char* name = level_name(*t, b.to);
    if (!name[0]) continue;
    for (const Opening& o : openings(b.cells)) {
      const Subtile place = exit_place(b.vertical, b.line, b.side, o);
      Exit e{};
      e.level = g_area.level;
      e.layer = g_area.layer;
      e.px = pixel_x(place.x, place.y);
      e.py = pixel_y(place.x, place.y);
      e.to = b.to;
      e.warp = false;
      std::snprintf(e.name, sizeof(e.name), "%s", name);
      found.push_back(e);
      if (at + 4 < sizeof(names))
        at += static_cast<size_t>(std::snprintf(names + at, sizeof(names) - at, "%s%s (an opening of %d subtiles)",
                                                at ? ", " : "", name, o.cells));
    }
  }
  if (at >= sizeof(names)) std::snprintf(names + sizeof(names) - 4, 4, "...");
  size_t kept = 0;
  EnterCriticalSection(&g_cs);
  for (int i = 0; i < g_exit_count; ++i)
    if (g_exits[i].level == g_area.level) g_exits[i--] = g_exits[--g_exit_count];
  for (const Exit& e : found)
    if (g_exit_count < kMaxExits) {
      g_exits[g_exit_count++] = e;
      ++kept;
    }
  LeaveCriticalSection(&g_cs);
  if (kept < found.size() && log_once(&g_exits_full_warned, LogLevel::kWarning))
    log_warn("exits: %d on the map in this game - no more are kept", kMaxExits);
  if (g_area.maps_off && log_once(&g_map_warned, LogLevel::kWarning))
    log_warn("exits: level %d: %u built rooms have no collision map where their place says - their edges with other "
             "areas are left out",
             g_area.level, g_area.maps_off);
  InterlockedIncrement(&g_exit_areas);
  InterlockedExchangeAdd(&g_warp_stat, static_cast<LONG>(warps));
  InterlockedExchangeAdd(&g_border_stat, static_cast<LONG>(found.size() - warps));
  logf("exits: level %d (layer %d): %zu exit%s%s%s", g_area.level, g_area.layer, found.size(),
       found.size() == 1 ? "" : "s", found.empty() ? "" : " - ", names);
}

// --- the automap's draw (its thread) -----------------------------------------------------------------------------------

// A named monster seen dead: its mark goes for good (a layout's spot of its name too), and the area's layout does not
// bring the spot back in this game.
void forget_dead(uint32_t unit, int level, const char* name) {
  bool let_go = false;
  EnterCriticalSection(&g_cs);
  for (int i = 0; i < g_mark_count; ++i) {
    const Mark& m = g_marks[i];
    const bool spot = !m.unit && name[0] && m.level == level && std::strcmp(m.name, name) == 0;
    if (m.unit != unit && !spot) continue;
    if (m.layout && m.name[0] && !dead_here(m.level, m.name) && g_dead_count < kMaxDead) {
      Dead& d = g_dead[g_dead_count++];
      d.level = m.level;
      std::snprintf(d.name, sizeof(d.name), "%s", m.name);
    }
    g_marks[i--] = g_marks[--g_mark_count];
    let_go = true;
  }
  LeaveCriticalSection(&g_cs);
  if (!let_go) return;
  InterlockedIncrement(&g_dead_stat);
  if (log_first(&g_dead_logged, kDeadLogs))
    logf("named enemies: %s (level %d) seen dead - its mark is let go for this game", name[0] ? name : "?", level);
}

// A named monster the automap draws: its mark follows it (a layout's spot of its name in its level becomes its mark), and
// stays where it was last drawn once it is out of the automap's sight.
void remember(uint32_t unit, int level, int layer, int32_t px, int32_t py, const char* name, bool is_champion) {
  EnterCriticalSection(&g_cs);
  Mark* mark = nullptr;
  for (int i = 0; i < g_mark_count && !mark; ++i)
    if (g_marks[i].unit == unit) mark = &g_marks[i];
  for (int i = 0; i < g_mark_count && !mark && name[0] && !is_champion; ++i)
    if (!g_marks[i].unit && g_marks[i].level == level && std::strcmp(g_marks[i].name, name) == 0) mark = &g_marks[i];
  if (!mark && g_mark_count < kMaxMarks) {
    mark = &g_marks[g_mark_count++];
    mark->layout = false;
    std::snprintf(mark->name, sizeof(mark->name), "%s", name);
  }
  if (mark) {
    mark->unit = unit;
    mark->level = level;
    mark->layer = layer;
    mark->px = px;
    mark->py = py;
    mark->seen = g_draw;
    mark->champion = is_champion;
  }
  LeaveCriticalSection(&g_cs);
  if (!mark && log_once(&g_full_warned, LogLevel::kWarning))
    log_warn("named enemies: %d marks on the map in this game - no more are kept", kMaxMarks);
}

void counted(Unit* u, uint32_t cls, uint16_t kind, const char* name) {
  const uint32_t id = game::unit_id(u);
  EnterCriticalSection(&g_cs);
  bool known = false;
  for (int i = 0; i < g_drawn_count && !known; ++i) known = g_drawn[i] == id;
  if (!known && g_drawn_count < kMaxDrawn) g_drawn[g_drawn_count++] = id;
  LeaveCriticalSection(&g_cs);
  if (known) return;
  InterlockedIncrement(&g_drawn_stat);
  if (log_first(&g_drawn_logged, kDrawnLogs))
    logf("named enemies: %s (monster class %u, kind 0x%X) drawn on the map", name[0] ? name : "?", cls, kind);
}

// Whether a name is written at this point: not when the same name was written close by in this draw (a champion
// pack's members share theirs). The draw thread only.
bool label_here(uint64_t point, const char* name, float scale) {
  if (g_labels_draw != g_draw) {
    g_labels_draw = g_draw;
    g_label_count = 0;
  }
  uint32_t hash = 2166136261u;  // FNV-1a
  for (const char* p = name; *p; ++p) hash = (hash ^ static_cast<uint8_t>(*p)) * 16777619u;
  const int32_t x = static_cast<int32_t>(point), y = static_cast<int32_t>(point >> 32);
  const float apart = kLabelApart * (scale > 0.0f ? scale : 1.0f);
  for (int i = 0; i < g_label_count; ++i) {
    const float dx = static_cast<float>(g_labels[i].x - x), dy = static_cast<float>(g_labels[i].y - y);
    if (g_labels[i].hash == hash && dx * dx + dy * dy < apart * apart) return false;
  }
  if (g_label_count < kMaxLabels) g_labels[g_label_count++] = Label{x, y, hash};
  return true;
}

void monster_drawn(Unit* u, const void* view) {
  const Tables* t = tables_of(game::unit_table_bank(u));
  if (!t) return;
  const uint32_t cls = game::unit_class(u);
  const bool row = cls < static_cast<uint32_t>(t->monsters);
  if (row && (t->mon_flags[cls] & kMonNpc)) return;
  const uint16_t kind = game::monster_type_flags(u);
  if (!named(kind, row && boss_row(t->mon_flags[cls], t->mon_rarity[cls]))) return;
  char name[kNameLen] = {};
  if (const char* raw = game::client_unit_name(u)) {
    std::snprintf(name, sizeof(name), "%s", raw);
    strip_tag(name);
  }
  const uint32_t id = game::unit_id(u);
  const int level = game::unit_level_id(u);
  if (game::unit_is_dead(u)) {
    forget_dead(id, level, name);
    return;
  }
  // The client's own rule: a pet (a revived unique of yours) is no enemy.
  Unit* me = game::local_client_player();
  int32_t px = 0, py = 0;
  if (!me || !game::is_hostile(me, u) || !game::unit_pixels(u, &px, &py)) return;
  const bool is_champion = champion(kind);
  remember(id, level, game::automap_layer(), px, py, name, is_champion);
  uint64_t point = 0;
  if (!game::map_point(view, px, py, &point)) return;
  const float scale = game::map_scale(view);
  game::map_marker(point, kMarkerEnemy, scale);
  if (label_here(point, name, scale))
    game::map_name(name, point, scale, is_champion ? kChampionColor : game::map_name_color());
  counted(u, cls, kind, name);
}

// Once a draw (the local player's own call): the marks on the automap's layer whose monster the automap does not draw
// itself now - a layout's spawn spot, or a named monster where it was last drawn -, and the exits of the area the
// player is in (-1: none; at a border the next area's names its own side, and would stand over this one's).
void marks_drawn(const void* view, bool enemies, int exits_of) {
  ++g_draw;
  const int layer = game::automap_layer();
  if (layer < 0) return;
  const float scale = game::map_scale(view);
  const int color = game::map_name_color();
  EnterCriticalSection(&g_cs);
  for (int i = 0; enemies && i < g_mark_count; ++i) {
    const Mark& m = g_marks[i];
    uint64_t point = 0;
    // Its monster drawn in this draw or the one before (the units come before the player, or after it).
    if (m.layer != layer || g_draw - m.seen <= 2 || !game::map_point(view, m.px, m.py, &point)) continue;
    game::map_marker(point, kMarkerEnemy, scale);
    if (label_here(point, m.name, scale)) game::map_name(m.name, point, scale, m.champion ? kChampionColor : color);
  }
  for (int i = 0; exits_of > 0 && i < g_exit_count; ++i) {
    const Exit& e = g_exits[i];
    uint64_t point = 0;
    if (e.level != exits_of || e.layer != layer || !game::map_point(view, e.px, e.py, &point)) continue;
    if (label_here(point, e.name, scale)) game::map_name(e.name, point, scale, kExitColor);
  }
  LeaveCriticalSection(&g_cs);
}

}  // namespace

void set_services(const D2RL::PluginContext* ctx, const D2RL::DataTableService* tables,
                  const D2RL::LocalizationService* strings) {
  if (!g_cs_ready) {
    InitializeCriticalSection(&g_cs);
    g_cs_ready = true;
  }
  g_ctx = ctx;
  g_tables = tables;
  g_strings = strings;
}

void set_draw_hooked(bool hooked) { InterlockedExchange(&g_draw_hooked, hooked ? 1 : 0); }

void bind(bool ui_thread) {
  const sites::MapFacts& m = sites::map_facts();
  const bool tables = g_tables && g_tables->getTable;
  const char* pass = !ui_thread                     ? "no UI thread from the loader"
                     : !g_cs_ready                  ? "not set up"
                     : !game::has_map_reveal()      ? "the game's map routines not found"
                     : !m.presets || !m.room_presets ? "the game's preset units not found as expected"
                     : !tables                      ? "no data table service from the loader"
                                                    : nullptr;
  const char* landmarks = pass                                                     ? pass
                          : !game::has_map_objects() && !game::has_map_tiles()     ? "the automap's lists not found as expected"
                                                                                   : nullptr;
  const char* enemies = pass                       ? pass
                        : !game::has_map_draw()    ? "the automap's unit draw not found as expected"
                        : !game::has_hostility()   ? "the client's hostility test not found"
                        : !g_draw_hooked           ? "the automap's unit draw not hooked"
                                                   : nullptr;
  const char* exits = pass                       ? pass
                      : !m.exits                 ? "the game's links between areas not found as expected"
                      : !game::has_map_draw()    ? "the automap's unit draw not found as expected"
                      : !g_draw_hooked           ? "the automap's unit draw not hooked"
                                                 : nullptr;
  cheats::set_why_not(cheats::kMapLandmarks, landmarks);
  cheats::set_why_not(cheats::kMapEnemies, enemies);
  cheats::set_why_not(cheats::kMapExits, exits);
  InterlockedExchange(&g_landmarks_ok, landmarks ? 0 : 1);
  InterlockedExchange(&g_enemies_ok, enemies ? 0 : 1);
  InterlockedExchange(&g_exits_ok, exits ? 0 : 1);
  logf("map: landmarks %s (waypoint tiles %s, object icons %s), named enemies %s, area names at exits %s",
       landmarks ? landmarks : "ready", game::has_map_tiles() ? "yes" : "NO", game::has_map_objects() ? "yes" : "NO",
       enemies ? enemies : "ready", exits ? exits : "ready");
}

bool landmarks_ready() { return g_landmarks_ok != 0; }
bool enemies_ready() { return g_enemies_ok != 0; }
bool exits_ready() { return g_exits_ok != 0; }

void collect() {
  if (!g_ctx || !g_tables || (!g_landmarks_ok && !g_enemies_ok && !g_exits_ok)) return;
  for (int b = 1; b < kBanks; ++b) {
    Tables& t = g_banks[b];
    if (t.ready || !read_bank(b, &t)) continue;
    InterlockedExchange(&t.ready, 1);
    char bosses[256] = {};
    size_t at = 0;
    for (int i = 0; i < t.bosses && at + 2 < sizeof(bosses); ++i)
      at += static_cast<size_t>(std::snprintf(bosses + at, sizeof(bosses) - at, "%s%s", i ? ", " : "",
                                              t.boss[i].text[0] ? t.boss[i].text : "?"));
    logf("map: bank %d: %d object classes with an icon (the waypoints' %d), %d super uniques (the first \"%s\"), %d "
         "bosses (%s), %d levels (%d named: level 2 \"%s\")",
         b, t.icon_kinds, t.waypoint_cell, t.supers, t.super_name[0], t.bosses, bosses, t.levels, t.level_names,
         level_name(t, 2));
  }
}

bool ready(uint8_t bank) { return tables_of(bank) != nullptr; }

void area_begins(int level_id, int layer, uint8_t bank, bool landmarks, bool enemies, bool exits) {
  g_area = Area{};
  const Tables* t = tables_of(bank);
  if (!t || level_id <= 0) return;
  g_area.on = true;
  g_area.level = level_id;
  g_area.layer = layer;
  g_area.bank = bank;
  g_area.act = level_id < t->levels ? t->level_act[level_id] : -1;
  g_area.landmarks = landmarks && g_landmarks_ok;
  g_area.enemies = enemies && g_enemies_ok;
  g_area.exits = exits && g_exits_ok;
  if (g_area.landmarks && game::has_map_tiles()) read_records(*t);
  if (g_area.enemies) {
    // The area's layout is read again: its spots read before go (the marks of the monsters seen stay).
    EnterCriticalSection(&g_cs);
    for (int i = 0; i < g_mark_count; ++i)
      if (!g_marks[i].unit && g_marks[i].level == level_id) g_marks[i--] = g_marks[--g_mark_count];
    LeaveCriticalSection(&g_cs);
  }
}

void room(void* drlg_room, void* active_room) {
  if (!g_area.on || !drlg_room) return;
  const Tables* t = tables_of(g_area.bank);
  if (!t) return;
  perf::Timer timer(perf::kMapMarksRoom);
  if (g_area.landmarks && active_room && !g_area.waypoint_records.empty()) waypoint_tiles(drlg_room, active_room);
  if (g_area.landmarks || g_area.enemies || g_area.exits) presets(drlg_room, *t);
  if (g_area.exits && active_room) border_cells(drlg_room, active_room);
}

void area_ends() {
  if (!g_area.on) return;
  if (g_area.landmarks) {
    InterlockedIncrement(&g_areas);
    InterlockedExchangeAdd(&g_icons, static_cast<LONG>(g_area.icons_new));
    InterlockedExchangeAdd(&g_waypoints, static_cast<LONG>(g_area.waypoints));
    logf("landmarks: level %d (layer %d, act %d): %u object icons on the map (%u new), %u waypoint tiles put on%s",
         g_area.level, g_area.layer, g_area.act + 1, g_area.icons, g_area.icons_new, g_area.waypoints,
         g_area.waypoint_records.empty() ? " (no waypoint of this kind of area is a floor tile)" : "");
  }
  if (g_area.enemies) {
    InterlockedExchangeAdd(&g_spot_stat, static_cast<LONG>(g_area.spots));
    logf("named enemies: level %d (layer %d): %u spawn spot%s%s%s", g_area.level, g_area.layer, g_area.spots,
         g_area.spots == 1 ? "" : "s", g_area.spots ? " - " : "", g_area.names);
  }
  if (g_area.exits) exits_found();
  if (g_area.landmarks || g_area.enemies)
    cheats::note("Map: %u landmarks and %u named enemies' spots in this area", g_area.icons + g_area.waypoints,
                 g_area.spots);
  g_area.on = false;
  g_area.warps.clear();
  g_area.borders.clear();
}

void unit_drawn(Unit* unit, const void* view) {
  if (!unit || !view) return;
  const bool enemies = g_enemies_ok && cheats::enabled(cheats::kMapEnemies);
  const bool exits = g_exits_ok && cheats::enabled(cheats::kMapExits);
  if (!enemies && !exits) return;
  perf::Timer timer(perf::kMapMarksDraw);
  const uint32_t type = game::unit_type(unit);
  if (type == game::kMonster) {
    if (enemies) monster_drawn(unit, view);
  } else if (type == game::kPlayer && game::is_local_player(unit)) {
    marks_drawn(view, enemies, exits ? game::unit_level_id(unit) : -1);
  }
}

void game_left() {
  if (g_cs_ready) {
    EnterCriticalSection(&g_cs);
    g_mark_count = 0;
    g_dead_count = 0;
    g_drawn_count = 0;
    g_exit_count = 0;
    LeaveCriticalSection(&g_cs);
  }
  for (volatile LONG* v : {&g_areas, &g_icons, &g_waypoints, &g_spot_stat, &g_drawn_stat, &g_dead_stat, &g_exit_areas,
                           &g_warp_stat, &g_border_stat})
    InterlockedExchange(v, 0);
}

Stats stats() {
  Stats s;
  s.areas = static_cast<unsigned>(g_areas);
  s.icons = static_cast<unsigned>(g_icons);
  s.waypoints = static_cast<unsigned>(g_waypoints);
  s.spots = static_cast<unsigned>(g_spot_stat);
  s.drawn = static_cast<unsigned>(g_drawn_stat);
  s.dead = static_cast<unsigned>(g_dead_stat);
  s.exit_areas = static_cast<unsigned>(g_exit_areas);
  s.warps = static_cast<unsigned>(g_warp_stat);
  s.borders = static_cast<unsigned>(g_border_stat);
  if (g_cs_ready) {
    EnterCriticalSection(&g_cs);
    for (int i = 0; i < g_mark_count; ++i) s.kept += g_marks[i].unit ? 1u : 0u;
    s.exits = static_cast<unsigned>(g_exit_count);
    LeaveCriticalSection(&g_cs);
  }
  return s;
}

}  // namespace d2rcc::mapmarks

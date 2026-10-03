#include "roomfill.h"

#include <windows.h>

#include <vector>

#include "cheats.h"
#include "game.h"
#include "log.h"
#include "mapmarks.h"
#include "mem.h"
#include "perf.h"
#include "sites.h"

namespace d2rcc::roomfill {
namespace {

constexpr int kMaxRooms = 8192;          // an area's room list is walked no further
constexpr int kMaxRoomUnits = 4096;      // ... nor a room's units
constexpr int kRoomsPerTick = 8;         // rooms built in one server frame at the most
constexpr double kBudgetMs = 2.0;        // ... and for this long at the most
constexpr ULONGLONG kFillWaitMs = 5000;  // the game's fill pass gets this long for the rooms built
constexpr uint32_t kSafetyTicks = 25;    // a look once a second (server frames) besides what announces an area

// The area being filled (the server thread only). The level pointer is only compared: the room list is walked again
// from the level the character stands in at each tick.
struct Job {
  void* level = nullptr;
  void* drlg = nullptr;
  int id = -1;
  uint8_t bank = 0;
  int built = 0, failed = 0;
  bool all_built = false;
  ULONGLONG started = 0, built_at = 0;
  std::vector<void*> refused;  // rooms the game could not build: not asked again
};
struct Done {
  void* drlg;
  int id;
  bool filled;   // its rooms were built and filled (else it had nothing to fill for the landmarks alone)
  bool objects;  // ... and its objects handed to the map (filled with the landmarks off: not yet)
};

Job g_job;
std::vector<Done> g_done;  // the areas filled (or with nothing to fill) on a DRLG of this game
void* g_read_drlg = nullptr;  // the area whose named enemies were read last (an area entered is read again)
int g_read_id = -1;
volatile LONG g_ready = 0, g_armed = 1, g_reset = 0;
volatile LONG g_areas = 0, g_rooms = 0, g_objects = 0, g_monsters = 0, g_reads_logged = 0;
LARGE_INTEGER g_freq{};

Done* done_of(void* drlg, int id) {
  for (Done& d : g_done)
    if (d.drlg == drlg && d.id == id) return &d;
  return nullptr;
}

double ms_since(const LARGE_INTEGER& t0) {
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  return g_freq.QuadPart ? static_cast<double>(now.QuadPart - t0.QuadPart) * 1000.0 / static_cast<double>(g_freq.QuadPart)
                         : 0.0;
}

bool refused(void* room) {
  for (void* r : g_job.refused)
    if (r == room) return true;
  return false;
}

bool filled(void* active) {
  const sites::FillFacts& f = sites::fill_facts();
  uint32_t state = 0;
  return mem::read_safe(reinterpret_cast<uintptr_t>(active) + static_cast<uintptr_t>(f.room_state_at), &state) &&
         (state & f.filled_bit) != 0;
}

// Some rooms built: as many as the frame's share allows. True when no room is left to build.
bool build_some() {
  LARGE_INTEGER t0;
  QueryPerformanceCounter(&t0);
  int now = 0, n = 0;
  for (void* room = game::level_first_room(g_job.level); room && n < kMaxRooms; room = game::room_next(room), ++n) {
    if (game::room_built(room) || refused(room)) continue;
    if (now >= kRoomsPerTick || ms_since(t0) >= kBudgetMs) return false;
    ++now;
    if (game::build_room(g_job.bank, room)) {
      ++g_job.built;
    } else {
      ++g_job.failed;
      g_job.refused.push_back(room);
    }
  }
  return true;
}

// Whether every built room of the area has been filled by the game's pass.
bool all_filled() {
  int n = 0;
  for (void* room = game::level_first_room(g_job.level); room && n < kMaxRooms; room = game::room_next(room), ++n)
    if (void* active = game::room_built(room); active && !filled(active)) return false;
  return true;
}

// What the area's built rooms hold: its objects (those with an icon go on the map) and its live monsters (the named
// ones are marked). Returns how many rooms are built, and how many of them the game has not filled.
int units_of(void* level, std::vector<mapmarks::PlacedObject>* objects, std::vector<Unit*>* monsters, int* unfilled) {
  const sites::FillFacts& f = sites::fill_facts();
  int n = 0, rooms = 0;
  for (void* room = game::level_first_room(level); room && n < kMaxRooms; room = game::room_next(room), ++n) {
    void* active = game::room_built(room);
    if (!active) continue;
    ++rooms;
    if (unfilled && !filled(active)) ++*unfilled;
    uintptr_t unit = mem::read_ptr(reinterpret_cast<uintptr_t>(active) + static_cast<uintptr_t>(f.room_units_at));
    for (int k = 0; unit && k < kMaxRoomUnits; ++k) {
      Unit* u = reinterpret_cast<Unit*>(unit);
      const uint32_t type = game::unit_type(u);
      int32_t px = 0, py = 0;
      if (objects && type == game::kObject && game::unit_pixels(u, &px, &py))
        objects->push_back(mapmarks::PlacedObject{game::unit_class(u), px, py});
      else if (monsters && type == game::kMonster && !game::unit_is_dead(u))
        monsters->push_back(u);
      unit = mem::read_ptr(unit + static_cast<uintptr_t>(f.unit_next_at));
    }
  }
  return rooms;
}

// The objects of an area filled before while the landmarks were off, handed to the map once they are on.
void give_objects(void* level, int id, uint8_t bank) {
  std::vector<mapmarks::PlacedObject> objects;
  units_of(level, &objects, nullptr, nullptr);
  const int icons = mapmarks::objects_found(bank, id, objects.data(), objects.size());
  InterlockedExchangeAdd(&g_objects, icons);
  logf("landmarks: level %d, filled before: %zu objects in the area, %d with an icon handed to the map", id,
       objects.size(), icons);
}

// The named enemies of an area filled before, read again as it is entered (they may have died, or been joined by
// others: a terror zone's Heralds come with kills).
void read_monsters(void* level, int id, uint8_t bank) {
  std::vector<Unit*> monsters;
  units_of(level, nullptr, &monsters, nullptr);
  const int named = mapmarks::monsters_found(bank, id, monsters.data(), monsters.size());
  if (log_first(&g_reads_logged, 4))
    logf("named enemies: level %d entered again: %d of its %zu monsters named, read from the server", id, named,
         monsters.size());
}

// Every object in the area's built rooms handed to the map (those with an icon go on it), and every monster (the named
// ones are marked).
void report(ULONGLONG now, bool landmarks, bool enemies) {
  std::vector<mapmarks::PlacedObject> objects;
  std::vector<Unit*> monsters;
  int unfilled = 0;
  const int rooms = units_of(g_job.level, landmarks ? &objects : nullptr, enemies ? &monsters : nullptr, &unfilled);
  const int icons = landmarks ? mapmarks::objects_found(g_job.bank, g_job.id, objects.data(), objects.size()) : 0;
  const int named = enemies ? mapmarks::monsters_found(g_job.bank, g_job.id, monsters.data(), monsters.size()) : 0;
  InterlockedIncrement(&g_areas);
  InterlockedExchangeAdd(&g_rooms, g_job.built);
  InterlockedExchangeAdd(&g_objects, icons);
  InterlockedExchangeAdd(&g_monsters, named);
  char what[160] = {};
  if (landmarks && enemies)
    std::snprintf(what, sizeof(what), "%zu objects in the area, %d with an icon handed to the map; %zu monsters, %d "
                  "of them named", objects.size(), icons, monsters.size(), named);
  else if (landmarks)
    std::snprintf(what, sizeof(what), "%zu objects in the area, %d with an icon handed to the map", objects.size(), icons);
  else
    std::snprintf(what, sizeof(what), "%zu monsters in the area, %d of them named", monsters.size(), named);
  logf("%s: level %d: %d rooms built ahead for the game to fill (%d were built already, %d could not be), filled in "
       "%llu ms%s; %s",
       landmarks ? "landmarks" : "named enemies", g_job.id, g_job.built, rooms > g_job.built ? rooms - g_job.built : 0,
       g_job.failed, static_cast<unsigned long long>(now - g_job.started), unfilled ? " (some not filled in time)" : "",
       what);
}

}  // namespace

void bind() {
  QueryPerformanceFrequency(&g_freq);
  const sites::FillFacts& f = sites::fill_facts();
  const bool ok = f.known && game::has_room_build();
  InterlockedExchange(&g_ready, ok ? 1 : 0);
  logf("landmarks: the shrines the game rolls %s; named enemies %s",
       ok ? "are put on the map ahead (the rooms of an area built as it is entered, for the game to fill)"
          : "are put on the map as the game shows them (its room fill not found as expected)",
       ok ? "are read from the server's side as an area is entered (its rooms filled the same way)"
          : "are marked as the client sees them");
}

bool ready() { return g_ready != 0; }
void arm() { InterlockedExchange(&g_armed, 1); }

void on_tick(void* game, Unit* player, uint32_t tick) {
  if (!g_ready) return;
  if (g_reset && InterlockedExchange(&g_reset, 0)) {
    g_job = Job{};
    g_done.clear();
    g_read_drlg = nullptr;
    g_read_id = -1;
  }
  const bool landmarks = cheats::enabled(cheats::kMapLandmarks) && mapmarks::landmarks_ready();
  const bool enemies = cheats::enabled(cheats::kMapEnemies) && mapmarks::enemies_ready();
  if (!landmarks && !enemies) {
    g_job = Job{};
    g_read_drlg = nullptr;
    g_read_id = -1;
    return;
  }
  // Looked at when something says the character may stand somewhere new, once a second besides, and at every frame
  // while an area is being filled.
  const bool armed = g_armed != 0;
  if (!g_job.level && !armed && tick % kSafetyTicks != 0) return;
  if (armed) InterlockedExchange(&g_armed, 0);
  void* level = game::unit_level(player);
  void* drlg = game::level_drlg(level);
  const int id = game::level_id_of(level);
  if (!level || !drlg || id <= 0) return;
  const uint8_t bank = game::game_bank(game);
  if (!mapmarks::ready(bank)) return;  // the tables, in the first seconds of a game
  // Filled for the landmarks: an area whose object groups can roll an object with an icon. For the named enemies: every
  // area but a town (its monsters are made as its rooms are filled).
  const bool wants = (landmarks && mapmarks::level_rolls_icons(bank, id)) || (enemies && !game::town_level(id));
  if (Done* d = done_of(drlg, id); d && (d->filled || !wants)) {
    // Filled before: its named enemies are read again as it is entered (a switch changed counts too), and its objects
    // handed to the map if the landmarks were off then.
    const bool entered = armed || drlg != g_read_drlg || id != g_read_id;
    if (enemies && d->filled && entered) read_monsters(level, id, bank);
    if (landmarks && d->filled && !d->objects && mapmarks::level_rolls_icons(bank, id)) {
      give_objects(level, id, bank);
      d->objects = true;
    }
    g_read_drlg = drlg;
    g_read_id = id;
    g_job = Job{};
    return;
  }
  if (!wants) {
    g_done.push_back(Done{drlg, id, false, false});  // nothing to fill it for
    g_job = Job{};
    return;
  }
  perf::Timer timer(perf::kRoomFill);
  const ULONGLONG now = GetTickCount64();
  if (g_job.level != level || g_job.drlg != drlg || g_job.id != id) {
    g_job = Job{};
    g_job.level = level;
    g_job.drlg = drlg;
    g_job.id = id;
    g_job.bank = bank;
    g_job.started = now;
  }
  if (!g_job.all_built) {
    if (!build_some()) return;  // more at the next frame
    g_job.all_built = true;
    g_job.built_at = now;
    return;  // the game fills them in its own pass
  }
  if (!all_filled() && now - g_job.built_at < kFillWaitMs) return;
  report(now, landmarks, enemies);
  if (Done* d = done_of(drlg, id)) {
    d->filled = true;
    d->objects = landmarks;
  } else {
    g_done.push_back(Done{drlg, id, true, landmarks});
  }
  g_read_drlg = drlg;
  g_read_id = id;
  g_job = Job{};
}

void game_left() {
  InterlockedExchange(&g_reset, 1);
  InterlockedExchange(&g_armed, 1);
  for (volatile LONG* v : {&g_areas, &g_rooms, &g_objects, &g_monsters, &g_reads_logged}) InterlockedExchange(v, 0);
}

Stats stats() {
  Stats s;
  s.areas = static_cast<unsigned>(g_areas);
  s.rooms = static_cast<unsigned>(g_rooms);
  s.objects = static_cast<unsigned>(g_objects);
  s.monsters = static_cast<unsigned>(g_monsters);
  return s;
}

}  // namespace d2rcc::roomfill

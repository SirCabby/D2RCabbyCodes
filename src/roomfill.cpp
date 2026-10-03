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
};

Job g_job;
std::vector<Done> g_done;  // the areas filled (or with nothing to fill) on a DRLG of this game
volatile LONG g_ready = 0, g_armed = 1, g_reset = 0;
volatile LONG g_areas = 0, g_rooms = 0, g_objects = 0;
LARGE_INTEGER g_freq{};

bool is_done(void* drlg, int id) {
  for (const Done& d : g_done)
    if (d.drlg == drlg && d.id == id) return true;
  return false;
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

// Every object in the area's built rooms, handed to the map (those with an icon go on it).
void report(ULONGLONG now) {
  const sites::FillFacts& f = sites::fill_facts();
  std::vector<mapmarks::PlacedObject> objects;
  int n = 0, unfilled = 0, rooms = 0;
  for (void* room = game::level_first_room(g_job.level); room && n < kMaxRooms; room = game::room_next(room), ++n) {
    void* active = game::room_built(room);
    if (!active) continue;
    ++rooms;
    if (!filled(active)) ++unfilled;
    uintptr_t unit = mem::read_ptr(reinterpret_cast<uintptr_t>(active) + static_cast<uintptr_t>(f.room_units_at));
    for (int k = 0; unit && k < kMaxRoomUnits; ++k) {
      Unit* u = reinterpret_cast<Unit*>(unit);
      int32_t px = 0, py = 0;
      if (game::unit_type(u) == game::kObject && game::unit_pixels(u, &px, &py))
        objects.push_back(mapmarks::PlacedObject{game::unit_class(u), px, py});
      unit = mem::read_ptr(unit + static_cast<uintptr_t>(f.unit_next_at));
    }
  }
  const int icons = mapmarks::objects_found(g_job.bank, g_job.id, objects.data(), objects.size());
  InterlockedIncrement(&g_areas);
  InterlockedExchangeAdd(&g_rooms, g_job.built);
  InterlockedExchangeAdd(&g_objects, icons);
  logf("landmarks: level %d: %d rooms built ahead for the game to fill (%d were built already, %d could not be), "
       "filled in %llu ms%s; %zu objects in the area, %d with an icon handed to the map",
       g_job.id, g_job.built, rooms > g_job.built ? rooms - g_job.built : 0, g_job.failed,
       static_cast<unsigned long long>(now - g_job.started), unfilled ? " (some not filled in time)" : "",
       objects.size(), icons);
}

}  // namespace

void bind() {
  QueryPerformanceFrequency(&g_freq);
  const sites::FillFacts& f = sites::fill_facts();
  const bool ok = f.known && game::has_room_build();
  InterlockedExchange(&g_ready, ok ? 1 : 0);
  logf("landmarks: the shrines the game rolls %s", ok ? "are put on the map ahead (the rooms of an area built as it is "
                                                        "entered, for the game to fill)"
                                                      : "are put on the map as the game shows them (its room fill not "
                                                        "found as expected)");
}

bool ready() { return g_ready != 0; }
void arm() { InterlockedExchange(&g_armed, 1); }

void on_tick(void* game, Unit* player, uint32_t tick) {
  if (!g_ready) return;
  if (g_reset && InterlockedExchange(&g_reset, 0)) {
    g_job = Job{};
    g_done.clear();
  }
  if (!cheats::enabled(cheats::kMapLandmarks)) {
    g_job = Job{};
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
  if (is_done(drlg, id)) {
    g_job = Job{};
    return;
  }
  const uint8_t bank = game::game_bank(game);
  if (!mapmarks::ready(bank)) return;  // the tables, in the first seconds of a game
  if (!mapmarks::level_rolls_icons(bank, id)) {
    g_done.push_back(Done{drlg, id});  // nothing it rolls has an icon
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
  report(now);
  g_done.push_back(Done{drlg, id});
  g_job = Job{};
}

void game_left() {
  InterlockedExchange(&g_reset, 1);
  InterlockedExchange(&g_armed, 1);
  for (volatile LONG* v : {&g_areas, &g_rooms, &g_objects}) InterlockedExchange(v, 0);
}

Stats stats() {
  Stats s;
  s.areas = static_cast<unsigned>(g_areas);
  s.rooms = static_cast<unsigned>(g_rooms);
  s.objects = static_cast<unsigned>(g_objects);
  return s;
}

}  // namespace d2rcc::roomfill

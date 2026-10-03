#include "mapreveal.h"

#include <windows.h>

#include <bitset>
#include <initializer_list>

#include "cheats.h"
#include "game.h"
#include "gate.h"
#include "log.h"
#include "mapmarks.h"
#include "perf.h"

namespace d2rcc::mapreveal {
namespace {

constexpr int kMaxLevels = 4096;  // level ids remembered (the game has about 140; mods add more)
constexpr int kMaxRooms = 8192;   // a level's room list is walked no further
constexpr double kBudgetMs = 3.0; // building rooms per frame; a large area takes a few frames
constexpr ULONGLONG kWaitNoteMs = 3000;
// The map is looked at when something says the character may stand in an area not revealed yet (arm: an
// area or act entered, a game joined, a switch), from then on every frame until that area is done - for
// this long at the most while it cannot be started (no character yet, the area not the one announced, the
// automap on another layer) - and once a second besides, should an area be entered that nothing announced.
constexpr ULONGLONG kArmedMs = 10000;
constexpr ULONGLONG kSafetyLookMs = 1000;

// What a pass over an area does with each room: reveal it whole, put on its landmarks, read its named enemies' spawn
// spots, find its exits (the last three mapmarks.cpp's). Each is done once per level and act visit.
enum Part : unsigned { kReveal = 1u, kLandmarks = 2u, kEnemies = 4u, kExits = 8u };
constexpr int kParts = 4;
constexpr unsigned kMarks = kLandmarks | kEnemies | kExits;

volatile LONG g_bound = 0;      // the pass can run: the game's map routines are there
volatile LONG g_reveal_ok = 0;  // ... and the reveal can
volatile LONG g_reset = 0;  // a game was left: the UI thread forgets its per-game state
volatile LONG g_armed = 0;  // the character may stand somewhere new
volatile LONG g_announced = -1;  // ... in this level, when the loader said which (-1: not said)
volatile LONG g_areas = 0;
volatile LONG g_rooms = 0;

// UI thread only. The level pointer is only compared, never read after the frame it was found in: the
// list is walked again from the level the player stands in each frame, so a level the game has freed
// (a new act) is never touched.
struct Job {
  void* level = nullptr;
  int id = -1;
  unsigned parts = 0;
  bool begun = false;
  int done = 0;     // rooms of the list handled
  int built = 0;    // ... built
  int failed = 0;   // ... that the game could not build
  int frames = 0;
  double work_ms = 0.0;
  ULONGLONG waiting_since = 0;
  bool noted_wait = false;
  bool finished = false;
};
Job g_job;
std::bitset<kMaxLevels> g_done[kParts];  // the levels gone over on the DRLG below, per part
void* g_drlg = nullptr;          // the client's DRLG (one per act): another one has new levels
bool g_said_thread = false;
gate::Work g_work(kArmedMs, kSafetyLookMs);  // when a frame looks at the map
int g_wanted = -1;               // the level the loader announced, until the character is seen in it
LARGE_INTEGER g_freq{};

int index_of(unsigned part) { return part == kReveal ? 0 : part == kLandmarks ? 1 : part == kEnemies ? 2 : 3; }

unsigned wanted_parts() {
  unsigned parts = 0;
  if (g_reveal_ok && cheats::enabled(cheats::kRevealMap)) parts |= kReveal;
  if (mapmarks::landmarks_ready() && cheats::enabled(cheats::kMapLandmarks)) parts |= kLandmarks;
  if (mapmarks::enemies_ready() && cheats::enabled(cheats::kMapEnemies)) parts |= kEnemies;
  if (mapmarks::exits_ready() && cheats::enabled(cheats::kMapExits)) parts |= kExits;
  return parts;
}

double ms_since(const LARGE_INTEGER& t0) {
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  return g_freq.QuadPart ? static_cast<double>(now.QuadPart - t0.QuadPart) * 1000.0 / static_cast<double>(g_freq.QuadPart)
                         : 0.0;
}

// One look at where the character stands, and the work on its area. True while the next frame has to look
// again: the area is being gone over, or it cannot be started yet.
bool look(ULONGLONG now) {
  const bool may_wait = g_work.may_wait(now);
  const unsigned wanted = wanted_parts();
  if (!wanted) {
    g_job = Job{};
    g_wanted = -1;
    return false;
  }
  game::ClientLevel lv;
  if (!game::client_level(&lv)) return may_wait;  // no character in a level yet
  if (lv.drlg != g_drlg) {
    g_drlg = lv.drlg;
    for (std::bitset<kMaxLevels>& d : g_done) d.reset();
    g_job = Job{};
  }
  // The loader may announce an area before the client's character stands in it: an area that is done
  // already is looked at again until the character is in the one announced.
  const bool elsewhere = g_wanted > 0 && lv.id != g_wanted && may_wait;
  if (!elsewhere) g_wanted = -1;
  const bool remembered = lv.id < kMaxLevels;
  unsigned needed = wanted;
  if (remembered)
    for (const unsigned part : {kReveal, kLandmarks, kEnemies, kExits})
      if (g_done[index_of(part)][static_cast<size_t>(lv.id)]) needed &= ~part;
  // The landmarks, the spawn spots and the exits need the rows the tick reads (in the first second of a game).
  const bool tables_wait = (needed & kMarks) && !mapmarks::ready(lv.bank);
  if (tables_wait) needed &= ~kMarks;
  const bool look_again = elsewhere || (tables_wait && may_wait);
  if (!needed) return look_again;
  const bool same = g_job.level == lv.level && g_job.id == lv.id && g_job.parts == needed;
  if (same && g_job.finished) return look_again;
  if (!same) {
    g_job = Job{};
    g_job.level = lv.level;
    g_job.id = lv.id;
    g_job.parts = needed;
    g_job.waiting_since = now;
  }
  // The game moves the automap to an area's layer shortly after the player enters the area. The
  // rooms wait for it, so their tiles land where the game draws them and no layer is switched.
  const int layer = game::level_layer(lv.bank, lv.id);
  const int active = game::automap_layer();
  if (layer < 0 || active != layer) {
    if (!g_job.noted_wait && now - g_job.waiting_since > kWaitNoteMs) {
      g_job.noted_wait = true;
      logf("map reveal: level %d waits for the automap (the level's layer %d, the automap's %d)", lv.id, layer,
           active);
    }
    return now - g_job.waiting_since < kArmedMs;  // then once a second
  }
  if (!g_job.begun) {
    g_job.begun = true;
    if (needed & kMarks)
      mapmarks::area_begins(lv.id, layer, lv.bank, needed & kLandmarks, needed & kEnemies, needed & kExits);
  }
  if (!g_said_thread && log_enabled()) {
    g_said_thread = true;
    logf("map reveal: going over areas on the UI thread (%lu)", GetCurrentThreadId());
  }
  LARGE_INTEGER t0;
  QueryPerformanceCounter(&t0);
  bool budget_left = true;
  int index = 0;
  void* room = game::level_first_room(lv.level);
  while (room && index < kMaxRooms) {
    if (index >= g_job.done) {
      if (!budget_left) break;
      if (void* built = game::build_room(lv.bank, room)) {
        ++g_job.built;
        if (needed & kReveal) game::reveal_built_room(built);
        if (needed & kMarks) mapmarks::room(room, built);
      } else {
        ++g_job.failed;
      }
      g_job.done = index + 1;
      budget_left = ms_since(t0) < kBudgetMs;
    }
    room = game::room_next(room);
    ++index;
  }
  g_job.work_ms += ms_since(t0);
  ++g_job.frames;
  if (room && index < kMaxRooms) return true;  // more rooms next frame
  if (room) log_warn("map reveal: level %d has more than %d rooms - the rest stay hidden", lv.id, kMaxRooms);
  if (remembered)
    for (const unsigned part : {kReveal, kLandmarks, kEnemies, kExits})
      if (needed & part) g_done[index_of(part)].set(static_cast<size_t>(lv.id));
  if (needed & kReveal) {
    InterlockedIncrement(&g_areas);
    InterlockedExchangeAdd(&g_rooms, g_job.built);
    logf("map reveal: level %d (layer %d): %d rooms revealed, %d not built, in %d frame%s (%.1f ms of work)", lv.id,
         layer, g_job.built, g_job.failed, g_job.frames, g_job.frames == 1 ? "" : "s", g_job.work_ms);
    cheats::note("Map revealed: %d rooms of this area", g_job.built);
  }
  if (needed & kMarks) mapmarks::area_ends();
  g_job.finished = true;
  return tables_wait && may_wait;
}

}  // namespace

void bind(bool ui_thread) {
  QueryPerformanceFrequency(&g_freq);
  const char* why = !ui_thread                        ? "no UI thread from the loader"
                    : !game::has_map_reveal()         ? "the game's map routines not found"
                    : !game::automap_saves_any_size() ? "the automap save cannot hold a whole area here"
                                                      : nullptr;
  cheats::set_why_not(cheats::kRevealMap, why);
  InterlockedExchange(&g_reveal_ok, why ? 0 : 1);
  InterlockedExchange(&g_bound, ui_thread && game::has_map_reveal() ? 1 : 0);
  logf("map reveal: %s", why ? why : "ready");
}

void arm(int level) {
  if (level > 0) InterlockedExchange(&g_announced, level);
  InterlockedExchange(&g_armed, 1);
}

void on_ui(ULONGLONG now) {
  if (!g_bound) return;
  perf::Timer timer(perf::kMapReveal);
  if (mapmarks::found_waiting()) mapmarks::put_found(now);  // what the game rolled in an area's rooms filled ahead
  if (g_reset && InterlockedExchange(&g_reset, 0)) {
    g_job = Job{};
    for (std::bitset<kMaxLevels>& d : g_done) d.reset();
    g_drlg = nullptr;
    g_work.stop();
    g_wanted = -1;
  }
  if (g_armed && InterlockedExchange(&g_armed, 0)) {
    g_work.start(now);
    const LONG announced = InterlockedExchange(&g_announced, -1);
    if (announced > 0) g_wanted = static_cast<int>(announced);
  }
  if (!g_work.due(now)) return;
  perf::Timer looking(perf::kMapRevealLook);
  g_work.went(look(now));
}

void game_left() {
  InterlockedExchange(&g_reset, 1);
  InterlockedExchange(&g_armed, 0);
  InterlockedExchange(&g_announced, -1);
  InterlockedExchange(&g_areas, 0);
  InterlockedExchange(&g_rooms, 0);
}

Stats stats() {
  Stats s;
  s.areas = static_cast<unsigned>(g_areas);
  s.rooms = static_cast<unsigned>(g_rooms);
  return s;
}

}  // namespace d2rcc::mapreveal

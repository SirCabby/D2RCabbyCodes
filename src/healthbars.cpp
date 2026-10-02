#include "healthbars.h"

#include <windows.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "cheats.h"
#include "game.h"
#include "imgui.h"
#include "log.h"
#include "mem.h"
#include "overlay.h"
#include "perf.h"

namespace d2rcc::healthbars {
namespace {

using Unit = game::Unit;

// MonStats: the game's record keeps the name's string id at +0x36 (its unit-name
// getter, 0x9A1B0, reads it there; +0x38 is the description) and the flags at
// +0x3C (0x3AF080); the flag bits follow monstats.txt's column order (boss 6,
// primeevil 7, npc 8, killable 15).
constexpr size_t kRowName = 0x36;
constexpr size_t kRowFlags = 0x3C;
constexpr uint32_t kFlagBoss = 1u << 6;
constexpr uint32_t kFlagPrimeEvil = 1u << 7;
constexpr uint32_t kFlagNpc = 1u << 8;
constexpr uint32_t kFlagKillable = 1u << 15;

// The client knows a monster's life only as a fraction: stat 6 in 256ths of 128
// points, so 0x8000 is full (the game's own percentage, 0x97490, divides by it).
constexpr float kClientFullLife = 32768.0f;

constexpr int kMaxRows = 2048;
constexpr int kMaxNames = 64;
constexpr int kNameLen = 64;
constexpr size_t kMaxUnits = 1024;
constexpr int kMaxBars = 256;
constexpr int kMaxBosses = 4;
constexpr float kBossNear = 40.0f;  // subtiles: a boss joins the boss bar this close (about two screens)
constexpr float kBossFar = 60.0f;   // ... and leaves it this far away
constexpr ULONGLONG kStaleMs = 300;
#ifdef D2RCC_DEV
// What the walk found is logged a few times a game, not as long as it changes: a log line takes the loader
// some 5 ms, on the thread that draws the game's frames (trace = true logs on, every three seconds at the most).
constexpr int kCountLogs = 6;
constexpr int kSideLogs = 4;
#endif

struct Row {
  uint32_t flags;
  uint16_t name;
};
struct BankRows {
  int count;
  Row rows[kMaxRows];
};
struct BossName {
  int bank;
  int row;
  char text[kNameLen];
};

enum Style : uint8_t { kNormal = 0, kChampion, kUnique, kBoss };
struct Bar {
  float x, y;  // the point above the head, in render pixels
  float life;  // 0 .. 1
  uint8_t style;
};
struct Boss {
  uint32_t id;
  float life;
  char name[kNameLen];
};
struct Snapshot {
  ULONGLONG at;
  int render_w, render_h;
  int side;  // game::panel_side()
  int bars, bosses;
  Bar bar[kMaxBars];
  Boss boss[kMaxBosses];
};

const D2RL::PluginContext* g_ctx = nullptr;
const D2RL::DataTableService* g_tables = nullptr;
const D2RL::LocalizationService* g_strings = nullptr;
volatile LONG g_bound = 0;   // collect() may run
volatile LONG g_legacy = 0;  // the last collection found legacy graphics
volatile LONG g_reset = 0;   // a game was left: the UI thread forgets its per-game state

// The MonStats copy: written by the game thread once per game, under the lock.
CRITICAL_SECTION g_tables_cs;
BankRows g_banks[4];  // by bank: 1 classic, 2 lod, 3 rotw (count 0: not read)
BossName g_names[kMaxNames];
int g_name_count = 0;
int g_first_bank = 0;  // the bank for a unit that names none of the above
volatile LONG g_tables_revision = 0;  // moves on with every copy

// UI thread only: its own copy of the above, taken when the revision has moved on, so the walk of the
// monsters takes no lock.
BankRows g_ui_banks[4];
BossName g_ui_names[kMaxNames];
int g_ui_name_count = 0;
int g_ui_first_bank = 0;
LONG g_ui_revision = 0;

// The snapshot: the UI thread publishes it, the present thread takes it.
CRITICAL_SECTION g_snap_cs;
bool g_cs_ready = false;
Snapshot g_published;

// UI thread only.
Snapshot g_work;
Unit* g_units[kMaxUnits];
uint32_t g_engaged[kMaxBosses];  // the bosses on the boss bar
int g_engaged_count = 0;
bool g_following = false;  // the last frame walked the monsters (its snapshot is the one published)
bool g_said_thread = false;
int g_last_side = -2;
int g_side_logged = 0;     // this game
ULONGLONG g_counts_at = 0;
int g_last_counts[4] = {-1, -1, -1, -1};
int g_counts_logged = 0;   // this game
uint32_t g_banks_seen = 0;  // the data banks monsters have named (bit per value)
uint32_t g_unattackable_seen[kMaxRows / 32] = {};  // the classes logged as out of reach (bit per class)

// Present thread only.
Snapshot g_view;
int g_logged_size[4] = {};

// --- the tables ------------------------------------------------------------------------------
const char* bank_name(int bank) { return bank == 3 ? "rotw" : bank == 2 ? "lod" : bank == 1 ? "classic" : "?"; }

// UI thread: the game thread's copy of the tables becomes this thread's, when there is a newer one.
void take_tables() {
  if (g_ui_revision == g_tables_revision) return;
  EnterCriticalSection(&g_tables_cs);
  g_ui_revision = g_tables_revision;
  for (int b = 1; b <= 3; ++b) {
    g_ui_banks[b].count = g_banks[b].count;
    std::memcpy(g_ui_banks[b].rows, g_banks[b].rows, sizeof(Row) * static_cast<size_t>(g_banks[b].count));
  }
  std::memcpy(g_ui_names, g_names, sizeof(BossName) * static_cast<size_t>(g_name_count));
  g_ui_name_count = g_name_count;
  g_ui_first_bank = g_first_bank;
  LeaveCriticalSection(&g_tables_cs);
}

// UI thread: a monster's row, and for a prime evil its name (kept until the tables are taken again).
bool row_of(uint8_t unit_bank, uint32_t cls, Row* out, const char** name) {
  const int bank = unit_bank >= 1 && unit_bank <= 3 && g_ui_banks[unit_bank].count ? unit_bank : g_ui_first_bank;
  if (!bank || cls >= static_cast<uint32_t>(g_ui_banks[bank].count)) return false;
  *out = g_ui_banks[bank].rows[cls];
  if (name && (out->flags & kFlagPrimeEvil)) {
    for (int i = 0; i < g_ui_name_count; ++i)
      if (g_ui_names[i].bank == bank && g_ui_names[i].row == static_cast<int>(cls)) *name = g_ui_names[i].text;
  }
  return true;
}

bool string_by_id(uint16_t id, char* out, size_t cap) {
  out[0] = 0;
  if (!g_ctx || !g_strings || !g_strings->getStringById) return false;
  uint32_t need = 0;
  return g_strings->getStringById(g_ctx, id, out, static_cast<uint32_t>(cap), &need) ==
             D2RL::Localization::Result::Success &&
         out[0];
}

// Some languages start a name with a grammar tag such as "[fs]"; the game drops
// everything up to the last "]" that closes a two-letter tag (0x1E1CF0).
void strip_tag(char* name) {
  size_t i = std::strlen(name);
  while (i > 0 && name[i] != ']') --i;
  if (name[i] != ']' || i < 3 || name[i - 3] != '[') return;
  std::memmove(name, name + i + 1, std::strlen(name + i + 1) + 1);
}

// --- the UI thread -----------------------------------------------------------------------------
bool engaged(uint32_t id) {
  for (int i = 0; i < g_engaged_count; ++i)
    if (g_engaged[i] == id) return true;
  return false;
}

// A champion has the unique bit as well as its own (the client's handler of a monster's mods, 0x12CE00, sets
// both, as the server's champion maker does), so the champion is asked first; a unique's or a Herald's minion
// is an ordinary monster whatever else it carries.
uint8_t style_of(Unit* u, uint32_t row_flags) {
  if (row_flags & (kFlagBoss | kFlagPrimeEvil)) return kBoss;
  const uint16_t kind = game::monster_type_flags(u);
  if (kind & (game::kMinion | game::kHeraldMinion)) return kNormal;
  if (kind & game::kSuperUnique) return kUnique;
  if (kind & game::kChampion) return kChampion;
  if (kind & (game::kUnique | game::kHerald)) return kUnique;
  return kNormal;
}

void publish(const Snapshot& s) {
  EnterCriticalSection(&g_snap_cs);
  g_published.at = GetTickCount64();
  g_published.render_w = s.render_w;
  g_published.render_h = s.render_h;
  g_published.side = s.side;
  g_published.bars = s.bars;
  g_published.bosses = s.bosses;
  std::memcpy(g_published.bar, s.bar, sizeof(Bar) * static_cast<size_t>(s.bars));
  std::memcpy(g_published.boss, s.boss, sizeof(Boss) * static_cast<size_t>(s.bosses));
  LeaveCriticalSection(&g_snap_cs);
}

// --- drawing ----------------------------------------------------------------------------------
constexpr ImU32 kFillTop = IM_COL32(236, 64, 48, 245);
constexpr ImU32 kFillBottom = IM_COL32(150, 18, 14, 245);
constexpr ImU32 kEmpty = IM_COL32(34, 8, 8, 215);
constexpr ImU32 kFrame = IM_COL32(0, 0, 0, 205);
constexpr ImU32 kChampionFrame = IM_COL32(112, 132, 255, 235);  // the game's champion blue
constexpr ImU32 kUniqueFrame = IM_COL32(214, 184, 104, 245);    // ... and its unique gold
constexpr ImU32 kBossFrame = IM_COL32(236, 196, 96, 255);
constexpr ImU32 kBossName = IM_COL32(236, 208, 146, 255);
constexpr ImU32 kShadow = IM_COL32(0, 0, 0, 220);

float snap(float v) { return std::floor(v + 0.5f); }

void shadowed_text(ImDrawList* dl, ImFont* font, float size, ImVec2 at, ImU32 color, const char* text, float offset) {
  dl->AddText(font, size, ImVec2(at.x + offset, at.y + offset), kShadow, text);
  dl->AddText(font, size, at, color, text);
}

void fill(ImDrawList* dl, float x0, float y0, float x1, float y1, float life) {
  const float fx = x0 + snap((x1 - x0) * std::clamp(life, 0.0f, 1.0f));
  dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), kEmpty);
  if (fx > x0) dl->AddRectFilledMultiColor(ImVec2(x0, y0), ImVec2(fx, y1), kFillTop, kFillTop, kFillBottom, kFillBottom);
}

// A floating bar, centred on x with its bottom edge at `bottom`.
void small_bar(ImDrawList* dl, float x, float bottom, const Bar& b, float k) {
  float w = 40.0f, h = 5.0f, frame_w = 1.0f;
  ImU32 frame = kFrame;
  if (b.style == kChampion) {
    w = 48.0f;
    frame = kChampionFrame;
  } else if (b.style == kUnique) {
    w = 52.0f;
    h = 6.0f;
    frame = kUniqueFrame;
  } else if (b.style == kBoss) {
    w = 68.0f;
    h = 7.0f;
    frame_w = 1.5f;
    frame = kBossFrame;
  }
  w = snap(w * k);
  h = std::max(3.0f, snap(h * k));
  const float f = std::max(1.0f, snap(frame_w * k));
  const float x0 = snap(x - w * 0.5f), y1 = snap(bottom), x1 = x0 + w, y0 = y1 - h;
  dl->AddRectFilled(ImVec2(x0 - f, y0 - f), ImVec2(x1 + f, y1 + f), frame);
  fill(dl, x0, y0, x1, y1, b.life);
}

// One boss at the top centre: its name, then a wide bar with the percentage in
// it. Returns the height it took.
float boss_block(ImDrawList* dl, ImFont* font, float cx, float top, float width, float k, const char* name, float life) {
  const float name_size = snap(21.0f * k), pct_size = snap(14.0f * k);
  const float px = std::max(1.0f, snap(k));
  const float w = snap(width), h = snap(15.0f * k);
  const ImVec2 ns = font->CalcTextSizeA(name_size, FLT_MAX, 0.0f, name);
  shadowed_text(dl, font, name_size, ImVec2(snap(cx - ns.x * 0.5f), top), kBossName, name, px);
  const float x0 = snap(cx - w * 0.5f), x1 = x0 + w;
  const float y0 = top + snap(ns.y + 3.0f * k), y1 = y0 + h;
  // A dark outer frame with a thin gold line inside it, like the game's own panels.
  dl->AddRectFilled(ImVec2(x0 - 3.0f * px, y0 - 3.0f * px), ImVec2(x1 + 3.0f * px, y1 + 3.0f * px), IM_COL32(0, 0, 0, 200));
  dl->AddRect(ImVec2(x0 - 2.0f * px, y0 - 2.0f * px), ImVec2(x1 + 2.0f * px, y1 + 2.0f * px), IM_COL32(150, 120, 62, 235),
              0.0f, 0, px);
  fill(dl, x0, y0, x1, y1, life);
  const float fx = x0 + snap(w * std::clamp(life, 0.0f, 1.0f));
  if (fx > x0) dl->AddRectFilled(ImVec2(x0, y0), ImVec2(fx, y0 + std::max(1.0f, snap(h * 0.2f))), IM_COL32(255, 255, 255, 40));
  for (int q = 1; q < 4; ++q) {
    const float tx = snap(x0 + w * static_cast<float>(q) * 0.25f);
    dl->AddRectFilled(ImVec2(tx, y0), ImVec2(tx + px, y1), IM_COL32(0, 0, 0, 90));
  }
  char pct[8];
  std::snprintf(pct, sizeof(pct), "%d%%", static_cast<int>(std::ceil(std::clamp(life, 0.0f, 1.0f) * 100.0f)));
  const ImVec2 ps = font->CalcTextSizeA(pct_size, FLT_MAX, 0.0f, pct);
  shadowed_text(dl, font, pct_size, ImVec2(snap(cx - ps.x * 0.5f), snap(y0 + (h - ps.y) * 0.5f)), IM_COL32(255, 255, 255, 250),
                pct, px);
  return y1 + 3.0f * px - top;
}

float hud_scale(float display_h) { return std::max(0.75f, display_h / 1080.0f); }
float boss_width(float display_w, float k) { return std::min(display_w * 0.5f, 520.0f * k); }

}  // namespace

void set_services(const D2RL::PluginContext* ctx, const D2RL::DataTableService* tables,
                  const D2RL::LocalizationService* strings) {
  if (!g_cs_ready) {
    InitializeCriticalSection(&g_tables_cs);
    InitializeCriticalSection(&g_snap_cs);
    g_cs_ready = true;
  }
  g_ctx = ctx;
  g_tables = tables;
  g_strings = strings;
}

void bind(bool ui_thread) {
  const bool stats = game::bound();
  const bool units = game::has_client_units() && game::has_hostility();
  const char* bars = !ui_thread                  ? "no UI thread from the loader"
                     : !stats || !units          ? "the game's monster list not found"
                     : !game::has_projection()   ? "the game's screen projection not found"
                                                 : nullptr;
  const char* boss = !ui_thread ? "no UI thread from the loader" : !stats || !units ? "the game's monster list not found" : nullptr;
  cheats::set_why_not(cheats::kMonsterBars, bars);
  cheats::set_why_not(cheats::kBossBar, boss);
  InterlockedExchange(&g_bound, g_cs_ready && (!bars || !boss) ? 1 : 0);
  logf("health bars: monster bars %s, boss bar %s", bars ? bars : "ready", boss ? boss : "ready");
}

void collect_tables() {
  if (!g_cs_ready || !g_ctx || !g_tables || !g_tables->getTable) {
    log_warn("health bars: no data table service - bosses are not recognised, NPCs are left out by area only");
    return;
  }
  static BankRows fresh[4];  // a copy per bank, off the stack
  static BossName names[kMaxNames];
  int name_count = 0, first = 0;
  const D2RL::DataTables::Bank order[3] = {D2RL::DataTables::Bank::Rotw, D2RL::DataTables::Bank::Lod,
                                           D2RL::DataTables::Bank::Classic};
  for (const D2RL::DataTables::Bank bank : order) {
    const int bi = static_cast<int>(bank);
    fresh[bi].count = 0;
    D2RL::DataTables::TableView view{};
    view.structSize = D2RL::DataTables::TableViewSize;
    const D2RL::DataTables::Result r = g_tables->getTable(g_ctx, bank, D2RL::DataTables::TableId::MonStats, &view);
    if (r != D2RL::DataTables::Result::Success || !view.rows || !view.rowCount) {
      logf("health bars: no %s MonStats table (%u)", bank_name(bi), static_cast<unsigned>(r));
      continue;
    }
    if (view.rowSize < kRowFlags + sizeof(uint32_t) || view.rowSize > 0x4000) {
      log_warn("health bars: unexpected %s MonStats row size %u - not used", bank_name(bi), view.rowSize);
      continue;
    }
    const uintptr_t rows = reinterpret_cast<uintptr_t>(view.rows);
    const uint32_t count = std::min<uint32_t>(view.rowCount, kMaxRows);
    int primes = 0, primes_not_boss = 0, npcs = 0;
    uint32_t read = 0;
    for (; read < count; ++read) {
      Row& row = fresh[bi].rows[read];
      const uintptr_t at = rows + static_cast<uintptr_t>(read) * view.rowSize;
      if (!mem::read_safe(at + kRowFlags, &row.flags) || !mem::read_safe(at + kRowName, &row.name)) break;
      if (row.flags & kFlagPrimeEvil) {
        ++primes;
        if (!(row.flags & kFlagBoss)) ++primes_not_boss;
      }
      if (row.flags & kFlagNpc) ++npcs;
    }
    // The layout check: a handful of prime evils, each of them also a boss, and some NPCs.
    if (read < count || primes < 1 || primes > kMaxNames || primes_not_boss || npcs < 1) {
      log_warn("health bars: the %s MonStats table does not look as expected (%u of %u rows of %u bytes read, %d prime "
               "evils, %d of them not bosses, %d NPCs) - not used",
               bank_name(bi), read, view.rowCount, view.rowSize, primes, primes_not_boss, npcs);
      continue;
    }
    fresh[bi].count = static_cast<int>(count);
    if (!first) first = bi;
    const int bank_names = name_count;
    bool all_same = true;
    for (uint32_t i = 0; i < count && name_count < kMaxNames; ++i) {
      if (!(fresh[bi].rows[i].flags & kFlagPrimeEvil)) continue;
      BossName& n = names[name_count++];
      n.bank = bi;
      n.row = static_cast<int>(i);
      if (string_by_id(fresh[bi].rows[i].name, n.text, sizeof(n.text))) strip_tag(n.text);
      if (std::strcmp(n.text, names[bank_names].text) != 0) all_same = false;
    }
    // Andariel, Duriel, Mephisto... do not share a name: one name for all of them
    // is the wrong field, and the boss bar says "Boss" rather than a wrong name.
    if (all_same && name_count - bank_names > 1) {
      log_warn("health bars: every %s prime evil is named \"%s\" - the names are not used", bank_name(bi),
               names[bank_names].text);
      for (int i = bank_names; i < name_count; ++i) names[i].text[0] = 0;
    }
    char list[400] = {};
    size_t used = 0;
    for (int i = bank_names; i < name_count && used + 48 < sizeof(list); ++i)
      used += static_cast<size_t>(std::snprintf(list + used, sizeof(list) - used, "%s%s (%d)", used ? ", " : "",
                                                names[i].text[0] ? names[i].text : "?", names[i].row));
    logf("health bars: %s MonStats, %u rows of %u bytes, %d NPCs, %d prime evils: %s", bank_name(bi), count, view.rowSize,
         npcs, primes, list);
  }
  EnterCriticalSection(&g_tables_cs);
  for (int b = 1; b <= 3; ++b) {
    g_banks[b].count = fresh[b].count;
    std::memcpy(g_banks[b].rows, fresh[b].rows, sizeof(Row) * static_cast<size_t>(fresh[b].count));
  }
  std::memcpy(g_names, names, sizeof(BossName) * static_cast<size_t>(name_count));
  g_name_count = name_count;
  g_first_bank = first;
  InterlockedIncrement(&g_tables_revision);
  LeaveCriticalSection(&g_tables_cs);
  if (!first) log_warn("health bars: no usable MonStats table - bosses are not recognised");
}

void collect() {
  if (!g_bound) return;
  perf::Timer timer(perf::kHealthBars);
  const bool want_bars = cheats::enabled(cheats::kMonsterBars) && !cheats::why_not(cheats::kMonsterBars);
  const bool want_boss = cheats::enabled(cheats::kBossBar) && !cheats::why_not(cheats::kBossBar);
  Snapshot& s = g_work;
  s.bars = s.bosses = 0;
  s.render_w = s.render_h = 0;
  s.side = 0;
  if (g_reset && InterlockedExchange(&g_reset, 0)) {
    g_engaged_count = 0;
    g_counts_logged = g_side_logged = 0;
  }
  // Nothing to follow - the switches off, no game, a town: the bars shown last are taken away, once. Nor is
  // anything drawn under the Esc menu, where the game stands still (the bosses on the boss bar stay the ones
  // they were).
  const bool paused = overlay::pause_open();
  Unit* me = (want_bars || want_boss) && !paused && !game::in_town() ? game::local_client_player() : nullptr;
  if (!me) {
    if (!paused) g_engaged_count = 0;
    if (g_following) publish(s);
    g_following = false;
    return;
  }
  g_following = true;
  take_tables();
  if (!g_said_thread && log_enabled()) {
    g_said_thread = true;
    logf("health bars: collecting on the UI thread (%lu)", GetCurrentThreadId());
  }
  const bool hd = game::hd_graphics();
  if ((g_legacy != 0) == hd) {
    InterlockedExchange(&g_legacy, hd ? 0 : 1);
    logf("health bars: %s graphics%s", hd ? "HD" : "legacy", hd ? "" : " - no bars above monsters (the boss bar stays)");
  }
  const bool project = want_bars && hd && game::render_size(&s.render_w, &s.render_h);
  s.side = game::panel_side();
#ifdef D2RCC_DEV
  if (s.side != g_last_side && log_enabled() && (overlay::settings().trace || g_side_logged < kSideLogs)) {
    ++g_side_logged;
    logf("health bars: side panels %d (0 none, 1 right, 2 left, 3 both)", s.side);
    g_last_side = s.side;
  }
#else
  (void)g_last_side;
  (void)g_side_logged;
#endif
  float mx = 0.0f, my = 0.0f;
  const bool placed = game::unit_position(me, &mx, &my);
  uint32_t kept[kMaxBosses];
  int kept_count = 0, enemies = 0;
  const size_t n = game::client_units(game::kMonster, g_units, kMaxUnits);
  for (size_t i = 0; i < n; ++i) {
    Unit* u = g_units[i];
    if (game::unit_is_dead(u)) continue;
    // Hydras and the like: nothing can hit them, whoever owns them.
    if (!game::unit_can_be_attacked(u)) {
#ifdef D2RCC_DEV
      const uint32_t cls = game::unit_class(u);
      if (cls < kMaxRows && !(g_unattackable_seen[cls / 32] & (1u << (cls % 32))) && log_enabled()) {
        g_unattackable_seen[cls / 32] |= 1u << (cls % 32);
        logf("health bars: monster class %u cannot be attacked - no bar", cls);
      }
#endif
      continue;
    }
    Row row{};
    const char* name = nullptr;
    const uint8_t bank = game::unit_table_bank(u);
#ifdef D2RCC_DEV
    if (bank < 32 && !(g_banks_seen & (1u << bank)) && log_enabled()) {
      g_banks_seen |= 1u << bank;
      logf("health bars: a monster uses data bank %u (class %u)", bank, game::unit_class(u));
    }
#endif
    const bool known = row_of(bank, game::unit_class(u), &row, want_boss ? &name : nullptr);
    // Town folk and the invulnerable are never enemies; the game decides the rest (pets, the mercenary, allies).
    if (known && ((row.flags & kFlagNpc) || !(row.flags & kFlagKillable))) continue;
    if (!game::is_hostile(me, u)) continue;
    const int32_t life_raw = game::get_stat(u, game::kHitpoints);
    if (life_raw <= 0) continue;
    ++enemies;
    const float life = std::min(1.0f, static_cast<float>(life_raw) / kClientFullLife);
    if (want_boss && (row.flags & kFlagPrimeEvil) && placed && s.bosses < kMaxBosses) {
      float bx = 0.0f, by = 0.0f;
      const uint32_t id = game::unit_id(u);
      if (game::unit_position(u, &bx, &by)) {
        const float d = std::hypot(bx - mx, by - my);
        if (d <= kBossNear || (d <= kBossFar && engaged(id))) {
          Boss& b = s.boss[s.bosses++];
          b.id = id;
          b.life = life;
          std::snprintf(b.name, sizeof(b.name), "%s", name && name[0] ? name : "Boss");
          kept[kept_count++] = id;
        }
      }
    }
    if (project && s.bars < kMaxBars) {
      Bar& b = s.bar[s.bars];
      if (game::project_above_head(u, &b.x, &b.y)) {
        b.life = life;
        b.style = style_of(u, row.flags);
        ++s.bars;
      }
    }
  }
  // Nearer monsters (lower on the screen) are drawn last, over the ones behind them;
  // several bosses (a clone, the three Ancients) keep a steady order.
  std::sort(s.bar, s.bar + s.bars, [](const Bar& a, const Bar& b) { return a.y < b.y; });
  std::sort(s.boss, s.boss + s.bosses, [](const Boss& a, const Boss& b) { return a.id < b.id; });
  for (int i = 0; i < kept_count; ++i) g_engaged[i] = kept[i];
  g_engaged_count = kept_count;
  publish(s);
  perf::count(perf::kBarMonsters, static_cast<int64_t>(n));
  perf::count(perf::kBarsShown, s.bars);
#ifdef D2RCC_DEV
  // What the walk found, when it changes: the first few times in a game (with trace = true for as long as it
  // changes), at most every three seconds.
  if (log_enabled() && (overlay::settings().trace || g_counts_logged < kCountLogs)) {
    const int counts[4] = {static_cast<int>(n), enemies, s.bars, s.bosses};
    const ULONGLONG now = GetTickCount64();
    if (std::memcmp(counts, g_last_counts, sizeof(counts)) != 0 && now - g_counts_at >= 3000) {
      std::memcpy(g_last_counts, counts, sizeof(counts));
      g_counts_at = now;
      ++g_counts_logged;
      logf("health bars: %d monsters known, %d enemies, %d bars, %d on the boss bar%s%s%s", counts[0], counts[1],
           counts[2], counts[3], s.bosses ? " - " : "", s.bosses ? s.boss[0].name : "",
           g_counts_logged == kCountLogs && !overlay::settings().trace ? "  (not logged again in this game)" : "");
    }
  }
#else
  (void)enemies;
  (void)g_counts_at;
  (void)g_last_counts;
  (void)g_counts_logged;
  (void)g_banks_seen;
  (void)g_unattackable_seen;
#endif
}

void game_left() {
  InterlockedExchange(&g_reset, 1);
  if (!g_cs_ready) return;
  EnterCriticalSection(&g_snap_cs);
  g_published.at = 0;
  g_published.bars = g_published.bosses = 0;
  LeaveCriticalSection(&g_snap_cs);
}

bool take_snapshot() {
  if (!g_cs_ready || !g_bound) return false;
  EnterCriticalSection(&g_snap_cs);
  const bool fresh = g_published.at && GetTickCount64() - g_published.at <= kStaleMs;
  if (fresh) {
    g_view.at = g_published.at;
    g_view.render_w = g_published.render_w;
    g_view.render_h = g_published.render_h;
    g_view.side = g_published.side;
    g_view.bars = g_published.bars;
    g_view.bosses = g_published.bosses;
    std::memcpy(g_view.bar, g_published.bar, sizeof(Bar) * static_cast<size_t>(g_published.bars));
    std::memcpy(g_view.boss, g_published.boss, sizeof(Boss) * static_cast<size_t>(g_published.bosses));
  }
  LeaveCriticalSection(&g_snap_cs);
  // The switches are read again here, so turning one off hides it on the next frame.
  if (!cheats::enabled(cheats::kMonsterBars)) g_view.bars = 0;
  if (!cheats::enabled(cheats::kBossBar)) g_view.bosses = 0;
  return fresh && (g_view.bars > 0 || g_view.bosses > 0);
}

void draw(ImFont* font) {
  const Snapshot& s = g_view;
  const ImVec2 size = ImGui::GetIO().DisplaySize;
  if (size.x <= 0.0f || size.y <= 0.0f) return;
  if (!font) font = ImGui::GetFont();
  const float k = hud_scale(size.y);
  ImDrawList* dl = ImGui::GetBackgroundDrawList();
  // The projection's pixels and the window's, once per change: they differ with resolution scaling.
  const int sizes[4] = {s.render_w, s.render_h, static_cast<int>(size.x), static_cast<int>(size.y)};
  if (s.bars > 0 && std::memcmp(sizes, g_logged_size, sizeof(sizes)) != 0) {
    std::memcpy(g_logged_size, sizes, sizeof(sizes));
    logf("health bars: projected points are %dx%d render pixels, drawn on a %dx%d window", sizes[0], sizes[1], sizes[2],
         sizes[3]);
  }
  if (s.bars > 0 && s.render_w > 0 && s.render_h > 0) {
    const float sx = size.x / static_cast<float>(s.render_w), sy = size.y / static_cast<float>(s.render_h);
    const float half = size.x * 0.5f;
    for (int i = 0; i < s.bars; ++i) {
      const Bar& b = s.bar[i];
      const float x = b.x * sx, y = b.y * sy;
      if (x < 0.0f || x > size.x || y < 0.0f || y > size.y) continue;
      // An open inventory or character panel covers its half of the screen.
      if (s.side == 3 || (s.side == 1 && x >= half) || (s.side == 2 && x < half)) continue;
      small_bar(dl, x, y - snap(2.0f * k), b, k);
    }
  }
  if (s.bosses > 0 && s.side != 3) {
    float top = snap(size.y * static_cast<float>(cheats::boss_bar_position()) / 100.0f);
    for (int i = 0; i < s.bosses; ++i)
      top += boss_block(dl, font, size.x * 0.5f, top, boss_width(size.x, k), k, s.boss[i].name, s.boss[i].life) + snap(8.0f * k);
  }
}

void draw_boss_preview(ImFont* font, int position_pct) {
  const ImVec2 size = ImGui::GetIO().DisplaySize;
  if (size.x <= 0.0f || size.y <= 0.0f) return;
  if (!font) font = ImGui::GetFont();
  const float k = hud_scale(size.y);
  boss_block(ImGui::GetForegroundDrawList(), font, size.x * 0.5f, snap(size.y * static_cast<float>(position_pct) / 100.0f),
             boss_width(size.x, k), k, "Boss bar", 0.75f);
}

bool legacy_graphics() { return g_legacy != 0; }

}  // namespace d2rcc::healthbars

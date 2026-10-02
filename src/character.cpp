#include "character.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <string>

#include "cheats.h"
#include "game.h"
#include "log.h"
#include "watchdog.h"

namespace d2rcc::character {
namespace {

using game::Unit;

constexpr size_t kMaxOwnSkills = 256;
// Each pass spends what the skills' prerequisites allow so far; a class's chains are a few deep.
constexpr int kSpendPasses = 12;
constexpr wchar_t kFileName[] = L"cabbycodes.presets.txt";

CRITICAL_SECTION g_cs;
bool g_cs_ready = false;
std::wstring g_path;
std::vector<presets::Preset> g_list;
bool g_changed = false;  // the list differs from the file
ULONGLONG g_retry_at = 0;  // a write that failed is tried again from this tick count on
bool g_want_respec = false;
std::string g_want_save;  // save the character's points under this name
bool g_want_load = false;
presets::Preset g_load;
std::string g_result;  // the last action's outcome, for the panel
// Something waits to be done (any of the three above): what the UI callback and the tick read, every frame
// and every server frame, before they take the lock.
volatile LONG g_waiting = 0;

// Under the lock: the flag follows the requests.
void note_waiting() {
  InterlockedExchange(&g_waiting, g_want_respec || g_want_load || !g_want_save.empty() ? 1 : 0);
}

struct Lock {
  Lock() { EnterCriticalSection(&g_cs); }
  ~Lock() { LeaveCriticalSection(&g_cs); }
  Lock(const Lock&) = delete;
  Lock& operator=(const Lock&) = delete;
};

std::string read_file(const std::wstring& path) {
  std::string text;
  FILE* f = _wfopen(path.c_str(), L"rb");
  if (!f) return text;
  char buf[4096];
  size_t n = 0;
  while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0 && text.size() < (1u << 20)) text.append(buf, n);
  std::fclose(f);
  return text;
}

// A new file beside the old one, then moved over it: a crash mid-write leaves the old list.
bool write_file(const std::wstring& path, const std::string& text) {
  const std::wstring temp = path + L".new";
  FILE* f = _wfopen(temp.c_str(), L"wb");
  if (!f) return false;
  const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
  if (std::fclose(f) != 0 || !ok) return false;
  return MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

// The outcome goes to the panel's line and to the status note alike.
void report(const char* fmt, ...) __attribute__((format(gnu_printf, 1, 2)));
void report(const char* fmt, ...) {
  char text[160];
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(text, sizeof(text), fmt, args);
  va_end(args);
  cheats::note("%s", text);
  Lock guard;
  g_result = text;
}

int stat_points(Unit* player) { return game::get_stat(player, game::kStatPoints); }
int skill_points(Unit* player) { return game::get_stat(player, game::kSkillPoints); }

bool do_respec(void* game, Unit* player) {
  if (!game::respec(game, player)) {
    log_warn("character: the game's reset refused the character");
    report("Respec failed");
    return false;
  }
  logf("character: respec - %d attribute points and %d skill points to spend", stat_points(player),
       skill_points(player));
  report("Respec: %d attribute points and %d skill points to spend", stat_points(player), skill_points(player));
  return true;
}

void do_save(Unit* player, const std::string& name) {
  if (!game::has_base_stat()) {  // without it every attribute would read 0
    report("Preset \"%s\" not saved: the base stat getter is missing", name.c_str());
    return;
  }
  presets::Preset p;
  p.name = name;
  p.char_class = static_cast<int>(game::unit_class(player));
  p.level = game::get_stat(player, game::kLevel);
  p.strength = game::get_base_stat(player, game::kStrength);
  p.dexterity = game::get_base_stat(player, game::kDexterity);
  p.vitality = game::get_base_stat(player, game::kVitality);
  p.energy = game::get_base_stat(player, game::kEnergy);
  game::SkillPoints skills[kMaxOwnSkills];
  const size_t n = game::own_skills(player, skills, kMaxOwnSkills);
  for (size_t i = 0; i < n; ++i) p.skills.push_back(presets::Skill{skills[i].id, skills[i].level});
  {
    Lock guard;
    bool replaced = false;
    for (presets::Preset& old : g_list) {
      if (old.char_class == p.char_class && old.name == p.name) {
        old = p;
        replaced = true;
        break;
      }
    }
    if (!replaced) g_list.push_back(p);
    g_changed = true;
  }
  watchdog::wake();  // the file is written now
  logf("character: preset '%s' saved (%s level %d: str %d dex %d vit %d ene %d, %d skill points in %zu skills)",
       p.name.c_str(), presets::class_name(p.char_class), p.level, p.strength, p.dexterity, p.vitality, p.energy,
       presets::skill_points(p), p.skills.size());
  report("Preset \"%s\" saved: %d skill points in %zu skills", p.name.c_str(), presets::skill_points(p),
         p.skills.size());
}

// True when the character was reset (whatever the points then allowed).
bool do_load(void* game, Unit* player, const presets::Preset& p) {
  const int cls = static_cast<int>(game::unit_class(player));
  if (cls != p.char_class) {
    report("Preset \"%s\" is for a %s", p.name.c_str(), presets::class_name(p.char_class));
    return false;
  }
  if (!game::respec(game, player)) {
    log_warn("character: the game's reset refused the character - preset '%s' not loaded", p.name.c_str());
    report("Preset \"%s\" not loaded: the reset failed", p.name.c_str());
    return false;
  }
  // Attributes: from the class's own values up to the preset's, as far as the points go.
  const struct {
    int stat;
    int target;
  } wants[4] = {{game::kStrength, p.strength}, {game::kDexterity, p.dexterity}, {game::kVitality, p.vitality},
                {game::kEnergy, p.energy}};
  int attrs_short = 0;
  for (const auto& w : wants) {
    const int have = game::get_base_stat(player, w.stat);
    const int need = w.target - have;
    if (need <= 0) continue;
    const int left = stat_points(player);
    if (left > 0) game::spend_stat_points(game, player, w.stat, need < left ? need : left);
    attrs_short += need - (game::get_base_stat(player, w.stat) - have);
  }
  // Skills: pass after pass, each spending what the skills learnt so far allow (prerequisites,
  // required levels); the game's handler checks every point.
  for (int pass = 0; pass < kSpendPasses; ++pass) {
    bool progress = false;
    for (const presets::Skill& s : p.skills) {
      const int have = game::skill_base_level(player, s.id);
      const int left = skill_points(player);
      if (left <= 0) break;
      if (have >= s.points) continue;
      game::spend_skill_points(game, player, s.id, s.points - have < left ? s.points - have : left);
      if (game::skill_base_level(player, s.id) > have) progress = true;
    }
    if (!progress) break;
  }
  int skills_short = 0;
  for (const presets::Skill& s : p.skills) {
    const int have = game::skill_base_level(player, s.id);
    if (have < s.points) skills_short += s.points - have;
  }
  logf("character: preset '%s' loaded - %d attribute and %d skill points short; %d and %d left to spend",
       p.name.c_str(), attrs_short, skills_short, stat_points(player), skill_points(player));
  if (attrs_short || skills_short)
    report("Preset \"%s\" loaded, %d attribute and %d skill points short", p.name.c_str(), attrs_short,
           skills_short);
  else
    report("Preset \"%s\" loaded", p.name.c_str());
  return true;
}

}  // namespace

void set_config_dir(const char* utf8_dir) {
  if (!g_cs_ready) {
    InitializeCriticalSection(&g_cs);
    g_cs_ready = true;
  }
  wchar_t wide[MAX_PATH] = {};
  if (utf8_dir && *utf8_dir && MultiByteToWideChar(CP_UTF8, 0, utf8_dir, -1, wide, MAX_PATH) > 0)
    g_path = std::wstring(wide) + kFileName;
  else
    g_path.clear();
}

void load() {
  if (!g_cs_ready || g_path.empty()) return;
  std::vector<presets::Preset> list = presets::parse(read_file(g_path));
  logf("character: %zu presets in %ls", list.size(), g_path.c_str());
  Lock guard;
  g_list = std::move(list);
  g_changed = false;
}

void save_if_changed(bool now) {
  if (!g_cs_ready || g_path.empty()) return;
  const ULONGLONG tick = GetTickCount64();
  std::string text;
  {
    Lock guard;
    if (!g_changed || (!now && tick < g_retry_at)) return;
    g_changed = false;
    text = presets::format(g_list);
  }
  if (write_file(g_path, text)) {
    logf("character: presets written (%zu bytes)", text.size());
    return;
  }
  log_warn("character: could not write %ls - trying again in 5 s", g_path.c_str());
  Lock guard;
  g_changed = true;  // unless a newer change already asked for a write
  g_retry_at = tick + 5000;
}

void request_respec() {
  if (!g_cs_ready) return;
  Lock guard;
  g_want_respec = true;
  note_waiting();
}

void request_save(const char* name) {
  if (!g_cs_ready) return;
  const std::string clean = presets::clean_name(name ? name : "");
  if (clean.empty()) return;
  Lock guard;
  g_want_save = clean;
  note_waiting();
}

bool request_load(const char* name, int char_class) {
  if (!g_cs_ready || !name) return false;
  Lock guard;
  for (const presets::Preset& p : g_list) {
    if (p.char_class == char_class && p.name == name) {
      g_load = p;
      g_want_load = true;
      note_waiting();
      return true;
    }
  }
  return false;
}

bool remove(const char* name, int char_class) {
  if (!g_cs_ready || !name) return false;
  Lock guard;
  for (size_t i = 0; i < g_list.size(); ++i) {
    if (g_list[i].char_class == char_class && g_list[i].name == name) {
      g_list.erase(g_list.begin() + static_cast<long>(i));
      g_changed = true;
      watchdog::wake();
      return true;
    }
  }
  return false;
}

std::vector<presets::Preset> list() {
  if (!g_cs_ready) return {};
  Lock guard;
  return g_list;
}

std::string last_result() {
  if (!g_cs_ready) return {};
  Lock guard;
  return g_result;
}

Pending pending() {
  Pending p;
  if (!g_cs_ready) return p;
  Lock guard;
  p.save = !g_want_save.empty();
  p.respec = g_want_respec;
  if (g_want_load) p.load = g_load.name;
  return p;
}

void cancel_reset() {
  if (!g_cs_ready) return;
  Lock guard;
  if (g_want_respec || g_want_load) logf("character: the waiting %s was cancelled", g_want_load ? "preset load" : "respec");
  g_want_respec = g_want_load = false;
  note_waiting();
}

void on_ui() {
  if (!g_waiting || !g_cs_ready) return;
  std::string save;
  {
    Lock guard;
    if (g_want_save.empty()) return;
    save.swap(g_want_save);
    note_waiting();
  }
  // The client's copy of the character: the same base attributes and skill points the character
  // screen and the skill tree show, kept current by the game.
  Unit* client = game::local_client_player();
  if (client) do_save(client, save);
  else report("Preset \"%s\" not saved: no character", save.c_str());
}

bool on_tick(void* game, Unit* player) {
  if (!g_waiting || !g_cs_ready || !game || !player) return false;
  bool respec = false, load = false;
  std::string save;
  presets::Preset preset;
  {
    Lock guard;
    if (!g_want_respec && g_want_save.empty() && !g_want_load) return false;
    respec = g_want_respec;
    save.swap(g_want_save);
    load = g_want_load;
    if (load) preset = g_load;
    g_want_respec = g_want_load = false;
    note_waiting();
  }
  bool reset = false;
  if (respec) {
    if (const char* why = cheats::why_not(cheats::kRespec)) report("Respec unavailable: %s", why);
    else reset = do_respec(game, player);
  }
  if (!save.empty()) do_save(player, save);
  if (load) {
    if (const char* why = cheats::why_not(cheats::kPresets)) report("Presets unavailable: %s", why);
    else reset = do_load(game, player, preset) || reset;
  }
  return reset;
}

void game_left() {
  if (!g_cs_ready) return;
  Lock guard;
  g_want_respec = g_want_load = false;
  g_want_save.clear();
  note_waiting();
}

}  // namespace d2rcc::character

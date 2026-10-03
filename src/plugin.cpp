// The D2RLoader plugin entry points and the plumbing every other module hangs
// off: the plugin context, the settings file, the console command, the
// lifecycle listeners, the worker that binds everything once the image is
// there, and the UI-thread pump that leaves the game when a cheat asks to.
#include <D2RLPlugin/api.h>

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "cheats.h"
#include "chronicle.h"
#include "config.h"
#include "context.h"
#include "dev.h"
#include "dropodds.h"
#include "game.h"
#include "healthbars.h"
#include "hooks_game.h"
#include "log.h"
#include "mem.h"
#include "overlay.h"
#include "perf.h"
#include "sites.h"
#include "terror.h"
#include "arealevel.h"
#include "autoid.h"
#include "character.h"
#include "consumables.h"
#include "cube.h"
#include "curses.h"
#include "itemlevel.h"
#include "itemseed.h"
#include "loot.h"
#include "mapmarks.h"
#include "mapreveal.h"
#include "remember.h"
#include "hometown.h"
#include "npcservice.h"
#include "passive.h"
#include "revive.h"
#include "version.h"
#include "watchdog.h"

namespace d2rcc {
namespace {

constexpr D2RL::PluginInfo kInfo{
    .infoSize = D2RL::PluginInfoSize,
    .abiVersion = D2RL_PLUGIN_ABI_VERSION,
    .id = "cabbycodes",
    .name = "CabbyCodes",
    .version = D2RCC_VERSION,
    .author = "SirCabby",
    .description = "In-game cheat panel for offline play: god mode, infinite mana, experience and damage "
                   "multipliers, movement speed, exit before death, invincible or passive mercenary and minions, "
                   "revives that stay until they die, no "
                   "freeze, poison or curses, infinite potions, scrolls, keys and gold, items identified as you "
                   "pick them up, imbues, sockets and "
                   "personalizing without their quests, cube recipes that use nothing up, no durability loss, respec "
                   "and skill presets, map reveal, named enemies, landmarks and where each exit leads on the map, "
                   "health bars for monsters and act bosses, a home town of your choice, and the automap and Show "
                   "Items kept the way you left them between games.",
    .flags = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
};

config::Settings g_settings;
char g_config_dir[MAX_PATH] = {};
HANDLE g_worker = nullptr;
HANDLE g_watchdog = nullptr;
HANDLE g_wake = nullptr;  // wakes the watchdog before its wait is over
volatile LONG g_stop = 0;
bool g_pump_seen = false;  // UI thread: the first UI callback is logged
volatile LONG g_sites_usable = -1;
volatile LONG g_save_pending = 0;
const D2RL::ThreadService* g_threads = nullptr;
const D2RL::WidgetService* g_widgets = nullptr;
const D2RL::DataTableService* g_tables = nullptr;
const D2RL::LocalizationService* g_strings = nullptr;
volatile LONG g_ui_ready = 0;     // the loader accepted work for its UI thread (it refuses during load)
volatile LONG g_exit_phase = 0;   // 0 idle, 1 Save and Exit queued for the UI thread, 2 dispatched
ULONGLONG g_exit_started = 0;
bool g_bound = false;

std::string narrow(const wchar_t* w) {
  if (!w || !*w) return "";
  const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
  std::string out(n > 0 ? static_cast<size_t>(n - 1) : 0, '\0');
  if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, out.data(), n, nullptr, nullptr);
  return out;
}

std::wstring log_dir() {
  const D2RL::PluginContext* ctx = context();
  std::wstring dir = ctx && ctx->pluginLogPath ? ctx->pluginLogPath : L"";
  const size_t cut = dir.find_last_of(L"\\/");
  return cut == std::wstring::npos ? L"" : dir.substr(0, cut + 1);
}

// --- the settings file --------------------------------------------------------------------
std::string read_config_text() {
  const D2RL::PluginContext* ctx = context();
  if (!ctx) return "";
  uint32_t required = 0;
  (void)ctx->ReadConfig(nullptr, 0, &required);
  if (required == 0) return "";
  std::string text(required, '\0');
  if (!ctx->ReadConfig(text.data(), required, &required)) return "";
  text.resize(std::strlen(text.c_str()));
  return text;
}

// Read before anything is logged: the settings say whether the log is written at all.
size_t g_settings_bytes = 0;
int g_settings_understood = 0;

void read_settings() {
  const std::string text = read_config_text();
  g_settings_understood = config::parse(text, &g_settings);
  g_settings_bytes = text.size();
}

void log_settings() {
  logf("settings: %d keys read (%zu bytes); god=%d mana=%d exp=x%.2f speed=+%d%% exit=%d/%d%%/%s bars=%d boss=%d/%d%% "
       "loot filter=%d chronicle=%d extra=%d elites=%d perfect=%d superior=%d ethereal=%d socketed=%d affixes=%d "
       "best=%d toggle=%s "
       "pause=%d",
       g_settings_understood, g_settings_bytes, g_settings.god_mode, g_settings.infinite_mana,
       static_cast<double>(g_settings.exp_multiplier), g_settings.move_speed_bonus, g_settings.exit_before_death,
       g_settings.exit_below_life_percent, config::exit_method_name(g_settings.exit_method),
       g_settings.monster_health_bars, g_settings.boss_health_bar, g_settings.boss_bar_position,
       g_settings.loot_filter_only, g_settings.chronicle_only, g_settings.extra_drops,
       g_settings.extra_drops_elites_only,
       g_settings.perfect_rolls, g_settings.all_superior, g_settings.all_ethereal, g_settings.all_socketed,
       g_settings.max_affixes, g_settings.best_affixes, config::key_name(g_settings.toggle_key),
       g_settings.show_on_pause);
  logf("settings: damage=x%.2f merc=%d minions=%d passivemerc=%d passiveminions=%d revives=%d unfreezable=%d "
       "unpoisonable=%d uncursable=%d tp=%d id=%d "
       "autoid=%d potions=%d keys=%d gold=%d imbue=%d addsockets=%d personalize=%d cube=%d durability=%d home=%d "
       "map=%d enemies=%d landmarks=%d exits=%d keep map=%d items=%d unfiltered=%d (remembered %d/%d/%d) log=%d/%s "
       "trace=%d",
       static_cast<double>(g_settings.damage_multiplier), g_settings.invincible_mercenary,
       g_settings.invincible_minions, g_settings.passive_mercenary, g_settings.passive_minions,
       g_settings.permanent_revives, g_settings.cannot_be_frozen, g_settings.cannot_be_poisoned,
       g_settings.cannot_be_cursed, g_settings.infinite_town_portal, g_settings.infinite_identify,
       g_settings.auto_identify, g_settings.infinite_potions, g_settings.infinite_keys, g_settings.infinite_gold,
       g_settings.infinite_imbue, g_settings.infinite_sockets, g_settings.infinite_personalize,
       g_settings.infinite_cube_ingredients, g_settings.no_durability_loss, g_settings.home_town, g_settings.reveal_map,
       g_settings.map_named_enemies, g_settings.map_landmarks, g_settings.map_exit_names, g_settings.remember_automap,
       g_settings.remember_show_items, g_settings.remember_show_items_unfiltered, g_settings.automap_was_open,
       g_settings.show_items_was_on, g_settings.show_items_unfiltered_was_on, g_settings.logging,
       config::log_level_name(g_settings.log_level), g_settings.trace);
}

const char* level_words(LogLevel least) {
  return least == LogLevel::kError     ? "errors only"
         : least == LogLevel::kWarning ? "warnings and errors"
                                       : "information, warnings and errors";
}

// The log follows its switch and its level (the panel, the console, the settings at load). What it says of
// itself is written whatever they are, so a log that ends, never begins or has gaps says why.
void apply_logging() {
  const bool on = cheats::enabled(cheats::kLogging);
  const LogLevel least = cheats::log_level();
  const bool switched = log_is_on() != on, moved = log_level() != least;
  if (!switched && !moved) return;
  LogAlways always;
  log_set_level(least);
  if (switched && !on) {
    logf("log: switched off - from here on nothing is written but what is asked for by name (the console's perf "
         "and dropcheck); the panel's Diagnostics section or `cabbycodes log on` switches it on again");
    log_enable(false);
  } else if (switched) {
    log_enable(true);
    logf("log: switched on - %s are written", level_words(least));
  } else if (on) {
    logf("log: from here on %s are written", level_words(least));
  }
}

void save_settings() {
  const D2RL::PluginContext* ctx = context();
  if (!ctx) return;
  cheats::to_settings(&g_settings);
  remember::to_settings(&g_settings);
  const std::string text = config::rewrite(read_config_text(), g_settings);
  if (ctx->WriteConfig(text.c_str())) logf("settings: written (%zu bytes)", text.size());
  else log_warn("settings: the loader refused to write the file");
}

// --- leaving the game -------------------------------------------------------------------------
// The ladder: the game's own Save and Exit (the pause menu's button sends the UI
// message PausePanelMessage/ExitGame; the plugin sends the same through the
// loader's widget service, on the UI thread), and if that cannot be queued,
// is refused, or does not leave the game in time, closing the game. Closing
// keeps the character alive at its last autosave.
void force_close() {
  logf("closing the game now (%s)", hooks::exit_reason());
  TerminateProcess(GetCurrentProcess(), 0);
}

bool dispatch(const D2RL::PluginContext* ctx, const char* target, const char* command, const char* text) {
  const D2RL::Widgets::UiAction action{
      .structSize = D2RL::Widgets::UiActionSize, .flags = 0, .target = target, .command = command, .text = text};
  const D2RL::Widgets::Result r = g_widgets->dispatchUiAction(ctx, &action);
  if (r != D2RL::Widgets::Result::Success)
    log_warn("ui action %s/%s '%s' refused by the loader (%u)", target, command, text, static_cast<unsigned>(r));
  return r == D2RL::Widgets::Result::Success;
}

void __cdecl exit_on_ui(const D2RL::PluginContext* ctx, void*) noexcept {
  if (g_stop || !g_widgets) return;
  // The same path as the button: the pause menu open, then its Save and Exit.
  dispatch(ctx, "PanelManager", "OpenPanel", "PauseLayoutGarden");
  if (dispatch(ctx, "PausePanelMessage", "ExitGame", "")) {
    logf("leaving the game: Save and Exit sent (%s)", hooks::exit_reason());
    g_exit_started = GetTickCount64();
    InterlockedExchange(&g_exit_phase, 2);
  } else {
    force_close();
  }
}

void begin_exit() {
  const D2RL::PluginContext* ctx = context();
  const bool save_and_exit = cheats::exit_method() == config::ExitMethod::kSaveAndExit;
  if (save_and_exit && ctx && g_threads && g_widgets && g_widgets->dispatchUiAction) {
    const D2RL::Threads::Result r = g_threads->runOnUiThread(ctx, exit_on_ui, nullptr);
    if (r == D2RL::Threads::Result::Success) {
      g_exit_started = GetTickCount64();
      InterlockedExchange(&g_exit_phase, 1);
      logf("leaving the game (%s): Save and Exit queued for the UI thread", hooks::exit_reason());
      return;
    }
    log_warn("leaving the game: the loader refused UI work (%u)", static_cast<unsigned>(r));
  } else if (save_and_exit) {
    log_warn("leaving the game: no UI thread or widget service - Save and Exit unavailable");
  }
  force_close();
}

void exit_watch(ULONGLONG now) {
  const LONG phase = g_exit_phase;
  if (phase == 1 && now - g_exit_started > 1500) {
    log_warn("the UI thread did not take the exit within 1.5 s");
    force_close();
  } else if (phase == 2 && now - g_exit_started > 6000 && game::local_player_id() != 0xFFFFFFFFu) {
    log_warn("still in the game 6 s after Save and Exit");
    force_close();
  }
}

void game_left() {
  if (InterlockedExchange(&g_exit_phase, 0) != 0) logf("left the game through Save and Exit");
}

// The loader refuses UI work while the plugin is loading (OwnerInactive), so
// this probe is retried from the watchdog until accepted; it re-queues itself
// and reports the first time it runs. From then on it runs at every root-widget
// update, once a frame. Only the health bars work every frame, while they have monsters to follow. The rest
// waits for something to happen - an area entered, a key, a panel, a switch, a preset to save - and is one
// read of a flag until then (each looks once a second besides, should what happened not have been announced).
void __cdecl ui_pump(const D2RL::PluginContext* ctx, void*) noexcept {
  if (g_stop) return;
  if (!g_pump_seen) {
    g_pump_seen = true;
    logf("ui thread: the loader delivered a UI callback");
  }
  if (g_bound) {
    perf::Timer timer(perf::kUiPump);
    const ULONGLONG now = GetTickCount64();
    healthbars::collect();
    character::on_ui();  // a preset save, at once even while the Esc menu pauses the game
    mapreveal::on_ui(now);
    remember::on_ui(now);
  }
  // A refused re-queue would end the chain; the watchdog then probes again.
  perf::Timer timer(perf::kUiRequeue);
  if (g_threads && g_threads->runOnUiThread &&
      g_threads->runOnUiThread(ctx, ui_pump, nullptr) != D2RL::Threads::Result::Success) {
    InterlockedExchange(&g_ui_ready, 0);
    log_warn("ui thread: the loader did not take the next UI callback - the watchdog queues it again");
    watchdog::wake();
  }
}

// --- the watchdog thread: saves, the UI probe, leaving the game --------------------------------
// The plugin's own thread, so nothing waits on the loader's queues. Nothing here touches game memory. It
// sleeps until it is woken (watchdog::wake: a setting to write, a preset, a request to leave the game) and
// looks a second later at the latest, should a wake ever be missed. While it waits for something by the
// clock - the loader to take UI work, a remembered change to have held, the game to be left, a sample to
// end - it looks 40 times a second.
constexpr DWORD kWatchdogBusyMs = 25;
constexpr DWORD kWatchdogIdleMs = 1000;

DWORD WINAPI watchdog_thread(void*) {
  log_own_thread();
  ULONGLONG last_probe = 0;
  while (!g_stop) {
    const ULONGLONG now = GetTickCount64();
    const bool remembered_changed = remember::save_due(now);  // asked every time: it clears its own flags
    if (InterlockedExchange(&g_save_pending, 0) || remembered_changed) save_settings();
    character::save_if_changed();
    perf::finish(now);
    if (!g_ui_ready && g_threads && now - last_probe > 1000) {
      last_probe = now;
      const D2RL::PluginContext* ctx = context();
      const D2RL::Threads::Result r = ctx ? g_threads->runOnUiThread(ctx, ui_pump, nullptr) : D2RL::Threads::Result::Unavailable;
      if (r == D2RL::Threads::Result::Success) {
        InterlockedExchange(&g_ui_ready, 1);
        logf("thread service: UI work accepted");
      }
    }
    if (hooks::exit_requested()) {
      hooks::clear_exit_request();
      begin_exit();
    }
    exit_watch(now);
    const bool by_the_clock = (!g_ui_ready && g_threads) || g_exit_phase != 0 || remember::save_waits() ||
                              perf::sampling();
    if (g_wake) WaitForSingleObject(g_wake, by_the_clock ? kWatchdogBusyMs : kWatchdogIdleMs);
    else Sleep(kWatchdogBusyMs);
  }
  character::save_if_changed(true);  // a preset saved just before the plugin unloads
  if (remember::save_due(~0ull)) save_settings();  // ... and a remembered change not written yet
  return 0;
}

void start_pump() {
  const D2RL::PluginContext* ctx = context();
  if (ctx->QueryService(&g_threads) != D2RL::ServiceQueryResult::Success ||
      !D2RL::HasThreadServiceField(g_threads, D2RL::ThreadServiceRequiredSize) || !g_threads->runOnUiThread) {
    g_threads = nullptr;
    log_warn("ThreadService unavailable");
    return;
  }
  if (ctx->QueryService(&g_tables) != D2RL::ServiceQueryResult::Success ||
      !D2RL::HasDataTableServiceField(g_tables, D2RL::DataTableServiceRequiredSize))
    g_tables = nullptr;
  if (!g_tables) log_warn("DataTableService unavailable: no area levels, and bosses are not recognised");
  arealevel::set_services(ctx, g_tables);
  loot::set_services(ctx, g_tables);
  dropodds::set_services(ctx, g_tables);
  itemseed::set_services(ctx, g_tables);
  consumables::set_services(ctx, g_tables);
  curses::set_services(ctx, g_tables);
  hometown::set_services(ctx, g_tables);
  revive::set_services(ctx, g_tables);
  if (ctx->QueryService(&g_strings) != D2RL::ServiceQueryResult::Success ||
      !D2RL::HasLocalizationServiceField(g_strings, D2RL::LocalizationServiceRequiredSize) || !g_strings->getStringById)
    g_strings = nullptr;
  if (!g_strings) log_warn("LocalizationService unavailable: the boss bar shows no names");
  healthbars::set_services(ctx, g_tables, g_strings);
  itemlevel::set_services(ctx, g_tables, g_strings);
  mapmarks::set_services(ctx, g_tables, g_strings);
  if (ctx->QueryService(&g_widgets) != D2RL::ServiceQueryResult::Success ||
      !D2RL::HasWidgetServiceField(g_widgets, D2RL::WidgetServiceRequiredSize) || !g_widgets->dispatchUiAction) {
    g_widgets = nullptr;
    log_warn("WidgetService unavailable: Save and Exit falls back to closing the game");
  }
}

// --- the worker: bind everything once the image is there ------------------------------------
DWORD WINAPI worker(void*) {
  log_own_thread();
  const D2RL::PluginContext* ctx = context();
  if (!ctx) return 0;
  const uintptr_t base = static_cast<uintptr_t>(ctx->exeBase);
  // The overlay first: its factory hooks must be in before the game makes its swap chain.
  overlay::install();
  bool ready = false;
  for (int i = 0; i < 90 && !g_stop; ++i) {
    if (sites::image_ready(base)) {
      ready = true;
      break;
    }
    Sleep(2000);
  }
  if (!ready) {
    log_warn("sites: the image never matched the probe signature - wrong build, or still encrypted");
    for (int k = 0; k < cheats::kCount; ++k) cheats::set_why_not(static_cast<cheats::Kind>(k), "unknown game build");
    return 0;
  }
  const int usable = sites::resolve(base);
  InterlockedExchange(&g_sites_usable, usable);
  sites::derive();
  logf("sites: %d of %d routines resolved exactly once (guarded-read faults so far: %ld)", usable,
       static_cast<int>(sites::kCount), mem::guard_faults());
  game::bind();
  hooks::install();
  healthbars::bind(g_threads != nullptr);
  mapreveal::bind(g_threads != nullptr);
  mapmarks::bind(g_threads != nullptr);
  remember::bind(g_threads != nullptr);
  g_bound = true;
#ifdef D2RCC_DEV
  {
    std::wstring path = log_dir() + L"D2R-";
    for (const char* c = ctx->buildName ? ctx->buildName : "unknown"; *c; ++c) path += static_cast<wchar_t>(*c);
    path += L".dumped.exe";
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) sites::dump_image(base, path.c_str());
  }
#endif
  return 0;
}

// --- lifecycle listeners --------------------------------------------------------------------
const char* event_name(D2RL::Lifecycle::GameplayEventKind kind) {
  using K = D2RL::Lifecycle::GameplayEventKind;
  switch (kind) {
    case K::GameJoined: return "GameJoined";
    case K::GameLeft: return "GameLeft";
    case K::LocalPlayerReady: return "LocalPlayerReady";
    case K::ActChanged: return "ActChanged";
    case K::LevelChanged: return "LevelChanged";
    case K::PlayerResurrected: return "PlayerResurrected";
    case K::PlayerLevelChanged: return "PlayerLevelChanged";
    case K::QuestCompleted: return "QuestCompleted";
    default: return "?";
  }
}

void __cdecl on_gameplay_event(const D2RL::PluginContext*, const D2RL::Lifecycle::GameplayEvent* e, void*) noexcept {
  if (!D2RL::Lifecycle::HasGameplayEventField(e, D2RL::Lifecycle::GameplayEventRequiredSize)) return;
  using K = D2RL::Lifecycle::GameplayEventKind;
  logf("event %s player=%u session=%llu prev=%d cur=%d difficulty=%u quest=%u", event_name(e->kind), e->playerId,
       static_cast<unsigned long long>(e->sessionGeneration), e->previousValue, e->currentValue, e->difficulty,
       e->questRecordId);
  if (e->kind == K::GameJoined || e->kind == K::LocalPlayerReady) remember::game_joined(e->sessionGeneration);
  if (e->kind == K::LocalPlayerReady) {
    game::set_local_player_id(e->playerId);
    logf("local player id %u: client unit %p, online=%d (the server unit is met by the tick)", e->playerId,
         static_cast<void*>(game::local_client_player()), game::is_online() ? 1 : 0);
  } else if (e->kind == K::LevelChanged) {
    game::set_current_level(e->currentValue);
  }
  // The character stands somewhere new, or in a game at all: the map reveal looks at where.
  if (e->kind == K::LevelChanged) mapreveal::arm(e->currentValue);
  else if (e->kind == K::GameJoined || e->kind == K::LocalPlayerReady || e->kind == K::ActChanged ||
           e->kind == K::PlayerResurrected)
    mapreveal::arm(-1);
  if (e->kind == K::GameLeft) {
    game::clear_local_player();
    game::set_current_level(-1);
    cheats::publish(cheats::Status{});
    overlay::set_pause_open(false);
    hooks::game_left();
    healthbars::game_left();
    loot::game_left();
    chronicle::game_left();
    character::game_left();
    mapreveal::game_left();
    mapmarks::game_left();
    remember::game_left();
    terror::game_left();
    hometown::game_left();
    game_left();
    watchdog::wake();  // what is remembered is written now
  }
}

// The game's UI messages, through the loader: the pause menu's open and close
// tell the panel when to appear, and a key, a button or a panel tells the automap and Show Items to look at
// what they are now. Several come every frame (the pointer moving, the monster under it), so those are told
// apart first and cost a comparison. Developer builds also log the messages, which is how the Save and Exit
// message was learned: each kind of message the first two times it is seen, never the ones that come every
// frame (a log line takes the loader some 5 ms, on the thread that draws the game's frames); with
// trace = true every message as it comes, but for those.
const D2RL::SharedEventService* g_shared = nullptr;
D2RL::SharedEvents::ListenerHandle g_ui_listener = D2RL::SharedEvents::InvalidHandle;

bool same_str(const char* a, const char* b) { return a && std::strcmp(a, b) == 0; }

#ifdef D2RCC_DEV
constexpr int kUiLogKinds = 256;     // kinds of message told apart
constexpr int kUiLogPerKind = 2;     // lines a kind gets
constexpr LONG kUiLogTraced = 2000;  // lines in all with trace = true, where every message is logged
struct UiKind {
  uint64_t target, command;
  char text[48];
  int seen;
};
UiKind g_ui_kinds[kUiLogKinds];  // UI thread only
int g_ui_kind_count = 0;
bool g_ui_kinds_full = false;
LONG g_ui_traced = 0;
uint64_t g_ui_last_target = 0, g_ui_last_command = 0;
char g_ui_last_text[48] = {};
unsigned g_ui_repeats = 0;

void log_ui_message(const D2RL::SharedEvents::UiMessageEvent* e, const char* text) {
  if (!log_enabled()) return;  // a kind is not counted as seen while its line would not be written
  if (overlay::settings().trace) {
    // Every message as it comes, repeats of one counted rather than written.
    const bool same = e->targetHash == g_ui_last_target && e->commandHash == g_ui_last_command &&
                      std::strncmp(text, g_ui_last_text, sizeof(g_ui_last_text) - 1) == 0;
    if (same) {
      ++g_ui_repeats;
      return;
    }
    if (g_ui_traced >= kUiLogTraced) return;
    ++g_ui_traced;
    char repeats[48] = {};
    if (g_ui_repeats) std::snprintf(repeats, sizeof(repeats), "  (previous repeated %u times)", g_ui_repeats);
    logf("ui message: target '%s' command '%s' text '%s'%s", e->target ? e->target : "", e->command ? e->command : "",
         text, repeats);
    g_ui_last_target = e->targetHash;
    g_ui_last_command = e->commandHash;
    std::snprintf(g_ui_last_text, sizeof(g_ui_last_text), "%s", text);
    g_ui_repeats = 0;
    return;
  }
  UiKind* kind = nullptr;
  for (int i = 0; i < g_ui_kind_count && !kind; ++i)
    if (g_ui_kinds[i].target == e->targetHash && g_ui_kinds[i].command == e->commandHash &&
        std::strncmp(text, g_ui_kinds[i].text, sizeof(g_ui_kinds[i].text) - 1) == 0)
      kind = &g_ui_kinds[i];
  if (!kind) {
    if (g_ui_kind_count == kUiLogKinds) {
      if (!g_ui_kinds_full) logf("ui message: %d kinds of message seen - no more are logged", kUiLogKinds);
      g_ui_kinds_full = true;
      return;
    }
    kind = &g_ui_kinds[g_ui_kind_count++];
    kind->target = e->targetHash;
    kind->command = e->commandHash;
    std::snprintf(kind->text, sizeof(kind->text), "%s", text);
    kind->seen = 0;
  }
  if (kind->seen >= kUiLogPerKind) return;
  ++kind->seen;
  logf("ui message: target '%s' command '%s' text '%s'%s", e->target ? e->target : "", e->command ? e->command : "",
       text, kind->seen == kUiLogPerKind ? "  (not logged again)" : "");
}
#endif

D2RL::SharedEvents::UiMessageAction __cdecl on_ui_message(const D2RL::PluginContext*,
                                                          const D2RL::SharedEvents::UiMessageEvent* e, void*) noexcept {
  if (!e || e->structSize < D2RL::SharedEvents::UiMessageEventRequiredSize) return D2RL::SharedEvents::UiMessageAction::Continue;
  perf::Timer timer(perf::kUiMessage);
  const char* target = e->target ? e->target : "";
  const char* command = e->command ? e->command : "";
  const char* text = e->text ? e->text : "";
  // Every frame: the pointer moved, and what the hover bar shows of the monster under it.
  if (target[0] == 'M' && same_str(target, "MonsterHealth")) return D2RL::SharedEvents::UiMessageAction::Continue;
  const bool input = target[0] == 'I' && same_str(target, "InputMessage");
  if (input && same_str(command, "MouseMove")) return D2RL::SharedEvents::UiMessageAction::Continue;
#ifdef D2RCC_DEV
  log_ui_message(e, text);
#endif
  if (input) {
    // A key, a button or a binding: the automap or Show Items may be another way now.
    remember::look();
    return D2RL::SharedEvents::UiMessageAction::Continue;
  }
  if (target[0] != 'P' && target[0] != 'F') return D2RL::SharedEvents::UiMessageAction::Continue;
  // The pause menu is the panel named PauseLayoutGarden; PanelManager announces
  // its opening and closing, and the menu itself its own Close and Exit Game.
  if (same_str(target, "PanelManager")) {
    const bool opened = same_str(command, "OpenPanel");
    const bool closed = same_str(command, "ClosePanel") || same_str(command, "UnloadPanel");
    if (same_str(text, "PauseLayoutGarden")) {
      if (opened) overlay::set_pause_open(true);
      else if (closed) overlay::set_pause_open(false);
    } else if (std::strncmp(text, "LootFilter", 10) == 0) {
      // The loot filter's editor panels: while one is open its rules may change under the drops.
      if (opened) loot::note_filter_panel(text, true);
      else if (closed) loot::note_filter_panel(text, false);
    } else if (same_str(text, "LoadScreenPanel")) {
      // A game's loading screen going away: the automap and Show Items are no longer put back.
      if (closed) remember::loading_screen_gone();
    }
    // A panel came or went: the automap's or Show Items' own, or the options' with their display modes.
    remember::look();
  } else if (same_str(target, "PausePanelMessage") || same_str(target, "FrontEndNavigation")) {
    overlay::set_pause_open(false);
    // Save and Exit, or back to the menus: the game is being left, what the three are now is not recorded.
    if (same_str(command, "ExitGame") || same_str(command, "ToFrontEnd")) remember::leaving();
  }
  return D2RL::SharedEvents::UiMessageAction::Continue;
}

void register_ui_messages(const D2RL::PluginContext* ctx) {
  if (ctx->QueryService(&g_shared) != D2RL::ServiceQueryResult::Success ||
      !D2RL::HasSharedEventServiceField(g_shared, D2RL::SharedEventServiceRequiredSize) ||
      !g_shared->registerUiMessageListener) {
    g_shared = nullptr;
    log_warn("SharedEventService unavailable - the panel cannot follow the pause menu (use the toggle key)");
    return;
  }
  const D2RL::SharedEvents::UiMessageListener listener{
      .structSize = D2RL::SharedEvents::UiMessageListenerSize,
      .flags = 0,
      .priority = 0,
      .reserved = 0,
      .callback = on_ui_message,
      .userData = nullptr,
  };
  if (g_shared->registerUiMessageListener(ctx, &listener, &g_ui_listener) != D2RL::SharedEvents::Result::Success)
    log_warn("UI message listener not registered - the panel cannot follow the pause menu (use the toggle key)");
}

void register_lifecycle(const D2RL::PluginContext* ctx) {
  const D2RL::LifecycleService* lifecycle = nullptr;
  if (ctx->QueryService(&lifecycle) != D2RL::ServiceQueryResult::Success ||
      !D2RL::HasLifecycleServiceField(lifecycle, D2RL::LifecycleServiceRequiredSize)) {
    log_warn("LifecycleService unavailable - the local player cannot be found");
    return;
  }
  using K = D2RL::Lifecycle::GameplayEventKind;
  constexpr K kinds[] = {K::GameJoined,   K::GameLeft,          K::LocalPlayerReady,   K::ActChanged,
                         K::LevelChanged, K::PlayerResurrected, K::PlayerLevelChanged, K::QuestCompleted};
  for (K kind : kinds) {
    const D2RL::Lifecycle::GameplayEventListener listener{
        .structSize = D2RL::Lifecycle::GameplayEventListenerSize,
        .kind = kind,
        .callback = on_gameplay_event,
    };
    D2RL::Lifecycle::ListenerHandle handle = D2RL::Lifecycle::InvalidHandle;
    if (lifecycle->registerGameplayEventListener(ctx, &listener, &handle) != D2RL::Lifecycle::Result::Success)
      log_warn("could not listen for %s", event_name(kind));
  }
}

// --- the console command ---------------------------------------------------------------------
const char* scope_name(D2RL::LoadScope s) {
  return s == D2RL::LoadScope::Mod ? "mod" : s == D2RL::LoadScope::Global ? "global" : "?";
}

bool parse_on_off(const char* s, bool* out) {
  if (!std::strncmp(s, "on", 2)) { *out = true; return true; }
  if (!std::strncmp(s, "off", 3)) { *out = false; return true; }
  return false;
}

D2RL::ConsoleCommandResult __cdecl on_console(D2R::Game::Client*, const D2RL::ConsoleCommandContext* cmd,
                                             void*) noexcept {
  if (!cmd || !cmd->plugin) return D2RL::ConsoleCommandResult::Failed;
  const D2RL::PluginContext* ctx = cmd->plugin;
  const char* args = cmd->args ? cmd->args : "";
  while (*args == ' ') ++args;
  char line[512];
  auto say = [&](const char* text) { ctx->WriteConsoleMessage(text); };
  if (!std::strncmp(args, "panel", 5)) {
    overlay::set_forced(!overlay::visible());
    say(overlay::visible() ? "cabbycodes: panel hidden" : "cabbycodes: panel shown");
    return D2RL::ConsoleCommandResult::Handled;
  }
  if (!std::strncmp(args, "sites", 5)) {
    for (int i = 0; i < sites::kCount; ++i) {
      const auto id = static_cast<sites::Id>(i);
      const sites::Result& r = sites::result(id);
      std::snprintf(line, sizeof(line), "  %-44s %s0x%llX (hits %d)", sites::spec(id).name, r.rva ? "" : "not found ",
                    static_cast<unsigned long long>(r.rva), r.hits);
      say(line);
    }
    return D2RL::ConsoleCommandResult::Handled;
  }
  if (!std::strncmp(args, "dump", 4)) {
    std::wstring path = log_dir() + L"D2R-manual.dumped.exe";
    const size_t n = sites::dump_image(static_cast<uintptr_t>(ctx->exeBase), path.c_str());
    std::snprintf(line, sizeof(line), "cabbycodes: dumped %zu bytes", n);
    say(line);
    return D2RL::ConsoleCommandResult::Handled;
  }
  if (!std::strncmp(args, "save", 4)) {
    overlay::on_setting_changed();
    say("cabbycodes: settings will be written");
    return D2RL::ConsoleCommandResult::Handled;
  }
  if (!std::strncmp(args, "perf", 4)) {
    // What the plugin's own work costs while you play: timed for a while, then written to the log.
    const int asked = std::atoi(args + 4);
    const int seconds = asked > 0 ? (asked > 120 ? 120 : asked) : 10;
    perf::start(seconds);
    watchdog::wake();
    std::snprintf(line, sizeof(line),
                  "cabbycodes: timing the plugin's work for %d s - keep playing; the log has the outcome (lines "
                  "that begin \"perf:\")",
                  seconds);
    say(line);
    return D2RL::ConsoleCommandResult::Handled;
  }
  if (!std::strncmp(args, "dropcheck", 9)) {
    // The next monster killed: its treasure class walked by the game itself, with nothing made, against the odds
    // the plugin works out for it (the log has the outcome).
    const int asked = std::atoi(args + 9);
    const int walks = asked > 0 ? asked : 5000;
    loot::check_drops(walks);
    std::snprintf(line, sizeof(line),
                  "cabbycodes: the next monster you kill has its treasure class walked %d times with nothing made "
                  "(a short hitch); the log says whether the odds worked out are the game's",
                  walks);
    say(line);
    return D2RL::ConsoleCommandResult::Handled;
  }
  struct Switch {
    const char* word;
    cheats::Kind kind;
  };
  static const Switch kSwitches[] = {{"god", cheats::kGodMode},       {"mana", cheats::kInfiniteMana},
                                     {"stamina", cheats::kInfiniteStamina}, {"exit", cheats::kExitBeforeDeath},
                                     {"bars", cheats::kMonsterBars},    {"bossbar", cheats::kBossBar},
                                     {"loot", cheats::kLootFilterOnly}, {"chronicle", cheats::kChronicleOnly},
                                     {"elites", cheats::kExtraDropsElite},
                                     {"perfect", cheats::kPerfectRolls},
                                     {"superior", cheats::kAllSuperior}, {"eth", cheats::kAllEthereal},
                                     {"sockets", cheats::kAllSocketed}, {"affixes", cheats::kMaxAffixes},
                                     {"best", cheats::kBestAffixes},
                                     {"merc", cheats::kInvincibleMerc}, {"minions", cheats::kInvinciblePets},
                                     {"passivemerc", cheats::kPassiveMerc},
                                     {"passiveminions", cheats::kPassivePets},
                                     {"revives", cheats::kPermanentRevives},
                                     {"unfreezable", cheats::kCannotBeFrozen},
                                     {"unpoisonable", cheats::kCannotBePoisoned},
                                     {"uncursable", cheats::kCannotBeCursed}, {"gold", cheats::kInfiniteGold},
                                     {"imbue", cheats::kInfiniteImbue}, {"addsockets", cheats::kInfiniteSockets},
                                     {"personalize", cheats::kInfinitePersonalize},
                                     {"cube", cheats::kInfiniteCubeIngredients},
                                     {"tp", cheats::kInfiniteTownPortal}, {"id", cheats::kInfiniteIdentify},
                                     {"autoid", cheats::kAutoIdentify},
                                     {"potions", cheats::kInfinitePotions}, {"keys", cheats::kInfiniteKeys},
                                     {"durability", cheats::kNoDurabilityLoss}, {"tz", cheats::kTerrorAll},
                                     {"map", cheats::kRevealMap}, {"enemies", cheats::kMapEnemies},
                                     {"landmarks", cheats::kMapLandmarks}, {"exits", cheats::kMapExits},
                                     {"ilvl", cheats::kItemLevel},
                                     {"keepmap", cheats::kRememberAutomap},
                                     {"keepitems", cheats::kRememberShowItems},
                                     {"keepunfiltered", cheats::kRememberShowItemsUnfiltered},
                                     {"log", cheats::kLogging}};
  for (const Switch& sw : kSwitches) {
    const size_t n = std::strlen(sw.word);
    if (!std::strncmp(args, sw.word, n) && args[n] == ' ') {
      bool on = false;
      if (!parse_on_off(args + n + 1, &on)) return D2RL::ConsoleCommandResult::InvalidArguments;
      cheats::set_enabled(sw.kind, on);
      overlay::on_setting_changed();
      std::snprintf(line, sizeof(line), "cabbycodes: %s %s", cheats::name(sw.kind), on ? "ON" : "OFF");
      say(line);
      return D2RL::ConsoleCommandResult::Handled;
    }
  }
  if (!std::strncmp(args, "exp ", 4)) {
    cheats::set_exp_multiplier(static_cast<float>(std::atof(args + 4)));
    overlay::on_setting_changed();
    std::snprintf(line, sizeof(line), "cabbycodes: experience x%.2f", static_cast<double>(cheats::exp_multiplier()));
    say(line);
    return D2RL::ConsoleCommandResult::Handled;
  }
  if (!std::strncmp(args, "dmg ", 4)) {
    cheats::set_damage_multiplier(static_cast<float>(std::atof(args + 4)));
    overlay::on_setting_changed();
    std::snprintf(line, sizeof(line), "cabbycodes: damage x%.2f", static_cast<double>(cheats::damage_multiplier()));
    say(line);
    return D2RL::ConsoleCommandResult::Handled;
  }
  if (!std::strncmp(args, "respec", 6)) {
    if (!cheats::status().player_found) {
      say("cabbycodes: enter a game first");  // a request made outside would wait for the next character
    } else if (const char* why = cheats::why_not(cheats::kRespec)) {
      std::snprintf(line, sizeof(line), "cabbycodes: respec unavailable (%s)", why);
      say(line);
    } else {
      character::request_respec();
      say("cabbycodes: skills and attributes will be reset");
    }
    return D2RL::ConsoleCommandResult::Handled;
  }
  if (!std::strncmp(args, "preset", 6)) {
    const char* rest = args + 6;
    while (*rest == ' ') ++rest;
    const int cls = cheats::status().char_class;
    const bool save = !std::strncmp(rest, "save ", 5), load = !std::strncmp(rest, "load ", 5);
    if ((save || load) && cls < 0) {
      say("cabbycodes: enter a game first");
    } else if (save) {
      character::request_save(rest + 5);
      say("cabbycodes: the character's points will be saved");
    } else if (load) {
      say(character::request_load(presets::clean_name(rest + 5).c_str(), cls)
              ? "cabbycodes: the preset will be loaded (skills and attributes are reset first)"
              : "cabbycodes: no preset of that name for this class");
    } else {
      for (const presets::Preset& p : character::list()) {
        std::snprintf(line, sizeof(line), "  %-12s %-32s level %d, %d skill points", presets::class_name(p.char_class),
                      p.name.c_str(), p.level, presets::skill_points(p));
        say(line);
      }
      say("cabbycodes: preset save <name> | preset load <name>");
    }
    return D2RL::ConsoleCommandResult::Handled;
  }
  if (!std::strncmp(args, "drops ", 6)) {
    cheats::set_extra_drops(std::atoi(args + 6));
    overlay::on_setting_changed();
    std::snprintf(line, sizeof(line), "cabbycodes: %d extra drops per %s", cheats::extra_drops(),
                  cheats::enabled(cheats::kExtraDropsElite) ? "champion, unique, boss or Herald killed"
                                                            : "kill or chest");
    say(line);
    return D2RL::ConsoleCommandResult::Handled;
  }
  if (!std::strncmp(args, "loglevel", 8)) {
    const char* rest = args + 8;
    while (*rest == ' ') ++rest;
    char word[16] = {};
    std::sscanf(rest, "%15s", word);
    LogLevel least = cheats::log_level();
    if (!config::log_level_from_name(word, &least)) {
      std::snprintf(line, sizeof(line), "cabbycodes: loglevel info | warning | error - the least severe kind of line "
                                        "the log takes; now %s (%s)",
                    config::log_level_name(cheats::log_level()), level_words(cheats::log_level()));
      say(line);
      return *word ? D2RL::ConsoleCommandResult::InvalidArguments : D2RL::ConsoleCommandResult::Handled;
    }
    cheats::set_log_level(least);
    overlay::on_setting_changed();
    std::snprintf(line, sizeof(line), "cabbycodes: the log takes %s%s", level_words(least),
                  cheats::enabled(cheats::kLogging) ? "" : " (once it is switched on: cabbycodes log on)");
    say(line);
    return D2RL::ConsoleCommandResult::Handled;
  }
  if (!std::strncmp(args, "home", 4) && (args[4] == ' ' || !args[4])) {
    // The home town: 0 the game's own, 1 .. 5 that act's town.
    const char* rest = args + 4;
    while (*rest == ' ') ++rest;
    if (*rest < '0' || *rest > '5' || (rest[1] && rest[1] != ' ')) {
      std::snprintf(line, sizeof(line),
                    "cabbycodes: home 0..5 - your home town, where your town portals lead and you wake after a "
                    "death: 0 the town of the act you are in (the game's own), 1 to 5 that act's town; now %d",
                    cheats::home_town());
      say(line);
      return *rest ? D2RL::ConsoleCommandResult::InvalidArguments : D2RL::ConsoleCommandResult::Handled;
    }
    cheats::set_home_town(*rest - '0');
    overlay::on_setting_changed();
    const int act = cheats::home_town();
    if (act)
      std::snprintf(line, sizeof(line), "cabbycodes: home is %s (act %d): town portals lead there%s",
                    hometown::town_name(act - 1), act,
                    hometown::wakes_at_home() ? ", and you wake there after a death" : "");
    else
      std::snprintf(line, sizeof(line),
                    "cabbycodes: no home town: town portals lead to the town of the act they are read in, and a "
                    "death wakes you in the town of the act you died in");
    say(line);
    return D2RL::ConsoleCommandResult::Handled;
  }
  if (!std::strncmp(args, "speed ", 6)) {
    cheats::set_move_speed_bonus(std::atoi(args + 6));
    overlay::on_setting_changed();
    std::snprintf(line, sizeof(line), "cabbycodes: movement speed +%d%%", cheats::move_speed_bonus());
    say(line);
    return D2RL::ConsoleCommandResult::Handled;
  }
  const cheats::Status st = cheats::status();
  std::snprintf(line, sizeof(line),
                "CabbyCodes v%s: build %s (%s), scope %s, mod '%s', sites %ld/%d, hooks %s, player %s (#%u life %d/%d "
                "mana %d/%d lvl %d exp %lld ticks %u). god=%d (%u hits on you, %u life losses refused) mana=%d exit=%d "
                "exp=x%.1f speed=+%d%%",
                D2RCC_VERSION, ctx->buildVersion ? ctx->buildVersion : "?", ctx->buildName ? ctx->buildName : "?",
                scope_name(ctx->loadScope), ctx->activeMod ? ctx->activeMod : "", static_cast<long>(g_sites_usable),
                static_cast<int>(sites::kCount), g_bound ? "installed" : "pending", st.player_found ? "found" : "-",
                st.player_id, st.hp, st.max_hp, st.mana, st.max_mana, st.level, static_cast<long long>(st.experience),
                st.ticks, cheats::enabled(cheats::kGodMode), st.hits_absorbed, hooks::life_losses_kept(),
                cheats::enabled(cheats::kInfiniteMana),
                cheats::enabled(cheats::kExitBeforeDeath), static_cast<double>(cheats::exp_multiplier()),
                cheats::move_speed_bonus());
  say(line);
  const loot::Stats loot_stats = loot::stats();
  std::snprintf(line, sizeof(line),
                "loot: filter only=%d extra drops=%d (elites only=%d) perfect=%d superior=%d ethereal=%d socketed=%d "
                "affixes=%d best=%d; this game %u hidden drops replaced, %u omitted, %u extra drops, %u drops the "
                "filter hides never made%s, %u picked among what it shows (%u came out otherwise%s; the longest a "
                "drop took %u.%u ms), %u values maxed, %u class items' skills maxed, %u affixes added, %u affixes "
                "upgraded",
                cheats::enabled(cheats::kLootFilterOnly), cheats::extra_drops(),
                cheats::enabled(cheats::kExtraDropsElite), cheats::enabled(cheats::kPerfectRolls),
                cheats::enabled(cheats::kAllSuperior), cheats::enabled(cheats::kAllEthereal),
                cheats::enabled(cheats::kAllSocketed), cheats::enabled(cheats::kMaxAffixes),
                cheats::enabled(cheats::kBestAffixes), loot_stats.replaced, loot_stats.omitted, loot_stats.extra,
                loot_stats.unmade, loot_stats.ask_first_off ? " (off for the session, see the log)" : "",
                loot_stats.picked, loot_stats.missed,
                loot_stats.odds_off ? "; picking is off for the session, see the log" : "",
                loot_stats.slowest_us / 1000, loot_stats.slowest_us % 1000 / 100, loot_stats.maxed,
                loot_stats.skill_items, loot_stats.affixes, loot_stats.upgraded);
  say(line);
  std::snprintf(line, sizeof(line),
                "superior items (perfect rolls): %u given the best kind of bonus that goes on them where the game's "
                "roll came to another first%s",
                loot_stats.kinds, loot_stats.kinds_off ? "; off for the session (see the log)" : "");
  say(line);
  if (const chronicle::Stats cs = chronicle::stats(); cs.read)
    std::snprintf(line, sizeof(line),
                  "chronicle: only what it misses=%d; it misses %d of %d uniques and %d of %d set items; %d of them "
                  "dropped this game",
                  cheats::enabled(cheats::kChronicleOnly), cs.uniques_missing, cs.uniques, cs.sets_missing, cs.sets,
                  cs.dropped);
  else
    std::snprintf(line, sizeof(line), "chronicle: only what it misses=%d%s", cheats::enabled(cheats::kChronicleOnly),
                  cheats::why_not(cheats::kChronicleOnly) ? "; unavailable (see the log)"
                                                           : "; not read in this game yet (it is read at a drop)");
  say(line);
  const itemseed::Stats seeds = itemseed::stats();
  std::snprintf(line, sizeof(line),
                "unit seed (the one a save keeps): %u armor pieces made from a seed that rolls their top base "
                "defense, %u came out otherwise; the longest a seed took to find: %u us%s",
                seeds.chosen, seeds.differed, seeds.longest_us,
                seeds.off ? "; off for the session (see the log)" : "");
  say(line);
  std::snprintf(line, sizeof(line),
                "combat: dmg=x%.2f merc=%d minions=%d unfreezable=%d unpoisonable=%d uncursable=%d (%u hits scaled, "
                "%u pet hits taken away, %u effects blocked, %u poisons ended, %u curses kept off and %u ended); "
                "items: tp=%d id=%d potions=%d keys=%d durability=%d gold=%d (%u tome charges and %u keys kept, %u "
                "wear rolls skipped, %u payments of %lld gold not taken); character: %s, %d attribute and %d skill "
                "points to spend",
                static_cast<double>(cheats::damage_multiplier()), cheats::enabled(cheats::kInvincibleMerc),
                cheats::enabled(cheats::kInvinciblePets), cheats::enabled(cheats::kCannotBeFrozen),
                cheats::enabled(cheats::kCannotBePoisoned), cheats::enabled(cheats::kCannotBeCursed), st.hits_scaled,
                st.pet_hits, st.effects_blocked, st.poisons_ended, curses::kept_off(), curses::ended(),
                cheats::enabled(cheats::kInfiniteTownPortal), cheats::enabled(cheats::kInfiniteIdentify),
                cheats::enabled(cheats::kInfinitePotions), cheats::enabled(cheats::kInfiniteKeys),
                cheats::enabled(cheats::kNoDurabilityLoss), cheats::enabled(cheats::kInfiniteGold),
                consumables::tome_charges_kept(), consumables::keys_kept(), st.wear_skipped, st.payments_free,
                static_cast<long long>(st.gold_kept),
                st.player_found ? presets::class_name(st.char_class) : "no character", st.stat_points,
                st.skill_points);
  say(line);
  {
    const hometown::Stats portal = hometown::stats();
    const char* portal_why = cheats::why_not(cheats::kHomeTown);
    const int choice = cheats::home_town();
    char reach[64] = "not in a game";
    if (portal.in_game && portal.known) {
      size_t at = 0;
      reach[0] = 0;
      for (int a = 0; a < portal.acts && at < sizeof(reach) - 4; ++a)
        if ((portal.reach >> a) & 1u)
          at += static_cast<size_t>(std::snprintf(reach + at, sizeof(reach) - at, "%s%d", at ? " " : "", a + 1));
    } else if (portal.in_game) {
      std::snprintf(reach, sizeof(reach), "not read yet");
    }
    std::snprintf(line, sizeof(line),
                  "home town: home=%d (%s)%s%s; the character has the town waypoints of acts %s and stands in act "
                  "%d; %u portals made to the home town, %u uses across acts (%u the client left to the server), "
                  "%u wakes at home%s",
                  choice, choice ? hometown::town_name(choice - 1) : "the game's own", portal_why ? " - " : "",
                  portal_why ? portal_why : "", reach, portal.here + 1, portal.made, portal.trips, portal.asked,
                  portal.wakes,
                  hometown::wakes_at_home() || portal_why ? "" : " (a death's wake is the game's own: not hooked)");
    say(line);
  }
  {
    // The three NPC item services: the switch, why it cannot work, whether the quest is out of the NPC's record
    // now, and how often the service was put in the menu.
    const npcservice::Stats services = npcservice::stats();
    const cheats::Kind kinds[npcservice::kServices] = {cheats::kInfiniteImbue, cheats::kInfiniteSockets,
                                                       cheats::kInfinitePersonalize};
    size_t at = static_cast<size_t>(std::snprintf(line, sizeof(line), "npc services:"));
    for (int s = 0; s < npcservice::kServices && at < sizeof(line); ++s) {
      const char* why = cheats::why_not(kinds[s]);
      at += static_cast<size_t>(std::snprintf(line + at, sizeof(line) - at, "%s %s (%s)=%d%s%s, %s, offered %u time%s",
                                              s ? ";" : "", npcservice::kServiceName[s], npcservice::kNpcName[s],
                                              cheats::enabled(kinds[s]), why ? " - " : "", why ? why : "",
                                              services.lifted[s] ? "no quest asks" : "the quest asks",
                                              services.offered[s], services.offered[s] == 1 ? "" : "s"));
    }
    say(line);
  }
  {
    // Infinite cube ingredients: the switch, why it cannot work, whether Clear Sockets' socket contents come back
    // too, and what came back this session.
    const cube::Stats cs = cube::stats();
    const char* why = cheats::why_not(cheats::kInfiniteCubeIngredients);
    std::snprintf(line, sizeof(line),
                  "cube: ingredients kept=%d%s%s (Clear Sockets' socket contents %s); %u transmutes kept %u "
                  "ingredients and %u socket contents: %u back in the inventory, %u at your feet, %u freed with no "
                  "place anywhere",
                  cheats::enabled(cheats::kInfiniteCubeIngredients), why ? " - " : "", why ? why : "",
                  cube::sockets_kept() ? "kept too" : "destroyed as the game has it", cs.transmutes,
                  cs.kept[cube::kIngredient], cs.kept[cube::kSocketed], cs.inventory, cs.ground, cs.lost);
    say(line);
  }
  {
    // Passive mercenary and minions: the switches, why they cannot work, and what the pets' AI ticks came to.
    const passive::Stats ps = passive::stats();
    const char* why = cheats::why_not(cheats::kPassiveMerc);
    std::snprintf(line, sizeof(line),
                  "passive pets: passivemerc=%d passiveminions=%d%s%s; %u AI ticks of your pets kept passive, %u of "
                  "them with an enemy to take on (%u enemy tests answered no, %u kept targets left aside)",
                  cheats::enabled(cheats::kPassiveMerc), cheats::enabled(cheats::kPassivePets), why ? " - " : "",
                  why ? why : "", ps.ticks, ps.held, ps.refused, ps.kept);
    say(line);
  }
  {
    // Permanent revives: the switch, why it cannot work, and the revives that stayed past their time this session.
    const revive::Stats rs = revive::stats();
    const char* why = cheats::why_not(cheats::kPermanentRevives);
    std::snprintf(line, sizeof(line), "permanent revives: revives=%d%s%s; %u revives of yours stayed past their time",
                  cheats::enabled(cheats::kPermanentRevives), why ? " - " : "", why ? why : "", rs.kept);
    say(line);
  }
  {
    // Identify on pickup: the switch, why it cannot work, and what it identified this session.
    const autoid::Stats as = autoid::stats();
    const char* why = cheats::why_not(cheats::kAutoIdentify);
    std::snprintf(line, sizeof(line),
                  "identify on pickup: autoid=%d%s%s; %u items identified as they were picked up (%u of them onto the "
                  "cursor)",
                  cheats::enabled(cheats::kAutoIdentify), why ? " - " : "", why ? why : "", as.identified, as.cursor);
    say(line);
  }
  const mapreveal::Stats map = mapreveal::stats();
  std::snprintf(line, sizeof(line),
                "display: map reveal=%d%s%s, this game %u areas revealed (%u rooms); item level=%d%s%s, %u names "
                "shown",
                cheats::enabled(cheats::kRevealMap), cheats::why_not(cheats::kRevealMap) ? " - " : "",
                cheats::why_not(cheats::kRevealMap) ? cheats::why_not(cheats::kRevealMap) : "", map.areas, map.rooms,
                cheats::enabled(cheats::kItemLevel), cheats::why_not(cheats::kItemLevel) ? " - " : "",
                cheats::why_not(cheats::kItemLevel) ? cheats::why_not(cheats::kItemLevel) : "",
                itemlevel::names_shown());
  say(line);
  {
    // Landmarks, named enemies and exits on the map: the switches, why they cannot work, and what this game came to.
    const mapmarks::Stats ms = mapmarks::stats();
    const char* lm_why = cheats::why_not(cheats::kMapLandmarks);
    const char* en_why = cheats::why_not(cheats::kMapEnemies);
    const char* ex_why = cheats::why_not(cheats::kMapExits);
    std::snprintf(line, sizeof(line),
                  "map: landmarks=%d%s%s, this game %u areas, %u object icons and %u waypoint tiles put on; enemies=%d%s%s, "
                  "%u spawn spots read, %u named enemies drawn (%u of them on the map now), %u marks let go as their "
                  "monster died; exits=%d%s%s, %u areas, %u warps and %u openings named (%u on the map now)",
                  cheats::enabled(cheats::kMapLandmarks), lm_why ? " - " : "", lm_why ? lm_why : "", ms.areas, ms.icons,
                  ms.waypoints, cheats::enabled(cheats::kMapEnemies), en_why ? " - " : "", en_why ? en_why : "",
                  ms.spots, ms.drawn, ms.kept, ms.dead, cheats::enabled(cheats::kMapExits), ex_why ? " - " : "",
                  ex_why ? ex_why : "", ms.exit_areas, ms.warps, ms.borders, ms.exits);
    say(line);
  }
  const terror::Stats terror_stats = terror::stats();
  const char* terror_why = cheats::why_not(cheats::kTerrorAll);
  std::snprintf(line, sizeof(line), "terror: all areas=%d%s%s; %s, %d areas terrorized, %d zones applied by the switch",
                cheats::enabled(cheats::kTerrorAll), terror_why ? " - " : "", terror_why ? terror_why : "",
                !terror_stats.in_game                              ? "not in a game"
                : terror_stats.checked && !terror_stats.has_zones ? "this game has no terror zones"
                                                                   : "in a game",
                terror_stats.areas, terror_stats.zones);
  say(line);
  std::snprintf(line, sizeof(line),
                "kept between games: automap=%d show items=%d unfiltered=%d; remembered: automap %s, show items %s, "
                "unfiltered %s; display modes %d/%d (0 hold, 1 toggle, 2 timed)",
                cheats::enabled(cheats::kRememberAutomap), cheats::enabled(cheats::kRememberShowItems),
                cheats::enabled(cheats::kRememberShowItemsUnfiltered),
                remember::remembered(remember::kAutomap) ? "open" : "closed",
                remember::remembered(remember::kShowItems) ? "on" : "off",
                remember::remembered(remember::kShowItemsUnfiltered) ? "on" : "off",
                remember::display_mode(remember::kShowItems), remember::display_mode(remember::kShowItemsUnfiltered));
  say(line);
  std::snprintf(line, sizeof(line), "log: %s, level %s (%s)%s", cheats::enabled(cheats::kLogging) ? "written" : "off",
                config::log_level_name(cheats::log_level()), level_words(cheats::log_level()),
                g_settings.trace ? "; trace = true: every line is written the instant it is made" : "");
  say(line);
  say("commands: cabbycodes panel | god on|off | mana on|off | stamina on|off | exit on|off | bars on|off | "
      "bossbar on|off | exp <x> | dmg <x> | speed <pct> | tz on|off | loot on|off | chronicle on|off | drops <n> | "
      "elites on|off | "
      "perfect on|off | superior on|off | eth on|off | sockets on|off | affixes on|off | best on|off | "
      "merc on|off | minions on|off | passivemerc on|off | passiveminions on|off | revives on|off | "
      "unfreezable on|off | "
      "unpoisonable on|off | uncursable on|off | tp on|off | id on|off | autoid on|off | potions on|off | "
      "keys on|off | "
      "gold on|off | imbue on|off | addsockets on|off | personalize on|off | cube on|off | durability on|off | "
      "home 0..5 | "
      "map on|off | enemies on|off | landmarks on|off | ilvl on|off | keepmap on|off | keepitems on|off | "
      "keepunfiltered on|off | log on|off | "
      "loglevel info|warning|error | respec | preset [save|load <name>] | "
      "save | sites | dump | dropcheck [walks] | perf [seconds]");
  return D2RL::ConsoleCommandResult::Handled;
}

}  // namespace

// The overlay's view of the settings.
const config::Settings& overlay::settings() { return g_settings; }
const char* overlay::config_dir_utf8() { return g_config_dir; }
// A switch or a number changed (the panel, the console): the file is written, and what waits for something to
// happen looks at once.
void overlay::on_setting_changed() {
  apply_logging();
  InterlockedExchange(&g_save_pending, 1);
  mapreveal::arm(-1);
  remember::look();
  watchdog::wake();
}

void watchdog::wake() {
  if (g_wake) SetEvent(g_wake);
}

}  // namespace d2rcc

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* { return &d2rcc::kInfo; }

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* ctx) noexcept -> bool {
  using namespace d2rcc;
  if (!D2RL::HasContext(ctx) || ctx->abiVersion != D2RL_PLUGIN_ABI_VERSION) return false;
  set_context(ctx);
  mem::guard_install();
  // The settings first: they say whether the log is written. One line says so when it is not.
  read_settings();
  log_enable(g_settings.logging);
  log_set_level(g_settings.log_level);
  if (!g_settings.logging || g_settings.log_level != LogLevel::kInfo) {
    LogAlways always;
    if (!g_settings.logging)
      logf("CabbyCodes v%s: the log is off (logging = false) - nothing is written but what is asked for by name; "
           "the panel's Diagnostics section or `cabbycodes log on` switches it on",
           D2RCC_VERSION);
    else
      logf("CabbyCodes v%s: the log takes %s (log_level = \"%s\"); the panel's Diagnostics section or `cabbycodes "
           "loglevel info` has it take everything",
           D2RCC_VERSION, level_words(g_settings.log_level), config::log_level_name(g_settings.log_level));
  }

  logf("CabbyCodes v%s loading: ABI %u, scope %s, exe base 0x%llX, build '%s' (%s), mod '%s'", D2RCC_VERSION,
       ctx->abiVersion, scope_name(ctx->loadScope), static_cast<unsigned long long>(ctx->exeBase),
       ctx->buildVersion ? ctx->buildVersion : "?", ctx->buildName ? ctx->buildName : "?",
       ctx->activeMod ? ctx->activeMod : "");
  {
    const std::string cfg = narrow(ctx->pluginConfigPath);
    const size_t cut = cfg.find_last_of("\\/");
    std::snprintf(g_config_dir, sizeof(g_config_dir), "%s", cut == std::string::npos ? "" : cfg.substr(0, cut + 1).c_str());
  }
  log_settings();
  cheats::init(g_settings);
  remember::init(g_settings);
  character::set_config_dir(g_config_dir);
  character::load();
  if (!g_settings.enabled) {
    logf("settings: enabled = false - the plugin stays idle (no hooks, no panel)");
    return true;
  }
  // From here on the game's threads hand their log lines to a writer thread (with trace = true they write
  // them themselves, as they are made).
  log_start(g_settings.trace);
  if (!ctx->RegisterConsoleCommand("cabbycodes", on_console, "CabbyCodes: status, switches and developer aids."))
    log_warn("console command not registered");
  register_lifecycle(ctx);
  register_ui_messages(ctx);
  start_pump();

  InterlockedExchange(&g_stop, 0);
  g_wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (!g_wake) log_warn("the watchdog has no wake event (error %lu) - it looks 40 times a second", GetLastError());
  g_watchdog = CreateThread(nullptr, 0, watchdog_thread, nullptr, 0, nullptr);
  g_worker = CreateThread(nullptr, 0, worker, nullptr, 0, nullptr);
  dev::start();
  logf("loaded; binding the game's routines in the background");
  return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
  using namespace d2rcc;
  InterlockedExchange(&g_stop, 1);
  watchdog::wake();
  dev::stop();
  if (g_worker) {
    WaitForSingleObject(g_worker, 5000);
    CloseHandle(g_worker);
    g_worker = nullptr;
  }
  if (g_watchdog) {
    // The event stays with a watchdog that did not end in time: it still waits on it.
    const bool ended = WaitForSingleObject(g_watchdog, 2000) == WAIT_OBJECT_0;
    CloseHandle(g_watchdog);
    g_watchdog = nullptr;
    if (ended && g_wake) {
      CloseHandle(g_wake);
      g_wake = nullptr;
    }
  }
  if (g_shared && g_ui_listener != D2RL::SharedEvents::InvalidHandle && g_shared->unregisterUiMessageListener) {
    g_shared->unregisterUiMessageListener(context(), g_ui_listener);
    g_ui_listener = D2RL::SharedEvents::InvalidHandle;
  }
  hooks::uninstall();
  overlay::uninstall();
  log_stop();  // after the lines that still wait
  set_context(nullptr);
}

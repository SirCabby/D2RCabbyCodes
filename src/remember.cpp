#include "remember.h"

#include "cheats.h"
#include "game.h"
#include "gate.h"
#include "log.h"
#include "overlay.h"
#include "perf.h"

namespace d2rcc::remember {
namespace {

constexpr ULONGLONG kSettleMs = 1000;           // kept at least this long after the character stands in the game,
constexpr ULONGLONG kLoadingFallbackMs = 10000; // ... and until the loading screen is gone (or this long)
constexpr ULONGLONG kGiveUpMs = 60000;          // a game whose character never shows up is not waited for
constexpr ULONGLONG kPressGapMs = 250;          // between two presses of one key (a panel in the way refuses one)
constexpr int kMaxPresses = 6;                  // ... and no more than this many in one load
constexpr ULONGLONG kModeRefreshMs = 250;       // the display modes change only in the game's options
constexpr ULONGLONG kSaveDelayMs = 1500;        // a change is written once it has held this long
// While a game runs the three are looked at when something happened that may have changed one - a key, a
// panel, a switch of ours - and not at every frame: at the frame after it, and once more a little later
// (what a key does may take a frame or two to show). Should something change them that announces nothing,
// a look once a second finds it.
constexpr int kLooksPerEvent = 2;
constexpr ULONGLONG kSecondLookMs = 150;
constexpr ULONGLONG kSafetyLookMs = 1000;

const cheats::Kind kKinds[kCount] = {cheats::kRememberAutomap, cheats::kRememberShowItems,
                                     cheats::kRememberShowItemsUnfiltered};
const game::KeyAction kKeys[kCount] = {game::kKeyAutomap, game::kKeyShowItems, game::kKeyShowItemsUnfiltered};
const char* const kLabels[kCount] = {"the automap", "Show Items", "Show Items (Unfiltered)"};

volatile LONG g_bound = 0;
volatile LONG g_remembered[kCount] = {};
volatile LONG g_mode[kCount] = {game::kDisplayUnknown, game::kDisplayUnknown, game::kDisplayUnknown};
volatile LONG g_armed = 0;         // a game began: the UI thread starts putting the three back
volatile LONG g_loading_gone = 0;  // the loading screen of that game went away
volatile LONG g_loading_up = 0;    // a loading screen is up now (a game's, or an act's)
volatile LONG g_leaving = 0;       // the game is being left: nothing more is recorded
volatile LONG g_reset = 0;         // the game was left: the UI thread forgets it
volatile LONG g_look = 0;          // something happened: the three may be another way now
volatile LONG g_dirty = 0;         // what is remembered changed since the file was written
volatile LONG g_flush = 0;         // ... write it now (the game was left)
volatile LONG64 g_changed_at = 0;
volatile LONG64 g_session = -1;    // the game session the last arming was for

const char* state_word(int w, bool on) { return w == kAutomap ? (on ? "open" : "closed") : (on ? "on" : "off"); }

// UI thread only.
enum Phase { kIdle, kRestore, kTrack, kLeft };
// Why the three are put back: a game was loaded, or a cinematic of the game's closed the automap.
enum Why { kAtLoad, kAfterCinematic };
struct Restore {
  Why why = kAtLoad;
  ULONGLONG armed = 0;
  ULONGLONG ready = 0;  // the character first stood in the game (after a cinematic: and no loading screen was up)
  int presses[kCount] = {};
  ULONGLONG last_press[kCount] = {};
  bool refused[kCount] = {};
};
Phase g_phase = kIdle;
Restore g_restore;
bool g_held = false;  // the last look found a panel open that the game closes the automap for (a cinematic)
ULONGLONG g_modes_at = 0;
gate::Looks g_looks(kLooksPerEvent, kSecondLookMs, kSafetyLookMs);

void begin_restore(ULONGLONG now, Why why) {
  g_phase = kRestore;
  g_restore = Restore{};
  g_restore.why = why;
  g_restore.armed = now;
}

// The option is on and usable, and for a Show Items key the game shows it in Toggle mode.
bool kept(int w) {
  if (!cheats::enabled(kKinds[w]) || cheats::why_not(kKinds[w])) return false;
  return w == kAutomap || g_mode[w] == game::kDisplayToggle;
}

void refresh_modes(ULONGLONG now) {
  if (!cheats::enabled(kKinds[kShowItems]) && !cheats::enabled(kKinds[kShowItemsUnfiltered])) return;
  if (g_modes_at && now - g_modes_at < kModeRefreshMs) return;
  g_modes_at = now;
  for (int w = kShowItems; w < kCount; ++w) {
    const int mode = game::item_name_display(kKeys[w]);
    if (InterlockedExchange(&g_mode[w], mode) != mode)
      logf("remember: %s is shown in %s mode", kLabels[w],
           mode == game::kDisplayHold     ? "Hold"
           : mode == game::kDisplayToggle ? "Toggle"
           : mode == game::kDisplayTimed  ? "Timed"
                                          : "an unknown");
  }
}

// While the game loads, or once a cinematic of the game's is over: press a key whose state is not the remembered
// one, for a moment (at a load until the loading screen is gone).
void restore(ULONGLONG now) {
  const bool after = g_restore.why == kAfterCinematic;
  game::Unit* me = game::local_client_player();
  // While a cinematic is up the game refuses the automap; after one, the loading screen of an act change may still
  // be up, and is waited for (10 s at the most).
  const bool held = game::automap_held_closed() == 1;
  const bool loading = after && g_loading_up && now - g_restore.armed < kLoadingFallbackMs;
  if (!me || game::unit_is_dead(me) || game::current_level() <= 0 || overlay::pause_open() || held || loading) {
    // After a cinematic the character is in the game already: it is waited for as long as it takes.
    if (!after && now - g_restore.armed > kGiveUpMs) {
      log_warn("remember: the character never stood in the game - nothing was put back");
      g_phase = kTrack;
      look();
    }
    return;
  }
  if (!g_restore.ready) g_restore.ready = now;
  for (int w = 0; w < kCount; ++w) {
    if (!kept(w) || g_restore.refused[w]) continue;
    const int now_on = game::key_action_state(kKeys[w]);
    if (now_on < 0 || (now_on != 0) == (g_remembered[w] != 0)) continue;
    if (g_restore.presses[w] >= kMaxPresses || now - g_restore.last_press[w] < kPressGapMs) continue;
    if (!game::press_key_action(kKeys[w])) {
      g_restore.refused[w] = true;
      log_warn("remember: the key for %s no longer runs what was checked - left as it is", kLabels[w]);
      continue;
    }
    ++g_restore.presses[w];
    g_restore.last_press[w] = now;
    const int after = game::key_action_state(kKeys[w]);
    logf("remember: pressed the key for %s (%s, wanted %s): now %s", kLabels[w], state_word(w, now_on != 0),
         state_word(w, g_remembered[w] != 0), after < 0 ? "unknown" : state_word(w, after != 0));
  }
  const bool loaded = after || g_loading_gone || now - g_restore.ready >= kLoadingFallbackMs;
  if (!loaded || now - g_restore.ready < kSettleMs) return;
  g_phase = kTrack;
  look();  // tracking starts from what they are now
  int presses = 0;
  if (after) {
    for (int w = 0; w < kCount; ++w) {
      if (!kept(w)) continue;
      presses += g_restore.presses[w];
      const int on = game::key_action_state(kKeys[w]);
      if (on < 0 || (on != 0) != (g_remembered[w] != 0))
        log_warn("remember: %s could not be made %s again after the game's cinematic (%d presses)", kLabels[w],
                 state_word(w, g_remembered[w] != 0), g_restore.presses[w]);
      else if (g_restore.presses[w])
        logf("remember: %s is %s again after the game's cinematic (%d press%s)", kLabels[w], state_word(w, on != 0),
             g_restore.presses[w], g_restore.presses[w] == 1 ? "" : "es");
    }
    if (presses) cheats::note("The automap put back after the game's cinematic");
    return;
  }
  for (int w = 0; w < kCount; ++w) {
    if (!cheats::enabled(kKinds[w])) continue;
    presses += g_restore.presses[w];
    const int on = game::key_action_state(kKeys[w]);
    if (!kept(w))
      logf("remember: %s is not kept (%s)", kLabels[w],
           cheats::why_not(kKinds[w]) ? cheats::why_not(kKinds[w]) : "the game does not show it in Toggle mode");
    else if (on >= 0 && (on != 0) == (g_remembered[w] != 0))
      logf("remember: %s is %s as it was (%d press%s)", kLabels[w], state_word(w, on != 0), g_restore.presses[w],
           g_restore.presses[w] == 1 ? "" : "es");
    else
      log_warn("remember: %s could not be made %s (%d presses)", kLabels[w], state_word(w, g_remembered[w] != 0),
               g_restore.presses[w]);
  }
  if (presses) cheats::note("Automap and Show Items put back as they were");
}

// While the game runs: what the three are now is what is remembered.
void track(ULONGLONG now) {
  // Under the Esc menu nothing changes by hand, and leaving the game starts there.
  if (overlay::pause_open()) return;
  // A panel the game closes the automap for as it opens (a cinematic) is the game's doing: nothing is recorded
  // while it is up, and once it is gone the three are put back.
  if (game::automap_held_closed() == 1) {
    if (!g_held && kept(kAutomap) && g_remembered[kAutomap])
      logf("remember: a cinematic of the game's closed the automap - it is opened again once the cinematic is over");
    g_held = true;
    return;
  }
  if (g_held) {
    g_held = false;
    begin_restore(now, kAfterCinematic);
    return;
  }
  for (int w = 0; w < kCount; ++w) {
    if (!kept(w)) continue;
    const int on = game::key_action_state(kKeys[w]);
    if (on < 0 || (on != 0) == (g_remembered[w] != 0)) continue;
    InterlockedExchange(&g_remembered[w], on);
    InterlockedExchange64(&g_changed_at, static_cast<LONG64>(now));
    InterlockedExchange(&g_dirty, 1);
    logf("remember: %s now %s", kLabels[w], state_word(w, on != 0));
  }
}

}  // namespace

void init(const config::Settings& s) {
  InterlockedExchange(&g_remembered[kAutomap], s.automap_was_open ? 1 : 0);
  InterlockedExchange(&g_remembered[kShowItems], s.show_items_was_on ? 1 : 0);
  InterlockedExchange(&g_remembered[kShowItemsUnfiltered], s.show_items_unfiltered_was_on ? 1 : 0);
}

void to_settings(config::Settings* s) {
  s->automap_was_open = g_remembered[kAutomap] != 0;
  s->show_items_was_on = g_remembered[kShowItems] != 0;
  s->show_items_unfiltered_was_on = g_remembered[kShowItemsUnfiltered] != 0;
}

void bind(bool ui_thread) {
  int usable = 0;
  for (int w = 0; w < kCount; ++w) {
    const char* why = !ui_thread                          ? "no UI thread from the loader"
                      : !game::has_key_action(kKeys[w])  ? "the game's key for it was not found"
                                                          : nullptr;
    cheats::set_why_not(kKinds[w], why);
    if (!why) ++usable;
  }
  InterlockedExchange(&g_bound, usable ? 1 : 0);
  logf("remember: %d of %d keys can be kept between games; remembered: the automap %s, Show Items %s, Show Items "
       "(Unfiltered) %s",
       usable, static_cast<int>(kCount), state_word(kAutomap, g_remembered[kAutomap] != 0),
       state_word(kShowItems, g_remembered[kShowItems] != 0),
       state_word(kShowItemsUnfiltered, g_remembered[kShowItemsUnfiltered] != 0));
}

void on_ui(ULONGLONG now) {
  if (!g_bound) return;
  perf::Timer timer(perf::kRemember);
  if (g_reset && InterlockedExchange(&g_reset, 0)) {
    g_phase = kIdle;
    g_held = false;
  }
  if (g_armed && InterlockedExchange(&g_armed, 0)) {
    begin_restore(now, kAtLoad);
    g_held = false;
    g_modes_at = 0;
  }
  if (g_phase == kIdle || g_phase == kLeft) return;
  if (g_leaving) {
    g_phase = kLeft;
    return;
  }
  if (g_phase == kRestore) {
    // While the game loads, every frame: the keys are pressed until the three are as they were.
    perf::Timer looking(perf::kRememberLook);
    refresh_modes(now);
    restore(now);
    return;
  }
  if (g_look && InterlockedExchange(&g_look, 0)) g_looks.happened(now);
  if (!g_looks.due(now)) return;
  perf::Timer looking(perf::kRememberLook);
  refresh_modes(now);
  track(now);
}

void look() { InterlockedExchange(&g_look, 1); }

void game_joined(uint64_t session) {
  if (InterlockedExchange64(&g_session, static_cast<LONG64>(session)) == static_cast<LONG64>(session)) return;
  InterlockedExchange(&g_loading_gone, 0);
  InterlockedExchange(&g_leaving, 0);
  InterlockedExchange(&g_armed, 1);
}

void loading_screen_shown() { InterlockedExchange(&g_loading_up, 1); }

void loading_screen_gone() {
  InterlockedExchange(&g_loading_up, 0);
  InterlockedExchange(&g_loading_gone, 1);
}

void leaving() { InterlockedExchange(&g_leaving, 1); }

void game_left() {
  InterlockedExchange(&g_reset, 1);
  InterlockedExchange(&g_armed, 0);
  InterlockedExchange(&g_leaving, 0);
  InterlockedExchange(&g_loading_up, 0);
  InterlockedExchange64(&g_session, -1);
  InterlockedExchange(&g_flush, 1);
}

bool save_waits() { return g_dirty != 0; }

bool save_due(ULONGLONG now) {
  if (!g_dirty) {
    InterlockedExchange(&g_flush, 0);
    return false;
  }
  if (!g_flush && now - static_cast<ULONGLONG>(g_changed_at) < kSaveDelayMs) return false;
  InterlockedExchange(&g_flush, 0);
  InterlockedExchange(&g_dirty, 0);
  return true;
}

bool remembered(Which w) { return w >= 0 && w < kCount && g_remembered[w] != 0; }
int display_mode(Which w) { return w > kAutomap && w < kCount ? static_cast<int>(g_mode[w]) : game::kDisplayUnknown; }

}  // namespace d2rcc::remember

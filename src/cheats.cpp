#include "cheats.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace d2rcc::cheats {
namespace {

volatile LONG g_on[kCount] = {};
volatile LONG g_exp_x100 = 100;
volatile LONG g_speed = 0;
volatile LONG g_exit_pct = 0;
volatile LONG g_exit_method = 0;
volatile LONG g_log_level = 0;
volatile LONG g_boss_bar_pos = 0;
volatile LONG g_extra_drops = 0;
volatile LONG g_damage_x100 = 100;
volatile LONG g_home_town = 0;
const char* volatile g_why[kCount] = {};

CRITICAL_SECTION g_status_cs;
bool g_cs_ready = false;
Status g_status;
char g_last_action[160] = {};

const char* const kNames[kCount] = {"God mode",        "Infinite mana",   "Infinite stamina",     "Exit before death",
                                    "Experience multiplier", "Movement speed", "All areas terrorized", "Area level (automap)",
                                    "Area level (waypoints)", "Monster health bars", "Boss health bar",
                                    "Loot filter drops only", "Chronicle drops only", "Extra drops",
                                    "Extra drops from elites only",
                                    "Perfect rolls",
                                    "All superior",         "All ethereal",    "All socketed", "Max affixes",
                                    "Best affixes",         "Damage multiplier",
                                    "Invincible mercenary", "Invincible minions", "Passive mercenary",
                                    "Passive minions",      "Permanent revives",    "Cannot be frozen",
                                    "Cannot be poisoned",   "Cannot be cursed", "Infinite town portal",
                                    "Infinite identify",    "Identify on pickup",
                                    "Infinite potions",     "Infinite keys",   "No durability loss",
                                    "Infinite gold",
                                    "Infinite imbues",      "Infinite sockets", "Infinite personalizing",
                                    "Infinite cube ingredients",
                                    "Ignore item requirements", "Ignore item requirements (mercenary)",
                                    "Reveal the map",       "Named enemies on the map", "Landmarks on the map",
                                    "Area names at exits",
                                    "Item level",      "Keep the automap", "Keep Show Items",
                                    "Keep Show Items (Unfiltered)", "Home town", "The log", "Respec",
                                    "Skill and attribute presets"};
}  // namespace

void init(const config::Settings& s) {
  if (!g_cs_ready) {
    InitializeCriticalSection(&g_status_cs);
    g_cs_ready = true;
  }
  InterlockedExchange(&g_on[kGodMode], s.god_mode ? 1 : 0);
  InterlockedExchange(&g_on[kInfiniteMana], s.infinite_mana ? 1 : 0);
  InterlockedExchange(&g_on[kInfiniteStamina], s.infinite_stamina ? 1 : 0);
  InterlockedExchange(&g_on[kExitBeforeDeath], s.exit_before_death ? 1 : 0);
  set_exp_multiplier(s.exp_multiplier);
  set_move_speed_bonus(s.move_speed_bonus);
  set_exit_below_percent(s.exit_below_life_percent);
  set_exit_method(s.exit_method);
  InterlockedExchange(&g_on[kTerrorAll], s.terror_all_areas ? 1 : 0);
  InterlockedExchange(&g_on[kAreaLevelAutomap], s.area_level_automap ? 1 : 0);
  InterlockedExchange(&g_on[kAreaLevelWaypoints], s.area_level_waypoints ? 1 : 0);
  InterlockedExchange(&g_on[kMonsterBars], s.monster_health_bars ? 1 : 0);
  InterlockedExchange(&g_on[kBossBar], s.boss_health_bar ? 1 : 0);
  set_boss_bar_position(s.boss_bar_position);
  InterlockedExchange(&g_on[kLootFilterOnly], s.loot_filter_only ? 1 : 0);
  InterlockedExchange(&g_on[kChronicleOnly], s.chronicle_only ? 1 : 0);
  set_extra_drops(s.extra_drops);
  InterlockedExchange(&g_on[kExtraDropsElite], s.extra_drops_elites_only ? 1 : 0);
  InterlockedExchange(&g_on[kPerfectRolls], s.perfect_rolls ? 1 : 0);
  InterlockedExchange(&g_on[kAllSuperior], s.all_superior ? 1 : 0);
  InterlockedExchange(&g_on[kAllEthereal], s.all_ethereal ? 1 : 0);
  InterlockedExchange(&g_on[kAllSocketed], s.all_socketed ? 1 : 0);
  InterlockedExchange(&g_on[kMaxAffixes], s.max_affixes ? 1 : 0);
  InterlockedExchange(&g_on[kBestAffixes], s.best_affixes ? 1 : 0);
  set_damage_multiplier(s.damage_multiplier);
  InterlockedExchange(&g_on[kInvincibleMerc], s.invincible_mercenary ? 1 : 0);
  InterlockedExchange(&g_on[kInvinciblePets], s.invincible_minions ? 1 : 0);
  InterlockedExchange(&g_on[kPassiveMerc], s.passive_mercenary ? 1 : 0);
  InterlockedExchange(&g_on[kPassivePets], s.passive_minions ? 1 : 0);
  InterlockedExchange(&g_on[kPermanentRevives], s.permanent_revives ? 1 : 0);
  InterlockedExchange(&g_on[kCannotBeFrozen], s.cannot_be_frozen ? 1 : 0);
  InterlockedExchange(&g_on[kCannotBePoisoned], s.cannot_be_poisoned ? 1 : 0);
  InterlockedExchange(&g_on[kCannotBeCursed], s.cannot_be_cursed ? 1 : 0);
  InterlockedExchange(&g_on[kInfiniteTownPortal], s.infinite_town_portal ? 1 : 0);
  InterlockedExchange(&g_on[kInfiniteIdentify], s.infinite_identify ? 1 : 0);
  InterlockedExchange(&g_on[kAutoIdentify], s.auto_identify ? 1 : 0);
  InterlockedExchange(&g_on[kInfinitePotions], s.infinite_potions ? 1 : 0);
  InterlockedExchange(&g_on[kInfiniteKeys], s.infinite_keys ? 1 : 0);
  InterlockedExchange(&g_on[kNoDurabilityLoss], s.no_durability_loss ? 1 : 0);
  InterlockedExchange(&g_on[kInfiniteGold], s.infinite_gold ? 1 : 0);
  InterlockedExchange(&g_on[kInfiniteImbue], s.infinite_imbue ? 1 : 0);
  InterlockedExchange(&g_on[kInfiniteSockets], s.infinite_sockets ? 1 : 0);
  InterlockedExchange(&g_on[kInfinitePersonalize], s.infinite_personalize ? 1 : 0);
  InterlockedExchange(&g_on[kInfiniteCubeIngredients], s.infinite_cube_ingredients ? 1 : 0);
  InterlockedExchange(&g_on[kIgnoreRequirements], s.ignore_requirements ? 1 : 0);
  InterlockedExchange(&g_on[kIgnoreRequirementsMerc], s.ignore_requirements_mercenary ? 1 : 0);
  set_home_town(s.home_town);
  InterlockedExchange(&g_on[kRevealMap], s.reveal_map ? 1 : 0);
  InterlockedExchange(&g_on[kMapEnemies], s.map_named_enemies ? 1 : 0);
  InterlockedExchange(&g_on[kMapLandmarks], s.map_landmarks ? 1 : 0);
  InterlockedExchange(&g_on[kMapExits], s.map_exit_names ? 1 : 0);
  InterlockedExchange(&g_on[kItemLevel], s.item_level ? 1 : 0);
  InterlockedExchange(&g_on[kRememberAutomap], s.remember_automap ? 1 : 0);
  InterlockedExchange(&g_on[kRememberShowItems], s.remember_show_items ? 1 : 0);
  InterlockedExchange(&g_on[kRememberShowItemsUnfiltered], s.remember_show_items_unfiltered ? 1 : 0);
  InterlockedExchange(&g_on[kLogging], s.logging ? 1 : 0);
  set_log_level(s.log_level);
}

const char* name(Kind k) { return k >= 0 && k < kCount ? kNames[k] : "?"; }

bool enabled(Kind k) {
  if (k == kExpMultiplier) return g_exp_x100 > 100;
  if (k == kMoveSpeed) return g_speed > 0;
  if (k == kExtraDrops) return g_extra_drops > 0;
  if (k == kDamageMultiplier) return g_damage_x100 > 100;
  if (k == kHomeTown) return g_home_town > 0;
  return k >= 0 && k < kCount && g_on[k] != 0;
}

void set_enabled(Kind k, bool on) {
  if (k >= 0 && k < kCount) InterlockedExchange(&g_on[k], on ? 1 : 0);
}

float exp_multiplier() { return static_cast<float>(g_exp_x100) / 100.0f; }
void set_exp_multiplier(float m) {
  if (m < 1.0f) m = 1.0f;
  if (m > 1000.0f) m = 1000.0f;
  InterlockedExchange(&g_exp_x100, static_cast<LONG>(m * 100.0f + 0.5f));
}
int move_speed_bonus() { return g_speed; }
void set_move_speed_bonus(int pct) {
  if (pct < 0) pct = 0;
  if (pct > 300) pct = 300;
  InterlockedExchange(&g_speed, pct);
}
int exit_below_percent() { return g_exit_pct; }
void set_exit_below_percent(int pct) {
  if (pct < 0) pct = 0;
  if (pct > 99) pct = 99;
  InterlockedExchange(&g_exit_pct, pct);
}
config::ExitMethod exit_method() { return static_cast<config::ExitMethod>(g_exit_method); }
void set_exit_method(config::ExitMethod m) { InterlockedExchange(&g_exit_method, static_cast<LONG>(m)); }

LogLevel log_level() { return static_cast<LogLevel>(g_log_level); }
void set_log_level(LogLevel least) {
  LONG n = static_cast<LONG>(least);
  if (n < static_cast<LONG>(LogLevel::kInfo)) n = static_cast<LONG>(LogLevel::kInfo);
  if (n > static_cast<LONG>(LogLevel::kError)) n = static_cast<LONG>(LogLevel::kError);
  InterlockedExchange(&g_log_level, n);
}

int boss_bar_position() { return g_boss_bar_pos; }
void set_boss_bar_position(int pct) {
  if (pct < 0) pct = 0;
  if (pct > 50) pct = 50;
  InterlockedExchange(&g_boss_bar_pos, pct);
}

int extra_drops() { return g_extra_drops; }
void set_extra_drops(int n) {
  if (n < 0) n = 0;
  if (n > 20) n = 20;
  InterlockedExchange(&g_extra_drops, n);
}

float damage_multiplier() { return static_cast<float>(g_damage_x100) / 100.0f; }
void set_damage_multiplier(float m) {
  if (!(m >= 1.0f)) m = 1.0f;
  if (m > 100.0f) m = 100.0f;
  InterlockedExchange(&g_damage_x100, static_cast<LONG>(m * 100.0f + 0.5f));
}

int home_town() { return g_home_town; }
void set_home_town(int act) {
  if (act < 0 || act > 5) act = 0;
  InterlockedExchange(&g_home_town, act);
}

const char* why_not(Kind k) { return k >= 0 && k < kCount ? g_why[k] : "?"; }
void set_why_not(Kind k, const char* reason) {
  // The log is the plugin's own: nothing the game lacks makes its switch unavailable.
  if (k >= 0 && k < kCount && k != kLogging) g_why[k] = reason;
}

Status status() {
  if (!g_cs_ready) return Status{};
  EnterCriticalSection(&g_status_cs);
  Status s = g_status;
  std::memcpy(s.last_action, g_last_action, sizeof(s.last_action));
  LeaveCriticalSection(&g_status_cs);
  return s;
}

void publish(const Status& st) {
  if (!g_cs_ready) return;
  EnterCriticalSection(&g_status_cs);
  g_status = st;
  LeaveCriticalSection(&g_status_cs);
}

void note(const char* fmt, ...) {
  char buf[160];
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  if (!g_cs_ready) return;
  EnterCriticalSection(&g_status_cs);
  std::memcpy(g_last_action, buf, sizeof(g_last_action));
  LeaveCriticalSection(&g_status_cs);
}

void to_settings(config::Settings* s) {
  s->god_mode = enabled(kGodMode);
  s->infinite_mana = enabled(kInfiniteMana);
  s->infinite_stamina = enabled(kInfiniteStamina);
  s->exit_before_death = enabled(kExitBeforeDeath);
  s->exp_multiplier = exp_multiplier();
  s->move_speed_bonus = move_speed_bonus();
  s->exit_below_life_percent = exit_below_percent();
  s->exit_method = exit_method();
  s->terror_all_areas = g_on[kTerrorAll] != 0;
  s->area_level_automap = g_on[kAreaLevelAutomap] != 0;
  s->area_level_waypoints = g_on[kAreaLevelWaypoints] != 0;
  s->monster_health_bars = g_on[kMonsterBars] != 0;
  s->boss_health_bar = g_on[kBossBar] != 0;
  s->boss_bar_position = boss_bar_position();
  s->loot_filter_only = g_on[kLootFilterOnly] != 0;
  s->chronicle_only = g_on[kChronicleOnly] != 0;
  s->extra_drops = extra_drops();
  s->extra_drops_elites_only = g_on[kExtraDropsElite] != 0;
  s->perfect_rolls = g_on[kPerfectRolls] != 0;
  s->all_superior = g_on[kAllSuperior] != 0;
  s->all_ethereal = g_on[kAllEthereal] != 0;
  s->all_socketed = g_on[kAllSocketed] != 0;
  s->max_affixes = g_on[kMaxAffixes] != 0;
  s->best_affixes = g_on[kBestAffixes] != 0;
  s->damage_multiplier = damage_multiplier();
  s->invincible_mercenary = g_on[kInvincibleMerc] != 0;
  s->invincible_minions = g_on[kInvinciblePets] != 0;
  s->passive_mercenary = g_on[kPassiveMerc] != 0;
  s->passive_minions = g_on[kPassivePets] != 0;
  s->permanent_revives = g_on[kPermanentRevives] != 0;
  s->cannot_be_frozen = g_on[kCannotBeFrozen] != 0;
  s->cannot_be_poisoned = g_on[kCannotBePoisoned] != 0;
  s->cannot_be_cursed = g_on[kCannotBeCursed] != 0;
  s->infinite_town_portal = g_on[kInfiniteTownPortal] != 0;
  s->infinite_identify = g_on[kInfiniteIdentify] != 0;
  s->auto_identify = g_on[kAutoIdentify] != 0;
  s->infinite_potions = g_on[kInfinitePotions] != 0;
  s->infinite_keys = g_on[kInfiniteKeys] != 0;
  s->no_durability_loss = g_on[kNoDurabilityLoss] != 0;
  s->infinite_gold = g_on[kInfiniteGold] != 0;
  s->infinite_imbue = g_on[kInfiniteImbue] != 0;
  s->infinite_sockets = g_on[kInfiniteSockets] != 0;
  s->infinite_personalize = g_on[kInfinitePersonalize] != 0;
  s->infinite_cube_ingredients = g_on[kInfiniteCubeIngredients] != 0;
  s->ignore_requirements = g_on[kIgnoreRequirements] != 0;
  s->ignore_requirements_mercenary = g_on[kIgnoreRequirementsMerc] != 0;
  s->home_town = home_town();
  s->reveal_map = g_on[kRevealMap] != 0;
  s->map_named_enemies = g_on[kMapEnemies] != 0;
  s->map_landmarks = g_on[kMapLandmarks] != 0;
  s->map_exit_names = g_on[kMapExits] != 0;
  s->item_level = g_on[kItemLevel] != 0;
  s->remember_automap = g_on[kRememberAutomap] != 0;
  s->remember_show_items = g_on[kRememberShowItems] != 0;
  s->remember_show_items_unfiltered = g_on[kRememberShowItemsUnfiltered] != 0;
  s->logging = g_on[kLogging] != 0;
  s->log_level = log_level();
}

}  // namespace d2rcc::cheats

// The settings file: parse the embedded default, change values, write them back
// in place (comments kept), read them again.
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

#include "config.h"

using namespace d2rcc::config;

static int g_failures = 0;
#define CHECK(cond)                                                     \
  do {                                                                  \
    if (!(cond)) {                                                      \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
      ++g_failures;                                                     \
    }                                                                   \
  } while (0)

static const char* kDefault =
    "# CabbyCodes - settings.\r\n"
    "\r\n"
    "[cabbycodes]\r\n"
    "# Master switch.\r\n"
    "enabled = true\r\n"
    "god_mode = false\r\n"
    "infinite_mana = false\r\n"
    "infinite_stamina = false\r\n"
    "exp_multiplier = 1.0   # 1.0 to 50.0\r\n"
    "move_speed_bonus = 0\r\n"
    "exit_before_death = false\r\n"
    "exit_below_life_percent = 0\r\n"
    "exit_method = \"save_and_exit\"  # or \"force_close\"\r\n"
    "show_on_pause = true\r\n"
    "terror_all_areas = false\r\n"
    "area_level_automap = false\r\n"
    "area_level_waypoints = false\r\n"
    "monster_health_bars = false\r\n"
    "boss_health_bar = false\r\n"
    "boss_bar_position = 0  # percent\r\n"
    "loot_filter_only = false\r\n"
    "extra_drops = 0  # 0 to 20\r\n"
    "perfect_rolls = false\r\n"
    "all_superior = false\r\n"
    "all_ethereal = false\r\n"
    "toggle_key = \"F7\"\r\n"
    "trace = false\r\n"
    "\r\n"
    "[d2rl]\r\n"
    "match = []\r\n";

int main() {
  Settings s;
  CHECK(parse(kDefault, &s) == 23);
  CHECK(s.enabled && !s.god_mode && s.exp_multiplier == 1.0f && s.toggle_key == 0x76);
  CHECK(s.exit_method == ExitMethod::kSaveAndExit);
  CHECK(!s.monster_health_bars && !s.boss_health_bar && s.boss_bar_position == 0);
  CHECK(!s.loot_filter_only && s.extra_drops == 0 && !s.perfect_rolls && !s.all_superior && !s.all_ethereal);
  CHECK(!s.chronicle_only);
  CHECK(!s.extra_drops_elites_only && !s.all_socketed && !s.max_affixes && !s.best_affixes);
  CHECK(!s.cannot_be_cursed && !s.infinite_gold && s.home_town == 0);
  CHECK(!s.infinite_imbue && !s.infinite_sockets && !s.infinite_personalize);
  CHECK(!s.infinite_cube_ingredients && !s.auto_identify);
  CHECK(!s.passive_mercenary && !s.passive_minions && !s.permanent_revives);
  CHECK(!s.logging && !s.trace);  // the log is written only when the file says so,
  CHECK(s.log_level == d2rcc::LogLevel::kInfo);  // ... and then all of it

  // What a new install gets: the loader writes res/cabbycodes.toml the first time the plugin loads. Every key is
  // in it at the default the plugin has for it (written back unchanged), so the log is off there too.
  if (std::ifstream f{"res/cabbycodes.toml", std::ios::binary}) {
    const std::string shipped((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    Settings installed;
    CHECK(parse(shipped, &installed) == 59);
    CHECK(rewrite(shipped, Settings{}) == shipped);
    CHECK(!installed.logging && installed.log_level == d2rcc::LogLevel::kInfo && !installed.trace);
  } else {
    std::printf("res/cabbycodes.toml is not at hand (run from the project's root) - the settings a new install gets "
                "were not checked\n");
  }

  s.god_mode = true;
  s.exp_multiplier = 12.5f;
  s.move_speed_bonus = 150;
  s.exit_method = ExitMethod::kForceClose;
  s.toggle_key = 0x2D;  // INSERT
  s.terror_all_areas = true;
  s.monster_health_bars = true;
  s.boss_bar_position = 20;
  s.loot_filter_only = true;
  s.chronicle_only = true;
  s.extra_drops = 3;
  s.extra_drops_elites_only = true;
  s.perfect_rolls = true;
  s.all_ethereal = true;
  s.all_socketed = true;
  s.max_affixes = true;
  s.best_affixes = true;
  s.damage_multiplier = 7.5f;
  s.invincible_mercenary = true;
  s.passive_minions = true;
  s.permanent_revives = true;
  s.cannot_be_poisoned = true;
  s.cannot_be_cursed = true;
  s.infinite_potions = true;
  s.auto_identify = true;
  s.infinite_keys = true;
  s.no_durability_loss = true;
  s.infinite_gold = true;
  s.infinite_sockets = true;
  s.infinite_personalize = true;
  s.infinite_cube_ingredients = true;
  s.home_town = 5;
  s.reveal_map = true;
  s.item_level = true;
  s.remember_automap = true;
  s.remember_show_items_unfiltered = true;
  s.automap_was_open = true;
  s.show_items_unfiltered_was_on = true;
  s.logging = false;
  s.log_level = d2rcc::LogLevel::kWarning;
  const std::string text = rewrite(kDefault, s);
  CHECK(text.find("god_mode = true\r\n") != std::string::npos);
  CHECK(text.find("exp_multiplier = 12.5   # 1.0 to 50.0\r\n") != std::string::npos);
  CHECK(text.find("exit_method = \"force_close\"  # or \"force_close\"\r\n") != std::string::npos);
  CHECK(text.find("toggle_key = \"INSERT\"\r\n") != std::string::npos);
  CHECK(text.find("terror_all_areas = true\r\n") != std::string::npos);
  CHECK(text.find("monster_health_bars = true\r\n") != std::string::npos);
  CHECK(text.find("boss_health_bar = false\r\n") != std::string::npos);
  CHECK(text.find("boss_bar_position = 20  # percent\r\n") != std::string::npos);
  CHECK(text.find("loot_filter_only = true\r\n") != std::string::npos);
  CHECK(text.find("extra_drops = 3  # 0 to 20\r\n") != std::string::npos);
  CHECK(text.find("perfect_rolls = true\r\n") != std::string::npos);
  CHECK(text.find("all_superior = false\r\n") != std::string::npos);
  CHECK(text.find("all_ethereal = true\r\n") != std::string::npos);
  CHECK(text.find("# Master switch.\r\n") != std::string::npos);
  CHECK(text.find("[d2rl]\r\nmatch = []\r\n") != std::string::npos);
  // Keys the file lacks are appended to the table, in their order, before the next table.
  CHECK(text.find("reveal_map = true\r\nitem_level = true\r\nchronicle_only = true\r\n"
                  "extra_drops_elites_only = true\r\n"
                  "all_socketed = true\r\nmax_affixes = true\r\n"
                  "best_affixes = true\r\ndamage_multiplier = 7.5\r\n"
                  "invincible_mercenary = true\r\ninvincible_minions = false\r\n"
                  "passive_mercenary = false\r\npassive_minions = true\r\npermanent_revives = true\r\n"
                  "cannot_be_frozen = false\r\n") != std::string::npos);
  CHECK(text.find("cannot_be_frozen = false\r\ncannot_be_poisoned = true\r\ncannot_be_cursed = true\r\n"
                  "infinite_town_portal = false\r\ninfinite_identify = false\r\nauto_identify = true\r\n"
                  "infinite_potions = true\r\n") != std::string::npos);
  CHECK(text.find("infinite_potions = true\r\ninfinite_keys = true\r\nno_durability_loss = true\r\n"
                  "infinite_gold = true\r\ninfinite_imbue = false\r\ninfinite_sockets = true\r\n"
                  "infinite_personalize = true\r\ninfinite_cube_ingredients = true\r\nhome_town = 5\r\n"
                  "remember_automap = true\r\nremember_show_items = false\r\n"
                  "remember_show_items_unfiltered = true\r\n") != std::string::npos);
  CHECK(text.find("automap_was_open = true\r\nshow_items_was_on = false\r\nshow_items_unfiltered_was_on = true\r\n"
                  "logging = false\r\nlog_level = \"warning\"\r\n\r\n[d2rl]") != std::string::npos);

  Settings back;
  CHECK(parse(text, &back) == 59);
  CHECK(back.cannot_be_cursed && back.infinite_gold && back.home_town == 5);
  CHECK(!back.infinite_imbue && back.infinite_sockets && back.infinite_personalize);
  CHECK(back.infinite_cube_ingredients);
  CHECK(!back.logging && back.log_level == d2rcc::LogLevel::kWarning);
  CHECK(back.god_mode && back.exp_multiplier == 12.5f && back.move_speed_bonus == 150);
  CHECK(back.exit_method == ExitMethod::kForceClose && back.toggle_key == 0x2D);
  CHECK(back.terror_all_areas);
  CHECK(back.monster_health_bars && !back.boss_health_bar && back.boss_bar_position == 20);
  CHECK(back.loot_filter_only && back.extra_drops == 3 && back.perfect_rolls && !back.all_superior && back.all_ethereal);
  CHECK(back.chronicle_only);
  CHECK(back.extra_drops_elites_only && back.all_socketed && back.max_affixes && back.best_affixes);
  CHECK(back.damage_multiplier == 7.5f && back.invincible_mercenary && !back.invincible_minions);
  CHECK(!back.passive_mercenary && back.passive_minions && back.permanent_revives);
  CHECK(!back.cannot_be_frozen && back.cannot_be_poisoned);
  CHECK(!back.infinite_town_portal && !back.infinite_identify && back.infinite_potions && back.auto_identify);
  CHECK(back.infinite_keys && back.no_durability_loss && back.reveal_map && back.item_level);
  CHECK(back.remember_automap && !back.remember_show_items && back.remember_show_items_unfiltered);
  CHECK(back.automap_was_open && !back.show_items_was_on && back.show_items_unfiltered_was_on);

  // A file missing keys gets them appended inside the table, before the next one.
  const std::string sparse = "[cabbycodes]\ngod_mode = true\n\n[d2rl]\nmatch = []\n";
  const std::string filled = rewrite(sparse, back);
  CHECK(filled.find("god_mode = true\nenabled = true\n") != std::string::npos);
  CHECK(filled.find("trace = false\n\n[d2rl]\n") != std::string::npos);
  CHECK(filled.find("toggle_key = \"INSERT\"\nlogging = false\nlog_level = \"warning\"\ntrace = false\n") !=
        std::string::npos);
  Settings s3;
  CHECK(parse(filled, &s3) == 59);
  CHECK(s3.cannot_be_cursed && s3.infinite_gold && s3.home_town == 5);
  CHECK(!s3.infinite_imbue && s3.infinite_sockets && s3.infinite_personalize);
  CHECK(s3.infinite_cube_ingredients && s3.auto_identify);
  CHECK(!s3.logging && s3.log_level == d2rcc::LogLevel::kWarning);
  Settings on_again;
  CHECK(parse("[cabbycodes]\nlogging = true\n", &on_again) == 1 && on_again.logging);
  CHECK(parse("[cabbycodes]\nlogging = off\n", &on_again) == 0 && !on_again.logging);  // no bool: the default
  // The least severe kind of line the log takes: by name, in any case; a name that is none leaves the default.
  Settings lv;
  CHECK(parse("[cabbycodes]\nlog_level = \"error\"\n", &lv) == 1 && lv.log_level == d2rcc::LogLevel::kError);
  CHECK(parse("[cabbycodes]\nlog_level = \"Warning\"\n", &lv) == 1 && lv.log_level == d2rcc::LogLevel::kWarning);
  CHECK(parse("[cabbycodes]\nlog_level = \"verbose\"\n", &lv) == 0 && lv.log_level == d2rcc::LogLevel::kInfo);
  CHECK(parse("[cabbycodes]\nlog_level = warning\n", &lv) == 1 && lv.log_level == d2rcc::LogLevel::kWarning);
  CHECK(std::string(log_level_name(d2rcc::LogLevel::kInfo)) == "info");
  CHECK(std::string(log_level_name(d2rcc::LogLevel::kWarning)) == "warning");
  CHECK(std::string(log_level_name(d2rcc::LogLevel::kError)) == "error");
  for (const d2rcc::LogLevel level : {d2rcc::LogLevel::kInfo, d2rcc::LogLevel::kWarning, d2rcc::LogLevel::kError}) {
    d2rcc::LogLevel back_again = d2rcc::LogLevel::kInfo;
    CHECK(log_level_from_name(log_level_name(level), &back_again) && back_again == level);
  }

  // Out-of-range values clamp; unknown keys and bad values are ignored.
  Settings s4;
  CHECK(parse("[cabbycodes]\nexp_multiplier = 999\nmove_speed_bonus = -5\ntoggle_key = \"nope\"\nbogus = 1\n"
              "boss_bar_position = 90\nextra_drops = 99\n",
              &s4) == 4);
  CHECK(s4.exp_multiplier == 50.0f && s4.move_speed_bonus == 0 && s4.toggle_key == 0x76 && s4.boss_bar_position == 50);
  CHECK(s4.extra_drops == 20);
  Settings s5;
  CHECK(parse("[cabbycodes]\nextra_drops = -3\n", &s5) == 1 && s5.extra_drops == 0);
  CHECK(parse("[cabbycodes]\nextra_drops_elites_only = true\n", &s5) == 1 && s5.extra_drops_elites_only);
  CHECK(parse("[cabbycodes]\nextra_drops_elites_only = 1\n", &s5) == 0 && !s5.extra_drops_elites_only);
  CHECK(parse("[cabbycodes]\nchronicle_only = true\n", &s5) == 1 && s5.chronicle_only);
  CHECK(parse("[cabbycodes]\nchronicle_only = on\n", &s5) == 0);
  // The home town: 0 the game's own, 1 to 5 an act's; anything else is the game's own.
  Settings s7;
  CHECK(parse("[cabbycodes]\nhome_town = 3\n", &s7) == 1 && s7.home_town == 3);
  CHECK(parse("[cabbycodes]\nhome_town = 6\n", &s7) == 1 && s7.home_town == 0);
  CHECK(parse("[cabbycodes]\nhome_town = -1\n", &s7) == 1 && s7.home_town == 0);
  CHECK(parse("[cabbycodes]\nhome_town = \"harrogath\"\n", &s7) == 0 && s7.home_town == 0);
  CHECK(parse("[cabbycodes]\ncannot_be_cursed = true\ninfinite_gold = true\n", &s7) == 2 && s7.cannot_be_cursed &&
        s7.infinite_gold);
  CHECK(parse("[cabbycodes]\ninfinite_gold = yes\n", &s7) == 0 && !s7.infinite_gold);
  CHECK(parse("[cabbycodes]\ninfinite_imbue = true\ninfinite_sockets = true\ninfinite_personalize = true\n", &s7) ==
            3 &&
        s7.infinite_imbue && s7.infinite_sockets && s7.infinite_personalize);
  CHECK(parse("[cabbycodes]\ninfinite_sockets = false\n", &s7) == 1 && !s7.infinite_sockets && !s7.infinite_imbue);
  CHECK(parse("[cabbycodes]\ninfinite_sockets = 1\n", &s7) == 0 && !s7.infinite_sockets);
  CHECK(parse("[cabbycodes]\ninfinite_cube_ingredients = true\n", &s7) == 1 && s7.infinite_cube_ingredients);
  CHECK(parse("[cabbycodes]\ninfinite_cube_ingredients = yes\n", &s7) == 0 && !s7.infinite_cube_ingredients);
  CHECK(parse("[cabbycodes]\nauto_identify = true\n", &s7) == 1 && s7.auto_identify);
  CHECK(parse("[cabbycodes]\nauto_identify = 1\n", &s7) == 0 && !s7.auto_identify);
  CHECK(parse("[cabbycodes]\npassive_mercenary = true\npassive_minions = true\n", &s7) == 2 &&
        s7.passive_mercenary && s7.passive_minions);
  CHECK(parse("[cabbycodes]\npassive_minions = yes\n", &s7) == 0 && !s7.passive_minions);  // no bool: the default
  CHECK(parse("[cabbycodes]\npermanent_revives = true\n", &s7) == 1 && s7.permanent_revives);
  CHECK(parse("[cabbycodes]\npermanent_revives = 1\n", &s7) == 0 && !s7.permanent_revives);  // no bool: the default
  Settings s6;
  CHECK(parse("[cabbycodes]\ndamage_multiplier = 999\n", &s6) == 1 && s6.damage_multiplier == 100.0f);
  CHECK(parse("[cabbycodes]\ndamage_multiplier = 0.25\n", &s6) == 1 && s6.damage_multiplier == 1.0f);
  CHECK(key_from_name("0x77") == 0x77 && key_from_name("f8") == 0x77 && key_from_name("") == 0);
  CHECK(std::strcmp(key_name(0x76), "F7") == 0);

  if (g_failures) {
    std::printf("%d failure(s)\n", g_failures);
    return 1;
  }
  std::printf("test_config: ok\n");
  return 0;
}

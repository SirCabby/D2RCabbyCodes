#include "config.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace d2rcc::config {
namespace {

struct KeyName {
  const char* name;
  int vk;
};
const KeyName kKeys[] = {
    {"F1", 0x70},  {"F2", 0x71},  {"F3", 0x72},  {"F4", 0x73},  {"F5", 0x74},  {"F6", 0x75},  {"F7", 0x76},
    {"F8", 0x77},  {"F9", 0x78},  {"F10", 0x79}, {"F11", 0x7A}, {"F12", 0x7B}, {"INSERT", 0x2D}, {"DELETE", 0x2E},
    {"HOME", 0x24}, {"END", 0x23}, {"PAGEUP", 0x21}, {"PAGEDOWN", 0x22}, {"PAUSE", 0x13}, {"SCROLLLOCK", 0x91},
    {"BACKQUOTE", 0xC0}, {"MINUS", 0xBD}, {"EQUALS", 0xBB},
};

std::string trim(const std::string& s) {
  size_t b = 0, e = s.size();
  while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) ++b;
  while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) --e;
  return s.substr(b, e - b);
}

bool ieq(const std::string& a, const char* b) { return _stricmp(a.c_str(), b) == 0; }

// One "key = value  # comment" line of the table, or not a key line at all.
struct KeyLine {
  std::string indent, key, value, comment;  // comment keeps its leading spaces and '#'
  bool ok = false;
};

KeyLine split_key_line(const std::string& line) {
  KeyLine k;
  size_t i = 0;
  while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
  k.indent = line.substr(0, i);
  size_t ks = i;
  while (i < line.size() && (std::isalnum(static_cast<unsigned char>(line[i])) || line[i] == '_' || line[i] == '-')) ++i;
  if (i == ks) return k;
  k.key = line.substr(ks, i - ks);
  while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
  if (i >= line.size() || line[i] != '=') return k;
  ++i;
  // The value ends at a '#' outside quotes.
  bool quoted = false;
  size_t vs = i, ve = line.size();
  for (size_t j = i; j < line.size(); ++j) {
    if (line[j] == '"') quoted = !quoted;
    else if (line[j] == '#' && !quoted) {
      ve = j;
      break;
    }
  }
  k.value = trim(line.substr(vs, ve - vs));
  k.comment = ve < line.size() ? line.substr(ve) : "";
  // Keep the spaces between the value and the comment with the comment.
  if (ve < line.size()) {
    size_t cs = ve;
    while (cs > vs && (line[cs - 1] == ' ' || line[cs - 1] == '\t')) --cs;
    k.comment = line.substr(cs);
  }
  k.ok = true;
  return k;
}

bool parse_bool(const std::string& v, bool* out) {
  if (v == "true") { *out = true; return true; }
  if (v == "false") { *out = false; return true; }
  return false;
}

std::string unquote(const std::string& v) {
  if (v.size() >= 2 && v.front() == '"' && v.back() == '"') return v.substr(1, v.size() - 2);
  return v;
}

std::string fmt_float(float f) {
  char b[32];
  std::snprintf(b, sizeof(b), "%.2f", static_cast<double>(f));
  // TOML wants a fractional part; %.2f always has one. Trim a trailing zero pair to one.
  std::string s = b;
  while (s.size() > 3 && s.back() == '0' && s[s.size() - 2] != '.') s.pop_back();
  return s;
}

std::string value_of(const Settings& s, const std::string& key) {
  if (key == "enabled") return s.enabled ? "true" : "false";
  if (key == "god_mode") return s.god_mode ? "true" : "false";
  if (key == "infinite_mana") return s.infinite_mana ? "true" : "false";
  if (key == "infinite_stamina") return s.infinite_stamina ? "true" : "false";
  if (key == "exp_multiplier") return fmt_float(s.exp_multiplier);
  if (key == "move_speed_bonus") return std::to_string(s.move_speed_bonus);
  if (key == "exit_before_death") return s.exit_before_death ? "true" : "false";
  if (key == "exit_below_life_percent") return std::to_string(s.exit_below_life_percent);
  if (key == "exit_method") return std::string("\"") + exit_method_name(s.exit_method) + "\"";
  if (key == "terror_all_areas") return s.terror_all_areas ? "true" : "false";
  if (key == "area_level_automap") return s.area_level_automap ? "true" : "false";
  if (key == "area_level_waypoints") return s.area_level_waypoints ? "true" : "false";
  if (key == "reveal_map") return s.reveal_map ? "true" : "false";
  if (key == "map_named_enemies") return s.map_named_enemies ? "true" : "false";
  if (key == "map_landmarks") return s.map_landmarks ? "true" : "false";
  if (key == "map_exit_names") return s.map_exit_names ? "true" : "false";
  if (key == "item_level") return s.item_level ? "true" : "false";
  if (key == "monster_health_bars") return s.monster_health_bars ? "true" : "false";
  if (key == "boss_health_bar") return s.boss_health_bar ? "true" : "false";
  if (key == "boss_bar_position") return std::to_string(s.boss_bar_position);
  if (key == "loot_filter_only") return s.loot_filter_only ? "true" : "false";
  if (key == "chronicle_only") return s.chronicle_only ? "true" : "false";
  if (key == "extra_drops") return std::to_string(s.extra_drops);
  if (key == "extra_drops_elites_only") return s.extra_drops_elites_only ? "true" : "false";
  if (key == "perfect_rolls") return s.perfect_rolls ? "true" : "false";
  if (key == "all_superior") return s.all_superior ? "true" : "false";
  if (key == "all_ethereal") return s.all_ethereal ? "true" : "false";
  if (key == "all_socketed") return s.all_socketed ? "true" : "false";
  if (key == "max_affixes") return s.max_affixes ? "true" : "false";
  if (key == "best_affixes") return s.best_affixes ? "true" : "false";
  if (key == "damage_multiplier") return fmt_float(s.damage_multiplier);
  if (key == "invincible_mercenary") return s.invincible_mercenary ? "true" : "false";
  if (key == "invincible_minions") return s.invincible_minions ? "true" : "false";
  if (key == "passive_mercenary") return s.passive_mercenary ? "true" : "false";
  if (key == "passive_minions") return s.passive_minions ? "true" : "false";
  if (key == "permanent_revives") return s.permanent_revives ? "true" : "false";
  if (key == "cannot_be_frozen") return s.cannot_be_frozen ? "true" : "false";
  if (key == "cannot_be_poisoned") return s.cannot_be_poisoned ? "true" : "false";
  if (key == "cannot_be_cursed") return s.cannot_be_cursed ? "true" : "false";
  if (key == "infinite_town_portal") return s.infinite_town_portal ? "true" : "false";
  if (key == "infinite_identify") return s.infinite_identify ? "true" : "false";
  if (key == "auto_identify") return s.auto_identify ? "true" : "false";
  if (key == "infinite_potions") return s.infinite_potions ? "true" : "false";
  if (key == "infinite_keys") return s.infinite_keys ? "true" : "false";
  if (key == "no_durability_loss") return s.no_durability_loss ? "true" : "false";
  if (key == "infinite_gold") return s.infinite_gold ? "true" : "false";
  if (key == "infinite_imbue") return s.infinite_imbue ? "true" : "false";
  if (key == "infinite_sockets") return s.infinite_sockets ? "true" : "false";
  if (key == "infinite_personalize") return s.infinite_personalize ? "true" : "false";
  if (key == "infinite_cube_ingredients") return s.infinite_cube_ingredients ? "true" : "false";
  if (key == "ignore_requirements") return s.ignore_requirements ? "true" : "false";
  if (key == "ignore_requirements_mercenary") return s.ignore_requirements_mercenary ? "true" : "false";
  if (key == "home_town") return std::to_string(s.home_town);
  if (key == "remember_automap") return s.remember_automap ? "true" : "false";
  if (key == "remember_show_items") return s.remember_show_items ? "true" : "false";
  if (key == "remember_show_items_unfiltered") return s.remember_show_items_unfiltered ? "true" : "false";
  if (key == "automap_was_open") return s.automap_was_open ? "true" : "false";
  if (key == "show_items_was_on") return s.show_items_was_on ? "true" : "false";
  if (key == "show_items_unfiltered_was_on") return s.show_items_unfiltered_was_on ? "true" : "false";
  if (key == "show_on_pause") return s.show_on_pause ? "true" : "false";
  if (key == "toggle_key") return std::string("\"") + key_name(s.toggle_key) + "\"";
  if (key == "logging") return s.logging ? "true" : "false";
  if (key == "log_level") return std::string("\"") + log_level_name(s.log_level) + "\"";
  if (key == "trace") return s.trace ? "true" : "false";
  return "";
}

const char* const kOrder[] = {"enabled", "god_mode", "infinite_mana", "infinite_stamina", "exp_multiplier",
                              "move_speed_bonus", "exit_before_death", "exit_below_life_percent", "exit_method",
                              "terror_all_areas", "area_level_automap", "area_level_waypoints",
                              "reveal_map", "map_named_enemies", "map_landmarks", "map_exit_names", "item_level",
                              "monster_health_bars", "boss_health_bar",
                              "boss_bar_position", "loot_filter_only", "chronicle_only", "extra_drops",
                              "extra_drops_elites_only",
                              "perfect_rolls", "all_superior", "all_ethereal", "all_socketed", "max_affixes",
                              "best_affixes",
                              "damage_multiplier",
                              "invincible_mercenary", "invincible_minions", "passive_mercenary", "passive_minions",
                              "permanent_revives", "cannot_be_frozen", "cannot_be_poisoned",
                              "cannot_be_cursed",
                              "infinite_town_portal", "infinite_identify", "auto_identify", "infinite_potions",
                              "infinite_keys", "no_durability_loss", "infinite_gold", "infinite_imbue",
                              "infinite_sockets", "infinite_personalize", "infinite_cube_ingredients",
                              "ignore_requirements", "ignore_requirements_mercenary", "home_town",
                              "remember_automap", "remember_show_items", "remember_show_items_unfiltered",
                              "automap_was_open", "show_items_was_on", "show_items_unfiltered_was_on", "show_on_pause",
                              "toggle_key", "logging", "log_level", "trace"};

bool set_from(Settings* s, const std::string& key, const std::string& raw) {
  const std::string v = trim(raw);
  if (key == "enabled") return parse_bool(v, &s->enabled);
  if (key == "god_mode") return parse_bool(v, &s->god_mode);
  if (key == "infinite_mana") return parse_bool(v, &s->infinite_mana);
  if (key == "infinite_stamina") return parse_bool(v, &s->infinite_stamina);
  if (key == "exp_multiplier") {
    char* end = nullptr;
    const double d = std::strtod(v.c_str(), &end);
    if (end == v.c_str()) return false;
    s->exp_multiplier = static_cast<float>(d);
    return true;
  }
  if (key == "move_speed_bonus") {
    char* end = nullptr;
    const long n = std::strtol(v.c_str(), &end, 10);
    if (end == v.c_str()) return false;
    s->move_speed_bonus = static_cast<int>(n);
    return true;
  }
  if (key == "exit_before_death") return parse_bool(v, &s->exit_before_death);
  if (key == "exit_below_life_percent") {
    char* end = nullptr;
    const long n = std::strtol(v.c_str(), &end, 10);
    if (end == v.c_str()) return false;
    s->exit_below_life_percent = static_cast<int>(n);
    return true;
  }
  if (key == "exit_method") {
    const std::string m = unquote(v);
    if (ieq(m, "save_and_exit")) s->exit_method = ExitMethod::kSaveAndExit;
    else if (ieq(m, "force_close")) s->exit_method = ExitMethod::kForceClose;
    else return false;
    return true;
  }
  if (key == "terror_all_areas") return parse_bool(v, &s->terror_all_areas);
  if (key == "area_level_automap") return parse_bool(v, &s->area_level_automap);
  if (key == "area_level_waypoints") return parse_bool(v, &s->area_level_waypoints);
  if (key == "reveal_map") return parse_bool(v, &s->reveal_map);
  if (key == "map_named_enemies") return parse_bool(v, &s->map_named_enemies);
  if (key == "map_landmarks") return parse_bool(v, &s->map_landmarks);
  if (key == "map_exit_names") return parse_bool(v, &s->map_exit_names);
  if (key == "item_level") return parse_bool(v, &s->item_level);
  if (key == "monster_health_bars") return parse_bool(v, &s->monster_health_bars);
  if (key == "boss_health_bar") return parse_bool(v, &s->boss_health_bar);
  if (key == "boss_bar_position") {
    char* end = nullptr;
    const long n = std::strtol(v.c_str(), &end, 10);
    if (end == v.c_str()) return false;
    s->boss_bar_position = static_cast<int>(n);
    return true;
  }
  if (key == "loot_filter_only") return parse_bool(v, &s->loot_filter_only);
  if (key == "chronicle_only") return parse_bool(v, &s->chronicle_only);
  if (key == "extra_drops") {
    char* end = nullptr;
    const long n = std::strtol(v.c_str(), &end, 10);
    if (end == v.c_str()) return false;
    s->extra_drops = static_cast<int>(n);
    return true;
  }
  if (key == "extra_drops_elites_only") return parse_bool(v, &s->extra_drops_elites_only);
  if (key == "perfect_rolls") return parse_bool(v, &s->perfect_rolls);
  if (key == "all_superior") return parse_bool(v, &s->all_superior);
  if (key == "all_ethereal") return parse_bool(v, &s->all_ethereal);
  if (key == "all_socketed") return parse_bool(v, &s->all_socketed);
  if (key == "max_affixes") return parse_bool(v, &s->max_affixes);
  if (key == "best_affixes") return parse_bool(v, &s->best_affixes);
  if (key == "damage_multiplier") {
    char* end = nullptr;
    const double d = std::strtod(v.c_str(), &end);
    if (end == v.c_str()) return false;
    s->damage_multiplier = static_cast<float>(d);
    return true;
  }
  if (key == "invincible_mercenary") return parse_bool(v, &s->invincible_mercenary);
  if (key == "invincible_minions") return parse_bool(v, &s->invincible_minions);
  if (key == "passive_mercenary") return parse_bool(v, &s->passive_mercenary);
  if (key == "passive_minions") return parse_bool(v, &s->passive_minions);
  if (key == "permanent_revives") return parse_bool(v, &s->permanent_revives);
  if (key == "cannot_be_frozen") return parse_bool(v, &s->cannot_be_frozen);
  if (key == "cannot_be_poisoned") return parse_bool(v, &s->cannot_be_poisoned);
  if (key == "cannot_be_cursed") return parse_bool(v, &s->cannot_be_cursed);
  if (key == "infinite_town_portal") return parse_bool(v, &s->infinite_town_portal);
  if (key == "infinite_identify") return parse_bool(v, &s->infinite_identify);
  if (key == "auto_identify") return parse_bool(v, &s->auto_identify);
  if (key == "infinite_potions") return parse_bool(v, &s->infinite_potions);
  if (key == "infinite_keys") return parse_bool(v, &s->infinite_keys);
  if (key == "no_durability_loss") return parse_bool(v, &s->no_durability_loss);
  if (key == "infinite_gold") return parse_bool(v, &s->infinite_gold);
  if (key == "infinite_imbue") return parse_bool(v, &s->infinite_imbue);
  if (key == "infinite_sockets") return parse_bool(v, &s->infinite_sockets);
  if (key == "infinite_personalize") return parse_bool(v, &s->infinite_personalize);
  if (key == "infinite_cube_ingredients") return parse_bool(v, &s->infinite_cube_ingredients);
  if (key == "ignore_requirements") return parse_bool(v, &s->ignore_requirements);
  if (key == "ignore_requirements_mercenary") return parse_bool(v, &s->ignore_requirements_mercenary);
  if (key == "home_town") {
    char* end = nullptr;
    const long n = std::strtol(v.c_str(), &end, 10);
    if (end == v.c_str()) return false;
    s->home_town = static_cast<int>(n);
    return true;
  }
  if (key == "remember_automap") return parse_bool(v, &s->remember_automap);
  if (key == "remember_show_items") return parse_bool(v, &s->remember_show_items);
  if (key == "remember_show_items_unfiltered") return parse_bool(v, &s->remember_show_items_unfiltered);
  if (key == "automap_was_open") return parse_bool(v, &s->automap_was_open);
  if (key == "show_items_was_on") return parse_bool(v, &s->show_items_was_on);
  if (key == "show_items_unfiltered_was_on") return parse_bool(v, &s->show_items_unfiltered_was_on);
  if (key == "show_on_pause") return parse_bool(v, &s->show_on_pause);
  if (key == "toggle_key") {
    const int vk = key_from_name(unquote(v).c_str());
    if (!vk) return false;
    s->toggle_key = vk;
    return true;
  }
  if (key == "logging") return parse_bool(v, &s->logging);
  if (key == "log_level") return log_level_from_name(unquote(v).c_str(), &s->log_level);
  if (key == "trace") return parse_bool(v, &s->trace);
  return false;
}

std::vector<std::string> lines_of(const std::string& text) {
  std::vector<std::string> out;
  size_t start = 0;
  while (start <= text.size()) {
    const size_t nl = text.find('\n', start);
    if (nl == std::string::npos) {
      if (start < text.size()) out.push_back(text.substr(start));
      break;
    }
    out.push_back(text.substr(start, nl - start));
    start = nl + 1;
  }
  return out;
}

bool is_table_header(const std::string& line, std::string* name) {
  const std::string t = trim(line);
  if (t.size() < 2 || t.front() != '[') return false;
  const size_t close = t.find(']');
  if (close == std::string::npos) return false;
  *name = trim(t.substr(1, close - 1));
  return true;
}

}  // namespace

int parse(const std::string& toml, Settings* out) {
  Settings s;
  int understood = 0;
  bool in_table = false;
  for (const std::string& line : lines_of(toml)) {
    std::string table;
    if (is_table_header(line, &table)) {
      in_table = ieq(table, "cabbycodes");
      continue;
    }
    if (!in_table) continue;
    const KeyLine k = split_key_line(line);
    if (!k.ok) continue;
    if (set_from(&s, k.key, k.value)) ++understood;
  }
  clamp(&s);
  *out = s;
  return understood;
}

std::string rewrite(const std::string& toml, const Settings& s) {
  std::vector<std::string> lines = lines_of(toml);
  const bool crlf = toml.find("\r\n") != std::string::npos;
  const char* eol = crlf ? "\r\n" : "\n";
  bool seen[sizeof(kOrder) / sizeof(kOrder[0])] = {};
  bool in_table = false;
  bool found_table = false;
  size_t table_end = lines.size();  // where a missing key is appended: the end of the table
  for (size_t i = 0; i < lines.size(); ++i) {
    std::string table;
    if (is_table_header(lines[i], &table)) {
      if (in_table) {
        table_end = i;
        in_table = false;
      }
      if (ieq(table, "cabbycodes")) {
        in_table = true;
        found_table = true;
        table_end = lines.size();
      }
      continue;
    }
    if (!in_table) continue;
    KeyLine k = split_key_line(lines[i]);
    if (!k.ok) continue;
    for (size_t j = 0; j < sizeof(kOrder) / sizeof(kOrder[0]); ++j) {
      if (k.key != kOrder[j]) continue;
      seen[j] = true;
      std::string rebuilt = k.indent + k.key + " = " + value_of(s, k.key) + k.comment;
      const bool had_cr = !lines[i].empty() && lines[i].back() == '\r';
      if (had_cr && (rebuilt.empty() || rebuilt.back() != '\r')) rebuilt += '\r';
      lines[i] = rebuilt;
    }
  }
  // Missing keys go at the end of the table (or a new table at the end).
  std::vector<std::string> extra;
  for (size_t j = 0; j < sizeof(kOrder) / sizeof(kOrder[0]); ++j)
    if (!seen[j]) extra.push_back(std::string(kOrder[j]) + " = " + value_of(s, kOrder[j]) + (crlf ? "\r" : ""));
  if (!extra.empty()) {
    if (!found_table) {
      if (!lines.empty() && !trim(lines.back()).empty()) lines.push_back(crlf ? "\r" : "");
      lines.push_back(std::string("[cabbycodes]") + (crlf ? "\r" : ""));
      table_end = lines.size();
    }
    // Insert before the trailing blank lines of the table, so the next table keeps its spacing.
    size_t at = table_end;
    while (at > 0 && at <= lines.size() && trim(lines[at - 1]).empty()) --at;
    lines.insert(lines.begin() + static_cast<long>(at), extra.begin(), extra.end());
  }
  std::string out;
  for (size_t i = 0; i < lines.size(); ++i) {
    std::string l = lines[i];
    if (crlf && !l.empty() && l.back() == '\r') l.pop_back();
    out += l;
    out += eol;
  }
  return out;
}

void clamp(Settings* s) {
  if (s->exp_multiplier < 1.0f) s->exp_multiplier = 1.0f;
  if (s->exp_multiplier > 1000.0f) s->exp_multiplier = 1000.0f;
  if (s->move_speed_bonus < 0) s->move_speed_bonus = 0;
  if (s->move_speed_bonus > 300) s->move_speed_bonus = 300;
  if (s->exit_below_life_percent < 0) s->exit_below_life_percent = 0;
  if (s->exit_below_life_percent > 99) s->exit_below_life_percent = 99;
  if (s->boss_bar_position < 0) s->boss_bar_position = 0;
  if (s->boss_bar_position > 50) s->boss_bar_position = 50;
  if (s->extra_drops < 0) s->extra_drops = 0;
  if (s->extra_drops > 20) s->extra_drops = 20;
  if (s->home_town < 0 || s->home_town > 5) s->home_town = 0;
  if (!(s->damage_multiplier >= 1.0f)) s->damage_multiplier = 1.0f;
  if (s->damage_multiplier > 100.0f) s->damage_multiplier = 100.0f;
  if (s->toggle_key <= 0 || s->toggle_key >= 256) s->toggle_key = 0x76;
}

int key_from_name(const char* name) {
  if (!name || !*name) return 0;
  for (const KeyName& k : kKeys)
    if (_stricmp(name, k.name) == 0) return k.vk;
  char* end = nullptr;
  const long v = std::strtol(name, &end, 0);
  return end != name && *end == '\0' && v > 0 && v < 256 ? static_cast<int>(v) : 0;
}

const char* key_name(int vk) {
  for (const KeyName& k : kKeys)
    if (k.vk == vk) return k.name;
  static char hex[8];
  std::snprintf(hex, sizeof(hex), "0x%02X", vk & 0xFF);
  return hex;
}

const char* exit_method_name(ExitMethod m) { return m == ExitMethod::kForceClose ? "force_close" : "save_and_exit"; }

const char* log_level_name(LogLevel level) {
  return level == LogLevel::kError ? "error" : level == LogLevel::kWarning ? "warning" : "info";
}

bool log_level_from_name(const char* name, LogLevel* out) {
  if (!name) return false;
  if (_stricmp(name, "info") == 0 || _stricmp(name, "information") == 0) *out = LogLevel::kInfo;
  else if (_stricmp(name, "warning") == 0 || _stricmp(name, "warn") == 0 || _stricmp(name, "warnings") == 0)
    *out = LogLevel::kWarning;
  else if (_stricmp(name, "error") == 0 || _stricmp(name, "errors") == 0) *out = LogLevel::kError;
  else return false;
  return true;
}

}  // namespace d2rcc::config

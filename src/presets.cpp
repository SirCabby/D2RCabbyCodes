#include "presets.h"

#include <cstdio>
#include <cstdlib>

namespace d2rcc::presets {
namespace {

const char* const kClassNames[] = {"Amazon", "Sorceress", "Necromancer", "Paladin",
                                   "Barbarian", "Druid", "Assassin", "Warlock"};

std::string trim(const std::string& s) {
  size_t b = 0, e = s.size();
  while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) ++b;
  while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) --e;
  return s.substr(b, e - b);
}

// A value up to a '#' outside quotes; a quoted value loses its quotes and escapes.
std::string value_of(const std::string& raw) {
  std::string v = trim(raw);
  if (v.empty() || v[0] != '"') {
    const size_t hash = v.find('#');
    return trim(hash == std::string::npos ? v : v.substr(0, hash));
  }
  std::string out;
  for (size_t i = 1; i < v.size(); ++i) {
    if (v[i] == '\\' && i + 1 < v.size()) {
      out += v[++i];
    } else if (v[i] == '"') {
      break;
    } else {
      out += v[i];
    }
  }
  return out;
}

bool to_int(const std::string& v, int* out) {
  char* end = nullptr;
  const long n = std::strtol(v.c_str(), &end, 10);
  if (end == v.c_str()) return false;
  *out = static_cast<int>(n);
  return true;
}

std::vector<Skill> parse_skills(const std::string& v) {
  std::vector<Skill> out;
  const char* p = v.c_str();
  while (*p) {
    char* end = nullptr;
    const long id = std::strtol(p, &end, 10);
    if (end == p) {
      ++p;
      continue;
    }
    p = end;
    if (*p != ':') continue;
    const long points = std::strtol(p + 1, &end, 10);
    if (end == p + 1) {
      ++p;
      continue;
    }
    p = end;
    if (id >= 0 && id <= 0xFFFF && points > 0 && points < 256) out.push_back(Skill{static_cast<int>(id), static_cast<int>(points)});
  }
  return out;
}

bool valid(const Preset& p) { return !p.name.empty() && p.char_class >= 0 && p.char_class < 16; }

std::string quoted(const std::string& s) {
  std::string out = "\"";
  for (char c : s) {
    if (c == '"' || c == '\\') out += '\\';
    out += c;
  }
  return out + "\"";
}

}  // namespace

std::vector<Preset> parse(const std::string& text) {
  std::vector<Preset> out;
  Preset cur;
  bool open = false;
  size_t start = 0;
  while (start <= text.size()) {
    size_t nl = text.find('\n', start);
    if (nl == std::string::npos) nl = text.size();
    const std::string line = trim(text.substr(start, nl - start));
    start = nl + 1;
    if (line.empty() || line[0] == '#') continue;
    if (line.rfind("[[preset]]", 0) == 0) {
      if (open && valid(cur)) out.push_back(cur);
      cur = Preset{};
      open = true;
      continue;
    }
    if (line[0] == '[') {  // some other table: its keys are not ours
      if (open && valid(cur)) out.push_back(cur);
      open = false;
      continue;
    }
    const size_t eq = line.find('=');
    if (!open || eq == std::string::npos) continue;
    const std::string key = trim(line.substr(0, eq));
    const std::string value = value_of(line.substr(eq + 1));
    if (key == "name") cur.name = clean_name(value);
    else if (key == "class") to_int(value, &cur.char_class);
    else if (key == "level") to_int(value, &cur.level);
    else if (key == "strength") to_int(value, &cur.strength);
    else if (key == "dexterity") to_int(value, &cur.dexterity);
    else if (key == "vitality") to_int(value, &cur.vitality);
    else if (key == "energy") to_int(value, &cur.energy);
    else if (key == "skills") cur.skills = parse_skills(value);
  }
  if (open && valid(cur)) out.push_back(cur);
  return out;
}

std::string format(const std::vector<Preset>& list) {
  std::string out =
      "# CabbyCodes for Diablo II: Resurrected - skill and attribute presets.\n"
      "#\n"
      "# The panel's Character section writes this file: Save keeps what the character spent its\n"
      "# points on, Load resets the character (like a Token of Absolution) and spends them again.\n"
      "# class: 0 Amazon, 1 Sorceress, 2 Necromancer, 3 Paladin, 4 Barbarian, 5 Druid, 6 Assassin,\n"
      "# 7 Warlock. Attributes are base values, the class's own points included. skills lists\n"
      "# \"skill id:points\" for the points spent; bonuses from items are not counted.\n";
  char line[64];
  for (const Preset& p : list) {
    if (!valid(p)) continue;
    out += "\n[[preset]]\n";
    out += "name = " + quoted(p.name) + "\n";
    std::snprintf(line, sizeof(line), "class = %d\n", p.char_class);
    out += line;
    std::snprintf(line, sizeof(line), "level = %d\n", p.level);
    out += line;
    std::snprintf(line, sizeof(line), "strength = %d\ndexterity = %d\n", p.strength, p.dexterity);
    out += line;
    std::snprintf(line, sizeof(line), "vitality = %d\nenergy = %d\n", p.vitality, p.energy);
    out += line;
    out += "skills = \"";
    for (size_t i = 0; i < p.skills.size(); ++i) {
      std::snprintf(line, sizeof(line), "%s%d:%d", i ? " " : "", p.skills[i].id, p.skills[i].points);
      out += line;
    }
    out += "\"\n";
  }
  return out;
}

int skill_points(const Preset& p) {
  int n = 0;
  for (const Skill& s : p.skills) n += s.points;
  return n;
}

const char* class_name(int char_class) {
  return char_class >= 0 && char_class < static_cast<int>(sizeof(kClassNames) / sizeof(kClassNames[0]))
             ? kClassNames[char_class]
             : "Unknown class";
}

std::string clean_name(const std::string& name) {
  std::string out;
  for (char c : name)
    if (static_cast<unsigned char>(c) >= 0x20 && c != 0x7F) out += c;
  out = trim(out);
  if (out.size() > kMaxName) {
    size_t cut = kMaxName;
    while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) --cut;  // not inside a UTF-8 character
    out = trim(out.substr(0, cut));
  }
  return out;
}

}  // namespace d2rcc::presets

#pragma once

#include <string>
#include <vector>

// Skill and attribute presets: what a character spent its points on, saved under a name and
// loaded back later (character.cpp resets the character and spends the points again). They live
// in cabbycodes.presets.txt beside the settings; text in, text out here, like config.cpp:
//
//   [[preset]]
//   name = "Blizzard"
//   class = 1           # 0 Amazon .. 7 Warlock
//   level = 85          # the level it was saved at (shown in the list)
//   strength = 60       # base attributes, the class's own included
//   dexterity = 25
//   vitality = 400
//   energy = 35
//   skills = "36:1 37:1 39:20"   # skill id:points spent (item bonuses not counted)
namespace d2rcc::presets {

struct Skill {
  int id = 0;
  int points = 0;
};

struct Preset {
  std::string name;
  int char_class = -1;
  int level = 0;
  int strength = 0, dexterity = 0, vitality = 0, energy = 0;
  std::vector<Skill> skills;  // in the game's own order, which spends prerequisites first
};

constexpr size_t kMaxName = 48;

// Presets with a name and a class; anything else in the text is skipped.
std::vector<Preset> parse(const std::string& text);
std::string format(const std::vector<Preset>& list);

int skill_points(const Preset& p);  // the points its skills take
const char* class_name(int char_class);
std::string clean_name(const std::string& name);  // trimmed, printable, at most kMaxName bytes

}  // namespace d2rcc::presets

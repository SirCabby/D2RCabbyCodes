// The presets file: format a list, read it back, and survive what a hand edit might do to it.
#include <cstdio>
#include <cstring>
#include <string>

#include "presets.h"

using namespace d2rcc::presets;

static int g_failures = 0;
#define CHECK(cond)                                                     \
  do {                                                                  \
    if (!(cond)) {                                                      \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
      ++g_failures;                                                     \
    }                                                                   \
  } while (0)

int main() {
  Preset a;
  a.name = "Blizzard \"cold\" \\ sorc";
  a.char_class = 1;
  a.level = 85;
  a.strength = 60;
  a.dexterity = 25;
  a.vitality = 400;
  a.energy = 35;
  a.skills = {{36, 1}, {37, 1}, {39, 20}, {59, 20}};
  Preset b;
  b.name = "Hammerdin";
  b.char_class = 3;
  b.level = 90;
  b.strength = 150;
  b.dexterity = 75;
  b.vitality = 350;
  b.energy = 15;
  b.skills = {{112, 20}, {113, 20}};

  const std::string text = format({a, b});
  CHECK(text.find("[[preset]]\nname = \"Blizzard \\\"cold\\\" \\\\ sorc\"\nclass = 1\n") != std::string::npos);
  CHECK(text.find("skills = \"36:1 37:1 39:20 59:20\"\n") != std::string::npos);

  const std::vector<Preset> back = parse(text);
  CHECK(back.size() == 2);
  if (back.size() == 2) {
    CHECK(back[0].name == a.name && back[0].char_class == 1 && back[0].level == 85);
    CHECK(back[0].strength == 60 && back[0].dexterity == 25 && back[0].vitality == 400 && back[0].energy == 35);
    CHECK(back[0].skills.size() == 4 && back[0].skills[2].id == 39 && back[0].skills[2].points == 20);
    CHECK(skill_points(back[0]) == 42);
    CHECK(back[1].name == "Hammerdin" && back[1].char_class == 3 && back[1].skills.size() == 2);
  }

  // Hand edits: CRLF, comments, a nameless preset, bad skill pairs, another table.
  const std::vector<Preset> edited = parse(
      "# note\r\n[[preset]]\r\nname = \"  Trapsin  \"  # comment\r\nclass = 6\r\n"
      "skills = \"251:20 x 252 253:0 254:300 255:5\"\r\n"
      "[[preset]]\r\nclass = 2\r\n"
      "[other]\r\nname = \"not a preset\"\r\n");
  CHECK(edited.size() == 1);
  if (edited.size() == 1) {
    CHECK(edited[0].name == "Trapsin" && edited[0].char_class == 6);
    CHECK(edited[0].skills.size() == 2 && edited[0].skills[0].id == 251 && edited[0].skills[1].id == 255);
  }
  CHECK(parse("").empty());
  CHECK(parse("[[preset]]\nname = \"no class\"\n").empty());

  // Names: control characters go, spaces are trimmed, long names are cut on a character boundary.
  CHECK(clean_name("  a\tb\n ") == "ab");
  const std::string longname = std::string(47, 'x') + "\xC3\xA9" + "yyy";  // an e-acute across the limit
  CHECK(clean_name(longname) == std::string(47, 'x'));
  CHECK(std::strcmp(class_name(7), "Warlock") == 0 && std::strcmp(class_name(9), "Unknown class") == 0);

  if (g_failures) {
    std::printf("%d failure(s)\n", g_failures);
    return 1;
  }
  std::printf("test_presets: ok\n");
  return 0;
}

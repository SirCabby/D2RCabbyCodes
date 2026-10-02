// The item seed model: the random stream, the seed pair rule, and, over the game's tables when they are at hand
// (D2RCC_EXCEL = the loader's compiled excel folder), what the searches find.
//   test_itemgen                     the checks
//   test_itemgen dump < cases        what each case makes, a line each: class quality level flags difficulty seed
//   test_itemgen find class quality level [wants] [seeds]   a search, timed
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include "itemgen.h"
#include "tables.h"

using namespace d2rcc::itemgen;

static int g_failures = 0;
#define CHECK(cond)                                                     \
  do {                                                                  \
    if (!(cond)) {                                                      \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
      ++g_failures;                                                     \
    }                                                                   \
  } while (0)

using tables::load;

static Request request(uint32_t cls, int32_t quality, int32_t ilvl, uint32_t flags, int difficulty) {
  Request r;
  r.item_class = cls;
  r.quality = quality;
  r.ilvl = ilvl;
  r.flags = flags;
  r.version = 101;
  r.difficulty = static_cast<uint8_t>(difficulty);
  r.game_bank = 3;
  r.game_type = 1;
  r.game_ladder = 1;
  return r;
}

static int dump(const Tables& t) {
  std::map<std::tuple<uint32_t, int, int, uint32_t, int>, std::shared_ptr<const Plan>> plans;
  char line[256];
  while (std::fgets(line, sizeof(line), stdin)) {
    unsigned cls = 0, flags = 0, seed = 0;
    int q = 0, ilvl = 0, diff = 0;
    if (std::sscanf(line, "%u %d %d %u %d %u", &cls, &q, &ilvl, &flags, &diff, &seed) != 6) continue;
    const auto key = std::make_tuple(cls, q, ilvl, flags, diff);
    auto it = plans.find(key);
    if (it == plans.end()) it = plans.emplace(key, make_plan(t, request(cls, q, ilvl, flags, diff))).first;
    Outcome o;
    if (it->second) o = simulate(*it->second, Search{}, seed);
    if (!o.followed) {
      std::printf("%u %d %d %u %d %u lost\n", cls, q, ilvl, flags, diff, seed);
      continue;
    }
    std::printf("%u %d %d %u %d %u q%d p%d,%d,%d s%d,%d,%d a%d f%d e%d k%d l%d r%08x:%08x\n", cls, q, ilvl, flags,
                diff, seed, o.quality, o.prefix[0], o.prefix[1], o.prefix[2], o.suffix[0], o.suffix[1], o.suffix[2],
                o.auto_affix, o.file_index, o.ethereal ? 1 : 0, o.sockets, o.skill_lines, o.rng_lo, o.rng_hi);
  }
  return 0;
}

static Wants wants_of(const char* s) {
  Wants w;
  for (; s && *s; ++s) {
    if (*s == 'p') w.perfect = true;
    if (*s == 'm') w.most = true;
    if (*s == 'b') w.best = true;
    if (*s == 'e') w.ethereal = true;
    if (*s == 's') w.sockets = true;
  }
  return w;
}

static int find(const Tables& t, int argc, char** argv) {
  if (argc < 5) return 2;
  const Request r = request(static_cast<uint32_t>(std::atoi(argv[2])), std::atoi(argv[3]), std::atoi(argv[4]), 0, 2);
  const char* why = nullptr;
  const auto plan = make_plan(t, r, &why);
  if (!plan) {
    std::printf("no plan: %s\n", why ? why : "?");
    return 1;
  }
  Search s;
  s.wants = wants_of(argc > 5 ? argv[5] : "pmb");
  const uint64_t seeds = argc > 6 ? std::strtoull(argv[6], nullptr, 0) : (1ull << 32);
  const auto begin = std::chrono::steady_clock::now();
  uint64_t tried = 0, hits = 0;
  uint32_t first = 0;
  uint32_t at = 0;
  while (tried < seeds) {
    const Found f = search(*plan, s, at, 1, seeds - tried);
    tried += f.tried;
    if (!f.all) break;
    if (!hits++) first = f.seed;
    at = f.seed + 1;
    if (hits <= 3) {
      const Outcome o = simulate(*plan, s, f.seed);
      std::printf("  seed %08x: q%d p%d,%d,%d s%d,%d,%d a%d f%d e%d k%d l%d\n", f.seed, o.quality, o.prefix[0],
                  o.prefix[1], o.prefix[2], o.suffix[0], o.suffix[1], o.suffix[2], o.auto_affix, o.file_index,
                  o.ethereal ? 1 : 0, o.sockets, o.skill_lines);
    }
  }
  const double sec =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
  std::printf("class %s quality %s level %s wants %s: %llu seeds with everything in %llu looked at (the first %08x), "
              "%.2f s, %.1f million seeds a second\n",
              argv[2], argv[3], argv[4], argc > 5 ? argv[5] : "pmb", static_cast<unsigned long long>(hits),
              static_cast<unsigned long long>(tried), first, sec, tried / sec / 1e6);
  return 0;
}

int main(int argc, char** argv) {
  // The random stream: the game's multiplier, the carry seeded 666; a roll of a power of two is a mask.
  Rng rng(1);
  CHECK(rng.step() == 0x6AC690C5u + 666u);
  CHECK(rng.hi == 0);
  Rng two(0xFFFFFFFFu);
  CHECK(two.step() == static_cast<uint32_t>(0xFFFFFFFFull * 0x6AC690C5ull + 666));
  CHECK(two.hi == static_cast<uint32_t>((0xFFFFFFFFull * 0x6AC690C5ull + 666) >> 32));
  Rng none(7);
  CHECK(none.roll(0) == 0 && none.roll(-3) == 0 && none.lo == 7 && none.hi == 666);
  Rng mask(12345), mod(12345);
  CHECK(mask.roll(8) == (mod.step() & 7));
  Rng a(99), b(99);
  CHECK(a.roll(100) == b.step() % 100);
  // Two seeds in a row: the second is the first times the multiplier plus a carry below the multiplier.
  {
    Rng game(0xDEADBEEFu);
    game.hi = 12345;
    for (int i = 0; i < 1000; ++i) {
      const uint32_t unit = game.step();
      Rng next = game;
      const uint32_t item = next.step();
      CHECK(in_a_row(unit, item));
    }
    CHECK(!in_a_row(1, 0x6AC690C5u - 1));  // a carry of -1
    CHECK(in_a_row(1, 0x6AC690C5u));
    CHECK(in_a_row(1, 0x6AC690C5u + 0x6AC690C4u));
    CHECK(!in_a_row(1, 0x6AC690C5u + 0x6AC690C5u));
  }
  // An armor's unit seed: the durability roll, then the defense.
  {
    UnitPlan u;
    u.armor = true;
    u.durability_n = 12;
    u.min_ac = 100;
    u.defense_n = 21;
    Rng r(4242);
    r.roll(12);
    CHECK(defense_of(u, 4242) == 100 + static_cast<int32_t>(r.roll(21)));
    uint32_t unit = 0;
    CHECK(find_unit_seed(u, 0x12345678u, true, 1, 100000, &unit));
    CHECK(in_a_row(unit, 0x12345678u) && defense_of(u, unit) == 120);
    u.durability_n = 0;  // no durability roll: the defense is the first step
    Rng first(77);
    CHECK(defense_of(u, 77) == 100 + static_cast<int32_t>(first.roll(21)));
  }
  const char* dir = std::getenv("D2RCC_EXCEL");
  Tables t;
  if (!dir || !*dir) {
    std::printf(g_failures ? "test_itemgen: %d FAILED\n" : "test_itemgen: ok (no tables: D2RCC_EXCEL is not set)\n",
                g_failures);
    return g_failures ? 1 : 0;
  }
  if (!load(dir, &t)) {
    std::printf("test_itemgen: the tables in %s could not be read\n", dir);
    return 1;
  }
  if (argc > 1 && !std::strcmp(argv[1], "dump")) return dump(t);
  if (argc > 1 && !std::strcmp(argv[1], "find")) return find(t, argc, argv);
  // Over the tables: a seed found with everything wanted makes it when followed again, the same twice.
  int plans = 0, found = 0;
  for (uint32_t cls = 0; cls < t.items_count; cls += 7) {
    for (const int quality : {4, 6, 7}) {
      const auto plan = make_plan(t, request(cls, quality, 85, 0, 2));
      if (!plan) continue;
      ++plans;
      Search s;
      s.wants.perfect = true;
      s.wants.most = quality != 7;
      const Found f = search(*plan, s, cls * 2654435761u, 0x9E3779B1u, 200000);
      if (!f.all) continue;
      ++found;
      const Outcome o = simulate(*plan, s, f.seed), again = simulate(*plan, s, f.seed);
      CHECK(o.followed && o.all && o.primary);
      CHECK(o.rng_lo == again.rng_lo && o.rng_hi == again.rng_hi && o.score == again.score);
      // Asking for nothing changes nothing of what the seed makes.
      const Outcome plain = simulate(*plan, Search{}, f.seed);
      CHECK(plain.followed && plain.rng_lo == o.rng_lo && plain.rng_hi == o.rng_hi &&
            plain.prefix[0] == o.prefix[0] && plain.suffix[0] == o.suffix[0] && plain.file_index == o.file_index);
    }
  }
  CHECK(plans > 50 && found > 20);
  std::printf(g_failures ? "test_itemgen: %d FAILED\n" : "test_itemgen: ok (%d plans, %d searches found a seed)\n",
              g_failures ? g_failures : plans, found);
  return g_failures ? 1 : 0;
}

#pragma once

#include <cstdint>
#include <memory>
#include <vector>

// A model of the game's item generator (D2R 3.3, expansion items): what an item seed makes. Research, not part of
// the plugin. The game draws two seeds for a new item, two outputs in a row of its own random stream. The unit's
// seed rolls what item init rolls (durability, an armor's base defense); the item's own seed rolls everything the
// generator does: the affix picks, every property value, the class skill lines, ethereal, sockets. A save keeps
// the unit's seed only, so the plugin's loot switches act in the generator and nothing is tied to the item's own
// seed later; what the plugin needs of the unit's seed is in src/itemseed.h.
//
// The model follows the generator roll by roll: pure computation over copies of the game's tables, no game calls,
// any thread. It was checked against the game (2026-09-28, an earlier plugin that made items from chosen seeds):
// 1000+ items, none otherwise than the model said; armor base defense from the unit seed 74 of 74. A request it
// cannot follow exactly (a classic item, a quality rolled by the seed, a property group) has no plan.
namespace d2rcc::itemgen {

// The game's random stream: multiply with carry, the carry seeded 666.
struct Rng {
  uint32_t lo = 0;
  uint32_t hi = 666;
  explicit Rng(uint32_t seed = 0) : lo(seed) {}
  uint32_t step() {
    const uint64_t t = static_cast<uint64_t>(lo) * 0x6AC690C5u + hi;
    lo = static_cast<uint32_t>(t);
    hi = static_cast<uint32_t>(t >> 32);
    return lo;
  }
  // The game's roll (seed, n): 0 without a step for n <= 0.
  uint32_t roll(int32_t n) {
    if (n <= 0) return 0;
    const uint32_t v = step();
    const uint32_t un = static_cast<uint32_t>(n);
    return (un & (un - 1)) == 0 ? v & (un - 1) : v % un;
  }
};
constexpr uint32_t kMultiplier = 0x6AC690C5u;
// The model's version: it goes up whenever what the model says a seed makes changes, and seeds kept from an earlier
// one are looked for again.
constexpr uint32_t kModelVersion = 1;

// The compiled rows the generator reads, as the game has them in memory (the loader's compiled tables have the
// same rows). Sizes are checked where the tables are copied.
constexpr uint32_t kAffixRow = 0x8C;
constexpr uint32_t kPropertyRow = 0x30;
constexpr uint32_t kItemsRow = 0x1C0;
constexpr uint32_t kTypesRow = 0xE8;
constexpr uint32_t kUniqueRow = 0x15C;
constexpr uint32_t kSetItemRow = 0x1CC;
constexpr uint32_t kQualityRow = 0x2C;
constexpr uint32_t kRareNameRow = 0x48;

struct Skill {
  int8_t char_class = -1;  // 0 Amazon .. 7 Warlock, -1 none
  int16_t req_level = 0;
  int16_t max_level = 0;
  int16_t item_type = 0;   // the item type the skill needs (0 none)
};

struct Tables {
  uint8_t bank = 0;
  std::vector<uint8_t> affix;  // suffixes, prefixes, automagic: an affix id is its row's index plus one
  uint32_t suffix_at = 0, prefix_at = 0, auto_at = 0, affix_count = 0;
  std::vector<uint8_t> props;
  uint32_t props_count = 0;
  std::vector<uint8_t> items;  // weapons, armor, misc: an item's class is its row
  uint32_t items_count = 0;
  std::vector<uint8_t> types;
  uint32_t types_count = 0;
  std::vector<uint8_t> unique;
  uint32_t unique_count = 0;
  std::vector<uint8_t> sets;
  uint32_t sets_count = 0;
  std::vector<uint8_t> quality;
  uint32_t quality_count = 0;
  std::vector<uint8_t> rare_names;  // suffix names, then prefix names
  uint32_t rare_suffix_count = 0, rare_prefix_count = 0;
  std::vector<Skill> skills;  // by skill id
  bool complete() const;
};

// A print of each table's rows (FNV-1a, folded to 32 bits): the game's tables in memory and the loader's compiled
// files are the same rows when their prints are.
struct Prints {
  uint32_t affix = 0, rare_names = 0, items = 0, types = 0, props = 0, unique = 0, sets = 0, quality = 0, skills = 0;
};
Prints prints_of(const Tables& tables);

// What is asked of the generator, from the game's creation request and the game it is for.
struct Request {
  uint32_t item_class = 0;
  int32_t ilvl = 1;
  int32_t quality = 0;      // the request's (+0x40): 2 normal .. 8 crafted
  uint32_t flags = 0;       // the request's (+0xC0): 2 never ethereal, 4 ethereal, 8 no sockets, 0x10 sockets,
                            // 0x20 the item level as the class skill bonus
  uint16_t version = 0;     // the item's version (100 and up: an expansion item)
  uint8_t difficulty = 0;   // Game +0x104
  uint8_t game_bank = 0;    // Game +0x106
  uint8_t game_type = 0;    // Game +0x101
  int32_t game_ladder = 0;  // Game +0x108
};

// The request's flags (+0xC0).
constexpr uint32_t kFlagNeverEthereal = 2, kFlagEthereal = 4, kFlagNoSockets = 8, kFlagSockets = 0x10,
                   kFlagSkillBonus = 0x20;

// The game makes a rare item in two ways a seed can be chosen for. As a drop: the request as the drop asks. As an
// imbue (Charsi's reward, 0x4FC230's service 1): a normal item of the player's becomes a rare one of its base,
// with the class skill step's bonus (the item level) and the base's ethereal state kept: an ethereal base's rare
// is ethereal for sure, another's never. Its item level is the character's level + 4.
// Whether the game's imbue takes a normal item of the class (its own test, 0x36AED0: the Items row's bitfield1
// bit 0, no quest item but Wirt's Leg), and a normal item of it can be (its type is not an always magic one, the
// base not an always unique one).
bool can_be_imbued(const Tables& tables, uint32_t item_class);
// The flags of an imbue's request: `ethereal` for a base that is ethereal.
constexpr uint32_t imbue_flags(bool ethereal) {
  return kFlagSkillBonus | (ethereal ? kFlagEthereal : kFlagNeverEthereal);
}
constexpr int32_t kImbueQuality = 6;
constexpr int32_t kImbueLeastLevel = 5;  // a character of level 1

// What the loot switches want of a new item.
struct Wants {
  bool perfect = false;   // every value at the top of its range, three class skill lines at +3
  bool most = false;      // as many affixes as the quality can have
  bool best = false;      // each affix the top version of its kind
  bool ethereal = false;  // ethereal where the item can be
  bool sockets = false;   // sockets where the item can have them
  bool any() const { return perfect || most || best || ethereal || sockets; }
  uint32_t bits() const {
    return (perfect ? 1u : 0) | (most ? 2u : 0) | (best ? 4u : 0) | (ethereal ? 8u : 0) | (sockets ? 16u : 0);
  }
};

// What a seed makes.
struct Outcome {
  bool followed = false;   // the model could follow the generator for this seed
  bool primary = false;    // the quality asked for came out (no fallback to a lower one)
  bool all = false;        // everything wanted is there
  int32_t quality = 0;
  int32_t prefix[3] = {};
  int32_t suffix[3] = {};
  int32_t auto_affix = 0;
  int32_t file_index = -1;  // the unique or set item's row, a superior item's QualityItems row
  bool ethereal = false;      // by the generator's roll (or the request)
  bool ethereal_mod = false;  // one of the item's properties makes it ethereal where it can be, whatever the roll
  int32_t sockets = 0;        // by the generator's roll
  bool socket_mod = false;    // one of the item's properties gives it sockets where it can have them
  bool top_defense = false;   // one of them (enhanced defense) gives an armor its top base defense plus one,
                              // whatever the unit's seed rolled
  int32_t skill_lines = 0;
  uint32_t rng_lo = 0, rng_hi = 0;  // the item's random stream when the generator is done
  double score = 0;                 // how much of what is wanted is there (higher is better)
};

class Plan;

// Everything a request's generation depends on, worked out once. Null when the model does not cover the request.
// `why`, when given, says why not.
std::shared_ptr<const Plan> make_plan(const Tables& tables, const Request& request, const char** why = nullptr);

// Requests that make the same items from the same seeds have the same fingerprint (whatever their item level).
uint64_t fingerprint(const Plan& plan);
const Request& request_of(const Plan& plan);
// Whether an item of the plan's base can be ethereal at all (a weapon or armor with durability; the request's
// "never ethereal" aside).
bool ethereal_base(const Plan& plan);
// The unique rows the plan can pick (their "dropped this game" state is asked at each search).
int unique_candidates(const Plan& plan, int32_t* rows, int capacity);

struct Search {
  Wants wants;
  // Which of the plan's unique candidates dropped already this game (by candidate, as unique_candidates lists them).
  uint64_t dropped = 0;
};

// How a search takes what is wanted: everything or the seed is dropped; everything but the values, which are
// weighed; or all of it weighed.
enum class Tier : int { kEverything = 0, kValuesWeighed = 1, kAllWeighed = 2 };

// What `seed` makes.
Outcome simulate(const Plan& plan, const Search& search, uint32_t seed);

struct Found {
  bool any = false;      // a seed was looked at
  bool all = false;      // ... that gives everything wanted
  uint32_t seed = 0;
  double score = 0;
  uint64_t tried = 0;
};

// Looks at `count` seeds, `start`, `start + stride`, ... (stride odd: every seed once in 2^32) for one that gives
// everything wanted; a seed is dropped at the first thing it misses. `stop`, when given, ends the search once
// nonzero.
Found search(const Plan& plan, const Search& search, uint32_t start, uint32_t stride, uint64_t count,
             const volatile long* stop = nullptr);
// The same walk for the seed that gives the most of what is wanted (it ends early at one that gives everything).
Found search_best(const Plan& plan, const Search& search, Tier tier, uint32_t start, uint32_t stride, uint64_t count,
                  const volatile long* stop = nullptr);

// The affixes the plan lets the picker choose from (their ids), and the ones only their item types ruled out: what
// the game's own test of an affix on the item must agree with.
void affix_candidates(const Plan& plan, std::vector<int32_t>* fit, std::vector<int32_t>* unfit);

// The unit's seed: what item init rolls from it. For an armor the durability roll, then the base defense.
struct UnitPlan {
  bool armor = false;
  int32_t durability_n = 0;  // the durability roll's range (no step when it is not above 0)
  int32_t defense_n = 0;     // the defense roll's: maxac - minac + 1
  int32_t min_ac = 0;
};
UnitPlan make_unit_plan(const Tables& tables, uint32_t item_class);
// The base defense `unit_seed` rolls for the armor (-1 when it is not one).
int32_t defense_of(const UnitPlan& plan, uint32_t unit_seed);
// Whether the game's random stream can give `unit_seed` and then `item_seed`: the second is the first times the
// multiplier plus a carry below the multiplier.
inline bool in_a_row(uint32_t unit_seed, uint32_t item_seed) {
  return static_cast<uint32_t>(item_seed - unit_seed * kMultiplier) < kMultiplier;
}
// A unit seed for the item seed: one the stream can give right before it, and for an armor under `perfect` one
// that rolls the top base defense. Tries `tries` seeds from `start`; false when none fits (`out` is then the last
// one that at least comes in a row).
bool find_unit_seed(const UnitPlan& plan, uint32_t item_seed, bool perfect, uint32_t start, uint32_t tries,
                    uint32_t* out);

}  // namespace d2rcc::itemgen

#pragma once

#include <D2RLPlugin/api.h>

#include <cstdint>
#include <functional>
#include <vector>

namespace d2rcc::game {
struct Unit;
}

// What a treasure class can drop that the loot filter shows, worked out instead of rolled for.
//
// The game makes a drop in three steps: it walks the treasure class to an item (the drop core), rolls the item's
// quality (the quality roll), and makes the item (the generator, which may make it ethereal or socketed, and
// something lesser when the quality asked for cannot be made). Under the loot filter switch a drop the filter
// hides used to be rolled again until one it shows came out. Here the same is reached without a roll that can
// miss: every item the treasure class leads to is listed with how often one walk of it makes that item, each
// with the chance of every quality the quality roll can give it and of every form the generator can make of
// that, and of those the ones the filter shows are kept, by the odds they have in the game. One of them is then
// picked, and the game makes that one: what drops is what rolling again and again would have dropped, and a drop
// stays away only when the treasure class has nothing left that the filter shows.
//
// What is kept may also go by the row of the unique or set item a form comes out as (the Chronicle's entries still
// to find, chronicle.h): a unique or set item of a wanted row is kept whatever the filter says of it, and what the
// filter shows is kept besides. Such a form names its row to the generator, as a unique still to come is named.
//
// Nothing of the game is called from here but through what it is handed (Request): its tables come through the
// loader, a treasure class's entries are read where the game keeps them. Game thread only.
namespace d2rcc::dropodds {

using game::Unit;

// --- the arithmetic (no game, no tables: tests/test_dropodds.cpp) -----------------------------------------------

// An ItemRatio row as the game keeps it (16 int32).
struct Ratio {
  int32_t unique, unique_divisor, unique_min;
  int32_t rare, rare_divisor, rare_min;
  int32_t set, set_divisor, set_min;
  int32_t magic, magic_divisor, magic_min;
  int32_t superior, superior_divisor;
  int32_t normal, normal_divisor;
};

// What the quality roll and the generator ask of an item's type and row.
struct Kind {
  bool normal_only = false;   // ItemTypes: only ever normal
  bool unique_only = false;   // Items: must be unique
  bool always_magic = false;  // ItemTypes: at least magic
  bool quest = false;         // Items: a quest item
  bool can_be_rare = false;   // ItemTypes
};

constexpr int kQualities = 8;  // a quality is 1 low, 2 normal, 3 superior, 4 magic, 5 set, 6 rare, 7 unique
struct Odds {
  double of[kQualities] = {};
};

// The game's quality roll (0x4421B0) as chances: `levels_above` the item level less the base's own, `magic_find`
// the killer's (with its owner's), `mods` the treasure classes' on the way to the item (magic, rare, set, unique:
// so many 1024ths off the chance's divisor).
Odds quality_odds(const Ratio& ratio, const Kind& kind, int32_t levels_above, int32_t magic_find,
                  const uint16_t mods[4]);

// What the generator makes of a quality that is asked for, in two steps. First what the item has to be (0x442D60):
// all superior (the loot switch) makes low and normal superior, an item type that is always magic is magic at
// least, one that is never rare is magic for rare, an item that must be unique is unique, a type that is only ever
// normal is normal.
int32_t quality_asked(const Kind& kind, int32_t quality, bool all_superior);
// Then what comes of a step that cannot make it: low and superior quality are for weapons and armor (normal for
// the rest); a set item for a base with a set item to make (magic else); a unique that cannot be made is rare, or
// magic for a type that is never rare (`made` false: nothing to pick among, or the row picked came already).
int32_t quality_made(const Kind& kind, int32_t asked, bool equipment, bool made);

// The generator's two rolls for an item that can be ethereal (5 in 100) and one that can have sockets (33 in
// 100), as the chances of the four outcomes: [0] neither, [1] ethereal only, [2] sockets only, [3] both.
struct Extras {
  double of[4] = {};
};
Extras extras_odds(double ethereal, double sockets);

// --- a treasure class as the drop core walks it -----------------------------------------------------------------

// The game's record (0x150 bytes) and one of its entries (0x20).
constexpr uint32_t kTcSize = 0x150;
constexpr uintptr_t kTcGroup = 0x00;         // int16
constexpr uintptr_t kTcLevel = 0x02;         // int16
constexpr uintptr_t kTcTotalClassic = 0x10;  // int32: the entries' chances together, in a classic game
constexpr uintptr_t kTcTotal = 0x14;         // ... and in the others
constexpr uintptr_t kTcPicks = 0x18;         // int32: below 0 the entries in their order
constexpr uintptr_t kTcNoDrop = 0x1C;        // int32
constexpr uintptr_t kTcCalc = 0x20;          // int32: its condition
constexpr uintptr_t kTcFlags = 0x24;         // u8
constexpr uintptr_t kTcFlags2 = 0x25;        // u8: what its condition is (1 a ladder season, 2 a quest, 4 a calc)
constexpr uintptr_t kTcMods = 0x26;          // u16[6]: magic, rare, set, unique; ethereal, sockets (of 1024)
constexpr uintptr_t kTcEntries = 0x38;       // the entries
constexpr uintptr_t kTcCount = 0x40;         // u64
constexpr uintptr_t kTcName = 0x50;          // char[]: its name (the game's debug trace prints it)
constexpr uint8_t kTcKeepsNoDrop = 0x20;     // NoDrop counts even when the walk is told to skip it
constexpr uint8_t kTcConditional = 0x40;     // the treasure class has a condition
constexpr uint8_t kTcHasConditional = 0x80;  // ... and so have entries of it
constexpr uint8_t kTcCalcCondition = 4;
constexpr uint32_t kEntrySize = 0x20;
constexpr uintptr_t kEntryStartClassic = 0x00;  // int32: where its share of the chances starts
constexpr uintptr_t kEntryStart = 0x04;
constexpr uintptr_t kEntryItem = 0x08;   // int32: an Items row, or a treasure class
constexpr uintptr_t kEntryFlags = 0x0C;  // u8
constexpr uintptr_t kEntryExtra = 0x1C;  // u16: the unique or set row it names, or gold's multiplier (256ths)
constexpr uint8_t kEntryUnique = 1;      // names a unique row
constexpr uint8_t kEntrySet = 2;         // ... a set item row
constexpr uint8_t kEntryClass = 4;       // a treasure class
constexpr uint8_t kEntryConditional = 0x40;
constexpr int kMods = 6;
constexpr int kModEthereal = 4, kModSockets = 5;

// Something a treasure class drops: an item, the treasure classes' mods on the way to it, and how many of it one
// walk of the treasure class makes on average.
struct Leaf {
  uint32_t item_class = 0;
  uint8_t flags = 0;    // the entry's kEntryUnique / kEntrySet
  uint16_t extra = 0;   // the entry's last word
  uint16_t mods[kMods] = {};
  uintptr_t entry = 0;  // the game's entry
  double weight = 0;
};

// Where the treasure classes are, and what only the drop core can say of one.
struct Classes {
  uintptr_t rows = 0;  // the game's records
  uint32_t count = 0;
  // Whether the drop core takes a conditional entry of a treasure class for the source at hand.
  std::function<bool(uintptr_t tc, const uint8_t* entry, int32_t chance)> takes;
};

// Every item one walk of the treasure class can make, NoDrop skipped (as the loot filter switch has the drop core
// walk it). False when the walk cannot be followed: a record that cannot be read, a treasure class that keeps its
// NoDrop, one nested deeper than the drop core goes. `conditional` when what came out depends on the source.
bool leaves_of(const Classes& classes, uintptr_t tc, std::vector<Leaf>* out, bool* conditional);

// --- an item that is asked for, and the forms it can take ---------------------------------------------------------

// The generator's request: its flags.
constexpr uint32_t kRequestSet29 = 1;  // the items of set 29 may come (the source is one of two monsters)
constexpr uint32_t kNeverEthereal = 2, kEthereal = 4, kNoSockets = 8, kSockets = 0x10;

// Rows of the UniqueItems or SetItems table, a bit a row.
class Rows {
 public:
  static constexpr uint32_t kMost = 0x1000;  // the unique step's own bound (the dropped bits go that far)
  bool has(uint32_t row) const { return row < kMost && (bits_[row >> 6] >> (row & 63) & 1) != 0; }
  bool set(uint32_t row, bool on = true) {  // false for a row past kMost
    if (row >= kMost) return false;
    if (on) bits_[row >> 6] |= 1ull << (row & 63);
    else bits_[row >> 6] &= ~(1ull << (row & 63));
    return true;
  }
  void clear() {
    for (uint64_t& w : bits_) w = 0;
  }
  int count() const {
    int n = 0;
    for (const uint64_t w : bits_) n += __builtin_popcountll(w);
    return n;
  }
  // The rows of this that the other has not.
  void without(const Rows& other) {
    for (uint32_t i = 0; i < kMost / 64; ++i) bits_[i] &= ~other.bits_[i];
  }
  bool operator==(const Rows& other) const {
    for (uint32_t i = 0; i < kMost / 64; ++i)
      if (bits_[i] != other.bits_[i]) return false;
    return true;
  }

 private:
  uint64_t bits_[kMost / 64] = {};
};

struct Request {
  void* game = nullptr;
  Unit* source = nullptr;
  Unit* killer = nullptr;
  uintptr_t tc = 0;            // the treasure class the drop core was given
  int32_t roll_level = 0;      // ... and the item level: what the quality roll goes by
  int32_t forced_quality = 0;  // ... and the quality, when its caller wants one (0: rolled)
  int32_t item_level = 0;      // the level the source's items get (the generator's request has it)
  uint32_t flags = 0;          // what the item maker puts in every request for the source (kRequestSet29)
  uint8_t bank = 0;            // the data tables the game's items come from
  bool all_superior = false, all_ethereal = false, all_socketed = false;  // the loot switches that change what
                                                                          // the filter is shown
  // What only the drop core can say, for the source: whether a treasure class's own condition holds, whether it
  // takes an item entry that has one, whether a condition of the game's (a calc) holds (-1: not to be asked now).
  std::function<bool(uintptr_t tc)> holds;
  std::function<bool(uintptr_t tc, const uint8_t* entry)> takes;
  std::function<int(int32_t calc)> calc;
  // The loot filter's answers for a class, a bit per quality and ethereal-or-socketed ((quality - 1) * 2 + es);
  // false for a class it cannot be asked about. Asked only with `filter`: without, the filter shows nothing (and
  // gold, which it judges by the amount, is not wanted).
  bool filter = true;
  std::function<bool(uint32_t item_class, uint16_t* shown)> shown;
  // The rows wanted whatever the filter says (the Chronicle's entries still to find): a unique of one of these rows
  // that has not dropped in the game, a set item of one of those. Null: no row is.
  const Rows* uniques = nullptr;
  const Rows* sets = nullptr;
};

// What the item maker asks of the generator.
struct Ask {
  uint32_t item_class = 0;
  int32_t quality = 0;    // 1..7
  int32_t named = 0;      // the file index: a unique or set row + 1 that is asked for by name, 0 none
  int32_t item_level = 0;
  uint32_t flags = 0;     // the request's
  // The treasure classes' chances of an ethereal or socketed item, while the drop core has not rolled them yet
  // (of 1024; once it has, the flags say what came of them).
  uint16_t ethereal_mod = 0, sockets_mod = 0;
};

// What has the generator make one form of an item and no other, and what that form is.
struct Force {
  int32_t name = 0;             // the file index to ask for, 0 the request's own
  uint32_t set = 0, clear = 0;  // the request's flags to set and to clear
  int32_t quality = 0;          // the quality it comes out with (0: whatever comes)
  int8_t ethereal = -1, sockets = -1;  // ... and ethereal, socketed: 1, 0, or -1 as it comes
  int32_t row = -1;             // ... and the unique or set row it is (-1: any)
};

enum class Answer {
  kAsItComes,    // whatever the generator makes of it, the filter shows (or nothing can be said of it)
  kSo,           // the filter shows some of what can come of it: make it as `force` says
  kNot,          // the filter hides whatever can come of it
  kNotThisTime,  // it shows some of it, and by their chance this one is not made (made as it comes, it might be one
                 // of those: it is no check of the answers)
};
// For an item the game is about to make: by the chance that what comes of it is something the filter shows (or a
// unique or set item of a wanted row), that (one of the forms it shows, by their chances) or not at all.
Answer judge(const Request& request, const Ask& ask, Force* force);
// Whether an item asked for with a quality may come out as a unique or set item of a wanted row: 1, 0, or -1 when
// that cannot be said (the tables are not the ones known). By the rows of its base, whatever the item's level.
int may_be_wanted(const Request& request, uint32_t item_class, int32_t quality);

// --- the drops of a session ------------------------------------------------------------------------------------

// One drop to have made.
struct Pick {
  int outcome = -1;                // which of the plan's
  uint32_t item_class = 0;
  int32_t quality = 0;             // the quality to ask the drop core for; 0 the game's own roll
  uint8_t entry[kEntrySize] = {};  // the entry to give the drop core
  uint16_t mods[kMods] = {};       // ... and the treasure class's mods
  Force force;                     // ... and what the request is to say besides
  bool gold = false;
};

struct Totals {
  int leaves = 0;    // items the treasure class leads to
  int outcomes = 0;  // ... and of them, with their qualities and forms, the ones the filter shows
  double walk = 0;   // items one walk makes
  double shown = 0;  // ... of which the filter shows this many
  bool conditional = false;  // the treasure class has entries only some sources get
};

class Plan {
 public:
  // False when the treasure class cannot be worked out (the caller goes on as before, rolling).
  bool build(const Request& request);
  const char* why_not() const { return why_; }
  bool empty() const { return left_ <= 0; }
  const Totals& totals() const { return totals_; }
  // One drop by the odds; false when nothing is left that can be made.
  bool pick(Pick* out);
  // What came of a pick: whether it was made and is what it was to be. One that was not is not picked again; a
  // unique that came is not there a second time, nor a set item whose row the caller took out of the wanted rows.
  void made(const Pick& pick, bool as_picked);
  void without_gold();  // gold is hidden after all (the filter goes by the amount)

 private:
  struct Outcome {
    Leaf leaf;
    int32_t quality = 0;     // asked for (0: the game's roll)
    int32_t comes = 0;       // the quality it comes out with (0: whatever comes)
    uint8_t pool = 0;        // the rows the unique or set step is to come to (dropodds.cpp)
    int8_t ethereal = -1, sockets = -1;
    bool gold = false;
    double base = 0;         // its weight but for what is left of its base's uniques (or wanted rows)
    double weight = 0;
  };
  void add(const Leaf& leaf);
  void weigh(Outcome* o);
  Request request_;
  std::vector<Outcome> outcomes_;
  Totals totals_;
  double left_ = 0;
  const char* why_ = nullptr;
};

// What one walk of the treasure class asks the item maker for, before the filter has its say: every item and
// quality with how many of it a walk makes. For a check against the drop core itself.
struct Expected {
  uint32_t item_class = 0;
  int32_t quality = 0;
  double count = 0;
};
bool expected(const Request& request, std::vector<Expected>* out);

// A treasure class's name, for the log (printable characters only).
void name_of(uintptr_t tc, char* out, size_t size);

void set_services(const D2RL::PluginContext* ctx, const D2RL::DataTableService* tables);
void reset();  // a game was left: what was read of the tables is read again

}  // namespace d2rcc::dropodds

#include "loot.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <utility>
#include <vector>

#include "cheats.h"
#include "chronicle.h"
#include "dropodds.h"
#include "game.h"
#include "log.h"
#include "mem.h"
#include "overlay.h"
#include "sites.h"
#include "superior.h"

namespace d2rcc::loot {
namespace {

const D2RL::PluginContext* g_ctx = nullptr;
const D2RL::DataTableService* g_tables = nullptr;
volatile LONG g_editor_open = 0;
char g_open_panels[8][64] = {};  // the loot filter panels open now (UI thread only)
volatile LONG g_replaced = 0;
volatile LONG g_omitted = 0;
volatile LONG g_extra = 0;
volatile LONG g_slowest_us = 0;  // the longest a drop session's search took
volatile LONG g_unmade = 0;      // drops not made at all: the filter hides whatever they could have become
volatile LONG g_maxed = 0;
volatile LONG g_maxed_logged = 0;  // values maxed, logged so far in this game
volatile LONG g_skill_items = 0;
volatile LONG g_affixes = 0;
volatile LONG g_upgraded = 0;
volatile LONG g_logged = 0;      // drop sessions logged so far (the first few are, in every build)
volatile LONG g_elite_logged = 0;     // ... and the ones that gave a champion, unique, boss or Herald its extra drops
volatile LONG g_ordinary_logged = 0;  // ... and the kills and chests the extra drops passed over
volatile LONG g_affix_logged = 0;  // ... and items given more affixes
volatile LONG g_skill_logged = 0;  // ... and class items given their top skill roll
volatile LONG g_upgrade_logged = 0;  // ... and affixes upgraded
volatile LONG g_row_warned = 0;
volatile LONG g_monstats_warned = 0;
// The loot filter asked before an item is made (see refuse()). The answers are kept until the filter may have
// changed: its editor opened or closed, a game left.
volatile LONG g_filter_generation = 1;
volatile LONG g_item_bank = 0;     // the data tables a drop's items were seen to come from (0 none seen yet)
volatile LONG g_refusable = 0;     // drops that could be refused, and the ones of them made all the same, since
volatile LONG g_audits = 0;        // ... the filter's answers were last asked anew (g_audit_generation)
volatile LONG g_audit_generation = 0;
volatile LONG g_mismatches = 0;    // answers the game's own test of a real item did not bear out
volatile LONG g_ask_first_off = 0;  // ... too many of them: items are made and then shown to the filter, as before
// The drops picked among what the filter shows (dropodds.h).
volatile LONG g_picked = 0;         // drops picked, this game
volatile LONG g_picks = 0;          // items made in a form that was worked out, since the plugin loaded
volatile LONG g_missed = 0;         // ... and the ones of them that came out otherwise
volatile LONG g_missed_logged = 0;  // ... of them logged so far
volatile LONG g_missed_game = 0;    // ... this game
volatile LONG g_odds_off = 0;       // too many of them: drops are rolled for until the filter shows one, as before
volatile LONG g_plan_warned = 0;    // treasure classes that could not be worked out, logged so far
volatile LONG g_check_walks = 0;    // the console's dropcheck: the walks to count at the next monster's drop
volatile LONG g_property_warned = 0;
volatile LONG g_side_warned = 0;
// A superior item's kind (superior.h).
volatile LONG g_kinds = 0;         // items given the best kind where the game's roll came to another first, this game
volatile LONG g_kind_logged = 0;   // ... logged so far
volatile LONG g_kind_misses = 0;   // items the step was told no for that came out otherwise
volatile LONG g_kinds_off = 0;     // too many of them: the kind is the game's pick again, for the session
volatile LONG g_kind_rows_warned = 0;
volatile LONG g_kind_asked_warned = 0;

uintptr_t g_choice[kMaxChoiceSites] = {};
volatile LONG g_choice_count = 0;
uintptr_t g_text_begin = 0;
uintptr_t g_text_end = 0;

thread_local int t_depth = 0;  // inside a drop session: our own passes call the game's routine again
// How the drop core's items are made while a session has it walk a treasure class (the item creation hook asks).
enum class Making : uint8_t {
  kRolled,   // the game's own walk: the filter is asked by what the quality asked for may become
  kWalked,   // ... with the odds at hand: by the forms the request can take, one of those the filter shows made
  kAsked,    // a treasure class of the plugin's own that asks the drop core a question: nothing is made
  kPicked,   // ... that has it make the one item picked
  kCounted,  // the game's own walk with nothing made, what it asks for counted (the console's dropcheck)
};
// A drop session that asks the loot filter before its items are made (the item creation hook asks refuse()).
struct Session {
  Unit* source = nullptr;  // what drops: the items are made beside it
  uint8_t bank = 0;        // the data tables its items come from
  // What a drop that is kept must be: one the loot filter shows, or a unique or set item the Chronicle misses
  // (either will do when both are asked).
  bool filter = false;
  bool chronicle = false;
  Making making = Making::kRolled;
  const dropodds::Request* odds = nullptr;  // kWalked: what the forms are worked out from
  const dropodds::Pick* pick = nullptr;     // kPicked
  std::map<uint32_t, int>* counted = nullptr;  // kCounted: by class and quality
  int refused = 0;         // drops not made
  int asked = 0;           // kAsked: items the drop core asked for
  // What the item maker puts in its requests for the source: the level its items get, its flags.
  bool seen = false;
  int32_t item_level = 0;
  uint32_t flags = 0;
  // The drop being made now: its request, its class, the quality asked for, whether it is one that would have
  // been refused, the form it is to take and what the request said before it was told so.
  const uint8_t* request = nullptr;
  bool audit = false;
  int32_t item_class = 0;
  int32_t quality = 0;
  bool said = false;
  dropodds::Force force;
  bool changed = false;
  int32_t name_was = 0;
  uint32_t flags_was = 0;
  // What came of the last one: the item as it was said to be.
  bool as_said = true;
};
thread_local Session* t_session = nullptr;
// The item an affix step is making while best affixes may upgrade its picks (the picker hook asks), else null.
thread_local Unit* t_best_item = nullptr;
// The item whose quality step ran last on this thread: the superior step, should the generator come to it, is that
// item's, and so is the automatic affix the generator picks last (pick_auto_affix). What the superior step's test was
// told (superior_fits), for the check when the item is made.
struct KindMark {
  Unit* item = nullptr;
  const uint8_t* request = nullptr;
  bool planned = false;           // the kinds were looked at (once a step)
  const uint8_t* rows = nullptr;  // the QualityItems rows of the item's bank
  uint32_t count = 0;
  uint32_t among = 0;             // how many of them the step picks among for the item, from the first on
  uint32_t fit = 0;               // a bit per kind of those that goes on the item
  uint32_t take = 0;              // ... and per kind to take (0: the game's pick stands)
  int first = -1;                 // the kind the game's roll came to first, of those that go on the item
  int taken = -1;                 // the kind the step was told yes for
  int refused = 0;                // kinds that go on the item and that the step was told no for
};
thread_local KindMark t_kind;
// ... and the kinds it picks among, as they were read.
thread_local uint8_t t_kind_rows[superior::kMaxKinds * superior::kRowSize];

// The compiled Items.txt row (Weapons, Armor and Misc in one table).
constexpr uint32_t kItemsRowSize = 0x1C0;
constexpr uintptr_t kItemsQualityLevel = 0x10D;  // u8: the base's own level (qlvl)
constexpr uintptr_t kItemsType = 0x12E;  // int16: the item's type (the one the rare step asks)
constexpr int16_t kItemsTypeGold = 4;    // ... of gold (the loot filter's own test for it)
constexpr uintptr_t kItemsUnique = 0x139;  // u8: the item must be unique
constexpr uintptr_t kItemsQuest = 0x13A;
constexpr uintptr_t kItemsMagicLevel = 0x150;  // u8: added to an item's affix level (wands, staves, orbs, circlets)
// A superior item's kind: how often an item that came out otherwise is put up with.
constexpr LONG kKindMissLimit = 3;
// The compiled MonStats row (the game's own field table, 0x39DB30, and the loader's compiled rows agree).
constexpr uint32_t kMonStatsRowSize = 0x1FC;
constexpr uintptr_t kMonStatsFlags = 0x3C;   // u32: a bit per flag column
constexpr uint32_t kMonStatsBoss = 1u << 6;  // the one the game's own boss test reads
constexpr uintptr_t kMonStatsRarity = 0x64;  // u8: the row's weight when an area's monsters are picked, 0 never
// The item generator's request.
constexpr uintptr_t kGameBank = 0x106;       // u8: the data tables a game's items come from
constexpr uintptr_t kRequestSource = 0x00;   // the unit the item is made for or beside (a drop's: what drops it)
constexpr uintptr_t kRequestLevel = 0x18;    // int32: the level the item gets
constexpr uintptr_t kRequestClass = 0x20;    // int32: the Items row
constexpr uintptr_t kRequestMode = 0x24;     // int32: 3 on the ground
constexpr int32_t kModeGround = 3;
constexpr uintptr_t kRequestVersion = 0x3A;  // u16: 100 and up an expansion item
constexpr uintptr_t kRequestRestore = 0x3C;  // nonzero: an existing item is made again (cube upgrades, restores)
constexpr uintptr_t kRequestQuality = 0x40;  // the quality asked for (0: rolled)
constexpr uintptr_t kRequestName = 0x50;     // int32: the file index asked for (a unique or set row + 1, 0 none)
constexpr uintptr_t kRequestFlags = 0xC0;    // 2 never ethereal, 4 ethereal for sure, 8 no sockets, 0x10 sockets
constexpr uintptr_t kRequestPrefixIds = 0xA8;  // int32[3]: the affix ids asked for, one per prefix the step makes
constexpr uintptr_t kRequestSuffixIds = 0xB4;  // ... and per suffix (the magic step's -1 forbids that side)
constexpr int kAffixSlots = 3;               // an item's prefix slots, and its suffix slots
constexpr uint32_t kRequestEthereal = 0x4;
constexpr uint32_t kRequestSockets = 0x10;
// The drop core's own cap when it is given no array; a pass never asks for more.
constexpr int kPassMax = 6;
// What a drop session's search may cost. It ends when nothing more is wanted; after this many items in a row
// that were made and cannot be kept (the filter hides them, or they are quest items), or as many passes in a row
// that keep nothing (a pass whose drops the filter refused makes none); after this many passes in a row that
// neither make nor refuse anything; at the most items it may make, which goes by how many are wanted, and at the
// most passes; and at its time (the log's own time left out). A champion, unique, boss or Herald is killed
// seldom and its treasure class has no gold to fall back on, so its search goes on longer, and three times as
// long again while the drop has nothing to show yet: such a kill seldom comes out empty.
constexpr int kPatience = 200;
constexpr int kPatienceElite = 300;
constexpr int kPatienceEliteFirst = 900;
constexpr int kPatiencePasses = 64;
constexpr int kBudgetItems = 300;      // the most items a session's passes make: this
constexpr int kBudgetPerWanted = 100;  // ... and this for each item wanted
constexpr int kBudgetPassesPerItem = 10;  // the most passes: this many for each item it may make
constexpr int64_t kBudgetUs = 30000;   // the longest a session's passes take
// A pass goes through the treasure class's picks in their order and stops at the most items it may make: a
// unique's is its item, then four potions. While the filter hides what the later picks make, a pass stops after
// the last pick that has given a keeper, so the tries go to the picks that can; every so many passes one goes
// through them all.
constexpr int kProbeEvery = 8;
// The loot filter asked before an item is made: the most Items rows a bank has, how often a drop that would be
// refused is made all the same (the game's own test of the real item then says whether refusing was right; often
// while a filter's answers are new, seldom once they have held), and how many answers may turn out wrong before
// the question is not asked any more.
constexpr uint32_t kMaxClasses = 2048;
constexpr LONG kAuditEvery = 32;     // ... for the first kAuditsOften of a filter's answers
constexpr LONG kAuditsOften = 128;
constexpr LONG kAuditSeldom = 1024;  // ... and after them
constexpr LONG kMismatchLimit = 3;
// Drops picked among what the filter shows: a session's picks end after this many in a row that gave nothing to
// keep, or that the game made no item for (no room beside the source); and when more than one in so many of the
// items made in a form that was worked out have come out otherwise (after the first few), drops are rolled for
// again, for the session.
constexpr int kPickMisses = 16;
constexpr int kPickNothing = 3;
constexpr LONG kMissedAfter = 32;
constexpr LONG kMissedShare = 8;
// The quality a question to the drop core is asked with, so that it rolls none.
constexpr int32_t kAskQuality = 2;
// The console's dropcheck: the most walks it counts.
constexpr LONG kCheckWalksMost = 200000;

bool on(cheats::Kind k) { return cheats::enabled(k) && !cheats::why_not(k); }

int64_t now_us() {
  static LARGE_INTEGER frequency{};
  if (!frequency.QuadPart) QueryPerformanceFrequency(&frequency);
  LARGE_INTEGER t{};
  QueryPerformanceCounter(&t);
  return frequency.QuadPart > 0 ? t.QuadPart * 1000000 / frequency.QuadPart : 0;
}

// The Items.txt row of a class, through the loader (game thread only; null elsewhere).
uintptr_t items_row_of(uint8_t bank, uint32_t item_class) {
  if (!g_ctx || !g_tables || !g_tables->findRowById) return 0;
  if (bank < 1 || bank > 3) return 0;
  D2RL::DataTables::RowView view{};
  view.structSize = D2RL::DataTables::RowViewSize;
  if (g_tables->findRowById(g_ctx, static_cast<D2RL::DataTables::Bank>(bank), D2RL::DataTables::TableId::Items,
                            item_class, &view) != D2RL::DataTables::Result::Success ||
      !view.row)
    return 0;
  if (view.rowSize != kItemsRowSize) {
    if (log_once(&g_row_warned, LogLevel::kWarning))
      log_warn("loot: the Items rows are %u bytes, not %u - quest items are not recognised", view.rowSize,
               kItemsRowSize);
    return 0;
  }
  return reinterpret_cast<uintptr_t>(view.row);
}

uintptr_t items_row(Unit* item) {
  return item ? items_row_of(game::unit_table_bank(item), game::unit_class(item)) : 0;
}

// 1 a quest item, 0 not, -1 unknown.
int quest_item(Unit* item) {
  const uintptr_t row = items_row(item);
  uint8_t quest = 0;
  if (!row || !mem::read_safe(row + kItemsQuest, &quest)) return -1;
  return quest != 0 ? 1 : 0;
}

// Gold (the loot filter goes by its amount, which only the pile itself has).
bool gold(Unit* item) {
  const uintptr_t row = items_row(item);
  int16_t type = 0;
  return row && mem::read_safe(row + kItemsType, &type) && type == kItemsTypeGold;
}

// A unique the player never saw may drop again this game: its "dropped" bit is cleared the way the
// game's own cube code clears it.
void forget_unique(void* game, Unit* item) {
  const int bits = sites::unique_bits_offset();
  if (!bits || game::item_quality(item) != game::kQualityUnique) return;
  const int32_t row = game::item_file_index(item);
  if (row < 0 || row > 0x1000) return;
  const uintptr_t at = reinterpret_cast<uintptr_t>(game) + static_cast<uintptr_t>(bits) + static_cast<uintptr_t>(row >> 5) * 4;
  uint32_t word = 0;
  if (mem::read_safe(at, &word)) mem::store(at, word & ~(1u << (row & 31)));
}

bool take_back(void* game, Unit* item) {
  forget_unique(game, item);
  return game::remove_ground_item(game, item);
}

// A boss by the game's data (the flag its own boss test reads): the act bosses and their Uber versions, Diablo
// Clone, Blood Raven, Radament, the Summoner, Izual, Griswold, Nihlathak, the colossal Ancients. The data marks the
// Putrid Defilers too, ordinary monsters for all that: a row an area's monsters are picked from (it has a rarity)
// is not counted. The row comes through the loader (game thread only).
bool boss(Unit* monster) {
  if (!g_ctx || !g_tables || !g_tables->getRow) return false;
  const uint8_t bank = game::unit_table_bank(monster);
  if (bank < 1 || bank > 3) return false;
  D2RL::DataTables::RowView view{};
  view.structSize = D2RL::DataTables::RowViewSize;
  const uint32_t cls = game::unit_class(monster);
  const D2RL::DataTables::Result r = g_tables->getRow(g_ctx, static_cast<D2RL::DataTables::Bank>(bank),
                                                      D2RL::DataTables::TableId::MonStats, cls, &view);
  if (r != D2RL::DataTables::Result::Success || !view.row || view.rowSize != kMonStatsRowSize) {
    if (log_once(&g_monstats_warned, LogLevel::kWarning))
      log_warn("loot: no MonStats row of %u bytes for monster class %u (result %u, %u bytes) - bosses are not "
               "recognised",
               kMonStatsRowSize, cls, static_cast<unsigned>(r), view.rowSize);
    return false;
  }
  const uintptr_t row = reinterpret_cast<uintptr_t>(view.row);
  uint32_t flags = 0;
  uint8_t rarity = 1;
  return mem::read_safe(row + kMonStatsFlags, &flags) && mem::read_safe(row + kMonStatsRarity, &rarity) &&
         (flags & kMonStatsBoss) != 0 && rarity == 0;
}

// The kills whose drops tend to be better, for the extra drops kept to them: a boss, a terror zone's Herald, a
// unique or super unique, a champion. Null for the rest: an ordinary monster, a unique's or a Herald's minion (they
// drop as ordinary monsters do), and whatever is no monster (a chest, a barrel). The game's champion maker
// (0x49AB40) gives a champion the unique bit too, and its super unique maker both of its own: the narrower name
// is asked first.
const char* elite(Unit* source, uint16_t* kind) {
  *kind = 0;
  if (game::unit_type(source) != game::kMonster) return nullptr;
  *kind = game::monster_type_flags(source);
  if (boss(source)) return "boss";
  if (*kind & (game::kMinion | game::kHeraldMinion)) return nullptr;
  if (*kind & game::kHerald) return "Herald";
  if (*kind & game::kSuperUnique) return "super unique";
  if (*kind & game::kChampion) return "champion";
  if (*kind & game::kUnique) return "unique";
  return nullptr;
}

// --- the loot filter asked before an item is made ---
//
// What the filter says of an item goes by three things only: its class, its quality, and whether it is ethereal
// or socketed (the filter's test and rule matcher read nothing else of an item; gold is the exception, judged by
// its amount). So the filter can be asked about an item before the game makes it, and a drop it would hide
// whatever it became need not be made, shown to the filter and taken off the ground again.

// A bit per quality (1 low .. 8 crafted) and ethereal-or-socketed.
constexpr uint32_t verdict_bit(int32_t quality, bool es) { return 1u << ((quality - 1) * 2 + (es ? 1 : 0)); }
constexpr uint32_t verdict_bits(int32_t quality) { return 3u << ((quality - 1) * 2); }

// What an item asked for with a quality may come out as, ethereal or socketed or not: that quality; rare for a
// unique that cannot be made; and magic, superior, normal and low for whatever fails further or must be something
// else (the generator's steps fall 7 -> 6 -> 4 -> 3 -> 2, 5 -> 4 -> 3 -> 2, 6 -> 4 -> 3 -> 2; an item type that is
// always magic, never rare or only normal; all superior). An item that must be unique is never refused.
constexpr uint32_t may_become(int32_t quality) {
  return verdict_bits(1) | verdict_bits(2) | verdict_bits(3) | verdict_bits(4) | verdict_bits(quality) |
         (quality == game::kQualityUnique ? verdict_bits(game::kQualityRare) : 0);
}

struct Verdict {
  uint32_t generation = 0;  // the filter generation it was asked in (0 never)
  uint16_t shown = 0;       // verdict_bit: the filter shows the class at that quality
  bool asked = false;       // the filter was asked: not for gold (it goes by the amount) or an unknown row
  bool always = true;       // never refused by what a quality may become: those, an item that must be unique, a
                            // quest item
};
Verdict g_verdicts[4][kMaxClasses];  // by bank and class; game thread only

const Verdict& verdict(uint8_t bank, uint32_t item_class) {
  static const Verdict kUnknown;
  if (bank < 1 || bank > 3 || item_class >= kMaxClasses) return kUnknown;
  Verdict& v = g_verdicts[bank][item_class];
  const uint32_t generation = static_cast<uint32_t>(g_filter_generation);
  if (v.generation == generation) return v;
  v = Verdict{};
  v.generation = generation;
  const uintptr_t row = items_row_of(bank, item_class);
  int16_t type = 0;
  uint8_t unique = 1, quest = 1;
  if (!row || !mem::read_safe(row + kItemsType, &type) || !mem::read_safe(row + kItemsUnique, &unique) ||
      !mem::read_safe(row + kItemsQuest, &quest) || type == kItemsTypeGold)
    return v;
  for (int32_t quality = 1; quality <= 8; ++quality)
    for (int es = 0; es < 2; ++es)
      if (game::loot_filter_would_show(bank, item_class, quality, es != 0))
        v.shown = static_cast<uint16_t>(v.shown | verdict_bit(quality, es != 0));
  v.asked = true;
  v.always = unique || quest;
  return v;
}

// An answer the game's own test of a real item did not bear out: asked again from now on, and after a few of
// them not asked at all. `says` is what the real item turned out to be for it ("the filter shows it").
void mismatch(const char* what, uint32_t item_class, int32_t asked, int32_t quality, bool es, const char* says) {
  InterlockedIncrement(&g_filter_generation);
  const LONG n = InterlockedIncrement(&g_mismatches);
  if (n <= kMismatchLimit)
    log_warn("loot: asking the loot filter first: %s (item class %u asked for with quality %d came out quality %d, "
             "%s; %s)",
             what, item_class, asked, quality, es ? "ethereal or socketed" : "neither ethereal nor socketed", says);
  if (n >= kMismatchLimit && !InterlockedExchange(&g_ask_first_off, 1))
    log_warn("loot: asking the loot filter first is off for this session - every drop is made and then shown to the "
             "filter (and the Chronicle), as before");
}

const char* filter_says(bool shows) { return shows ? "the filter shows it" : "the filter hides it"; }

// An item made whatever is asked, never refused: one that must be unique, a quest item (and one whose row cannot be
// read).
bool always_made(uint8_t bank, uint32_t item_class) {
  const uintptr_t row = items_row_of(bank, item_class);
  uint8_t unique = 1, quest = 1;
  return !row || !mem::read_safe(row + kItemsUnique, &unique) || !mem::read_safe(row + kItemsQuest, &quest) ||
         unique != 0 || quest != 0;
}

// A drop of the session's that is about to be made: its class and the quality asked for, or false.
bool session_drop(const Session& s, const uint8_t* request, int32_t* item_class, int32_t* quality) {
  const uintptr_t at = reinterpret_cast<uintptr_t>(request);
  uintptr_t source = 0;
  int32_t mode = 0, restore = 1;
  uint16_t version = 0;
  if (!request || !mem::read_safe(at + kRequestSource, &source) || !mem::read_safe(at + kRequestMode, &mode) ||
      !mem::read_safe(at + kRequestRestore, &restore) || !mem::read_safe(at + kRequestVersion, &version) ||
      !mem::read_safe(at + kRequestClass, item_class) || !mem::read_safe(at + kRequestQuality, quality))
    return false;
  // An expansion item only: a classic one's quality is rolled again whatever was asked.
  return source == reinterpret_cast<uintptr_t>(s.source) && mode == kModeGround && restore == 0 && version >= 100 &&
         *item_class >= 0 && *quality >= game::kQualityInferior && *quality <= game::kQualityUnique;
}

// What the item maker puts in every request for the source: the first of a session's says.
void note_request(Session* s, const uint8_t* request) {
  if (s->seen) return;
  const uintptr_t at = reinterpret_cast<uintptr_t>(request);
  int32_t level = 0;
  uint32_t flags = 0;
  if (!mem::read_safe(at + kRequestLevel, &level) || !mem::read_safe(at + kRequestFlags, &flags)) return;
  s->seen = true;
  s->item_level = level;
  s->flags = flags & dropodds::kRequestSet29;
}

// The form the item is to take, said to the generator through its request: the row asked for by name, and whether
// it is to be ethereal or have sockets. The request gets its own back once the item is made.
void say(Session* s, uint8_t* request, const dropodds::Force& force) {
  s->said = true;
  s->force = force;
  if (!force.name && !force.set && !force.clear) return;
  const uintptr_t at = reinterpret_cast<uintptr_t>(request);
  if (!mem::read_safe(at + kRequestName, &s->name_was) || !mem::read_safe(at + kRequestFlags, &s->flags_was)) {
    s->said = false;
    return;
  }
  s->changed = true;
  if (force.name) mem::store(at + kRequestName, force.name);
  mem::store(at + kRequestFlags, (s->flags_was & ~force.clear) | force.set);
}

// An item made in a form that was worked out came out otherwise. Too many of them, and drops are rolled for again.
void missed(uint32_t item_class, int32_t asked, const dropodds::Force& force, int32_t quality, bool ethereal,
            bool socketed, int32_t row) {
  InterlockedIncrement(&g_missed_game);
  const LONG n = InterlockedIncrement(&g_missed);
  if (overlay::settings().trace || log_first(&g_missed_logged, 4, LogLevel::kWarning))
    log_warn("loot: item class %u asked for with quality %d was to come out with quality %d%s%s (row %d) and came "
             "out with quality %d%s%s (row %d) - that form of it is not picked again for this drop",
             item_class, asked, force.quality,
             force.ethereal < 0 ? "" : force.ethereal ? ", ethereal" : ", not ethereal",
             force.sockets < 0 ? "" : force.sockets ? ", socketed" : ", without sockets", force.row, quality,
             ethereal ? ", ethereal" : "", socketed ? ", socketed" : "", row);
  const LONG made = g_picks;
  if (made >= kMissedAfter && n * kMissedShare > made && !InterlockedExchange(&g_odds_off, 1))
    log_warn("loot: %ld of the %ld items made in a form that was worked out came out otherwise - drops are rolled "
             "for until the loot filter shows one, as before, for this session",
             static_cast<long>(n), static_cast<long>(made));
}

// A treasure class of the plugin's own, for the drop core to walk in place of the game's: a copy of one of the
// game's records (its name is what the game's debug trace would print) with one entry, one pick, no NoDrop, no
// mods and no condition.
struct OwnClass {
  alignas(8) uint8_t record[dropodds::kTcSize] = {};
  alignas(8) uint8_t entry[dropodds::kEntrySize] = {};
  uint8_t condition = 0;  // what the game's record says its condition is

  template <typename T>
  void set(uintptr_t offset, T value) {
    std::memcpy(record + offset, &value, sizeof(T));
  }

  bool from(uintptr_t tc) {
    if (!mem::copy_from(record, tc, sizeof(record))) return false;
    std::memset(entry, 0, sizeof(entry));
    condition = record[dropodds::kTcFlags2];
    set<int32_t>(dropodds::kTcTotalClassic, 1);
    set<int32_t>(dropodds::kTcTotal, 1);
    set<int32_t>(dropodds::kTcPicks, -1);  // the entries in their order: nothing is rolled for the one
    set<int32_t>(dropodds::kTcNoDrop, 0);
    record[dropodds::kTcFlags] = 0;
    record[dropodds::kTcFlags2] = 0;
    std::memset(record + dropodds::kTcMods, 0, sizeof(uint16_t) * dropodds::kMods);
    set<uintptr_t>(dropodds::kTcEntries, reinterpret_cast<uintptr_t>(entry));
    set<uint64_t>(dropodds::kTcCount, 1);
    return true;
  }
};

// What only the drop core can say, asked of it with a treasure class of the plugin's own and the item creation
// hook refusing what it then asks the item maker for: an item asked for is a yes. And the one item of a pick made.
struct Asker {
  TcDropCoreFn original = nullptr;
  Session* session = nullptr;
  void* game = nullptr;
  Unit* source = nullptr;
  Unit* killer = nullptr;
  const void* tc = nullptr;
  int32_t item_level = 0;
  int questions = 0;
  std::vector<std::pair<uintptr_t, bool>> held;  // the treasure classes asked about
  std::vector<std::pair<int32_t, bool>> calcs;   // ... and the game's conditions

  bool ask(OwnClass& own) {
    Session& s = *session;
    const Making was = s.making;
    s.making = Making::kAsked;
    s.asked = 0;
    Unit* got[1] = {};
    int32_t count = 0;
    original(game, source, killer, own.record, kAskQuality, item_level, 1, got, &count, 1);
    s.making = was;
    ++questions;
    if (count > 0 && got[0]) take_back(game, got[0]);  // none is made
    return s.asked > 0;
  }

  // Whether the item maker finds room beside the source at all (its request says what every request for the
  // source says); without, nothing can be asked.
  bool room() {
    OwnClass own;
    return own.from(reinterpret_cast<uintptr_t>(tc)) && ask(own) && session->seen;
  }

  // Whether a treasure class's own condition holds for the source.
  bool holds(uintptr_t nested) {
    for (const auto& h : held)
      if (h.first == nested) return h.second;
    OwnClass own;
    if (!own.from(nested)) return false;
    own.record[dropodds::kTcFlags] = dropodds::kTcConditional;
    own.record[dropodds::kTcFlags2] = own.condition;
    const bool yes = ask(own);
    held.emplace_back(nested, yes);
    return yes;
  }

  // Whether the drop core takes an entry with a condition (an item's own).
  bool takes(const uint8_t* entry) {
    OwnClass own;
    if (!own.from(reinterpret_cast<uintptr_t>(tc))) return false;
    own.record[dropodds::kTcFlags] = dropodds::kTcHasConditional;
    std::memcpy(own.entry, entry, sizeof(own.entry));
    std::memset(own.entry + dropodds::kEntryStartClassic, 0, 8);
    return ask(own);
  }

  // Whether a condition of the game's (a calc) holds for the source: 1 or 0.
  int calc(int32_t id) {
    for (const auto& c : calcs)
      if (c.first == id) return c.second ? 1 : 0;
    OwnClass own;
    if (!own.from(reinterpret_cast<uintptr_t>(tc))) return 0;
    own.record[dropodds::kTcFlags] = dropodds::kTcConditional;
    own.record[dropodds::kTcFlags2] = dropodds::kTcCalcCondition;
    own.set<int32_t>(dropodds::kTcCalc, id);
    const bool yes = ask(own);
    calcs.emplace_back(id, yes);
    return yes ? 1 : 0;
  }

  // The item of a pick: the game's entry alone in a treasure class, the quality asked for with it, and the item
  // creation hook saying the rest.
  Unit* make(const dropodds::Pick& pick) {
    OwnClass own;
    if (!own.from(reinterpret_cast<uintptr_t>(tc))) return nullptr;
    std::memcpy(own.entry, pick.entry, sizeof(own.entry));
    std::memcpy(own.record + dropodds::kTcMods, pick.mods, sizeof(pick.mods));
    Session& s = *session;
    const Making was = s.making;
    s.making = Making::kPicked;
    s.pick = &pick;
    s.as_said = true;
    Unit* got[1] = {};
    int32_t count = 0;
    original(game, source, killer, own.record, pick.quality, item_level, 1, got, &count, 1);
    s.making = was;
    s.pick = nullptr;
    return count > 0 ? got[0] : nullptr;
  }
};

// What the odds are worked out from, for a session's treasure class: what the loot filter shows of each class, and
// the unique and set rows the Chronicle misses (chronicle.cpp read them for the session).
void odds_of(dropodds::Request* odds, void* game, Unit* source, Unit* killer, const void* tc, int32_t forced_quality,
             int32_t item_level, uint8_t bank, bool filter, bool chronicle) {
  odds->game = game;
  odds->source = source;
  odds->killer = killer;
  odds->tc = reinterpret_cast<uintptr_t>(tc);
  odds->roll_level = item_level;
  odds->forced_quality = forced_quality;
  odds->bank = bank;
  odds->all_superior = on(cheats::kAllSuperior);
  odds->all_ethereal = on(cheats::kAllEthereal);
  odds->all_socketed = on(cheats::kAllSocketed);
  odds->filter = filter;
  if (filter)
    odds->shown = [bank](uint32_t item_class, uint16_t* shown) {
      const Verdict& v = verdict(bank, item_class);
      if (!v.asked) return false;
      *shown = v.shown;
      return true;
    };
  odds->uniques = chronicle ? &chronicle::missing_uniques() : nullptr;
  odds->sets = chronicle ? &chronicle::missing_sets() : nullptr;
}

// ... and what only the drop core can say, once it can be asked (not from inside a walk of its own).
void ask_with(dropodds::Request* odds, Asker* asker) {
  odds->holds = [asker](uintptr_t nested) { return asker->holds(nested); };
  odds->takes = [asker](uintptr_t, const uint8_t* entry) { return asker->takes(entry); };
  odds->calc = [asker](int32_t id) { return asker->calc(id); };
}

// The console's dropcheck: the treasure class walked by the drop core itself, with nothing made, and what it asked
// the item maker for counted against what the odds say a walk asks for.
void check(TcDropCoreFn original, void* game, Unit* source, Unit* killer, const void* tc, int32_t forced_quality,
           int32_t item_level, int walks) {
  const LogAlways always;  // asked for by name: written with the log switched off too
  Session session;
  session.source = source;
  mem::read_safe(reinterpret_cast<uintptr_t>(game) + kGameBank, &session.bank);
  std::map<uint32_t, int> counted;
  session.making = Making::kCounted;
  session.counted = &counted;
  Session* const outer = t_session;
  ++t_depth;
  t_session = &session;
  const int64_t began = now_us();
  for (int i = 0; i < walks; ++i) {
    Unit* got[kPassMax] = {};
    int32_t count = 0;
    original(game, source, killer, tc, forced_quality, item_level, 1, got, &count, kPassMax);
    for (int k = 0; k < count && k < kPassMax; ++k)
      if (got[k]) take_back(game, got[k]);  // none is made
  }
  const int64_t took = now_us() - began;
  session.counted = nullptr;
  session.making = Making::kRolled;
  Asker asker{original, &session, game, source, killer, tc, item_level};
  dropodds::Request odds;
  odds_of(&odds, game, source, killer, tc, forced_quality, item_level, session.bank, false, false);
  ask_with(&odds, &asker);
  if (!session.seen) asker.room();
  odds.item_level = session.item_level;
  odds.flags = session.flags;
  std::vector<dropodds::Expected> expected;
  const bool worked_out = session.seen && dropodds::expected(odds, &expected);
  t_session = outer;
  --t_depth;
  char name[48];
  dropodds::name_of(reinterpret_cast<uintptr_t>(tc), name, sizeof(name));
  if (!worked_out) {
    log_warn("dropcheck: monster class %u, treasure class '%s': %d walks counted, but what it drops cannot be "
             "worked out (%s)",
             game::unit_class(source), name, walks,
             session.seen ? "a classic game, or tables or a treasure class that cannot be read"
                          : "the item maker found no room beside the monster");
    return;
  }
  static const char* const kQuality[8] = {"?", "low", "normal", "superior", "magic", "set", "rare", "unique"};
  double said_all = 0, walked_all = 0, said_by[8] = {}, walked_by[8] = {};
  std::map<uint32_t, double> said;
  for (const dropodds::Expected& e : expected) {
    const double n = e.count * walks;
    said[e.item_class << 4 | static_cast<uint32_t>(e.quality & 7)] += n;
    said_all += n;
    said_by[e.quality & 7] += n;
  }
  double unexpected = 0;
  for (const auto& c : counted) {
    walked_all += c.second;
    walked_by[c.first & 7] += c.second;
    if (!said.count(c.first)) unexpected += c.second;
  }
  // A count varies as its root at most (less for a pick that is sure to come).
  double worst = 0, worst_said = 0, worst_walked = 0;
  uint32_t worst_kind = 0;
  int never = 0, kinds = 0;
  for (const auto& e : said) {
    if (e.second < 5) continue;
    ++kinds;
    const auto c = counted.find(e.first);
    const double walked = c == counted.end() ? 0 : c->second;
    if (walked == 0) ++never;
    const double off = std::fabs(walked - e.second) / std::sqrt(e.second);
    if (off > worst) worst = off, worst_kind = e.first, worst_said = e.second, worst_walked = walked;
  }
  logf("dropcheck: monster class %u, treasure class '%s' (item level %d, the items' level %d, quality asked %d): %d "
       "walks in %ld ms, %d questions to the drop core",
       game::unit_class(source), name, item_level, session.item_level, forced_quality, walks,
       static_cast<long>(took / 1000), asker.questions);
  logf("dropcheck: a walk asks for %.4f items as worked out, %.4f as walked; %zu kinds of item and quality worked "
       "out, %zu walked",
       said_all / walks, walked_all / walks, said.size(), counted.size());
  for (int q = 7; q >= 1; --q)
    if (said_by[q] > 0 || walked_by[q] > 0)
      logf("dropcheck:   %-8s %10.1f worked out, %8.0f walked", kQuality[q], said_by[q], walked_by[q]);
  logf("dropcheck: of the %d kinds worked out to come 5 times or more, the one furthest off is item class %u as %s: "
       "%.1f worked out, %.0f walked (%.1f times what chance allows); %d of them never came; %.0f items walked "
       "were of kinds not worked out",
       kinds, worst_kind >> 4, kQuality[worst_kind & 7], worst_said, worst_walked, worst, never, unexpected);
  const double share = said_all > 0 ? walked_all / said_all : 0;
  if (worst > 6 || share < 0.98 || share > 1.02 || unexpected > walked_all * 0.002 || never > 0)
    log_warn("dropcheck: the odds worked out are not the drop core's - send the log");
  else
    logf("dropcheck: the odds worked out are the drop core's");
}

// The item's type (its Items row's, the one the rare step asks), -1 when unknown.
int item_type(Unit* item) {
  const uintptr_t row = items_row(item);
  int16_t type = -1;
  return row && mem::read_safe(row + kItemsType, &type) ? type : -1;
}

// The compiled affix row (MagicSuffix, MagicPrefix and AutoMagic alike, game::kAffixRowSize bytes).
constexpr uintptr_t kAffixName = 0x00;       // char[32]
constexpr size_t kAffixNameSize = 32;
constexpr uintptr_t kAffixVersion = 0x22;    // u16: 100 and up only on expansion items
constexpr uintptr_t kAffixMods = 0x24;       // three {int32 property, param, min, max}; a property below 0 unused
constexpr uintptr_t kAffixModSize = 0x10;
constexpr int kAffixModCount = 3;
constexpr uintptr_t kAffixSpawnable = 0x54;  // u8
constexpr uintptr_t kAffixLevel = 0x58;      // int32: the lowest affix level it comes at
constexpr uintptr_t kAffixGroup = 0x5C;      // int32: an item has one affix of a group at most
constexpr uintptr_t kAffixMaxLevel = 0x60;   // int32: the highest (0 none)
constexpr uintptr_t kAffixRare = 0x64;       // u8: may go on rare, crafted and tempered items
constexpr uintptr_t kAffixClass = 0x66;      // u8: the class it is kept to (0xFF none)
constexpr uintptr_t kAffixFrequency = 0x82;  // u8: 0 never picked
// The compiled Properties row: seven {function, stat, value} slots (a function of 0 unused, a stat of 0xFFFF none).
constexpr uint32_t kPropertyRowSize = 0x30;
constexpr uintptr_t kPropertyVal = 0x0A;     // u16[7]
constexpr uintptr_t kPropertyFunc = 0x18;    // u8[7]
constexpr uintptr_t kPropertyStat = 0x20;    // u16[7]
constexpr int kPropertySlots = 7;
constexpr uint16_t kNoStat = 0xFFFF;
// The item qualities whose picks take only rows marked rare: rare, crafted and tempered (the picker's own mask).
constexpr uint32_t kRareOnlyQualities = 0x340;
// The most rows of one group and stats the upgrade looks at (the largest family, life on all its bases, has 39).
constexpr int kMaxFamily = 64;

// Which side the next affix goes to: a coin, as in the game's own loop (not the item's seed: nothing replays it).
bool coin() {
  thread_local uint64_t s = 0;
  if (!s) {
    LARGE_INTEGER t{};
    QueryPerformanceCounter(&t);
    s = (static_cast<uint64_t>(t.QuadPart) ^ 0x9E3779B97F4A7C15ull) * (GetCurrentThreadId() | 1u);
    if (!s) s = 1;
  }
  s ^= s << 13;
  s ^= s >> 7;
  s ^= s << 17;
  return (s >> 40) & 1;
}

// A fresh item: not an existing item made again (it keeps the affixes it had), not a quest item (or one whose row
// is unknown), not one of the game's display items.
bool fresh(Unit* item, const uint8_t* request) {
  if (!item || !request) return false;
  int32_t restore = 1;
  if (!mem::read_safe(reinterpret_cast<uintptr_t>(request) + kRequestRestore, &restore) || restore != 0) return false;
  return quest_item(item) == 0 && (game::item_flags(item) & game::kItemPreview) == 0;
}

// A fresh item the switch can give more affixes.
bool maxing(Unit* item, const uint8_t* request) {
  return on(cheats::kMaxAffixes) && game::has_affix_picker() && fresh(item, request);
}

// While an affix step runs (max affixes' additions included), the picker hook may upgrade the picks for the item it
// makes: a fresh expansion item (a classic one is picked by the classic picker, under other rules).
class BestAffixScope {
 public:
  BestAffixScope(Unit* item, const uint8_t* request) : saved_(t_best_item) {
    t_best_item = on(cheats::kBestAffixes) && game::item_version(item) != 0 && fresh(item, request) ? item : nullptr;
  }
  ~BestAffixScope() { t_best_item = saved_; }
  BestAffixScope(const BestAffixScope&) = delete;
  BestAffixScope& operator=(const BestAffixScope&) = delete;

 private:
  Unit* saved_;
};

// The id the request asks for as the side's next affix (the step's own walk through the request: one per affix made).
int32_t forced_id(const uint8_t* request, bool prefix, int index) {
  int32_t id = 0;
  if (index < 0 || index >= kAffixSlots) return 0;
  const uintptr_t at = reinterpret_cast<uintptr_t>(request) + (prefix ? kRequestPrefixIds : kRequestSuffixIds) +
                       static_cast<uintptr_t>(index) * sizeof(int32_t);
  return mem::read_safe(at, &id) ? id : 0;
}

struct Side {
  int count = 0;  // affixes on the side
  int free = -1;  // its first empty slot, -1 none
};

bool read_side(Unit* item, bool prefix, Side* out) {
  *out = Side{};
  for (int i = 0; i < kAffixSlots; ++i) {
    const int id = game::affix_id(item, prefix, i);
    if (id < 0) return false;
    if (id) ++out->count;
    else if (out->free < 0) out->free = i;
  }
  return true;
}

// One more affix on a side, the way the step picks one: the request's id for the side's next affix, the game's
// picker (which applies the affix's properties), the id in the side's first empty slot. 1 when it added one, 0 when
// nothing fits, -1 when the slots cannot be read back.
int add_affix(Unit* item, const uint8_t* request, bool prefix, Side* side) {
  if (side->free < 0) return 0;
  const int id = game::pick_affix(item, prefix, forced_id(request, prefix, side->count));
  if (!id) return 0;
  game::set_affix(item, prefix, side->free, id);
  return read_side(item, prefix, side) ? 1 : -1;
}

// The rare and crafted steps' loop, carried on until the item has `target` affixes: a coin for the side unless one
// side is full or has nothing left to pick, at most three a side.
int top_up(Unit* item, const uint8_t* request, int target, int* before) {
  Side pre, suf;
  if (!read_side(item, true, &pre) || !read_side(item, false, &suf)) return 0;
  *before = pre.count + suf.count;
  bool pre_open = pre.free >= 0, suf_open = suf.free >= 0;
  int added = 0;
  while (pre.count + suf.count < target && (pre_open || suf_open)) {
    const bool prefix = pre_open && (!suf_open || coin());
    Side& side = prefix ? pre : suf;
    bool& open = prefix ? pre_open : suf_open;
    const int r = add_affix(item, request, prefix, &side);
    if (r < 0) break;
    if (r == 0) {
      open = false;
      continue;
    }
    ++added;
    if (side.free < 0) open = false;
  }
  return added;
}

void note_affixes(Unit* item, const char* what, int before, int added, int target) {
  if (added <= 0) return;
  InterlockedExchangeAdd(&g_affixes, added);
  if (overlay::settings().trace || log_first(&g_affix_logged, 4))
    logf("loot: max affixes: %s item (class %u, level %d) had %d affix%s, %d added (at most %d)", what,
         game::unit_class(item), game::item_level(item), before, before == 1 ? "" : "es", added, target);
}

// --- best affixes ---

// The three kinds of rows in the affix table, each picked among on its own: suffixes, prefixes, and the automatic
// affixes (a base's own, of its Items row's AutoMagic group).
enum class Affix { kSuffix, kPrefix, kAutomatic };

const char* affix_word(Affix kind) {
  return kind == Affix::kPrefix ? "prefix" : kind == Affix::kSuffix ? "suffix" : "automatic affix";
}

template <typename T>
T field(const uint8_t* row, uintptr_t offset) {
  T v{};
  mem::read_safe(reinterpret_cast<uintptr_t>(row) + offset, &v);
  return v;
}

// The Properties rows of the item's bank, through the loader (game thread only).
struct PropertyRows {
  const uint8_t* rows = nullptr;
  uint32_t count = 0;
};

bool property_rows(Unit* item, PropertyRows* out) {
  if (!g_ctx || !g_tables || !g_tables->getTable) return false;
  const uint8_t bank = game::unit_table_bank(item);
  if (bank < 1 || bank > 3) return false;
  D2RL::DataTables::TableView view{};
  view.structSize = D2RL::DataTables::TableViewSize;
  if (g_tables->getTable(g_ctx, static_cast<D2RL::DataTables::Bank>(bank), D2RL::DataTables::TableId::Properties,
                         &view) != D2RL::DataTables::Result::Success ||
      !view.rows || !view.rowCount)
    return false;
  if (view.rowSize != kPropertyRowSize) {
    if (log_once(&g_property_warned, LogLevel::kWarning))
      log_warn("loot: the Properties rows are %u bytes, not %u - best affixes leaves the game's picks", view.rowSize,
               kPropertyRowSize);
    return false;
  }
  out->rows = static_cast<const uint8_t*>(view.rows);
  out->count = view.rowCount;
  return true;
}

// One stat an affix gives. What it is: the stat (or, for a property function with no stat such as dmg%, the
// function), the value a class-skills property names its class by, and the skill or skill tab the affix's param names
// (a param that is itself an amount - a socket count, a per-level rate, a length - is left out). How much: its range,
// as magnitudes, so an affix that lowers requirements gives more the lower it goes.
struct Part {
  uint32_t stat;
  uint32_t val;
  int32_t layer;
  int32_t lo, hi;
};
constexpr int kMaxParts = kAffixModCount * kPropertySlots;
struct Parts {
  Part p[kMaxParts];
  int n = 0;
};

bool part_before(const Part& a, const Part& b) {
  if (a.stat != b.stat) return a.stat < b.stat;
  if (a.val != b.val) return a.val < b.val;
  return a.layer < b.layer;
}

bool same_stats(const Parts& a, const Parts& b) {
  if (a.n != b.n) return false;
  for (int i = 0; i < a.n; ++i)
    if (a.p[i].stat != b.p[i].stat || a.p[i].val != b.p[i].val || a.p[i].layer != b.p[i].layer) return false;
  return true;
}

int32_t magnitude(int32_t v) { return v == INT32_MIN ? INT32_MAX : v < 0 ? -v : v; }

// The stats an affix row gives, sorted (rows that give the same stats line up part for part). Property functions 14
// (sockets) and 17 (a stat set to the param) take the param as the amount; 15 and 16 set a fixed min and max beside
// them (dmg-pois: min, max, and its length from the param).
bool affix_parts(const uint8_t* row, const PropertyRows& props, Parts* out) {
  out->n = 0;
  for (int m = 0; m < kAffixModCount; ++m) {
    const uint8_t* mod = row + kAffixMods + m * kAffixModSize;
    const int32_t prop = field<int32_t>(mod, 0), param = field<int32_t>(mod, 4);
    const int32_t lo = field<int32_t>(mod, 8), hi = field<int32_t>(mod, 12);
    if (prop < 0) continue;
    if (static_cast<uint32_t>(prop) >= props.count) return false;
    const uintptr_t p = reinterpret_cast<uintptr_t>(props.rows) + static_cast<uintptr_t>(prop) * kPropertyRowSize;
    uint8_t funcs[kPropertySlots] = {};
    uint16_t stats[kPropertySlots] = {}, vals[kPropertySlots] = {};
    if (!mem::copy_from(funcs, p + kPropertyFunc, sizeof(funcs)) || !mem::copy_from(stats, p + kPropertyStat, sizeof(stats)) ||
        !mem::copy_from(vals, p + kPropertyVal, sizeof(vals)))
      return false;
    bool param_is_amount = false;
    for (const uint8_t f : funcs) param_is_amount = param_is_amount || (f >= 14 && f <= 17);
    for (int j = 0; j < kPropertySlots; ++j) {
      if (!funcs[j]) continue;
      if (out->n >= kMaxParts) return false;
      int32_t a = lo, b = hi;
      if ((funcs[j] == 14 || funcs[j] == 17) && param) a = b = param;
      else if (funcs[j] == 15) b = lo;
      else if (funcs[j] == 16) a = hi;
      a = magnitude(a);
      b = magnitude(b);
      Part& part = out->p[out->n++];
      part.stat = stats[j] == kNoStat ? 0x10000u | funcs[j] : stats[j];
      part.val = vals[j];
      part.layer = param_is_amount ? 0 : param;
      part.lo = a < b ? a : b;
      part.hi = a < b ? b : a;
    }
  }
  std::sort(out->p, out->p + out->n, part_before);
  return true;
}

// What the picker checks of the item for every row.
struct ItemFacts {
  int alvl = 0;           // the affix level
  bool magic = false;     // the base has a magic level: a row's weight is its frequency times its level
  uint16_t version = 0;   // 100 and up: an expansion item
  bool rare_only = false;
};

// What the picker works out about the item (the same in the game's own picker, 0x58C940, and D2RCore's): the affix
// level from the item level (at least 1, and at least the base's own level), plus the base's magic level when it has
// one, else less half the base's level (twice the item level less 99 near the top); 1..99.
bool item_facts(Unit* item, ItemFacts* out) {
  const uintptr_t row = items_row(item);
  uint8_t base = 0, magic = 0;
  if (!row || !mem::read_safe(row + kItemsQualityLevel, &base) || !mem::read_safe(row + kItemsMagicLevel, &magic))
    return false;
  int level = game::item_level(item);
  if (level < 1) level = 1;
  if (level < base) level = base;
  int alvl = 0;
  if (magic) alvl = level + magic;
  else if (level < 99 - base / 2) alvl = level - base / 2;
  else alvl = 2 * level - 99;
  out->alvl = alvl < 1 ? 1 : alvl > 99 ? 99 : alvl;
  out->magic = magic != 0;
  out->version = game::item_version(item);
  const int32_t quality = game::item_quality(item);
  out->rare_only = quality >= 0 && quality < 10 && ((kRareOnlyQualities >> quality) & 1);
  return true;
}

// Whether the picker could have picked the row for the item, by the rules it asks of every row but the group and the
// class (the family shares those with the pick): spawnable when the step asks for spawnable rows, an expansion row
// only on an expansion item, the item's affix level inside the row's window, rare-only rows for rare, crafted and
// tempered items, the game's own item-type test, and a weight above 0 (a frequency, and a level when the base's
// magic level multiplies the weight by it): a row the picker could never land on is never kept.
bool could_pick(Unit* item, const uint8_t* row, const ItemFacts& it, bool spawnable_only) {
  if (spawnable_only && !field<uint8_t>(row, kAffixSpawnable)) return false;
  if (field<uint16_t>(row, kAffixVersion) >= 100 && it.version < 100) return false;
  const int32_t level = field<int32_t>(row, kAffixLevel), max_level = field<int32_t>(row, kAffixMaxLevel);
  if (level > it.alvl || (max_level && it.alvl > max_level)) return false;
  if (it.rare_only && !field<uint8_t>(row, kAffixRare)) return false;
  if (!field<uint8_t>(row, kAffixFrequency) || (it.magic && level < 1)) return false;
  return game::affix_fits(item, row);
}

// The rows of the pick's group and class among its kind (prefixes, suffixes or automatic affixes) that give the same
// stats are one affix in its versions. Of those the game could have picked for the item, the one that gives the most:
// the sum of its stats' top values, each against the most any version gives, then the same for the bottoms of the
// ranges. A tie keeps the game's own pick. Returns the id to keep (the pick when nothing beats it or the rows cannot
// be read).
int32_t best_affix(Unit* item, const game::AffixTable& table, int32_t picked, Affix kind, bool spawnable_only) {
  const uint32_t first = kind == Affix::kPrefix   ? table.prefixes
                         : kind == Affix::kSuffix ? table.suffixes
                                                  : table.automagic;
  const uint32_t last = kind == Affix::kPrefix   ? table.automagic
                        : kind == Affix::kSuffix ? table.prefixes
                                                 : table.count;
  const uint32_t index = static_cast<uint32_t>(picked - 1);
  if (index < first || index >= last) {
    if (log_once(&g_side_warned, LogLevel::kWarning))
      log_warn("loot: best affixes: the picker's %s %d is not among the table's %s rows (%u..%u) - left as picked",
               affix_word(kind), picked, affix_word(kind), first + 1, last);
    return picked;
  }
  const uint8_t* mine = table.rows + static_cast<size_t>(index) * game::kAffixRowSize;
  PropertyRows props;
  Parts want;
  ItemFacts it;
  if (!item_facts(item, &it) || !property_rows(item, &props) || !affix_parts(mine, props, &want) || !want.n)
    return picked;
  const int32_t group = field<int32_t>(mine, kAffixGroup);
  const uint8_t cls = field<uint8_t>(mine, kAffixClass);
  uint32_t family[kMaxFamily];
  int members = 0;
  int32_t top[kMaxParts] = {};
  Parts parts;
  for (uint32_t i = first; i < last && members < kMaxFamily; ++i) {
    const uint8_t* row = table.rows + static_cast<size_t>(i) * game::kAffixRowSize;
    if (field<int32_t>(row, kAffixGroup) != group || field<uint8_t>(row, kAffixClass) != cls) continue;
    if (!affix_parts(row, props, &parts) || !same_stats(parts, want)) continue;
    family[members++] = i;
    for (int k = 0; k < parts.n; ++k) top[k] = parts.p[k].hi > top[k] ? parts.p[k].hi : top[k];
  }
  const auto score = [&top](const Parts& ps, double* bottoms) {
    double tops = 0;
    *bottoms = 0;
    for (int k = 0; k < ps.n; ++k) {
      const double most = top[k] > 0 ? top[k] : 1;
      tops += ps.p[k].hi / most;
      *bottoms += ps.p[k].lo / most;
    }
    return tops;
  };
  constexpr double kEpsilon = 1e-9;
  double best_bottoms = 0;
  double best_tops = score(want, &best_bottoms);
  uint32_t best = index;
  for (int f = 0; f < members; ++f) {
    const uint32_t i = family[f];
    const uint8_t* row = table.rows + static_cast<size_t>(i) * game::kAffixRowSize;
    if (i == index || !affix_parts(row, props, &parts) || !could_pick(item, row, it, spawnable_only)) continue;
    double bottoms = 0;
    const double tops = score(parts, &bottoms);
    if (tops > best_tops + kEpsilon || (tops > best_tops - kEpsilon && bottoms > best_bottoms + kEpsilon)) {
      best = i;
      best_tops = tops;
      best_bottoms = bottoms;
    }
  }
  return static_cast<int32_t>(best + 1);
}

// An affix row's name, for the log (printable characters only).
void affix_name(const uint8_t* row, char (&out)[kAffixNameSize + 1]) {
  std::memset(out, 0, sizeof(out));
  if (!mem::copy_from(out, reinterpret_cast<uintptr_t>(row) + kAffixName, kAffixNameSize)) return;
  for (size_t i = 0; i < kAffixNameSize && out[i]; ++i)
    if (static_cast<unsigned char>(out[i]) < 0x20 || static_cast<unsigned char>(out[i]) > 0x7E) out[i] = '?';
}

void note_upgrade(Unit* item, const game::AffixTable& table, int32_t picked, int32_t kept, Affix kind) {
  InterlockedIncrement(&g_upgraded);
  if (!log_enabled() || (!overlay::settings().trace && !log_first(&g_upgrade_logged, 4))) return;
  char from[kAffixNameSize + 1], to[kAffixNameSize + 1];
  affix_name(table.row(picked), from);
  affix_name(table.row(kept), to);
  ItemFacts it;
  item_facts(item, &it);
  logf("loot: best affixes: %s %d \"%s\" (level %d) -> %d \"%s\" (level %d) on item class %u, affix level %d",
       affix_word(kind), picked, from, field<int32_t>(table.row(picked), kAffixLevel), kept, to,
       field<int32_t>(table.row(kept), kAffixLevel), game::unit_class(item), it.alvl);
}

// --- perfect rolls: a superior item's kind ---

static_assert(superior::kPropertyRowSize == kPropertyRowSize, "one Properties row");

// The QualityItems rows of the item's bank, through the loader (game thread only).
bool kind_rows(Unit* item, const uint8_t** rows, uint32_t* count) {
  if (!g_ctx || !g_tables || !g_tables->getTable) return false;
  const uint8_t bank = game::unit_table_bank(item);
  if (bank < 1 || bank > 3) return false;
  D2RL::DataTables::TableView view{};
  view.structSize = D2RL::DataTables::TableViewSize;
  if (g_tables->getTable(g_ctx, static_cast<D2RL::DataTables::Bank>(bank), D2RL::DataTables::TableId::QualityItems,
                         &view) != D2RL::DataTables::Result::Success ||
      !view.rows || !view.rowCount)
    return false;
  if (view.rowSize != superior::kRowSize || view.rowCount > static_cast<uint32_t>(superior::kMaxKinds)) {
    if (log_once(&g_kind_rows_warned, LogLevel::kWarning))
      log_warn("loot: QualityItems has %u rows of %u bytes, not at most %d of %u - a superior item's kind is left to "
               "the game",
               view.rowCount, view.rowSize, superior::kMaxKinds, superior::kRowSize);
    return false;
  }
  *rows = static_cast<const uint8_t*>(view.rows);
  *count = view.rowCount;
  return true;
}

// How many of the kinds the step picks among for the item, as the step itself counts them: the few for a base whose
// type is thrown or that has no durability. 0 when the rows cannot be read.
int kinds_picked_among(Unit* item, uint32_t kinds) {
  const sites::SuperiorFacts& facts = sites::superior_facts();
  const uintptr_t row = items_row(item);
  const uint8_t bank = game::unit_table_bank(item);
  int16_t type = -1;
  uint8_t no_durability = 0, thrown = 0;
  if (!row || !mem::read_safe(row + kItemsType, &type) || type < 0 ||
      !mem::read_safe(row + static_cast<uintptr_t>(facts.no_durability_at), &no_durability) || !g_tables->getRow)
    return 0;
  D2RL::DataTables::RowView view{};
  view.structSize = D2RL::DataTables::RowViewSize;
  if (g_tables->getRow(g_ctx, static_cast<D2RL::DataTables::Bank>(bank), D2RL::DataTables::TableId::ItemTypes,
                       static_cast<uint32_t>(type), &view) != D2RL::DataTables::Result::Success ||
      !view.row || view.rowSize <= static_cast<uint32_t>(facts.thrown_at) ||
      !mem::read_safe(reinterpret_cast<uintptr_t>(view.row) + static_cast<uintptr_t>(facts.thrown_at), &thrown))
    return 0;
  return superior::picked_among(static_cast<int>(kinds), facts.few, thrown != 0, no_durability != 0);
}

// The kinds the step picks among for a fresh item, each put to the game's own test, and the best of those that go
// on it (the mark keeps both). Whatever cannot be read leaves the mark without kinds to take: the game's pick stands.
void plan_kind(SuperiorFitsFn original, Unit* item, KindMark* mark) {
  if (!on(cheats::kPerfectRolls) || g_kinds_off || !sites::superior_facts().known || !fresh(item, mark->request))
    return;
  const uint8_t* rows = nullptr;
  uint32_t count = 0;
  PropertyRows props;
  if (!kind_rows(item, &rows, &count) || !property_rows(item, &props)) return;
  const int among = kinds_picked_among(item, count);
  uint8_t* const copy = t_kind_rows;
  if (among < 1 || !mem::copy_from(copy, reinterpret_cast<uintptr_t>(rows), static_cast<size_t>(among) * superior::kRowSize))
    return;
  uint8_t property[superior::kPropertyRowSize];
  const auto property_row = [&props, &property](int32_t id) -> const uint8_t* {
    if (static_cast<uint32_t>(id) >= props.count) return nullptr;
    const uintptr_t at = reinterpret_cast<uintptr_t>(props.rows) + static_cast<uintptr_t>(id) * kPropertyRowSize;
    return mem::copy_from(property, at, sizeof(property)) ? property : nullptr;
  };
  superior::Kind kinds[superior::kMaxKinds];
  uint32_t fit = 0;
  for (int k = 0; k < among; ++k) {
    kinds[k].fits = original(item, rows + static_cast<size_t>(k) * superior::kRowSize) != 0;
    if (!kinds[k].fits) continue;
    if (!superior::bonuses_of(copy + static_cast<size_t>(k) * superior::kRowSize, property_row, &kinds[k])) return;
    fit |= 1u << k;
  }
  mark->rows = rows;
  mark->count = count;
  mark->among = static_cast<uint32_t>(among);
  mark->fit = fit;
  mark->take = superior::best(kinds, among);
}

// Which of the kinds looked at a row of the step's is: by where it lies among the table's rows, or, should the step
// read its rows from elsewhere, by what it holds. -1 when it is none of them.
int kind_of(const KindMark& mark, const uint8_t* row) {
  const uintptr_t at = reinterpret_cast<uintptr_t>(row), first = reinterpret_cast<uintptr_t>(mark.rows);
  if (at >= first && at < first + static_cast<uintptr_t>(mark.count) * superior::kRowSize) {
    const uintptr_t kind = (at - first) / superior::kRowSize;
    return (at - first) % superior::kRowSize == 0 && kind < mark.among ? static_cast<int>(kind) : -1;
  }
  uint8_t held[superior::kRowSize];
  if (!mem::copy_from(held, at, sizeof(held))) return -1;
  for (uint32_t k = 0; k < mark.among; ++k)
    if (!std::memcmp(held, t_kind_rows + static_cast<size_t>(k) * superior::kRowSize, sizeof(held)))
      return static_cast<int>(k);
  return -1;
}

// The kinds of a mask, for the log ("2 4 7").
void kinds_text(uint32_t mask, char (&out)[40]) {
  size_t n = 0;
  out[0] = 0;
  for (int k = 0; k < superior::kMaxKinds && n + 4 < sizeof(out); ++k)
    if (mask & (1u << k)) n += static_cast<size_t>(std::snprintf(out + n, sizeof(out) - n, n ? " %d" : "%d", k));
}

// An item is made: what came of the superior step's test being told no. The mark is used up.
void check_kind(Unit* item) {
  const KindMark mark = t_kind;
  t_kind = KindMark{};
  if (!item || mark.item != item || !mark.refused) return;
  const int32_t quality = game::item_quality(item), kind = game::item_file_index(item);
  if (quality == game::kQualitySuperior && mark.taken >= 0 && kind == mark.taken) return;
  const LONG misses = InterlockedIncrement(&g_kind_misses);
  char fit[40];
  kinds_text(mark.fit, fit);
  log_warn("loot: item class %u was to be superior of kind %d (of the kinds %s that go on it; %d were refused) and "
           "came out with quality %d, kind %d%s",
           game::unit_class(item), mark.taken, fit, mark.refused, quality, kind,
           misses >= kKindMissLimit ? " - a superior item's kind is the game's pick for the rest of the session" : "");
  if (misses >= kKindMissLimit) InterlockedExchange(&g_kinds_off, 1);
}

}  // namespace

void set_services(const D2RL::PluginContext* ctx, const D2RL::DataTableService* tables) {
  g_ctx = ctx;
  g_tables = tables;
}

void game_left() {
  InterlockedExchange(&g_replaced, 0);
  InterlockedExchange(&g_omitted, 0);
  InterlockedExchange(&g_extra, 0);
  InterlockedExchange(&g_slowest_us, 0);
  InterlockedExchange(&g_maxed, 0);
  InterlockedExchange(&g_maxed_logged, 0);
  InterlockedExchange(&g_skill_items, 0);
  InterlockedExchange(&g_kinds, 0);
  InterlockedExchange(&g_kind_logged, 0);
  InterlockedExchange(&g_affixes, 0);
  InterlockedExchange(&g_upgraded, 0);
  InterlockedExchange(&g_unmade, 0);
  InterlockedExchange(&g_picked, 0);
  InterlockedExchange(&g_missed_game, 0);
  dropodds::reset();
  // The lifecycle listener runs on the UI thread, like the panel messages: a panel that never
  // reported its closing does not keep the filter off in the next game.
  for (auto& panel : g_open_panels) panel[0] = 0;
  InterlockedExchange(&g_editor_open, 0);
  // Another character may come with another profile: the filter is asked again.
  InterlockedIncrement(&g_filter_generation);
}

void note_filter_panel(const char* name, bool open) {
  if (!name || !*name) return;
  int free_slot = -1, found = -1, count = 0;
  for (int i = 0; i < 8; ++i) {
    if (!g_open_panels[i][0]) {
      if (free_slot < 0) free_slot = i;
      continue;
    }
    ++count;
    if (std::strncmp(g_open_panels[i], name, sizeof(g_open_panels[i]) - 1) == 0) found = i;
  }
  if (open && found < 0 && free_slot >= 0) {
    std::strncpy(g_open_panels[free_slot], name, sizeof(g_open_panels[free_slot]) - 1);
    ++count;
  } else if (!open && found >= 0) {
    g_open_panels[found][0] = 0;
    --count;
  }
  // The rules may be other ones from here on: what the filter said of each class is asked again.
  InterlockedIncrement(&g_filter_generation);
  if (InterlockedExchange(&g_editor_open, count > 0 ? 1 : 0) != (count > 0 ? 1 : 0))
    logf("loot: loot filter editor %s (%s)", count > 0 ? "open - drops are not filtered until it closes" : "closed", name);
}

void check_drops(int walks) {
  InterlockedExchange(&g_check_walks, walks < 1 ? 0 : walks > kCheckWalksMost ? kCheckWalksMost : walks);
}

bool refuse(uint8_t* request) {
  Session* const s = t_session;
  if (!s) return false;
  const uintptr_t at = reinterpret_cast<uintptr_t>(request);
  // A walk that makes nothing: whatever the drop core asks the item maker for is not made.
  if (s->making == Making::kAsked || s->making == Making::kCounted) {
    uintptr_t source = 0;
    int32_t item_class = -1, quality = 0;
    if (request && mem::read_safe(at + kRequestSource, &source) && source == reinterpret_cast<uintptr_t>(s->source))
      note_request(s, request);
    ++s->asked;
    if (s->counted && request && mem::read_safe(at + kRequestClass, &item_class) &&
        mem::read_safe(at + kRequestQuality, &quality) && item_class >= 0 && quality >= 0 && quality < 8)
      ++(*s->counted)[static_cast<uint32_t>(item_class) << 4 | static_cast<uint32_t>(quality)];
    return true;
  }
  int32_t item_class = 0, quality = 0;
  if (!session_drop(*s, request, &item_class, &quality)) return false;
  note_request(s, request);
  s->request = request;
  s->audit = false;
  s->item_class = item_class;
  s->quality = quality;
  s->said = false;
  s->changed = false;
  s->as_said = true;
  // The item of a pick: made, in the form the pick says.
  if (s->making == Making::kPicked) {
    if (s->pick && s->pick->quality && s->pick->item_class == static_cast<uint32_t>(item_class))
      say(s, request, s->pick->force);
    return false;
  }
  if (g_ask_first_off) return false;
  // Not before a drop's item has been seen to come from the tables the answers are asked of.
  if (static_cast<LONG>(s->bank) != g_item_bank) return false;
  if (s->making == Making::kWalked && s->odds && !g_odds_off) {
    // By the forms the generator can make of the request: one the filter shows (or a unique or set item the
    // Chronicle misses), by the chance of those.
    dropodds::Ask ask;
    ask.item_class = static_cast<uint32_t>(item_class);
    ask.quality = quality;
    ask.item_level = s->item_level;
    mem::read_safe(at + kRequestLevel, &ask.item_level);
    mem::read_safe(at + kRequestName, &ask.named);
    mem::read_safe(at + kRequestFlags, &ask.flags);
    dropodds::Force force;
    const dropodds::Answer answer = dropodds::judge(*s->odds, ask, &force);
    if (answer == dropodds::Answer::kAsItComes) return false;
    if (answer == dropodds::Answer::kSo) {
      say(s, request, force);
      return false;
    }
    // Refused by the chance of the forms it may take: made all the same, it might well be one that is wanted, so it
    // is no check of the answers.
    if (answer == dropodds::Answer::kNotThisTime) {
      s->request = nullptr;
      ++s->refused;
      return true;
    }
  } else {
    // By what the quality asked for may become: what the filter shows of the class at those qualities, and whether
    // the item may come out as a unique or set item the Chronicle misses.
    bool keep = false;
    if (s->filter) {
      const Verdict& v = verdict(s->bank, static_cast<uint32_t>(item_class));
      keep = v.always || (v.shown & may_become(quality)) != 0;
    } else {
      keep = always_made(s->bank, static_cast<uint32_t>(item_class));
    }
    if (!keep && s->chronicle)
      keep = !s->odds || dropodds::may_be_wanted(*s->odds, static_cast<uint32_t>(item_class), quality) != 0;
    if (keep) return false;
  }
  if (InterlockedExchange(&g_audit_generation, g_filter_generation) != g_filter_generation) {
    InterlockedExchange(&g_refusable, 0);
    InterlockedExchange(&g_audits, 0);
  }
  if (InterlockedIncrement(&g_refusable) % (g_audits < kAuditsOften ? kAuditEvery : kAuditSeldom) == 0) {
    InterlockedIncrement(&g_audits);
    s->audit = true;
    return false;
  }
  s->request = nullptr;
  ++s->refused;
  return true;
}

void made(uint8_t* request, Unit* item) {
  check_kind(item);
  Session* const s = t_session;
  if (!s || !request || s->request != request) return;
  // The class and the quality asked for are the ones the request had before the item was made.
  const bool audit = s->audit, said = s->said;
  const int32_t item_class = s->item_class, asked = s->quality;
  s->request = nullptr;
  s->audit = false;
  s->said = false;
  if (s->changed) {
    const uintptr_t at = reinterpret_cast<uintptr_t>(request);
    mem::store(at + kRequestName, s->name_was);
    mem::store(at + kRequestFlags, s->flags_was);
    s->changed = false;
  }
  if (!item || game::unit_class(item) != static_cast<uint32_t>(item_class)) return;
  const uint8_t bank = game::unit_table_bank(item);
  if (bank >= 1 && bank <= 3 && g_item_bank != static_cast<LONG>(bank)) InterlockedExchange(&g_item_bank, bank);
  if (bank != s->bank || g_ask_first_off) return;
  // The real item says whether the answers hold: the filter's own test of it, what it came out as, and whether it is
  // a unique or set item the Chronicle misses (asked before the drop keeps it: its row is still missing then).
  const Verdict* const v = s->filter ? &verdict(s->bank, static_cast<uint32_t>(item_class)) : nullptr;
  const bool filter = v && v->asked;
  if (!filter && !s->chronicle) return;
  const uint32_t cls = static_cast<uint32_t>(item_class);
  const int32_t quality = game::item_quality(item);
  const uint32_t flags = game::item_flags(item);
  const bool ethereal = (flags & game::kItemEthereal) != 0, socketed = (flags & game::kItemSocketed) != 0;
  const bool es = ethereal || socketed;
  const int32_t row = game::item_file_index(item);
  const bool shows = filter && game::loot_filter_shows(item);
  const bool wanted = s->chronicle && chronicle::missing(item);
  if (said) {
    InterlockedIncrement(&g_picks);
    const dropodds::Force& f = s->force;
    s->as_said = (!f.quality || quality == f.quality) && (f.ethereal < 0 || ethereal == (f.ethereal == 1)) &&
                 (f.sockets < 0 || socketed == (f.sockets == 1)) && (f.row < 0 || row == f.row);
  }
  if (s->making == Making::kRolled) {
    if (filter && !v->always) {
      if (quality < 1 || quality > 8 || !(may_become(asked) & verdict_bit(quality, es)))
        mismatch("an item came out as a quality that was not expected of it", cls, asked, quality, es,
                 filter_says(shows));
      else if (shows != ((v->shown & verdict_bit(quality, es)) != 0))
        mismatch("the filter's answer for the item is not the one it gave before the item was made", cls, asked,
                 quality, es, filter_says(shows));
      else if (audit && shows)
        mismatch("an item that would not have been made is one the filter shows", cls, asked, quality, es,
                 filter_says(shows));
    }
    if (audit && wanted)
      mismatch("an item that would not have been made is one the Chronicle misses", cls, asked, quality, es,
               "the Chronicle misses its row");
    return;
  }
  if (quality < 1 || quality > 8) return;
  if (filter && shows != ((v->shown & verdict_bit(quality, es)) != 0))
    mismatch("the filter's answer for the item is not the one it gave before the item was made", cls, asked, quality,
             es, filter_says(shows));
  else if (audit && shows)
    mismatch("an item that would not have been made is one the filter shows", cls, asked, quality, es,
             filter_says(shows));
  else if (audit && wanted)
    mismatch("an item that would not have been made is one the Chronicle misses", cls, asked, quality, es,
             "the Chronicle misses its row");
  else if (said && !s->as_said)
    missed(cls, asked, s->force, quality, ethereal, socketed, row);
}

void drop(TcDropCoreFn original, void* game, Unit* source, Unit* killer, const void* tc, int32_t forced_quality,
          int32_t item_level, int32_t skip_nodrop, Unit** out, int32_t* out_count, int32_t out_max) {
  const bool filter_on = on(cheats::kLootFilterOnly);
  const uint32_t type = game::unit_type(source);
  // The console's dropcheck: the next monster's treasure class, before its drop.
  const LONG walks = g_check_walks;
  if (walks > 0 && t_depth == 0 && game && source && tc && !out && type == game::kMonster &&
      InterlockedCompareExchange(&g_check_walks, 0, walks) == walks)
    check(original, game, source, killer, tc, forced_quality, item_level, static_cast<int>(walks));
  // A monster's drop comes without an array, an object's (chest, barrel, urn, body, rack) with its
  // own; the game's drop test commands (a monster with an array) and our own passes are left alone.
  const bool ours = t_depth == 0 && game && source && tc && game::has_item_removal() &&
                    (!out || (out_count && out_max > 0 && type == game::kObject));
  // The filter reads the character's profile; while its editor is open the rules may be changing, and nothing is
  // filtered then (what the filter and the Chronicle let through together is everything while the filter does).
  const bool editing = filter_on && g_editor_open != 0;
  uint8_t bank = 0;
  if (ours) mem::read_safe(reinterpret_cast<uintptr_t>(game) + kGameBank, &bank);
  // The Chronicle is read again for every drop (an item identified since may have filled an entry); its rows are the
  // expansion's, so not in a classic game. Unread, its switch leaves the drop to the game.
  const bool by_chronicle = ours && on(cheats::kChronicleOnly) && !editing && bank >= 2 && chronicle::read();
  // Under either switch every pick drops, and what drops is what the loot filter shows or the Chronicle misses.
  const bool picky = filter_on || by_chronicle;
  // The extra drops: for every kill and chest, or kept to the kills that tend to drop better (which also get the
  // longer search).
  int extra = ours && on(cheats::kExtraDrops) ? cheats::extra_drops() : 0;
  uint16_t kind_bits = 0;
  const char* const kind = ours && (picky || extra > 0) ? elite(source, &kind_bits) : nullptr;
  if (extra > 0 && !kind && on(cheats::kExtraDropsElite)) {
    extra = 0;
    if (overlay::settings().trace || log_first(&g_ordinary_logged, 4))
      logf("loot: extra drops: %s %u (kind 0x%X) is no champion, unique, boss or Herald - none added",
           type == game::kObject ? "object" : "monster", game::unit_class(source), kind_bits);
  }
  if (!ours || (!picky && extra <= 0)) {
    original(game, source, killer, tc, forced_quality, item_level, skip_nodrop, out, out_count, out_max);
    return;
  }
  const bool by_filter = filter_on && !editing && game::has_loot_filter();
  Session session;
  session.source = source;
  session.bank = bank;
  const bool filtering = by_filter || by_chronicle;
  session.filter = by_filter;
  session.chronicle = by_chronicle;
  // Under the filter a drop it hides is not made at all (nor one the Chronicle has): the item creation hook asks, by
  // what the treasure class and the generator can make (the odds) when that can be worked out, else by what a
  // quality may become.
  const bool asking = filtering && !g_ask_first_off;
  Asker asker{original, &session, game, source, killer, tc, item_level};
  dropodds::Request odds;
  odds_of(&odds, game, source, killer, tc, forced_quality, item_level, session.bank, by_filter, by_chronicle);
  session.odds = &odds;
  // Not in a classic game, and not before an item has been seen to come from the tables the odds are read from
  // (the first drop of a run says: its own walk is rolled, its picks are worked out).
  const auto worked_out = [&session, asking]() {
    return asking && !g_odds_off && !g_ask_first_off && session.bank >= 2 &&
           static_cast<LONG>(session.bank) == g_item_bank;
  };
  if (worked_out()) session.making = Making::kWalked;
  // What a drop that stays must be; and a unique or set item that stays is not missing again in this game.
  const auto wanted = [by_filter, by_chronicle](Unit* it) {
    return (by_filter && game::loot_filter_shows(it)) || (by_chronicle && chronicle::missing(it));
  };
  const auto stays = [by_chronicle](Unit* it) {
    if (by_chronicle) chronicle::dropped(it);
  };
  ++t_depth;
  t_session = asking ? &session : nullptr;
  Unit* local[kPassMax] = {};
  int32_t local_count = 0;
  Unit** items = out ? out : local;
  int32_t* count = out ? out_count : &local_count;
  const int32_t cap = out ? out_max : kPassMax;
  // Under the loot filter's (or the Chronicle's) rule every pick drops: NoDrop is skipped, the game's own forced-drop
  // mode.
  original(game, source, killer, tc, forced_quality, item_level, picky ? 1 : skip_nodrop, items, count, cap);
  const int made_first = *count < cap ? *count : cap;
  int hidden = session.refused;
  bool gold_hidden = false;
  for (int i = 0; filtering && i < made_first; ++i) {
    if (!items[i]) continue;
    // A quest item comes whatever is asked (the filter shows every one too).
    if (quest_item(items[i]) == 1 || wanted(items[i])) {
      stays(items[i]);
      continue;
    }
    const bool pile = gold(items[i]);
    if (!take_back(game, items[i])) continue;
    gold_hidden = gold_hidden || pile;
    items[i] = nullptr;
    ++hidden;
  }
  const int dropped = made_first + session.refused;
  const int shown = dropped - hidden;
  // Every pick the filter hides is made up for from the same treasure class, then the extra drops.
  int want = hidden + extra;
  int kept = 0, misses = 0, fruitless = 0, empty_passes = 0, passes = 0, tried = 0, nothing = 0, otherwise = 0;
  const int budget = kBudgetItems + kBudgetPerWanted * want;
  const int64_t began = want > 0 ? now_us() - log_spent_us() : 0;
  int64_t took = 0;
  // Picked among what the filter shows of what the treasure class drops, each by the odds it has in the game: no
  // pick can miss, and picks stay away only when nothing is left that the filter shows.
  dropodds::Plan plan;
  bool planned = false;
  if (want > 0 && worked_out()) {
    if (!session.seen) asker.room();
    if (session.seen) {
      odds.item_level = session.item_level;
      odds.flags = session.flags;
      ask_with(&odds, &asker);
      planned = plan.build(odds);
      took = now_us() - log_spent_us() - began;
      if (!planned && (overlay::settings().trace || log_first(&g_plan_warned, 4, LogLevel::kWarning))) {
        char name[48];
        dropodds::name_of(reinterpret_cast<uintptr_t>(tc), name, sizeof(name));
        log_warn("loot: what treasure class '%s' drops cannot be worked out (%s) - its drops are rolled for until "
                 "one is %s",
                 name, plan.why_not() ? plan.why_not() : "?",
                 by_filter && by_chronicle ? "what the loot filter shows or the Chronicle misses"
                 : by_chronicle            ? "what the Chronicle misses"
                                           : "what the loot filter shows");
      }
    }
  }
  if (planned) {
    if (gold_hidden) plan.without_gold();
    while (want > 0 && !plan.empty() && misses < kPickMisses && nothing < kPickNothing && took < kBudgetUs) {
      dropodds::Pick pick;
      if (!plan.pick(&pick)) {
        plan.made(pick, false);
        ++misses;
        continue;
      }
      Unit* const it = asker.make(pick);
      took = now_us() - log_spent_us() - began;
      if (!it) {
        ++nothing;
        continue;
      }
      nothing = 0;
      ++tried;
      if (quest_item(it) == 1 || !wanted(it)) {
        take_back(game, it);
        if (pick.gold) plan.without_gold();
        else plan.made(pick, false);
        ++misses;
        ++otherwise;
        continue;
      }
      // The Chronicle's rows first: the plan weighs the item's class again with a set item that came taken out.
      stays(it);
      plan.made(pick, session.as_said);
      if (!session.as_said) ++otherwise;
      misses = 0;
      ++kept;
      --want;
    }
  }
  // How far into a pass the keepers have come (the drop itself says first): 0 none yet.
  int reach = 0;
  for (int i = 0; filtering && i < made_first; ++i)
    if (items[i]) reach = i + 1;
  // Else rolled for: the treasure class walked again until something the filter shows comes out; a pass never
  // makes more than is still wanted.
  session.making = Making::kRolled;
  while (!planned && want > 0 && empty_passes < kPatiencePasses && tried < budget &&
         passes < budget * kBudgetPassesPerItem && took < kBudgetUs) {
    const int patience = !kind ? kPatience : shown + kept > 0 ? kPatienceElite : kPatienceEliteFirst;
    if (misses >= patience || fruitless >= patience) break;
    int most = want < kPassMax ? want : kPassMax;
    if (filtering && (passes + 1) % kProbeEvery != 0 && most > reach) most = reach > 0 ? reach : 1;
    Unit* got[kPassMax] = {};
    int32_t got_count = 0;
    const int refused = session.refused, was = kept;
    original(game, source, killer, tc, forced_quality, item_level, 1, got, &got_count, most);
    ++passes;
    const int made = got_count < kPassMax ? got_count : kPassMax;
    empty_passes = made > 0 || session.refused > refused ? 0 : empty_passes + 1;
    tried += made;
    for (int i = 0; i < made; ++i) {
      Unit* it = got[i];
      if (!it) continue;
      // A quest item comes once, with the drop itself; one the filter hides is tried again.
      if (quest_item(it) == 1 || (filtering && !wanted(it))) {
        take_back(game, it);
        ++misses;
        continue;
      }
      stays(it);
      misses = 0;
      ++kept;
      --want;
      if (i + 1 > reach) reach = i + 1;
    }
    fruitless = kept > was ? 0 : fruitless + 1;
    took = now_us() - log_spent_us() - began;
  }
  t_session = nullptr;
  --t_depth;
  const int replaced = kept < hidden ? kept : hidden;
  if (replaced) InterlockedExchangeAdd(&g_replaced, replaced);
  if (kept > replaced) InterlockedExchangeAdd(&g_extra, kept - replaced);
  if (want > 0) InterlockedExchangeAdd(&g_omitted, want);
  if (session.refused) InterlockedExchangeAdd(&g_unmade, session.refused);
  if (planned && kept) InterlockedExchangeAdd(&g_picked, kept);
  const LONG took_us = took < 0 ? 0 : took > 0x7FFFFFFF ? 0x7FFFFFFF : static_cast<LONG>(took);
  for (LONG seen = g_slowest_us; took_us > seen;) {
    const LONG was = InterlockedCompareExchange(&g_slowest_us, took_us, seen);
    if (was == seen) break;
    seen = was;
  }
  if (!(hidden || extra) || !log_enabled() ||
      !(overlay::settings().trace || log_first(&g_logged, 4) || (kind && log_first(&g_elite_logged, 4))))
    return;
  char what[40] = "";
  if (kind) std::snprintf(what, sizeof(what), " (%s, kind 0x%X)", kind, kind_bits);
  // What the drop was kept to, in the words of each switch (the loot filter's alone as they always were).
  const bool both = by_filter && by_chronicle;
  const char* const not_kept = both           ? "neither shown by the loot filter nor missing from the Chronicle"
                               : by_chronicle ? "not missing from the Chronicle"
                                              : "hidden by the loot filter";
  const char* const kept_to = both           ? "what the filter shows or the Chronicle misses"
                              : by_chronicle ? "what the Chronicle misses"
                                             : "what the filter shows";
  if (planned) {
    char name[48];
    dropodds::name_of(reinterpret_cast<uintptr_t>(tc), name, sizeof(name));
    const dropodds::Totals& t = plan.totals();
    logf("loot: %s %u%s drop: %d dropped, %d %s, %d extra asked; %d picked among %s of treasure class '%s' (%d "
         "items in %d forms %s; a walk makes %.2f items, %.4f of them %s), %d came out otherwise, %d questions to "
         "the drop core, %ld.%ld ms; %d omitted%s%s",
         type == game::kObject ? "object" : "monster", game::unit_class(source), what, dropped, hidden, not_kept,
         extra, kept, kept_to, name, t.leaves, t.outcomes, by_chronicle ? "wanted" : "it shows", t.walk, t.shown,
         by_chronicle ? "wanted" : "shown", otherwise, asker.questions, static_cast<long>(took_us / 1000),
         static_cast<long>(took_us % 1000 / 100), want,
         want <= 0                  ? ""
         : nothing >= kPickNothing  ? " - no room beside it"
         : took >= kBudgetUs        ? " - out of time"
         : misses >= kPickMisses    ? " - the picks gave nothing to keep"
                                    : " - the treasure class has nothing (left) that ",
         want <= 0 || nothing >= kPickNothing || took >= kBudgetUs || misses >= kPickMisses ? ""
         : both                                                                            ? "the loot filter shows or the Chronicle misses"
         : by_chronicle                                                                    ? "the Chronicle misses"
                                                                                           : "the loot filter shows");
    return;
  }
  logf("loot: %s %u%s drop: %d dropped, %d %s, %d extra asked; %d added in %d passes (%d items made, %d not made as "
       "%s, %ld.%ld ms), %d omitted%s",
       type == game::kObject ? "object" : "monster", game::unit_class(source), what, dropped, hidden, not_kept, extra,
       kept, passes, made_first + tried, session.refused,
       both ? "nothing wanted them" : by_chronicle ? "the Chronicle has them" : "the filter hides them",
       static_cast<long>(took_us / 1000), static_cast<long>(took_us % 1000 / 100), want,
       want <= 0                           ? ""
       : took >= kBudgetUs                 ? " - out of time"
       : empty_passes >= kPatiencePasses   ? " - the treasure class makes nothing more"
       : tried >= budget || passes >= budget * kBudgetPassesPerItem
           ? " - at the most a drop may try"
       : both         ? " - nothing the loot filter shows or the Chronicle misses turned up"
       : by_chronicle ? " - nothing the Chronicle misses turned up"
                      : " - nothing the loot filter shows turned up");
}

int32_t quality_step(Unit* item, uint8_t* request, int32_t quality) {
  // The generator's next step is this item's: should it be the superior step, its test asks by the mark.
  t_kind = KindMark{};
  t_kind.item = item;
  t_kind.request = request;
  const bool superior = on(cheats::kAllSuperior);
  const bool ethereal = on(cheats::kAllEthereal);
  const bool socketed = on(cheats::kAllSocketed);
  if ((!superior && !ethereal && !socketed) || !item || !request) return quality;
  const uintptr_t req = reinterpret_cast<uintptr_t>(request);
  // An existing item made again (a cube upgrade, a restore) keeps what it was; quest items (and
  // anything whose row is unknown) are made the game's way.
  int32_t restore = 1;
  if (!mem::read_safe(req + kRequestRestore, &restore) || restore != 0 || quest_item(item) != 0) return quality;
  if (superior) {
    int32_t asked = 0;
    if (mem::read_safe(req + kRequestQuality, &asked) && (asked == game::kQualityInferior || asked == game::kQualityNormal))
      mem::store(req + kRequestQuality, static_cast<int32_t>(game::kQualitySuperior));
    if (quality == game::kQualityInferior || quality == game::kQualityNormal) quality = game::kQualitySuperior;
  }
  // The game's own tests still decide. Ethereal: expansion weapons and armor with durability, not set or
  // inferior items, nor anything made "never ethereal" (vendor stock, gambling, quest items). Sockets: the
  // socket roll runs for normal and superior items whose base can have sockets at the item's level, "no
  // sockets" (a new character's gear) still wins, and the count is rolled as usual; only its chance is skipped.
  const uint32_t add = (ethereal ? kRequestEthereal : 0) | (socketed ? kRequestSockets : 0);
  uint32_t flags = 0;
  if (add && mem::read_safe(req + kRequestFlags, &flags)) mem::store(req + kRequestFlags, flags | add);
  return quality;
}

void set_choice_sites(const uintptr_t* return_addresses, int count, uintptr_t text_begin, uintptr_t text_end) {
  const int n = count < kMaxChoiceSites ? count : kMaxChoiceSites;
  for (int i = 0; i < n; ++i) g_choice[i] = return_addresses[i];
  g_text_begin = text_begin;
  g_text_end = text_end;
  InterlockedExchange(&g_choice_count, n);
}

bool choice_sites_known() { return g_choice_count > 0 && g_text_end > g_text_begin; }

int32_t property_roll(Unit* unit, int32_t a, int32_t b, int32_t rolled, uintptr_t return_address) {
  if (a == b || !on(cheats::kPerfectRolls) || !choice_sites_known()) return rolled;
  // Only the game's own property code (D2RCore's reimplementations live outside it), and never a
  // pick: which skill, which class, which of several stats stays random.
  if (return_address < g_text_begin || return_address >= g_text_end) return rolled;
  for (LONG i = 0; i < g_choice_count; ++i)
    if (return_address == g_choice[i]) return rolled;
  // Monsters' own properties stay as rolled, and so do the game's display items.
  if (game::unit_type(unit) != game::kItem || (game::item_flags(unit) & game::kItemPreview)) return rolled;
  const int32_t top = a > b ? a : b;
  if (top != rolled) {
    InterlockedIncrement(&g_maxed);
    if (log_first(&g_maxed_logged, 3))
      logf("loot: perfect roll %d in [%d, %d] -> %d (from rva 0x%llX)", rolled, a < b ? a : b, top, top,
           static_cast<unsigned long long>(return_address - sites::exe_base()));
  }
  return top;
}

int32_t class_skill_bonus(Unit* item, int32_t bonus) {
  // The step's rolls are 0..99 plus the bonus (count) and half of it (each level): the top bonus (sites.cpp) makes both
  // land on their tops. The game still rolls, and which skills come is still its pick.
  const int32_t top = sites::affix_facts().skill_bonus;
  if (!top || bonus >= top || !on(cheats::kPerfectRolls)) return bonus;
  if (game::unit_type(item) != game::kItem || (game::item_flags(item) & game::kItemPreview)) return bonus;
  InterlockedIncrement(&g_skill_items);
  if (overlay::settings().trace || log_first(&g_skill_logged, 3))
    logf("loot: perfect roll: class item (class %u, level %d) rolls its skill lines with bonus %d (the game's was %d)",
         game::unit_class(item), game::item_level(item), top, bonus);
  return top;
}

int32_t superior_fits(SuperiorFitsFn original, Unit* item, const uint8_t* row) {
  const int32_t fits = original ? original(item, row) : 0;
  KindMark& mark = t_kind;
  if (!fits || !original || !item || mark.item != item) return fits;
  if (!mark.planned) {
    mark.planned = true;
    plan_kind(original, item, &mark);
  }
  if (!mark.take) return fits;
  // A kind that was not looked at, or not seen to go on the item (the step picks among more kinds than was taken,
  // or among others), is the item's, as the game has it; when a kind was refused before it, made() says so.
  const int kind = kind_of(mark, row);
  if (kind < 0 || !(mark.fit & (1u << kind))) {
    mark.take = 0;
    if (!mark.refused && log_once(&g_kind_asked_warned, LogLevel::kWarning))
      log_warn("loot: the superior step asks of a kind (row %p) that is not one of the %u it was taken to pick among "
               "(the table's %u rows from %p) - that item's kind is the game's pick",
               static_cast<const void*>(row), mark.among, mark.count, static_cast<const void*>(mark.rows));
    return fits;
  }
  if (mark.first < 0) mark.first = kind;
  if (!(mark.take & (1u << kind))) {
    ++mark.refused;
    return 0;
  }
  mark.taken = kind;
  if (mark.refused) {
    InterlockedIncrement(&g_kinds);
    if (log_enabled() && (overlay::settings().trace || log_first(&g_kind_logged, 4))) {
      char fit[40];
      kinds_text(mark.fit, fit);
      logf("loot: perfect roll: superior item (class %u, level %d) takes kind %d of the kinds %s that go on it; the "
           "game's roll came to kind %d first",
           game::unit_class(item), game::item_level(item), mark.taken, fit, mark.first);
    }
  }
  return fits;
}

int32_t magic_affixes(MagicAffixesFn original, Unit** item_ref, uint8_t* request) {
  Unit* item = item_ref ? reinterpret_cast<Unit*>(mem::read_ptr(reinterpret_cast<uintptr_t>(item_ref))) : nullptr;
  const BestAffixScope best(item, request);
  const int32_t made = original ? original(item_ref, request) : 0;
  if (!made || !maxing(item, request)) return made;
  // A magic item has one prefix slot and one suffix slot in use. The request's -1 forbids a side: no prefix at all,
  // or no suffix beside a prefix.
  Side pre, suf;
  if (!read_side(item, true, &pre) || !read_side(item, false, &suf)) return made;
  const int before = pre.count + suf.count;
  int added = 0;
  if (!pre.count && forced_id(request, true, 0) >= 0 && add_affix(item, request, true, &pre) > 0) ++added;
  if (!suf.count && forced_id(request, false, 0) >= 0 && add_affix(item, request, false, &suf) > 0) ++added;
  note_affixes(item, "magic", before, added, 2);
  return made;
}

int32_t rare_affixes(AffixesFn original, Unit* item, uint8_t* request) {
  const BestAffixScope best(item, request);
  const int32_t made = original ? original(item, request) : 0;
  const sites::AffixFacts& f = sites::affix_facts();
  if (!made || !f.rare_max || !maxing(item, request)) return made;
  // A jewel's count tops out lower; an item whose type is unknown is left as the game made it.
  const int type = item_type(item);
  if (type < 0) return made;
  const int target = type == f.jewel_type ? f.jewel_max : f.rare_max;
  int before = 0;
  const int added = top_up(item, request, target, &before);
  note_affixes(item, type == f.jewel_type ? "rare jewel" : "rare", before, added, target);
  return made;
}

int32_t crafted_affixes(AffixesFn original, Unit* item, uint8_t* request) {
  const BestAffixScope best(item, request);
  const int32_t made = original ? original(item, request) : 0;
  const sites::AffixFacts& f = sites::affix_facts();
  // The picker keeps an affix's group off an expansion item only (the classic one leaves that to the step): crafted
  // items are expansion items anyway.
  if (!made || !f.crafted_max || !maxing(item, request) || game::item_version(item) == 0) return made;
  int before = 0;
  const int added = top_up(item, request, f.crafted_max, &before);
  note_affixes(item, "crafted", before, added, f.crafted_max);
  return made;
}

int32_t pick_affix(PickAffixFn original, Unit* item, int32_t spawnable, int32_t must, int32_t apply, int32_t prefix,
                   int32_t forced) {
  if (!original) return 0;
  // Only the picks an affix step makes for the fresh item it is making (the steps' hooks say which), never an id the
  // request asks for, and only with the table the rows are read from (the properties are applied from it below).
  game::AffixTable table;
  if (!item || item != t_best_item || forced > 0 || !on(cheats::kBestAffixes) || !game::affix_table(item, &table))
    return original(item, spawnable, must, apply, prefix, forced);
  // The pick is the game's own (the item's seed moves as it would); its properties wait for the version kept.
  const int32_t picked = original(item, spawnable, must, 0, prefix, forced);
  if (picked <= 0) return picked;
  if (!table.row(picked)) {
    log_warn("loot: best affixes: the picker's %s %d is past the table's %u rows - its properties are not applied",
             prefix ? "prefix" : "suffix", picked, table.count);
    return picked;
  }
  const Affix kind = prefix ? Affix::kPrefix : Affix::kSuffix;
  const int32_t kept = best_affix(item, table, picked, kind, spawnable != 0);
  if (apply) game::apply_affix(item, table.row(kept));
  if (kept != picked) note_upgrade(item, table, picked, kept, kind);
  return kept;
}

int32_t pick_auto_affix(AutoAffixFn original, Unit* item, int32_t spawnable, int32_t must, int32_t apply,
                        int32_t prefix, int32_t forced, int32_t group) {
  if (!original) return 0;
  // Only the generator's pick for a fresh expansion item (the one whose quality step ran last on this thread: the
  // generator runs that step first for every item it makes), never an id asked for, and only with the table the rows
  // are read from. The kept row is of the pick's group, so the picker's group test holds for it as for the pick.
  game::AffixTable table;
  if (!on(cheats::kBestAffixes) || !item || item != t_kind.item || forced > 0 || group <= 0 ||
      game::item_version(item) == 0 || !fresh(item, t_kind.request) || !game::affix_table(item, &table))
    return original(item, spawnable, must, apply, prefix, forced, group);
  // The pick is the game's own (the item's seed moves as it would); the generator writes the id answered and applies
  // that row's properties itself (it asks for none to be applied here).
  const int32_t picked = original(item, spawnable, must, 0, prefix, forced, group);
  if (picked <= 0) return picked;
  if (!table.row(picked)) {
    log_warn("loot: best affixes: the picker's automatic affix %d is past the table's %u rows - left as picked%s",
             picked, table.count, apply ? ", its properties not applied" : "");
    return picked;
  }
  const int32_t kept = best_affix(item, table, picked, Affix::kAutomatic, spawnable != 0);
  if (apply) game::apply_affix(item, table.row(kept));
  if (kept != picked) note_upgrade(item, table, picked, kept, Affix::kAutomatic);
  return kept;
}

Stats stats() {
  Stats s;
  s.replaced = static_cast<unsigned>(g_replaced);
  s.omitted = static_cast<unsigned>(g_omitted);
  s.extra = static_cast<unsigned>(g_extra);
  s.slowest_us = static_cast<unsigned>(g_slowest_us);
  s.unmade = static_cast<unsigned>(g_unmade);
  s.ask_first_off = g_ask_first_off != 0;
  s.picked = static_cast<unsigned>(g_picked);
  s.missed = static_cast<unsigned>(g_missed_game);
  s.odds_off = g_odds_off != 0;
  s.maxed = static_cast<unsigned>(g_maxed);
  s.skill_items = static_cast<unsigned>(g_skill_items);
  s.kinds = static_cast<unsigned>(g_kinds);
  s.kinds_off = g_kinds_off != 0;
  s.affixes = static_cast<unsigned>(g_affixes);
  s.upgraded = static_cast<unsigned>(g_upgraded);
  return s;
}

}  // namespace d2rcc::loot

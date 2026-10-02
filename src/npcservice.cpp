#include "npcservice.h"

#include <windows.h>

#include <cstdio>
#include <vector>

#include "cheats.h"
#include "game.h"
#include "log.h"
#include "mem.h"
#include "sites.h"

namespace d2rcc::npcservice {
namespace {

// The client's routine of a row: no arguments, it writes one entry of one NPC's menu and answers 1.
using MenuRoutineFn = uint8_t(__fastcall*)() noexcept;

// The shape of those routines. The NPC's menu is an index into the client's menus, there twice at the start
// (`cmp qword [rip+count], imm8` for its bounds check, `mov qword [rsp+40h], imm32`) with the menus themselves
// (`lea rbx, [rip+menus]`: {the menus, their count}); then the entry's own routine, the one run when the entry is
// picked, is stored in the menu (`lea rdx, [rip+routine]`, the menu at nine times the index times eight); it ends
// with its answer (`mov al, 1`).
constexpr const char* kRoutineEntry =
    "40 53 48 83 EC 30 48 83 3D ?? ?? ?? ?? ?? 48 8D 1D ?? ?? ?? ?? 48 C7 44 24 40 ?? 00 00 00 77";
constexpr const char* kRoutineStore = "48 8D 15 ?? ?? ?? ?? 48 8D 0C C0 48 8B 05 ?? ?? ?? ?? 48 89 54 C8";
constexpr const char* kRoutineEnd = "B0 01 48 83 C4 30 5B C3";
constexpr size_t kRoutineSpan = 0x100;
constexpr size_t kMenuSize = 0x48;  // a menu of the client's: the NPC's class (int16) first
constexpr uint64_t kMaxMenus = 4096;

struct Bound {
  bool ok = false;
  const char* why = "the game's NPC tables are not read yet";
  uintptr_t quest_at = 0;       // the quest of the NPC's record: the byte the tick writes
  uint8_t quest = 0;            // ... as the game has it (the quest's number plus one)
  MenuRoutineFn add = nullptr;  // the client's routine that puts the service in the NPC's menu
  uintptr_t menus = 0;          // the client's menus the routine writes to: {the menus, their count}
  int menu = -1;                // ... and which of them
};
Bound g_bound[kServices];
volatile LONG g_lifted[kServices] = {};  // the quest is out of the record (the tick wrote it)
volatile LONG g_offered[kServices] = {};
volatile LONG g_offer_logged[kServices] = {};
volatile LONG g_changed_warned[kServices] = {};
volatile LONG g_menu_warned[kServices] = {};
volatile LONG g_stopped = 0;  // the plugin unloads: the records stay as the game has them

constexpr cheats::Kind kKinds[kServices] = {cheats::kInfiniteImbue, cheats::kInfiniteSockets,
                                            cheats::kInfinitePersonalize};

bool on(Service s) { return cheats::enabled(kKinds[s]) && !cheats::why_not(kKinds[s]); }

unsigned long long rva(uintptr_t at) {
  return static_cast<unsigned long long>(at ? at - sites::exe_base() : 0);
}

bool matches(const uint8_t* code, size_t size, const char* pattern, size_t* at) {
  const std::vector<size_t> hits = mem::find_all(code, size, mem::parse_pattern(pattern), 1);
  if (hits.empty()) return false;
  *at = hits[0];
  return true;
}

// A row's routine is one of the client's menu routines: which menu it writes, of which menus, and the routine
// its entry runs when it is picked (for the log).
struct Routine {
  uintptr_t menus = 0;
  int menu = -1;
  uintptr_t picked = 0;
};
bool menu_routine(uintptr_t fn, Routine* out) {
  const mem::Range text = mem::section(sites::exe_base(), ".text");
  uint8_t code[kRoutineSpan] = {};
  size_t entry = 0, store = 0, end = 0;
  if (!fn || !text.contains(fn) || !text.contains(fn + sizeof(code)) || !mem::copy_from(code, fn, sizeof(code)) ||
      !matches(code, 32, kRoutineEntry, &entry) || entry != 0 || !matches(code, sizeof(code), kRoutineStore, &store) ||
      !matches(code, sizeof(code), kRoutineEnd, &end) || end < store)
    return false;
  int32_t count_at = 0, menus_at = 0, routine_at = 0;
  std::memcpy(&count_at, code + 9, sizeof(count_at));
  std::memcpy(&menus_at, code + 17, sizeof(menus_at));
  std::memcpy(&routine_at, code + store + 3, sizeof(routine_at));
  // The count is compared by an instruction of 8 bytes (an imm8 after the disp32), the others are `lea`s of 7.
  const uintptr_t count = fn + 6 + 8 + static_cast<intptr_t>(count_at);
  out->menus = fn + 14 + 7 + static_cast<intptr_t>(menus_at);
  out->menu = code[13];
  out->picked = fn + store + 7 + static_cast<intptr_t>(routine_at);
  return out->menu == code[26] && count == out->menus + 8 && text.contains(out->picked) &&
         !text.contains(out->menus) && mem::module_range(sites::exe_base()).contains(out->menus);
}

// The menu a routine writes is the NPC's. Asked as a menu opens, on the thread that makes and changes the menus
// (the game builds them as it starts, on the heap).
bool menu_is_of(const Bound& b, uint32_t npc_class) {
  uint64_t count = 0;
  int16_t cls = 0;
  const uintptr_t first = mem::read_ptr(b.menus);
  return first && b.menu >= 0 && mem::read_safe(b.menus + 8, &count) && count > static_cast<uint64_t>(b.menu) &&
         count <= kMaxMenus && mem::read_safe(first + static_cast<uintptr_t>(b.menu) * kMenuSize, &cls) &&
         cls > 0 && static_cast<uint32_t>(cls) == npc_class;
}

}  // namespace

const char* bind() {
  for (int s = 0; s < kServices; ++s) {
    g_bound[s] = Bound{};
    InterlockedExchange(&g_lifted[s], 0);
  }
  InterlockedExchange(&g_stopped, 0);
  const auto none = [](const char* why) {
    for (Bound& b : g_bound) b.why = why;
    logf("npc services: %s - Charsi, Larzuk and Anya do their services as the game has them", why);
    return why;
  };
  const sites::NpcFacts& facts = sites::npc_facts();
  if (!facts.known) return none("the game's NPC item services not found");
  int32_t row_count = 0;
  std::vector<uint8_t> records(static_cast<size_t>(facts.record_count) * static_cast<size_t>(facts.record_size));
  static MenuRow rows[kMaxMenuRows];
  if (facts.record_count > kMaxRecords || facts.menu_row_size != static_cast<int>(sizeof(MenuRow)) ||
      !mem::copy_from(records.data(), facts.records, records.size()) || !mem::read_safe(facts.menu_count, &row_count) ||
      row_count < 1 || row_count > kMaxMenuRows ||
      !mem::copy_from(rows, facts.menu_rows, static_cast<size_t>(row_count) * sizeof(MenuRow)))
    return none("the game's NPC tables cannot be read");
  const Layout layout{facts.record_size, facts.service_at, facts.quest_at, facts.quest_max, facts.pending_flag};
  const int numbers[kServices] = {facts.imbue, facts.sockets, facts.personalize};
  int usable = 0;
  for (int s = 0; s < kServices; ++s) {
    Bound& b = g_bound[s];
    Found found;
    Routine routine;
    if (!find(records.data(), facts.record_count, layout, rows, row_count, numbers[s], kNpcClass[s], &found)) {
      b.why = "its NPC is not in the game's tables the way expected";
      log_warn("npc services: %s (service %d): no NPC of class %d with a quest for it in the %d records and the "
               "client's %d rows - it stays the quest's reward",
               kServiceName[s], numbers[s], kNpcClass[s], facts.record_count, row_count);
      continue;
    }
    if (!menu_routine(static_cast<uintptr_t>(found.routine), &routine)) {
      b.why = "its menu routine is not the way expected";
      log_warn("npc services: %s: the routine of %s's row (0x%llX) is not one of the client's menu routines - it "
               "stays the quest's reward",
               kServiceName[s], kNpcName[s], rva(static_cast<uintptr_t>(found.routine)));
      continue;
    }
    b.quest_at = facts.records + static_cast<uintptr_t>(found.record) * static_cast<uintptr_t>(facts.record_size) +
                 static_cast<uintptr_t>(facts.quest_at);
    b.quest = static_cast<uint8_t>(found.quest + 1);
    b.add = reinterpret_cast<MenuRoutineFn>(static_cast<uintptr_t>(found.routine));
    b.menus = routine.menus;
    b.menu = routine.menu;
    b.why = nullptr;
    b.ok = true;
    ++usable;
    logf("npc services: %s: %s (class %d, record %d at 0x%llX) does service %d for quest %d; the client's row %d "
         "puts it in menu %d with 0x%llX (the entry runs 0x%llX)",
         kServiceName[s], kNpcName[s], found.npc_class, found.record,
         rva(facts.records + static_cast<uintptr_t>(found.record) * static_cast<uintptr_t>(facts.record_size)),
         numbers[s], found.quest, found.row, routine.menu, rva(static_cast<uintptr_t>(found.routine)),
         rva(routine.picked));
  }
  logf("npc services: %d of %d bound", usable, static_cast<int>(kServices));
  return usable ? nullptr : "the game's NPC tables are not the way expected";
}

const char* why_not(Service s) { return s >= 0 && s < kServices ? g_bound[s].why : "?"; }

void on_tick() {
  if (g_stopped) return;
  for (int s = 0; s < kServices; ++s) {
    const Bound& b = g_bound[s];
    const bool want = b.ok && on(static_cast<Service>(s));
    if (want == (g_lifted[s] != 0)) continue;
    uint8_t now = 0;
    if (!b.ok || !mem::read_safe(b.quest_at, &now)) continue;
    if (want) {
      // Only a record as the game has it is changed: another value is somebody else's.
      if (now != b.quest) {
        if (log_once(&g_changed_warned[s], LogLevel::kWarning))
          log_warn("npc services: %s's record has %d for its quest where the game had %d - left as it is, %s stays "
                   "the quest's reward",
                   kNpcName[s], now, b.quest, kServiceName[s]);
        continue;
      }
      const uint8_t no_quest = 0;
      if (!mem::copy_to(b.quest_at, &no_quest, 1)) continue;
      InterlockedExchange(&g_lifted[s], 1);
      logf("npc services: %s: no quest asks for it now (quest %d taken out of %s's record)", kServiceName[s],
           b.quest - 1, kNpcName[s]);
    } else {
      // Put back only where it was taken out: a record that holds something else by now is somebody else's.
      if (now == 0 && !mem::copy_to(b.quest_at, &b.quest, 1)) continue;
      InterlockedExchange(&g_lifted[s], 0);
      if (now == 0)
        logf("npc services: %s: quest %d asks for it again (put back in %s's record)", kServiceName[s], b.quest - 1,
             kNpcName[s]);
      else
        log_warn("npc services: %s's record has %d for its quest where the plugin left 0 - left as it is",
                 kNpcName[s], now);
    }
  }
}

void restore() {
  // No tick writes from here on; one that is at it now is through in far less than this.
  if (InterlockedExchange(&g_stopped, 1) == 0) Sleep(40);
  for (int s = 0; s < kServices; ++s) {
    const Bound& b = g_bound[s];
    if (!b.ok || !InterlockedExchange(&g_lifted[s], 0)) continue;
    uint8_t now = 0xFF;
    if (mem::read_safe(b.quest_at, &now) && now == 0) mem::copy_to(b.quest_at, &b.quest, 1);
  }
}

void menu_opens(Unit* npc) {
  if (!npc || g_stopped || game::unit_type(npc) != game::kMonster) return;
  const uint32_t cls = game::unit_class(npc);
  for (int s = 0; s < kServices; ++s) {
    const Bound& b = g_bound[s];
    // Only once the server does the service without its quest (the tick has the quest out of the record): an
    // entry the server would refuse is not offered.
    if (!b.ok || cls != static_cast<uint32_t>(kNpcClass[s]) || !on(static_cast<Service>(s)) || !g_lifted[s])
      continue;
    if (!menu_is_of(b, cls)) {
      if (log_once(&g_menu_warned[s], LogLevel::kWarning))
        log_warn("npc services: the client's menu %d is not %s's - %s is not put in it", b.menu, kNpcName[s],
                 kServiceName[s]);
      continue;
    }
    b.add();
    InterlockedIncrement(&g_offered[s]);
    if (log_first(&g_offer_logged[s], 4))
      logf("npc services: %s offers %s (the entry put in the menu; the quest is not asked)", kNpcName[s],
           kServiceName[s]);
    cheats::note("%s offers %s", kNpcName[s], kServiceName[s]);
  }
}

Stats stats() {
  Stats st;
  for (int s = 0; s < kServices; ++s) {
    st.lifted[s] = g_lifted[s] != 0;
    st.offered[s] = static_cast<unsigned>(g_offered[s]);
  }
  return st;
}

}  // namespace d2rcc::npcservice

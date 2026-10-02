#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace d2rcc::game {
struct Unit;
}

// NPC item services without their quests. Three of the game's people do something to an item as a quest's
// reward, once: Charsi imbues (a plain item is made again as a rare one; Tools of the Trade), Larzuk adds sockets
// (Siege on Harrogath), Anya personalizes (Betrayal of Harrogath). Under a service's switch its NPC offers it at every
// talk and does it as often as asked, the quest done or not, and the quest's own reward is neither needed nor
// used up. Both sides of the game ask the quest, each in its own way:
//   the client   puts the service in the NPC's menu as the menu opens, when its copy of the player's quest record
//                (sent along as the talk begins) has a reward to be had for the quest: a table of rows {NPC
//                class, quest, flag, value, routine} that the menu's opener walks, calling the routine of a row
//                that holds. The routine adds the entry. The opener is hooked (hooks_game.cpp), and for the NPC
//                of a service that is switched on the row's routine is called before it: the game's own, on the
//                thread the game calls it on. The game takes the entry out again as the next talk begins (it
//                resets the menus then, whoever is talked to).
//   the server   takes the request (0x2E) only when the NPC's record of the game's NPC table names the service
//                asked for, and for a record that names a quest only with a reward to be had: it then saves the
//                character with the reward marked used, does the service in a second pass, and marks the reward
//                used. For a record that names no quest the service is done at once, and no quest record is
//                looked at or changed. So the quest is taken out of the NPC's record while the service is
//                switched on: one byte of the game's own table, written by the tick (the server thread) and put
//                back when the switch goes off or the plugin unloads. Nothing is hooked on the server.
// What is done to the item is the game's own: the imbue makes the item again as a rare one of the character's
// level plus four (every loot switch acts on it as on any new item); the sockets are the most the base can have
// at the item's level for a normal or superior item, one or two for a magic one, one for a rare, set, unique or
// crafted one; the name is the character's. The game's tests of the item stay too: an item with sockets gets no
// more, only an item of low, normal or superior quality without sockets is imbued, and so on.
namespace d2rcc::npcservice {

using game::Unit;

enum Service : int { kImbue = 0, kSockets, kPersonalize, kServices };

// Who does each: the NPC's class (its MonStats row), and the names the console and the log use.
constexpr int kNpcClass[kServices] = {154, 511, 512};
constexpr const char* kNpcName[kServices] = {"Charsi", "Larzuk", "Anya"};
constexpr const char* kServiceName[kServices] = {"imbue", "sockets", "personalize"};

constexpr int kMaxRecords = 255;  // the getter counts them in a byte; 3.3 has 43
constexpr int kMaxMenuRows = 64;  // ... and 9 rows

// Where the game's code has a record's fields (sites.cpp reads them from the handler).
struct Layout {
  int record_size = 0;
  int service_at = 0;    // the item service the NPC does (0 none)
  int quest_at = 0;      // the quest that asks for it, plus one (0: no quest asks)
  int quest_max = 0;     // the highest the handler takes
  int pending_flag = 0;  // the quest flag both sides ask for: a reward to be had
};

// A row of the client's table, as the game has it.
struct MenuRow {
  int32_t npc_class = 0;
  int32_t quest = 0;
  int32_t flag = 0;
  int32_t value = 0;     // what the quest's flag must be for the routine to be called: set (1), or not (0)
  uint64_t routine = 0;  // () -> al: the entry put in the NPC's menu
};
static_assert(sizeof(MenuRow) == 0x18);

struct Found {
  int record = -1;       // which record of the NPC table
  int npc_class = 0;
  int quest = -1;        // the quest that asks (the record has it plus one)
  int row = -1;          // which of the client's rows
  uint64_t routine = 0;  // ... and its routine
};

inline int record_class(const uint8_t* records, int index, const Layout& l) {
  int16_t cls = 0;
  std::memcpy(&cls, records + static_cast<size_t>(index) * static_cast<size_t>(l.record_size), sizeof(cls));
  return cls;
}

// From copies of the two tables: the one NPC that does the service, and the client's row that puts the service
// in its menu. False unless both sides say the same: one record names the service, it is the NPC expected, a
// quest asks for it, and one row of that NPC waits for that quest's reward.
inline bool find(const uint8_t* records, int count, const Layout& l, const MenuRow* rows, int row_count,
                 int service, int npc_class, Found* out) {
  *out = Found{};
  if (!records || !rows || count < 1 || count > kMaxRecords || row_count < 1 || row_count > kMaxMenuRows ||
      l.record_size < 3 || l.service_at < 2 || l.service_at >= l.record_size || l.quest_at < 2 ||
      l.quest_at >= l.record_size || l.quest_at == l.service_at || l.quest_max < 1 || l.quest_max > 254 ||
      service < 1 || service > 255 || npc_class < 1)
    return false;
  const size_t size = static_cast<size_t>(l.record_size);
  int record = -1;
  for (int i = 0; i < count; ++i) {
    if (records[static_cast<size_t>(i) * size + static_cast<size_t>(l.service_at)] != service) continue;
    if (record >= 0) return false;  // two NPCs for one service: not the table this is about
    record = i;
  }
  if (record < 0 || record_class(records, record, l) != npc_class) return false;
  const int quest = records[static_cast<size_t>(record) * size + static_cast<size_t>(l.quest_at)];
  if (quest < 1 || quest > l.quest_max) return false;
  int row = -1;
  for (int i = 0; i < row_count; ++i) {
    // An NPC may have rows for other quests (a travel, a hire): the one that waits for a reward is the service's.
    if (rows[i].npc_class != npc_class || rows[i].flag != l.pending_flag) continue;
    if (row >= 0) return false;
    row = i;
  }
  if (row < 0 || rows[row].quest != quest - 1 || rows[row].value == 0 || !rows[row].routine) return false;
  out->record = record;
  out->npc_class = npc_class;
  out->quest = quest - 1;
  out->row = row;
  out->routine = rows[row].routine;
  return true;
}

// After sites::derive(): the two tables read, each service's NPC found in both, and the row's routine one of the
// client's menu routines. Null when at least one service can work, else why none can.
const char* bind();
const char* why_not(Service s);  // null when the service can work

// Server thread (the tick): each service's quest is out of its NPC's record while its switch is on, and back in
// when it is off.
void on_tick();
// At unload (any thread): every record as the game has it.
void restore();
// UI thread, the hook on the client's menu opener: for the NPC of a service that is switched on, and whose quest
// the tick has taken out of its record, the service is put in its menu (which must be that NPC's: the routine
// writes a menu by its number).
void menu_opens(Unit* npc);

// For the console (any thread).
struct Stats {
  bool lifted[kServices] = {};       // the quest is out of the NPC's record now
  unsigned offered[kServices] = {};  // this session: menus the service was put in
};
Stats stats();

}  // namespace d2rcc::npcservice

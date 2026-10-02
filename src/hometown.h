#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace d2rcc::game {
struct Unit;
}

// The home town. One town is the character's base: its town portals lead there from any act, and after a death it
// wakes there. (The game's own: a Scroll or Tome of Town Portal opens a portal to the town of the act it is read
// in, and a death wakes the character in the town of the act it died in.)
//
// The game makes a town portal as a pair: the portal beside the player, then the one in town (the "town end"),
// each linked to the other. Its pair maker refuses a pair across acts, and a portal's use moves the player inside
// one act. But the town end maker itself takes any act the game has made, and the game has its own way from act
// to act: the move to a level that waypoints and the Hellgate take. Four hooks on the server (hooks_game.cpp) put
// them together:
//   a town portal's cast     marks the portals made inside it as a scroll's or tome's (not a quest's or a
//                            traveller's portal, which the pair maker also makes);
//   the town end maker       is handed the home town for the destination (its act made first, by the game's own
//                            routine, when no player has been there in this game), and the portal beside the
//                            player is told where it leads (the label the client shows, and the act the game
//                            looks for the other portal in);
//   a portal's use           for a portal that leads into another act: the player is taken into that act's town
//                            by the game's move to a level. Towards the town that is the whole trip (the town end
//                            stands at the spot the move arrives at); back to where the portal was read the
//                            game's own use then does the rest, as for any portal in that act's town;
//   a death's wake           the game wakes the player in the town of the act it died in, as always; when the
//                            home town is another act's, the player is taken on to it by the same move, to the
//                            spot a wake in that town comes to.
// And one on the client (UI thread). In a town the client does not wait for the server when a town portal is
// used: it builds the rooms at the portal's other end itself, in the act it is in, moves its player there and
// only then asks the server. For a portal whose other end stands in another act that act has no such level (the
// level it makes has no size, and the map generator stops the game), so
//   the client's trip        is not made ahead of the server for such a portal: the hook answers that it cannot
//                            be, and the client sends the ordinary request to use the portal and waits for the
//                            server, as it does for every portal used outside a town.
// Nothing changes while the choice is "the game's own", or the home town is the town of the act the player is
// in. A town can be home only when the character has its waypoint (the game's own record of the acts a character
// has reached): a town it has not falls back to the game's own. All of it on the server thread.
namespace d2rcc::hometown {

using game::Unit;

constexpr int kActs = 5;
// The compiled rows read here. ActInfo: an act's town. Levels: a level's act, and its waypoint's number (0xFF
// none); the row is the level's id.
constexpr uint32_t kActInfoRowSize = 0x8C;
constexpr size_t kActInfoTown = 0x00;
constexpr uint32_t kLevelsRowSize = 0x18C;
constexpr size_t kLevelsAct = 0x0D;
constexpr size_t kLevelsWaypoint = 0xEC;
constexpr int kMaxLevels = 1024;  // 3.3 has 138
constexpr int kNoWaypoint = 0xFF;
constexpr int kMaxWaypoints = 0x70;  // the game's own bound

// Each act's town and the town's waypoint, and every level's act.
struct Towns {
  int acts = 0;                   // 5, 4 in the classic tables
  int level[kActs] = {};          // the town's level id
  int waypoint[kActs] = {};       // the number of the town's waypoint
  int levels = 0;
  uint8_t act_of[kMaxLevels] = {};  // by level id
  bool is_town(int id) const {
    for (int a = 0; a < acts; ++a)
      if (level[a] == id) return true;
    return false;
  }
  int act(int id) const { return id > 0 && id < levels ? act_of[id] : -1; }
};

// From copies of the two tables' rows. False when they do not describe acts with a town each: a town must be a
// level of its own act with a waypoint, and the towns' waypoints must rise with the acts, the first act's being
// the first of all (so a table laid out another way is not read as one that fits).
inline bool towns_of(const uint8_t* actinfo, uint32_t acts, const uint8_t* levels, uint32_t level_count, Towns* out) {
  *out = Towns{};
  if (!actinfo || !levels || acts < 1 || acts > static_cast<uint32_t>(kActs) || level_count < 2 ||
      level_count > static_cast<uint32_t>(kMaxLevels))
    return false;
  out->acts = static_cast<int>(acts);
  out->levels = static_cast<int>(level_count);
  for (uint32_t i = 0; i < level_count; ++i) {
    const uint8_t act = levels[static_cast<size_t>(i) * kLevelsRowSize + kLevelsAct];
    if (act >= kActs) return false;
    out->act_of[i] = act;
  }
  for (uint32_t a = 0; a < acts; ++a) {
    int32_t town = 0;
    std::memcpy(&town, actinfo + static_cast<size_t>(a) * kActInfoRowSize + kActInfoTown, sizeof(town));
    if (town <= 0 || town >= static_cast<int32_t>(level_count) || out->act_of[town] != a) return false;
    const int waypoint = levels[static_cast<size_t>(town) * kLevelsRowSize + kLevelsWaypoint];
    if (waypoint == kNoWaypoint || waypoint >= kMaxWaypoints) return false;
    if (a == 0 ? waypoint != 0 : waypoint <= out->waypoint[a - 1]) return false;
    out->level[a] = town;
    out->waypoint[a] = waypoint;
  }
  return true;
}

// Which act's town is home to a player in act `here`: where a portal read there leads to, and where a death
// there wakes. choice: 0 the game's own, 1 .. 5 an act's town; reach: bit a set when the character has the
// waypoint of act a's town.
constexpr int chosen_act(int choice, int here, unsigned reach, int acts) {
  if (choice < 1 || choice > acts || here < 0 || here >= acts) return here;
  const int act = choice - 1;
  return ((reach >> act) & 1u) != 0 ? act : here;
}

// What a portal's use comes to for a player in act `here` and a portal that leads to a level of act `there`.
enum class Use : int {
  kGameOwn = 0,  // the same act: the game's use
  kToTown,       // into the act's town, where the portal leads: the move is the trip
  kToField,      // into the act's town, then the game's use (the portal leads to a level of that act)
};
constexpr Use use_of(int here, int there, bool leads_to_town) {
  return here < 0 || there < 0 || here == there ? Use::kGameOwn : leads_to_town ? Use::kToTown : Use::kToField;
}

void set_services(const D2RL::PluginContext* ctx, const D2RL::DataTableService* tables);
bool has_tables();
// After sites::derive() and game::bind(): null when the option can work, else why not.
const char* bind();

// Game thread (the tick): the towns of the three table banks (read once), and once a second which towns the
// character can reach, for the panel.
void on_tick(void* game, Unit* player, unsigned tick);
void game_left();  // any thread

// Server thread, the hooks. A cast: while one is open on a thread, the portals made there are a scroll's or
// tome's of that player.
class Cast {
 public:
  explicit Cast(Unit* player);
  ~Cast();
  Cast(const Cast&) = delete;
  Cast& operator=(const Cast&) = delete;

 private:
  Unit* before_;
};
// The level the town end is made in: `destination` (the game's own), or the home town, its act made if need be.
int town_end_level(void* game, Unit* player, Unit* portal, int destination);
// The town end was asked for in `level` rather than in `destination` (made: null when the game could not make
// it, and has then taken the portal beside the player away too): the portal beside the player is told where it
// leads, when the town end stands where it was asked for.
void town_end_made(Unit* player, Unit* portal, Unit* made, int level, int destination);
// A portal's use (the operate function's context: the game, the object, the user; the object's class further on).
enum class Used : int {
  kGameOwn = 0,  // the game's use runs
  kDone,         // the player was moved: the use is over (it answers 1)
  kRefused,      // the move did not arrive: the use is refused (it answers 0)
};
Used use(void** context);
// A death's wake (the server's handler of the request a dead player's client sends). `dying` before the game's
// own: the local player in its dead mode, with a home town chosen. `woke` after it, for a player that was dying:
// alive now and in the town of the act it died in, it is taken on to the home town when that is another act's.
bool dying(Unit* player);
void woke(void* game, Unit* player);
bool wakes_at_home();  // the wake's handler is hooked and moves the way taken for granted (any thread)
void wake_hooked(bool hooked);  // hooks_game.cpp says so
// UI thread, the client's units: the portal's other end stands in another act than the portal, so the client
// must leave the trip to the server. Whatever the switch says: such a portal may stand from before it went off.
bool leads_across_acts(Unit* player, Unit* portal);

// For the panel and the console (any thread).
struct Stats {
  bool in_game = false;  // the tick has run since the last game ended
  bool known = false;    // the game's acts and towns are read for the character's tables
  int acts = 0;          // 5, 4 for a classic character
  int here = -1;         // the act the character stands in
  unsigned reach = 0;    // bit a: the character has the waypoint of act a's town
  unsigned made = 0;     // this session: town ends made in the home town
  unsigned trips = 0;    // ... uses that crossed acts
  unsigned wakes = 0;    // ... deaths that woke in the home town
  unsigned asked = 0;    // ... and trips the client left to the server
};
Stats stats();
const char* town_name(int act);  // "Rogue Encampment" .. "Harrogath"

}  // namespace d2rcc::hometown

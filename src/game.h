#pragma once

#include <cstddef>
#include <cstdint>

// The game's routines the cheats call, bound to the addresses sites.cpp found,
// and the unit fields the hooks read. Unit layout (D2R, from the research):
// +0x00 type, +0x04 class, +0x08 id, +0x0C mode, +0x10 unit data, +0x38 path,
// +0x88 stat list, +0x124 flags (bit 2: can be attacked, bit 16: dead), +0x1BD data-table bank. Stat
// values for life, mana and stamina are in 256ths of a point.
namespace d2rcc::game {

struct Unit;  // opaque: only read through the accessors below

enum Stat : int {
  kStrength = 0,
  kEnergy = 1,
  kDexterity = 2,
  kVitality = 3,
  kStatPoints = 4,   // attribute points to spend
  kSkillPoints = 5,  // skill points to spend
  kHitpoints = 6,
  kMaxHp = 7,
  kMana = 8,
  kMaxMana = 9,
  kStamina = 10,
  kMaxStamina = 11,
  kLevel = 12,
  kExperience = 13,
  kVelocityPercent = 67,
  kHpRegen = 74,
};

enum UnitType : uint32_t { kPlayer = 0, kMonster = 1, kObject = 2, kMissile = 3, kItem = 4, kTile = 5 };

// States (states.txt rows).
enum State : int { kStatePoison = 2 };

// Objects: the two town portal classes (objects.txt rows; the first is what a scroll or tome makes).
enum ObjectClass : uint32_t { kTownPortal = 59, kPermanentTownPortal = 60 };
constexpr uint32_t kObjectModeOpen = 2;  // a portal that can be used

// A monster's kind, from its unit data (+0x1A): the bits the game's own test (0x38E870) takes. A Herald is a
// terror zone's, and comes with its own minions.
enum MonsterTypeFlag : uint16_t {
  kSuperUnique = 0x02, kChampion = 0x04, kUnique = 0x08, kMinion = 0x10, kHerald = 0x200, kHeraldMinion = 0x400,
};

// An item's quality (its unit data +0x00) and flags (+0x18).
enum ItemQuality : int32_t {
  kQualityInferior = 1, kQualityNormal = 2, kQualitySuperior = 3, kQualityMagic = 4,
  kQualitySet = 5, kQualityRare = 6, kQualityUnique = 7, kQualityCrafted = 8,
};
enum ItemFlag : uint32_t { kItemSocketed = 0x800, kItemEthereal = 0x400000, kItemPreview = 0x80000000u };

// After sites::resolve: binds the stat helpers and the unit lookups. Returns
// whether the stat helpers are usable; every cheat needs them.
bool bind();
bool bound();

int32_t get_stat(Unit* u, int stat, uint16_t layer = 0);       // the total: base plus states and items
int32_t get_base_stat(Unit* u, int stat, uint16_t layer = 0);  // the unit's own base value
bool has_base_stat();
void set_stat(Unit* u, int stat, int32_t value, uint16_t layer = 0);  // sets the base value

uint32_t unit_type(Unit* u);
uint32_t unit_class(Unit* u);
uint32_t unit_id(Unit* u);
uint32_t unit_mode(Unit* u);
bool unit_dead_flag(Unit* u);
bool unit_is_dead(Unit* u);             // the dead flag, or a dying or dead mode
bool unit_can_be_attacked(Unit* u);     // the game's own flag: never on hydras and the like
bool unit_position(Unit* u, float* x, float* y);  // players and monsters: subtile coordinates
uint16_t monster_type_flags(Unit* u);   // MonsterTypeFlag bits, 0 for anything else
uint8_t unit_table_bank(Unit* u);       // which data tables the unit uses (1 classic, 2 lod, 3 rotw)

Unit* server_unit(uint32_t id, uint32_t type);  // the authoritative game's unit, or null
Unit* client_unit(uint32_t id, uint32_t type);  // the client's mirror, or null

// The local player: its id comes from D2RLoader's LocalPlayerReady event. The
// authoritative (server) unit is recognised by that id where the hooks meet it
// - every player unit passes through the stat-regeneration tick each frame -
// because the global server unit table does not list it. The client's mirror
// is looked up in the client unit table.
void set_difficulty(int d);  // 0 normal, 1 nightmare, 2 hell (from the loader's lifecycle events)
int difficulty();
void set_local_player_id(uint32_t id);
void clear_local_player();
uint32_t local_player_id();
bool is_local_player(Unit* u);         // a player unit carrying the local id
void note_local_server_player(Unit* u);  // the tick met it
Unit* local_server_player();           // the unit the tick met last in this game, or null
Unit* local_client_player();

bool is_online();  // the client is in an online (Battle.net) state

// The area the local player is in (the loader's LevelChanged event), -1 outside a game.
void set_current_level(int id);
int current_level();
bool in_town();

// Health bars. The client's side of the game, so the UI thread only: the
// client's unit tables change as its game loop runs there.
bool has_client_units();
size_t client_units(uint32_t type, Unit** out, size_t cap);  // every unit of a type the client knows
bool has_projection();   // the above-head point, the graphics mode and the render size are all known
bool hd_graphics();      // false in legacy graphics
bool render_size(int* w, int* h);  // what projected points are measured in
bool project_above_head(Unit* u, float* x, float* y);  // where the game draws above the unit (render pixels)
bool has_hostility();
bool is_hostile(Unit* me, Unit* other);  // the client's own rule: false for NPCs in town, pets, the mercenary
int panel_side();  // 0 no side panel, 1 right (inventory, skills), 2 left (character, stash, ...), 3 both; -1 unknown

// Items (loot, server thread). The item's unit data holds its quality (+0x00), its flags (+0x18) and its
// UniqueItems/SetItems row (+0x34).
int32_t item_quality(Unit* item);    // 0 when not an item
uint32_t item_flags(Unit* item);
int32_t item_file_index(Unit* item);  // -1 when unknown
int32_t item_level(Unit* item);       // its item level (+0x38), 0 when not an item; any thread
uint16_t item_version(Unit* item);    // its version (+0x40): 0 a classic item, else an expansion one

// An item's affixes (server thread, inside the item generator's affix steps). An item keeps three prefix ids and
// three suffix ids (ids of the game's combined MagicSuffix, MagicPrefix and AutoMagic table, 0 an empty slot) just
// before its unit data's +0x00. The picker is the one the magic, rare and crafted steps ask: a random affix of the
// kind the item can have by the game's rules (spawnable, its affix level inside the row's level window, the item's
// type among the row's types, rare rows only for rare and crafted items, a class's rows only on that class's items,
// no group the item has already, weighted by frequency); `forced` an id the request asks for (0 none). The affix's
// properties are applied to the item as it is picked. 0 when no affix fits.
bool has_affix_picker();
int affix_id(Unit* item, bool prefix, int slot);  // -1 unknown
int pick_affix(Unit* item, bool prefix, int32_t forced);
void set_affix(Unit* item, bool prefix, int slot, int id);
// The affix rows themselves (server thread): the table of the item's data-table bank, the combined MagicSuffix,
// MagicPrefix and AutoMagic rows (an affix id is its row's index plus one) and where each kind starts; whether the
// game lets a row's item types take the item; and a row's properties applied to the item, rolled as the picker rolls
// them when it applies what it picked.
constexpr uint32_t kAffixRowSize = 0x8C;
struct AffixTable {
  const uint8_t* rows = nullptr;
  uint32_t count = 0;
  uint32_t suffixes = 0, prefixes = 0, automagic = 0;  // the row index each kind starts at
  const uint8_t* row(int32_t id) const {
    return id >= 1 && static_cast<uint32_t>(id) <= count ? rows + static_cast<size_t>(id - 1) * kAffixRowSize
                                                          : nullptr;
  }
};
bool has_affix_rows();
bool affix_table(Unit* item, AffixTable* out);
bool affix_fits(Unit* item, const uint8_t* row);
void apply_affix(Unit* item, const uint8_t* row);
// An item's two seeds (any thread, guarded reads). The unit's (unit +0x30) is what item init rolled from: the
// durability, an armor's base defense, the picture; it is the one a save keeps. The item's own (item data +0x10) is
// what the generator rolled from: affixes, values, ethereal, sockets; a save does not keep it.
struct ItemSeeds {
  uint32_t unit_seed = 0;
  uint32_t item_seed = 0;
};
bool item_seeds(Unit* item, ItemSeeds* out);
// Take an item that lies on the ground out of the game, the way the game's own item expiry does:
// the clients are told the unit left its room, its collision footprint is cleared, it leaves the
// room's lists and is freed. Only for an item made this frame that nothing else holds.
bool has_item_removal();
bool remove_ground_item(void* game, Unit* item);
// The game's loot filter: whether the character's active profile shows the item (true when the
// filter is off or has no rules). Reads the item and the filter only.
bool has_loot_filter();
bool loot_filter_shows(Unit* item);
// The same test asked about an item that does not exist: one of this class, quality and ethereal-or-socketed.
// Of an item the filter reads its unit type, class and data-table bank and its data's quality and flags, and
// nothing else (the Items row says the rest): a stand-in with those five, in the plugin's own memory, goes to the
// game's test. Not for gold, which the filter judges by its amount (a stat).
bool loot_filter_would_show(uint8_t bank, uint32_t item_class, int32_t quality, bool ethereal_or_socketed);

// Pets (server thread). A monster's AI record (unit data +0x30) names its owner by unit type and
// id, the pair the game's own minion-owner lookup resolves: summons and the mercenary alike. The
// mercenary also carries the unit flag the game tests before it reads the hireling table.
bool owned_by_local_player(Unit* u);  // a monster the local player owns
bool is_mercenary(Unit* u);
// Which of a player's pet lists a unit is in: the game's own lookup, the first question of its pet removal. A player
// keeps a list per pet type (a PetType row: Revive's revives, a Decoy, skeletons, golems...); a monster it owns that
// is in none (one an item's Reanimate As raised) answers 0, as does anything that is no player's pet. Reads only.
bool has_pet_lookup();
int pet_type(Unit* player, uint32_t unit_id);

// The unit's own skills (not those an item grants) of its class, with the points spent in each:
// the skill list at +0x100, one node per skill (+0x00 its Skills row: u16 id, charclass at +0x2C;
// +0x08 the next node; +0x40 the base level; +0x4C the granting item's id, -1 for its own).
struct SkillPoints {
  int id;
  int level;
};
size_t own_skills(Unit* u, SkillPoints* out, size_t cap);
int skill_base_level(Unit* u, int skill_id);  // 0 when the unit has no such skill

// The game's own routines for a character's points (server thread, inside the tick). The reset is
// the Token of Absolution's: every skill point and attribute point spent comes back. The spenders
// are the server's handlers for the client's requests, with every check the game makes (class,
// prerequisites, required level, the most points a skill takes, points left).
bool has_respec();
bool respec(void* game, Unit* player);
bool has_spenders();
void spend_stat_points(void* game, Unit* player, int stat, int points);    // stat 0..3
void spend_skill_points(void* game, Unit* player, int skill, int points);

// The antidote potion's cure (server thread, inside the tick): every stat list the state has on the
// unit (64 a call at most) is taken off and freed - the unlink runs the list's end callback, which turns
// the state off and tells the clients - and the state is turned off if it is still on once none is left.
struct Cured {
  int lists = 0;         // stat lists taken off (a poison that stacks keeps one per source)
  bool cleared = false;  // the state was still on after them and was turned off here
};
bool has_cure();
Cured cure_state(void* game, Unit* u, int state);

// Whether a state is on a unit (server thread): the game's own test, the one the antidote's cure asks.
bool has_state_test();
bool state_on(Unit* u, int state);

// Where a unit stands (the thread that owns the unit): the level of its room, -1 when it has none. Read field
// by field, as the game's room getter and its level getter read them (a path's room, room +0x18 the DRLG room,
// +0x90 its level, +0x1F8 the id).
int unit_level_id(Unit* u);

// Town portals across acts (server thread, inside the portal hooks). The game record keeps an act it has made at
// its acts ({acts, count}; an act is made when a player first enters it, by the routine the act change calls).
// An object's destination is a level id in two bytes of its data (+0x08 the low, +0x78 the high; D2RCore widened
// it to 16 bits and owns the setter). The move to a level is the game's own, the one waypoints take: into the
// level's act first when the player stands in another, then to the spot of that kind in the level. A character's
// waypoints are kept per difficulty in its player data (+0x58).
bool has_town_travel();
int object_destination(Unit* object);                  // -1 when it is no object
void set_object_destination(Unit* object, int level);
bool act_loaded(void* game, int act);                  // act 0 .. 4
bool load_act(void* game, int act);                    // true when the game has the act afterwards
void warp_to_level(void* game, Unit* player, int level, int spot);
bool has_waypoint(void* game, Unit* player, int number);

// An item handed to a player (server thread) the way the game's quest reward giver hands one over, for an item that
// lies on no inventory and not on the ground (made, or taken out of one): into the inventory grid - a free spot of
// page 0, the inventory's own move (which counts what an item there gives), the client told of an item added, and
// the player's items worked out again when it is a charm whose stats count there - or, with no room, at the
// player's feet: the item made one that lies on the ground (its flag 0x4000 off, its page 0xFF, its body location 0,
// its mode 3, its place 0), a free spot on the ground near the player, the item put there and the clients told.
// Each false when it could not be done (the item is then still the caller's).
bool has_item_handover();
bool put_in_inventory(void* game, Unit* player, Unit* item);
bool put_at_feet(void* game, Unit* player, Unit* item);
// What is in an item's sockets taken out of it and kept (server thread), the way the cube's product routine takes it
// out when a recipe keeps it: each item of the item's inventory taken out (its stats leave the item's), its page
// 0xFF, its place 0, its body location 0, its mode 4. Returns how many were taken out (at most cap: the rest stay
// in the sockets).
bool has_socket_takeout();
int take_out_socketed(Unit* item, Unit** out, int cap);
// An item identified (server thread) the way an Identify scroll and Deckard Cain identify one: the game's identify
// routine sets the item's identified flag, counts a charm's stats where they count, tells the clients (of an item
// that is stored, worn or in the belt; not of one on the cursor) and records a unique or set item that dropped in
// the Chronicle of every player in the game. Nothing for an item that is identified already.
bool has_identify();
void identify_item(void* game, Unit* player, Unit* item);

// Keys (server thread, inside the key-use hook): whether the player carries a key the game's key use
// would take. The walk is the key use's own: its inventory getter, the item list, and its item-type
// test with the key's type, for an item stored in the inventory grid (not the stash or cube), with no
// item held on the cursor (the key use takes no key then). The inventory: +0x00 a magic, +0x10 the
// first item, +0x40 the cursor item; an item's data: +0x55 its page (0 the inventory grid), +0xB0 the
// next item.
bool has_key_check();
bool carries_key(Unit* player);

// The map, on the client's side (UI thread, where the game builds its rooms and its automap). The
// local player's room (its path +0x20) is an ActiveRoom: the part of a DRLG room built for play.
// ActiveRoom +0x18 is the DRLG room, DRLG room +0x90 its level; a level keeps the client's DRLG (one
// per act) at +0x1C8, its id at +0x1F8 and its DRLG rooms in a list (+0x10, next at +0x48). The DRLG
// keeps its automap callback at +0x838 (the server's game has no automap).
struct ClientLevel {
  void* level = nullptr;
  void* drlg = nullptr;
  int id = -1;
  uint8_t bank = 0;  // the local player's data-table bank, the byte the DRLG routines take
};
bool has_map_reveal();
bool automap_saves_any_size();  // D2RCore's checked cell count is in the automap serializer
bool client_level(ClientLevel* out);  // the level the local player stands in, on the client's DRLG
int automap_layer();                  // the automap's active layer, -1 before it has one
int level_layer(uint8_t bank, int level_id);  // the layer the game keeps a level's map on, -1 unknown
int level_type(uint8_t bank, int level_id);   // the level's type, what the automap's tile lookup is asked for (-1)
void* level_first_room(void* level);
void* room_next(void* room);
// The room built for play if it is not (the game's CreateActiveRoom), then all of it put on the automap
// by the client DRLG's own automap callback. False when the room could not be built.
bool reveal_room(uint8_t bank, void* room);
// ... the two halves: the room built (its ActiveRoom, null when it could not be), and a built room put on the automap.
void* build_room(uint8_t bank, void* room);
void reveal_built_room(void* active_room);

// Landmarks and named enemies on the map (client, UI thread: where the game builds its rooms and its automap). The
// automap keeps a layer per map (the active one is what it draws), each with lists of cells: the floor tiles', the
// walls', the units' icons. A cell is put on one the way the game's own reveal puts one on: a floor tile by the game's
// per-tile add (it marks the tile as on the map and asks the tile lookup which cell the tile is), an object's icon by
// the list insert with the key the game gives an object it has seen ({0, cell, pixels / 10 + (1, -3)}). A list keeps a
// key once. Only for the active layer, and only while it is the layer of the level the rooms are of.
bool has_map_tiles();    // the per-tile add (sites::map_facts().tiles)
bool has_map_objects();  // the list insert (sites::map_facts().objects)
bool map_put_tile(uint8_t bank, void* tile, void* room);  // onto the active layer's floor list
bool map_put_cell(uint16_t cell, int32_t x, int32_t y);   // onto its units' list
// Drawing inside the automap's own draw of a unit, with its view (the routine's second argument): a point of the map
// (pixels: a unit's path keeps its own, an object's are its subtiles' (x - y) * 16, (x + y) * 8) on the automap's
// screen, false when it falls outside the automap; one of the automap's eight unit markers there, a name above it.
bool has_map_draw();
bool map_point(const void* view, int32_t px, int32_t py, uint64_t* point);
float map_scale(const void* view);
void map_marker(uint64_t point, int marker, float scale);
void map_name(const char* text, uint64_t point, float scale, int color);
int map_name_color();                     // the color the game draws an NPC's name in there
const char* client_unit_name(Unit* u);    // the client's name of a unit: a unique monster's own name (UI thread)
bool unit_pixels(Unit* u, int32_t* x, int32_t* y);  // where a unit is drawn (its path's pixels), any type

// Three keys, on the client's side (UI thread, where the game runs its key bindings and opens its panels):
// the Automap key (Tab) and the two Show Items keys. What they show is read where the game's own getters
// read it: the automap panel's UI var, and a byte per Show Items key. It is changed only the way a key press
// changes it: the key-action table's press function for the key (0x18 bytes per action id: press, release,
// flags), which is the game's own handler, or the wrapper D2RCore put in its place - D2RCore wraps the
// Automap and Show Items keys to remember them for the session, so a press through the table keeps its
// memory in step. Each Show Items key has a display mode among the game's options (Item Name Display,
// Unfiltered Item Name Display): Hold (on while the key is held), Toggle (a press turns it on or off) or
// Timed (a press shows the names for a while).
enum KeyAction : int { kKeyAutomap = 0, kKeyShowItems, kKeyShowItemsUnfiltered, kKeyActionCount };
enum ItemNameDisplay : int { kDisplayUnknown = -1, kDisplayHold = 0, kDisplayToggle = 1, kDisplayTimed = 2 };
bool has_key_action(KeyAction a);      // its table entry checked out at bind, and its state can be read
bool key_action_wrapped(KeyAction a);  // the entry runs D2RCore's wrapper of the game's handler
bool press_key_action(KeyAction a);    // one press; false when the entry no longer holds what was checked
int key_action_state(KeyAction a);     // 1 on (the automap open), 0 off, -1 unknown
int item_name_display(KeyAction a);    // a Show Items key's display mode (ItemNameDisplay); -1 for the automap

}  // namespace d2rcc::game

#pragma once

#include <cstddef>
#include <cstdint>

// The game routines the cheats use, found by byte signature in the decrypted
// D2R.exe image - never by a fixed address. The RVAs recorded beside each
// signature are where the D2R 3.2.92777 / 3.3.93847 research (RuffnecKk, MIT)
// found the routine; they only seed the log's "moved by" column.
namespace d2rcc::sites {

// D2RCore (the loader) itself patches the first 6-10 bytes of the stat helpers
// with a jump into its own extended-stats code, so their signatures start past
// that patch (Spec::offset) and they are only ever called, never hooked.
enum Id : int {
  kGetUnitStat = 0,      // STATLIST_GetUnitStat (unit, statId, u16 layer) -> int32
  kSetUnitStat,          // STATLIST_SetUnitStat (unit, statId, value, u16 layer)
  kAddUnitStat,          // STATLIST_AddUnitStat (unit, statId, delta, u16 layer)
  kGetUnitBaseStat,      // STATLIST_GetUnitBaseStat (unit, statId, u16 layer) -> int32
  kSetEvent,             // EVENT_SetEvent (game, unit, eventType, frame, customId, customParam, arg7)
  kExecuteEvents,        // SUNITDMG_ExecuteEvents (game, attacker, defender, D2Damage*): applies the damage
  kFinalizeDamage,       // SUNITDMG_FinalizeDamage (game, attacker, defender, D2Damage*): status, mode, death
  kPlayerStatRegen,      // D2GAME_PLAYER_ApplyStatRegen (game, unit, a3, a4): every frame per player
  kDeathHandler,         // (game, player, killer): the gold penalty and the experience loss
  kClientGetLocalPlayer, // CLIENT_GetLocalPlayer (dataContext) -> client unit; also names the client unit table
  kGetUnitByIdAndType,   // (id, type) -> unit; two copies, one per unit table (client and server)
  kIsOnlineState,        // CLIENT_IsOnlineState () -> bool
  kPlayerAddExperience,  // PLAYER_AddExperience (game, player, level, amount): every experience gain
  kGetManaCost,          // D2Common_SKILLMANA_GetManaCost (dataCtx u8, skillId, level) -> cost in 256ths
  kManaCostServerCall,   // the call to it inside D2GAME_SKILLMANA_Consume (the unit is in rsi there)
  kManaCostClientCall,   // the client's call to it before its own mana check (the unit is in rbx there)
  kDesecrateCurrentZone,  // DesecrateGetCurrentScheduledZone (bool* changed) -> zone record, or 0
  kDesecrateConfigForTime,  // (int64 utcSeconds, bool* changed) -> the config record (zones at +0x250, count +0x258)
  kDesecrateJob,         // () : applies the current zone to every game and tells the clients (called at rotations)
  kTerrorShardUse,       // a Worldstone Shard's use (item-use callback, pSpell 15): its act's manual zone applied as
                         // manual terror; its calls name the terror routines (derived below)
  kClientGetLevelName,   // CLIENT_GetLevelName (u8 dataCtx, levelId) -> UTF-8 name: the automap, the waypoints, the HUD
  kAutomapUpdate,        // the automap panel's update: builds its party text
  kAutomapInfoUpdate,    // the automap's info text (area name, difficulty, version, terror zone), a virtual update
  kWaypointPopulate,     // the waypoint panel's fill: sets each waypoint button's label
  // Health bars: where the game draws above a unit, and who is an enemy (called on the UI thread).
  kProjectAboveHead,     // (unit, float2 offset) -> x | y << 32 in render pixels, or -1: the point above the unit's head
  kIsHdGraphics,         // () -> bool: HD graphics (false in legacy graphics, where the projection does not apply)
  kRenderWidth,          // () -> int: the width the projected points are measured in
  kRenderHeight,         // () -> int: ... and the height
  kIsHostile,            // (unit, other, -1) -> nonzero when other is an enemy of unit (the client's own rule)
  kPanelSideSet,         // (side): records which side panels are open (0 none, 1 right, 2 left, 3 both)
  kUnitListNext,         // the unit hash tables' bucket walk: names the next-unit field
  // Loot: the treasure-class drops, the item generator, the property roll, item creation, the loot filter.
  kTcDropCore,           // TreasureClassDropCore (game, source, killer, tc, forcedQuality, ilvl, skipNoDrop, out, outCount, outMax)
  kItemQualityStep,      // (item, request) -> quality: the item generator's first step, for every freshly made item
  kSocketRollFlags,      // the socket roll's test of the request flags (8 never socketed, 0x10 socketed): checked only
  kPropertyRoll,        // (unit, a, b) -> a value in [min(a,b), max(a,b)]: every property value an item gets
  kCreateItem,           // (game, request, use the request's seeds) -> item: every item the game makes; with the
                         // third argument the unit's seed is the request's +0x58 and the item's its +0x5C
  kLootFilterShown,      // (manager, item) -> bool: the game's loot filter shows the item
  kLootFilterCallPair,   // the ground labels' `call manager; ...; call shown`: names the manager getter
  kUnitGetRoom,          // UNITS_GetRoom (unit) -> room
  kRoomRemovedUnit,      // (room, type, id): tell the clients a unit left the room
  kUnitFreeCollision,    // (unit): clear the unit's collision footprint
  kRoomUnlinkUnit,       // (unit): take the unit off its room's unit lists
  kFreeUnit,             // SUNIT_FreeUnit (game, unit)
  // The property roll's call sites that choose rather than roll a value (call instructions): a
  // skill id (skill-rand), a class (randclassskill), one stat of several, the two property-group picks,
  // and the param a property group's entry names within a range (a skill tab), in each of the three pick modes.
  kRollPickSkill,
  kRollPickClass,
  kRollPickStat,
  kRollPickGroupA,
  kRollPickGroupB,
  kRollPickGroupParam,   // pick mode 0 (every entry), in the group dispatcher
  kRollPickGroupParamA,  // pick mode 1, in the routine of group pick A
  kRollPickGroupParamB,  // pick mode 2, in the routine of group pick B
  kUniqueBitSet,         // the unique generator's `or [game+disp], bit`: names the "dropped this game" bits
  // Consumables: whether a used item is removed (the client's use request and the server's check
  // both ask), and the tome quantity update with the item-use effect's call that takes a charge.
  kShouldRemoveOnUse,    // ITEMS_ShouldRemoveOnUse (item) -> bool: a used scroll or potion is removed
  kSyncItemQuantity,     // (game, player, item, delta): a tome's (or stack's) quantity and its bound skill
  kItemUseBookDebit,     // the item-use effect's `call` to it with delta -1 for a tome (call instruction)
  // Character: the Token of Absolution's reset, and the server's handlers for spending points
  // (the client's 0x3A / 0x3B requests, 5 bytes: opcode, u16 id, u16 extra points).
  kResetStatsAndSkills,  // D2GAME_PLAYER_ResetStatsAndSkills (game, player) -> bool (al)
  kAllocateStatPoints,   // (game, player, packet, size): spends 1 + extra attribute points
  kAllocateSkillPoints,  // (game, player, packet, size): spends 1 + extra skill points
  // Cannot be poisoned: the antidote potion's cure (its item-use callback), per state it cures. Its five
  // calls name the routines (derived below); the site is the first call instruction.
  kCureStates,
  // Infinite keys: the server's key use, which a locked chest's or door's operate function asks for a key.
  kUseKey,               // (game, player) -> int: 1 once it took a key from the inventory, 0 when there is none
  // Map reveal (the client's side, UI thread): a room built for play, and the automap's reveal of a whole room.
  kCreateActiveRoom,     // DRLGROOM_CreateActiveRoom (u8 bank, DrlgRoom*) -> ActiveRoom*: tiles, collision, the room
  kAutomapRoomCallback,  // (ActiveRoom*): the client DRLG's automap callback: every tile of the room onto its layer
  kAutomapSaveCheck,     // the automap serializer's checked 32-bit cell count (D2RCore's): a layer of any size saves
  // Kept between games (UI thread): the key handlers the game's key-action table runs for Tab and the two Show
  // Items keys, and the tail that hands a key binding to that table.
  kShowItemsPress,       // (which: 0 Show Items, 1 Show Items (Unfiltered)): Toggle mode flips it, Hold/Timed turn it on
  kShowItemsRelease,     // (which): Hold mode turns it off; names the two display-mode settings and the on/off bytes
  kAutomapKey,           // () : the Automap key: toggles the automap panel, then asks the panel's UI var
  kKeyActionSelect,      // `lea rdx, [table]` then the press or release dispatch: names the key-action table
  // No durability loss (server thread): the two routines that take durability from a player's gear.
  kItemWear,             // (game, owner, item): a hit's wear roll on one item, the weapon of a player whose melee hit
                         // lands or one armor piece of a player hit; 1 durability, broken (armor) at 0
  kImpaleWear,           // (game, player, weapon, chance, amount): Impale's wear on the weapon it was used with
  // Item level: the server's item writer puts a level in every item it sends a client, the real one only when the
  // item's Items row has ShowLevel (a placeholder 1 otherwise); the client's item-name builder adds it after the name.
  kItemLevelSend,        // the writer's `jne` to its real-level branch after the ShowLevel test (network form only)
  kItemNameBuild,        // (item, char name[0x400]): the inventory's item name (inv.cpp), " (N)" for ShowLevel rows
  // Max affixes (server thread): the item generator's affix steps by quality. Each fills the item's three prefix and
  // three suffix slots through one affix picker and writes them with two slot setters (derived below).
  kMagicAffixes,         // (Unit** item, request) -> 1: a prefix on a coin, a suffix on a coin (sure without a prefix)
  kRareAffixes,          // (item, request) -> 1 when it made any: the rare names, then a rolled count of affixes, at
                         // most 3 a side (expansion items; classic ones have their own step)
  kCraftedAffixes,       // (item, request) -> 1: the rare names, then 1..4 affixes by item level
  // Perfect rolls: a class item's "+skill" lines (0..3 of them, each +1..+3), rolled in a step of their own.
  kClassSkillBonus,      // (game, item, item level, first skill, skill count, bonus): the bonus adds to both rolls
  // ... and a superior item's kind (its QualityItems row), which the superior step picks: a roll on the item's own
  // seed, then the game's test whether the kind it came to goes on the item; the first that does is the item's.
  kSuperiorStep,         // (item, request) -> 1 when the item got a kind (read, not hooked): how many kinds it
                         // picks among, and that it asks the test below
  kSuperiorFits,         // (item, QualityItems row) -> nonzero when the kind goes on the item; its one caller is
                         // the step
  // Best affixes (server thread): the affix picker the magic and crafted steps call (hooked), the game's test that
  // an affix row can go on an item, and the affix table of a data-table bank.
  kAffixPicker,          // (item, spawnable only, must pick, apply its properties, prefix, forced id) -> affix id or 0:
                         // an expansion item goes to D2RCore's picker, a classic one to D2RCore's classic picker
  kAutoAffixPicker,      // ... its twin with a seventh argument, an AutoMagic group, handed on to D2RCore's picker:
                         // the automatic affix, a base's own (a paladin shield's all res), the generator's last pick
  kAffixFitsItem,       // (item, affix row) -> nonzero when the row's item types take the item (and a socket affix
                         // only a base that can have sockets)
  kAffixTable,           // (u8 bank) -> the affix table: +0x00 rows, +0x08 count, +0x18 / +0x20 / +0x28 where the
                         // suffixes, prefixes and automagic rows start (0x8C bytes a row; an id is the index plus one)
  // Cannot be cursed (server thread): the two steps that put a monster's curse on one unit. The curse itself is put
  // on by a routine D2RCore replaced at its entry (it can be called, not hooked); these are its callers.
  kCurseSkillStep,       // (target, context) -> 1 when cursed: a curse skill's step for one unit in its range (the
                         // necromancer's curses, Defense Curse, Blood Mana, the Oblivion Knights' random curse)
  kCursedModStep,        // (game, monster, target, level): the "Cursed" monster modifier's Amplify Damage, for one
                         // unit near the monster
  // Infinite gold (server thread): what every vendor takes its price with.
  kVendorPay,            // (game, player, amount) -> 1 when paid: gold from the inventory, the rest from the stash
  // The home town (server thread, but for the last): the portals a scroll or tome makes, their use, the game's
  // own way from one act to another, a death's wake, and the client's trip through a portal ahead of the server.
  kTownPortalStart,      // (game, player, item) -> al: a town portal's cast: the portal beside the player and the one
                         // in town made, the first kept with the player until the cast is over
  kPortalTownEnd,        // (game, player, portal, destination level, source level) -> the portal made at the
                         // destination, the two linked (any act that is loaded)
  kPortalOperate,        // (context) -> 1 when used: a portal's use (the object operate function of both town
                         // portal classes): the player moved to the other portal, in the same act
  kWarpToLevel,          // (game, player, level, spot): the player moved to a level, into its act first when it is
                         // in another (waypoints, the Hellgate, the travel the act's people offer)
  kLoadAct,              // (game, u8 act): an act made for the game, its town first (the act change's own call)
  kWaypointActive,       // (waypoint data, u16 number) -> nonzero when the character has that waypoint
  kWakeInTown,           // (game, player, packet, size) -> 0: the server's handler of the request a dead player's
                         // client sends: life, mana and stamina filled, the player moved to the town of the act it
                         // died in, then out of its dead mode
  kClientPortalTrip,     // client, UI thread: (player, portal) -> al: whether the client makes the trip through a
                         // town portal itself, ahead of the server (it builds the rooms at the portal's other end
                         // in the act it is in and moves its player there, then asks the server)
  // NPC item services without their quests (Charsi's imbue, Larzuk's sockets, Anya's personalize). The server's
  // handler of the request reads the NPC's record of the game's NPC table and asks the quest the record names for
  // a reward to be had; the client puts a service in an NPC's menu by a table of its own, walked as the menu opens.
  kNpcRecord,            // (u8 bank, NPC class) -> the NPC's record: names the NPC table (read, never called)
  kNpcItemService,       // (game, player, packet, second pass) -> int: the server's handler of an NPC's item
                         // service (read, not hooked): how it reads the record, and what each service does
  kNpcMenuOpen,          // client, UI thread: (NPC, after its speech): an NPC's menu opened: the entries its
                         // quests add, each by the routine its row of the client's table names, then the menu
  // Infinite cube ingredients (server thread). A transmute's second pass hands the recipe it matched to the product
  // routine, which takes every item out of the cube, makes the products, frees what Clear Sockets takes out of the
  // item it changes and every ingredient it used up, and puts the products in the cube. The quest reward giver puts
  // an item in a player's inventory, or at the player's feet, with the game's own routines (derived below).
  kCubeProducts,         // (game, player, recipe, its ingredients as matched): a recipe's products made
  kItemFree,             // (game, item): an item that lies on no inventory and not on the ground freed, with what
                         // is in its sockets
  kSocketedFree,         // (game, item): what is in an item's sockets freed
  kItemGive,             // (game, player, item code, level, quality, at the feet if no room) -> the item: a quest
                         // reward made and handed over (read, not called): its calls name the routines
  // God mode (any thread): whether a stat of a kind of unit may go down, asked by D2RCore's four stat writers (set
  // and add, of a unit and of a stat list) before they lower a value. It reads the game's no-damage switches.
  kProtectedStat,        // (stat, unit type) -> al: nonzero when the value may not go down
  // The Chronicle (read, never called or hooked): the uniques, set items and runewords a player has found, kept in
  // its player data; the routines say where, how its maps keep their entries, and which rows are entries at all.
  kChronicleOfPlayer,    // (player) -> its Chronicle: the player data plus an offset
  kChronicleFind,        // (Chronicle, u8 kind: 0 set item, 1 unique, 2 runeword, u32 id) -> the entry or 0: where the
                         // kinds' maps are, and a map's layout
  kChronicleShare,       // (Chronicle, u8 kind) -> float: the share of the kind's entries found; names the game's
                         // own lists of the entries there are, and the counts it divides by
  // Identify on pickup (server thread). The pick-up routine moves an item from the ground into the player's
  // inventory, belt or cube, or onto the cursor, and then tells the clients with the item notice; the identify
  // routine is the one an Identify scroll and Deckard Cain call.
  kItemNotice,           // (game, player, item, flags, command, mode, page, body location, x | y << 16): the clients
                         // told where an item went (124 callers; hooked, acting only on the pick-up routine's call)
  kItemPickup,           // (player, item id, to the inventory, reach, path test, into the cube) -> al: an item picked
                         // up from the ground (read, not hooked): its last call is the notice of the item's new place
  kItemIdentify,         // (game, player, item, u8 the player's own): an item identified, the client told, a unique
                         // or set item recorded in the Chronicle
  // Passive mercenary and minions (server thread). Every monster's AI tick goes through one dispatcher, and every way
  // an AI finds a target asks one enemy test, but for a target kept on the monster, read through one getter.
  kAiDispatch,           // (game, monster, 0, 0, 0): a monster's AI tick: a target found first for the AIs that take
                         // one handed in, then the AI's own routine (hooked)
  kEnemyTest,            // (game, unit, other, u8 flag in r9b) -> nonzero when the other unit is an enemy of the unit
                         // or of its owner (hooked)
  kEnemyTestEntries,     // its two entry stubs, `mov r9b, 1` and `xor r9d, r9d` each before a jump to it: its only
                         // callers, the first the one 39 calls go to (read, not hooked)
  kAiEnemyCheck,         // (game, unit, other) -> nonzero for an enemy: the AI's own check, the unit searches'
                         // filter; it and the one right after it end in a jump to the first stub (read)
  kKeptTarget,           // (monster) -> the kind of the target kept on it (1 a player, 2 a monster, 3 a unit near, 4
                         // a missile; 0 none): the Warlock's demons keep theirs there (Attract and Confuse set one
                         // on hostile monsters); hooked
  // Permanent revives (server thread). Revive gives the monster it raises the killself monster mod and a game event
  // at the end of its time; the event runs the mod's timer, which takes a player's pet out of its owner's pet list and
  // kills it. The game's lookup of which pet list a unit is in tells a revive from the other pets the timer ends.
  kKillSelfTimer,        // (game, monster, mod, the monster's unique flag): the killself mod's timer: a monster with the
                         // uninterruptable state has the event again 3 frames on, else a player's pet is taken out of
                         // its owner's pet list and killed, any other monster put in its death mode (hooked)
  kPetTypeOf,            // (player, unit id) -> the pet type of the player's pet list the unit is in, 0 none: the first
                         // question of the pet removal the timer calls (called, read only)
  kCount
};

// Addresses derived from the routines above (data the routines refer to).
enum Derived : int {
  dClientUnitTable = 0,  // the client's unit hash table
  dServerUnitTable,      // the authoritative game's unit hash table
  dServerGetUnit,        // the (id, type) lookup that uses the server table
  dClientGetUnit,        // ... and the client table
  dInvulnerablePlayers,  // byte: players take no damage (the game's own debug switch)
  dInvulnerableMonsters, // byte: monsters take no damage
  dPanelSide,            // int: which side panels are open (0 none, 1 right, 2 left, 3 both)
  dLootFilterManager,    // () -> the loot filter's manager (a singleton getter)
  // The antidote's cure calls, in order.
  dStatListByState,      // (unit, state) -> the unit's first stat list for the state, or 0
  dUnlinkStatList,       // (unit, list): off the unit; the list's end callback turns its state off
  dFreeStatList,         // (u8 dataCtx, list)
  dStateOn,              // (unit, state) -> nonzero when the state is on
  dToggleState,          // (unit, state, on): the state's bit, and the unit queued so the clients hear
  // The key use's own calls: the player's inventory, and the item-type test it asks with the key's type.
  dGetInventory,         // (unit, const char* file, int line) -> the unit's inventory
  dItemTypeTest,         // ITEMS_CheckItemTypeId (item, item type) -> nonzero when the item is of that type
  // The automap callback's: the active automap layer, and the level record that names a level's layer.
  dAutomapLayer,         // a pointer to the active layer (its +0x00 is the layer id), 0 before the automap starts
  dLevelDefRecord,       // (u8 bank, level id) -> the level's record (+0x08: the automap layer its map is kept on)
  // The Show Items release handler's calls: the two display-mode settings, their value, and the on/off setter
  // (which names the on/off bytes).
  dItemNameDisplay,      // () -> the "Item Name Display" setting (the value is 0 hold, 1 toggle, 2 timed)
  dUnfilteredNameDisplay,  // () -> the "Unfiltered Item Name Display" setting
  dSettingValue,         // (setting) -> its value
  dShowItemsState,       // {u8* on, u64 count}: on[0] Show Items, on[1] Show Items (Unfiltered)
  dUiVars,               // the classic UI vars: a byte per panel id, nonzero while the panel is open
  dKeyActions,           // the key-action table: {press, release, flags}, 0x18 bytes per action id
  // A Worldstone Shard's use and the zone job's rotation: the terror routines (named only when both make the same
  // calls, and the game and config layouts terror.cpp reads are where their code has them).
  dTerrorEnabled,        // (game) -> bool: the game has terror zones
  dTerrorApply,          // (game, zone, u8 kind): the zone's levels terrorized (1 the rotation's, 2 manual); D2RCore's
  dTerrorClientUpdate,   // (game): every client in the game is sent the terrorized levels
  dTerrorRemoveKind,     // (game, u8 kind): one kind of terror taken off, then the levels waiting behind it applied
  // The magic affix step's calls: the affix picker, and the prefix and suffix slot setters (item, id, slot).
  dPickAffix,            // (item, spawnable only, must pick, apply its mods, prefix, forced id) -> affix id or 0
  dSetPrefix,
  dSetSuffix,
  // The rare step's: its two picks call D2RCore's picker straight (the call instructions, suffix then prefix), and
  // after its loop each slot's affix is applied with the game's apply routine.
  dRarePickSuffix,
  dRarePickPrefix,
  dApplyAffix,           // (0, 0, item, affix row, 0, 0): the row's properties onto the item, rolled as the picker does
  // The curse steps' call: what puts a state with its stats on a unit for a time (D2RCore's at its entry).
  dCurseApply,
  // The town portal's: the pair maker its cast calls (which calls the town end maker), and the setter of an
  // object's destination the town end maker calls (D2RCore's).
  dPortalPair,
  dSetObjectDest,        // (object, u16 level)
  dCount
};

struct Spec {
  Id id;
  const char* name;
  const char* pattern;  // IDA/CE style, ?? = wildcard
  int offset;           // the pattern starts this many bytes into the routine
  uint64_t seed_rva;    // where the 92777 research found it (0 = unknown)
};

struct Result {
  uint64_t rva = 0;  // the routine's RVA: the first match minus Spec::offset (0 = none)
  int hits = 0;      // how many matches the image has (1 is what a usable site needs)
  uint64_t all[4] = {};  // up to four match RVAs, for the sites that legitimately have two
};

const Spec& spec(Id id);
const Result& result(Id id);
inline bool found(Id id) { return result(id).hits == 1; }
uintptr_t address(Id id);      // exe base + rva, or 0 when not found exactly once
uintptr_t derived(Derived d);  // 0 when not derived
int unit_next_offset();        // the unit field that links a hash bucket's units (0 when not derived)
int unique_bits_offset();      // the game's "unique dropped this game" bits (a dword per 32 rows; 0 when not derived)
int key_item_type();           // the item type the key use looks for (ItemTypes row; 0 when not derived)
int automap_panel_id();        // the UI var the Automap key asks about after its toggle (0 when not derived)
// What the affix steps are made of, read from their own code and data (a zero: not derived).
struct AffixFacts {
  int prefix_slots = 0;  // where the item data keeps its three prefix ids (the prefix setter's store; before +0x00)
  int suffix_slots = 0;  // ... and its three suffix ids, right after them
  int jewel_type = 0;    // the item type the rare step counts as a jewel
  int rare_max = 0;      // the rare step's highest count (the top of its table)
  int jewel_max = 0;     // ... and a jewel's (a base plus a coin)
  int crafted_max = 0;   // the crafted step's highest count (its top item-level minimum, the top of its roll)
  int skill_bonus = 0;   // a class skill step bonus that puts its count roll and every level roll at their tops
  bool auto_same = false;  // the automatic affix's picker calls the affix picker's two pickers, with its group
};
const AffixFacts& affix_facts();
// How the superior step picks a kind, read from its own code (not known: nothing is taken for granted).
struct SuperiorFacts {
  bool known = false;       // the step counts its kinds and asks the hooked test the way it was read
  int few = 0;              // the kinds a base that is thrown or has no durability picks among, from the first on
  int no_durability_at = 0;  // where an Items row says the base has no durability (a byte)
  int thrown_at = 0;        // where an ItemTypes row says the type is thrown (a byte)
};
const SuperiorFacts& superior_facts();
// What the curse steps and the town portal are made of, read from their own code (a zero: not derived).
struct CurseFacts {
  int state_offset = 0;      // where a curse skill's step keeps the state it puts on, in its context
  int amplify_skill = 0;     // the skill the Cursed monster modifier casts (Amplify Damage)
  int skill_state = 0;       // where a Skills row has the state the skill puts on its targets
};
const CurseFacts& curse_facts();
struct PortalFacts {
  int portal_class = 0;      // the object class of the portal a town portal's cast makes
  int town_spot = 0;         // the kind of spot in a town where its portals stand
  int used_state = 0;        // the state a player has for a moment after a portal's use
  int class_offset = 0;      // where a portal's use finds the object's class in its context
  int acts_offset = 0;       // where the game record keeps its acts ({acts, count})
  int other_level_offset = 0;  // where a client's portal keeps the level its other end stands in (a u16 of the unit)
};
const PortalFacts& portal_facts();
// A death's wake, read from its handler's code.
struct WakeFacts {
  bool known = false;        // the handler asks for the dead mode and moves the player with the move to a level
  int dead_mode = 0;         // the mode a player must be in to be woken
  int town_spot = 0;         // the kind of spot in the town where it wakes (0: where a game starts, too)
};
const WakeFacts& wake_facts();
// The NPC item services, read from the code of the three routines above (not known: nothing is taken for granted).
struct NpcFacts {
  bool known = false;        // all of it was found, and the server's and the client's side fit together
  uintptr_t records = 0;     // the NPC table: a record per NPC, the NPC's class (int16) first
  int record_size = 0;
  int record_count = 0;
  int service_at = 0;        // where a record has the item service its NPC does (0 none)
  int quest_at = 0;          // ... and the quest that asks for it, plus one (0: no quest asks)
  int quest_max = 0;         // the highest the handler takes there
  int pending_flag = 0;      // the quest flag the handler asks for: a reward to be had
  int imbue = 0;             // the services' numbers, each told by what the handler does for it: an item made
  int sockets = 0;           // again as a rare one, the socketed flag set, the personalized flag set
  int personalize = 0;
  uintptr_t menu_rows = 0;   // the client's rows: {int32 NPC class, int32 quest, int32 flag, int32 value, routine}
  uintptr_t menu_count = 0;  // where their count is (an int32)
  int menu_row_size = 0;
};
const NpcFacts& npc_facts();
// Infinite cube ingredients, read from the code of the four routines above (not known: nothing is taken for granted).
struct CubeFacts {
  bool known = false;             // all of it was found, and the routines the four call in common are the same ones
  uintptr_t ingredient_free = 0;  // where the product routine's free of an ingredient it used up returns to
  uintptr_t sockets_free = 0;     // ... and its free of what Clear Sockets takes out of the sockets
  // How the quest reward giver puts an item in the inventory:
  uintptr_t page_grid = 0;        // (player, page, not classic) -> the grid of an inventory page
  uintptr_t grid_spot = 0;        // (inventory, item, grid, u16* x, u16* y, page) -> nonzero with a free spot there
  uintptr_t inventory_move = 0;   // (player, places, unit ids, count, 0) -> al: the items put where the places say
  uintptr_t item_notice = 0;      // (game, player, item, flags, command, mode, page, body location, x | y << 16):
                                  // the client told of an item (the product routine tells it the same way)
  uintptr_t charm_counts = 0;     // (item, player) -> nonzero when the item's stats count where it lies (a charm)
  uintptr_t refresh_items = 0;    // (game, player, 0, 0): what the player's items give, worked out again
  // ... and at the player's feet (the product routine's own "keep the socket contents" uses the same setters):
  uintptr_t item_flags = 0;       // (item, flags, on)
  uintptr_t item_page = 0;        // (item, page)
  uintptr_t item_body = 0;        // (item, body location)
  uintptr_t unit_mode = 0;        // (unit, mode)
  uintptr_t unit_x = 0;           // (unit, x)
  uintptr_t unit_y = 0;           // (unit, y)
  uintptr_t unit_coords = 0;      // (unit, int32 out[2])
  uintptr_t ground_spot = 0;      // (game, room, int32 at[2], int32 out[2], u8 1) -> the room of a free spot, or 0
  uintptr_t ground_put = 0;       // (game, player, item, room, x, y): the item on the ground there, the clients told
  // How the product routine takes the items out of a socketed item and keeps them:
  uintptr_t first_item = 0;       // (inventory) -> its first item, or 0
  uintptr_t take_out = 0;         // (inventory, item) -> the item, taken out of it (0 when it was not in it)
};
const CubeFacts& cube_facts();
// The Chronicle, read from the code of the three routines above (not known: nothing is taken for granted).
struct ChronicleFacts {
  bool known = false;
  int of_player = 0;             // where a player's data keeps its Chronicle
  int sets_at = 0;               // where the Chronicle keeps its map of set items found (kind 0)
  int uniques_at = 0;            // ... of uniques found (kind 1)
  int count_at = 0;              // a map: how many buckets (a power of two)
  int buckets_at = 0;            // ... the buckets
  int next_at = 0;               // an entry: the next of its bucket
  int key_at = 0;                // ... its id (u32): the row
  uintptr_t unique_list = 0;     // the game's uniques that are entries: {data, count}, a pointer to the row each,
  uintptr_t unique_count = 0;    // the row's id its first 16 bits
  uintptr_t unique_total = 0;    // how many it counts (u64)
  uintptr_t set_list = 0;        // ... the set items and the sets: {row, u64 tag} each, a set item tagged 0
  uintptr_t set_count = 0;
  uintptr_t set_total = 0;       // how many set items it counts
  int set_entry = 0;             // bytes an entry of the set list
};
const ChronicleFacts& chronicle_facts();
// Identify on pickup, read from the code of the three routines above (not known: nothing is taken for granted).
struct IdentifyFacts {
  bool known = false;          // the pick-up routine's notice of the item's new place and the identify routine's
                               // notice both call the hooked notice routine
  uintptr_t pickup_notice = 0;  // where the pick-up routine's notice of the item's new place returns to
  uint32_t identified = 0;     // the item flag the identify routine tests and sets
};
const IdentifyFacts& identify_facts();
// The protected-stat test reads the no-damage switches ExecuteEvents reads (players first, monsters last).
bool protected_stat_checked();
// The enemy test is what the AI asks: its two entry stubs jump to it, and the AI's own two enemy checks end in a jump
// to the first.
bool enemy_test_checked();
// Permanent revives, read from the code of the two routines above (not known: nothing is taken for granted).
struct ReviveFacts {
  bool known = false;         // the killself timer removes a player's pet with its kill flag, through a routine that
                              // asks the pet lookup above first
  uintptr_t pet_removal = 0;  // (game, owner, unit id, kill): the pet out of its owner's pet list, killed
};
const ReviveFacts& revive_facts();
uintptr_t call_target(Id site);  // where a call-site entry's `call rel32` goes (0 when unknown or not a call)
uintptr_t exe_base();
// The live bytes at a routine's entry (the signature's span), for the loader's expected-bytes checks.
size_t entry_bytes(Id id, uint8_t* out, size_t capacity);

// True once the image is decrypted: the probe signature (a routine the loader does not patch) matches.
bool image_ready(uintptr_t exe_base);
// Scan the image's .text once; logs every result. Returns how many sites matched exactly once.
int resolve(uintptr_t exe_base);
// After resolve: the data addresses the routines refer to. Logs each.
void derive();
// Write the image as it is in memory to `path` (fix_dump.py makes it loadable). Returns bytes written.
size_t dump_image(uintptr_t exe_base, const wchar_t* path);

}  // namespace d2rcc::sites

#include "game.h"

#include <windows.h>

#include <cstdio>
#include <cstring>

#include "log.h"
#include "mem.h"
#include "sites.h"

namespace d2rcc::game {
namespace {

using GetStatFn = int32_t(__fastcall*)(Unit*, int32_t, uint16_t) noexcept;
using SetStatFn = void(__fastcall*)(Unit*, int32_t, int32_t, uint16_t) noexcept;
using LookupFn = Unit*(__fastcall*)(uint32_t id, uint32_t type) noexcept;
using IsOnlineFn = bool(__fastcall*)() noexcept;
// (unit, float2 offset packed x | y << 32) -> the rounded point x | y << 32, or -1 when the unit is not drawn.
using AboveHeadFn = uint64_t(__fastcall*)(Unit* unit, uint64_t offset) noexcept;
using IsHdFn = bool(__fastcall*)() noexcept;
using DimensionFn = int32_t(__fastcall*)() noexcept;
using IsHostileFn = int32_t(__fastcall*)(Unit* unit, Unit* other, int32_t unused) noexcept;
using GetRoomFn = void*(__fastcall*)(Unit* unit) noexcept;
using RoomRemovedFn = void(__fastcall*)(void* room, uint32_t type, uint32_t id) noexcept;
using UnitFn = void(__fastcall*)(Unit* unit) noexcept;
using FreeUnitFn = void(__fastcall*)(void* game, Unit* unit) noexcept;
using ManagerFn = void*(__fastcall*)() noexcept;
using ShownFn = bool(__fastcall*)(void* manager, Unit* item) noexcept;
using RespecFn = bool(__fastcall*)(void* game, Unit* player) noexcept;  // `mov al, 1`: only al is set
using PacketHandlerFn = void(__fastcall*)(void* game, Unit* player, const uint8_t* packet, int32_t size) noexcept;
using StatListByStateFn = void*(__fastcall*)(Unit* unit, int32_t state) noexcept;
using UnlinkStatListFn = void(__fastcall*)(Unit* unit, void* list) noexcept;
using FreeStatListFn = void(__fastcall*)(uint32_t dataCtx, void* list) noexcept;
using StateOnFn = uint32_t(__fastcall*)(Unit* unit, int32_t state) noexcept;
using ToggleStateFn = void(__fastcall*)(Unit* unit, int32_t state, int32_t on) noexcept;
// The key use's calls: the inventory (the file and line are only for the getter's own assert) and the
// item-type test.
using GetInventoryFn = void*(__fastcall*)(Unit* unit, const char* file, int32_t line) noexcept;
using ItemTypeTestFn = int32_t(__fastcall*)(Unit* item, int32_t type) noexcept;
// The map: a DRLG room built for play (the bank in the low byte), the automap's reveal of a built room,
// and a level's record.
using CreateActiveRoomFn = void*(__fastcall*)(uint32_t bank, void* room) noexcept;
using AutomapRoomFn = void(__fastcall*)(void* active_room) noexcept;
using LevelDefRecordFn = const void*(__fastcall*)(uint32_t bank, int32_t level_id) noexcept;
// The key-action table's press functions take nothing (a Show Items thunk passes its key's index itself), the
// display-mode settings are function-local statics behind their getters, and a setting's value is its low byte.
using KeyPressFn = void(__fastcall*)() noexcept;
using SettingFn = void*(__fastcall*)() noexcept;
using SettingValueFn = uint32_t(__fastcall*)(void* setting) noexcept;
// The affix steps' picker (item, spawnable rows only, must pick (else a coin decides), apply the affix's properties,
// prefix (else suffix), forced id) -> the affix id or 0, and their slot setters (item, id, slot).
using PickAffixFn = int32_t(__fastcall*)(Unit* item, int32_t spawnable, int32_t must, int32_t apply, int32_t prefix,
                                         int32_t forced) noexcept;
using SetAffixFn = void(__fastcall*)(Unit* item, int32_t id, int32_t slot) noexcept;
// Best affixes: a bank's affix table, whether an affix row's item types take an item, and a row's properties applied
// to an item (the kind 0 of the apply routine: an affix; the rare step passes zeros for the rest).
using AffixTableFn = const void*(__fastcall*)(uint32_t bank) noexcept;
using AffixFitsFn = int32_t(__fastcall*)(Unit* item, const void* row) noexcept;
using ApplyAffixFn = void(__fastcall*)(int32_t kind, void* source, Unit* item, const void* row, int32_t a5,
                                       int32_t a6) noexcept;
// The home town: an object's destination set (D2RCore's), an act made for the game, the move to a
// level (the spot is the kind of place in the level the player is put at), and the waypoint test.
using SetObjectDestFn = void(__fastcall*)(Unit* object, uint32_t level) noexcept;
using LoadActFn = void(__fastcall*)(void* game, uint32_t act) noexcept;
using WarpToLevelFn = void(__fastcall*)(void* game, Unit* player, int32_t level, int32_t spot) noexcept;
using WaypointActiveFn = uint32_t(__fastcall*)(const void* waypoints, uint32_t number) noexcept;
// An item handed to a player: the quest reward giver's calls (sites.cpp reads them from its code). A place in an
// inventory is four int32 {mode, page, x | y << 16, body location}; the grid spot writes x and y as int32s.
using PageGridFn = uint32_t(__fastcall*)(Unit* player, uint32_t page, uint32_t not_classic) noexcept;
using GridSpotFn = int32_t(__fastcall*)(void* inventory, Unit* item, uint32_t grid, int32_t* x, int32_t* y,
                                        uint32_t page) noexcept;
using InventoryMoveFn = uint8_t(__fastcall*)(Unit* player, const int32_t* places, const uint32_t* ids, uint64_t count,
                                             uint32_t flag) noexcept;
using ItemNoticeFn = void(__fastcall*)(void* game, Unit* player, Unit* item, uint32_t flags, uint32_t command,
                                       int32_t mode, uint32_t page, uint32_t body, uint32_t position) noexcept;
using CharmCountsFn = int32_t(__fastcall*)(Unit* item, Unit* player) noexcept;
using RefreshItemsFn = void(__fastcall*)(void* game, Unit* player, void* a3, int32_t a4) noexcept;
using ItemFlagsFn = void(__fastcall*)(Unit* item, uint32_t flags, uint32_t on) noexcept;
using UnitValueFn = void(__fastcall*)(Unit* unit, uint32_t value) noexcept;  // page, body location, mode, x, y
using UnitCoordsFn = void(__fastcall*)(Unit* unit, int32_t* out) noexcept;  // {x, y}
using GroundSpotFn = void*(__fastcall*)(void* game, void* room, const int32_t* at, int32_t* out,
                                        uint32_t flag) noexcept;
using GroundPutFn = void(__fastcall*)(void* game, Unit* player, Unit* item, void* room, int32_t x, int32_t y) noexcept;
using FirstItemFn = Unit*(__fastcall*)(void* inventory) noexcept;
using TakeOutFn = Unit*(__fastcall*)(void* inventory, Unit* item) noexcept;
// An item identified (game, player, item, u8: 1 the player's own, as an Identify scroll and Cain call it; a unique or
// set item that dropped then goes into every player's Chronicle, else the player's alone).
using IdentifyFn = void(__fastcall*)(void* game, Unit* player, Unit* item, uint32_t own) noexcept;
// Which of a player's pet lists a unit is in (player, unit id) -> the pet type, 0 when it is in none.
using PetTypeFn = int32_t(__fastcall*)(Unit* player, uint32_t unit_id) noexcept;
// Item requirements: the restricted-socket test (unit, item, body location) -> al, nonzero when another item the unit
// wears shares a restricted socket type with this one; a player's live pet of a type (game, player, type, 0: a gone
// one does not count) -> the unit or 0.
using RestrictedFn = bool(__fastcall*)(Unit* unit, Unit* item, int32_t body_location) noexcept;
using PetOfTypeFn = Unit*(__fastcall*)(void* game, Unit* player, int32_t type, int32_t any) noexcept;
// The automap: a tile put on a layer's list (u8 bank, the tile, its DRLG room, the list), a cell put on one (the list,
// {node, bool} out, the key: u16 0, u16 cell, int32 x, int32 y), a point of the map on the automap's screen (the view,
// out, x | y << 32 pixels) -> out, one of its unit markers (x | y << 32, marker, scale), a name (UTF-8, x | y << 32,
// scale, color), and the client's name of a unit.
using MapPutTileFn = void(__fastcall*)(uint32_t bank, void* tile, void* room, void* list) noexcept;
using MapInsertFn = void*(__fastcall*)(void* list, void* out, const void* key) noexcept;
using MapPointFn = uint64_t*(__fastcall*)(const void* view, uint64_t* out, uint64_t pixels) noexcept;
using MapMarkerFn = void(__fastcall*)(uint64_t point, uint32_t marker, float scale) noexcept;
using MapNameFn = void(__fastcall*)(const char* text, uint64_t point, float scale, uint32_t color) noexcept;
using UnitNameFn = const char*(__fastcall*)(Unit* unit) noexcept;

GetStatFn g_get = nullptr;
GetStatFn g_get_base = nullptr;
SetStatFn g_set = nullptr;
LookupFn g_server_lookup = nullptr;
LookupFn g_client_lookup = nullptr;
IsOnlineFn g_is_online = nullptr;
AboveHeadFn g_above_head = nullptr;
IsHdFn g_is_hd = nullptr;
DimensionFn g_render_w = nullptr;
DimensionFn g_render_h = nullptr;
IsHostileFn g_is_hostile = nullptr;
GetRoomFn g_get_room = nullptr;
RoomRemovedFn g_room_removed = nullptr;
UnitFn g_free_collision = nullptr;
UnitFn g_room_unlink = nullptr;
FreeUnitFn g_free_unit = nullptr;
ManagerFn g_lf_manager = nullptr;
ShownFn g_lf_shown = nullptr;
RespecFn g_respec = nullptr;
PacketHandlerFn g_spend_stats = nullptr;
PacketHandlerFn g_spend_skills = nullptr;
StatListByStateFn g_list_by_state = nullptr;
UnlinkStatListFn g_unlink_list = nullptr;
FreeStatListFn g_free_list = nullptr;
StateOnFn g_state_on = nullptr;
ToggleStateFn g_toggle_state = nullptr;
GetInventoryFn g_get_inventory = nullptr;
ItemTypeTestFn g_item_type = nullptr;
CreateActiveRoomFn g_create_room = nullptr;
AutomapRoomFn g_automap_room = nullptr;
LevelDefRecordFn g_level_def = nullptr;
SettingFn g_name_display[2] = {};  // Item Name Display, Unfiltered Item Name Display
SettingValueFn g_setting_value = nullptr;
PickAffixFn g_pick_affix = nullptr;
SetAffixFn g_set_prefix = nullptr;
SetAffixFn g_set_suffix = nullptr;
AffixTableFn g_affix_table = nullptr;
AffixFitsFn g_affix_fits = nullptr;
ApplyAffixFn g_apply_affix = nullptr;
SetObjectDestFn g_set_object_dest = nullptr;
LoadActFn g_load_act = nullptr;
WarpToLevelFn g_warp = nullptr;
WaypointActiveFn g_waypoint_active = nullptr;
PageGridFn g_page_grid = nullptr;
GridSpotFn g_grid_spot = nullptr;
InventoryMoveFn g_inventory_move = nullptr;
ItemNoticeFn g_item_notice = nullptr;
CharmCountsFn g_charm_counts = nullptr;
RefreshItemsFn g_refresh_items = nullptr;
ItemFlagsFn g_item_flags = nullptr;
UnitValueFn g_item_page = nullptr;
UnitValueFn g_item_body = nullptr;
UnitValueFn g_unit_mode = nullptr;
UnitValueFn g_unit_x = nullptr;
UnitValueFn g_unit_y = nullptr;
UnitCoordsFn g_unit_coords = nullptr;
GroundSpotFn g_ground_spot = nullptr;
GroundPutFn g_ground_put = nullptr;
FirstItemFn g_first_item = nullptr;
TakeOutFn g_take_out = nullptr;
IdentifyFn g_identify = nullptr;
PetTypeFn g_pet_type = nullptr;
RestrictedFn g_restricted = nullptr;
PetOfTypeFn g_pet_of_type = nullptr;
RefreshItemsFn g_items_refresh = nullptr;  // the same routine as g_refresh_items, bound by what the requirements read
MapPutTileFn g_map_put_tile = nullptr;
MapInsertFn g_map_insert = nullptr;
MapPointFn g_map_point = nullptr;
MapMarkerFn g_map_marker = nullptr;
MapNameFn g_map_name = nullptr;
UnitNameFn g_unit_name = nullptr;
uintptr_t g_key_press[kKeyActionCount] = {};  // the entry's press function as bind checked it (0: unusable)
bool g_key_wrapped[kKeyActionCount] = {};
constexpr int kPanels = 32;     // the panel gate's (its walk: `cmp ebx, 20h`)
uint32_t g_automap_closers = 0;  // the panels whose opening closes the automap, a bit each
bool g_cure_failed = false;  // an unlink left its list on the unit: the cure stays off
volatile LONG g_local_id = -1;
volatile LONG g_difficulty = 0;
volatile LONG g_level = -1;
Unit* volatile g_local_server = nullptr;
bool g_bound = false;

// Unit fields read here only (see game.h for the ones the hooks share).
constexpr uintptr_t kUnitData = 0x10;
constexpr uintptr_t kUnitPath = 0x38;
constexpr uintptr_t kUnitFlags = 0x124;
constexpr uintptr_t kUnitBank = 0x1BD;
constexpr uintptr_t kMonsterTypeFlags = 0x1A;  // in the unit data
constexpr uintptr_t kUnitSeed = 0x30;           // the seed the unit was made with (a save keeps an item's)
constexpr uintptr_t kItemDataSeed = 0x10;       // an item's unit data: the seed its own random stream started from
constexpr uintptr_t kItemDataFlags = 0x18;      // ... its flags
constexpr uintptr_t kItemDataFileIndex = 0x34;  // ... and its UniqueItems/SetItems row
constexpr uintptr_t kItemDataLevel = 0x38;      // ... and its item level
constexpr uintptr_t kItemDataVersion = 0x40;    // ... and its version (u16; 0 classic)
constexpr int kAffixSlots = 3;                  // prefix slots, and as many suffix slots
constexpr uint32_t kFlagCanBeAttacked = 0x4;
constexpr uint32_t kFlagDead = 0x10000;
constexpr uint32_t kFlagMercenary = 0x200;
// A monster's AI record (monster data +0x30): the game it lives in, and its owner's id and unit type.
constexpr uintptr_t kMonsterAi = 0x30;
constexpr uintptr_t kAiGame = 0x60;
constexpr uintptr_t kAiOwnerId = 0x68;
constexpr uintptr_t kAiOwnerType = 0x6C;
// Skills: the unit's list, its nodes, and the two fields of a Skills row read here.
constexpr uintptr_t kUnitSkills = 0x100;
constexpr uintptr_t kSkillRow = 0x00;
constexpr uintptr_t kSkillNext = 0x08;
constexpr uintptr_t kSkillBaseLevel = 0x40;
constexpr uintptr_t kSkillOwnerItem = 0x4C;
constexpr uintptr_t kSkillRowClass = 0x2C;
constexpr int kMaxSkillNodes = 512;
constexpr uintptr_t kGameDataCtx = 0x106;  // Game: the data-table context byte the stat list free takes
constexpr uintptr_t kGameDifficulty = 0x104;  // ... and its difficulty (0 normal, 1 nightmare, 2 hell)
// An object's data: its destination level, low and high byte (the getter 0x34AD40, D2RCore's setter).
constexpr uintptr_t kObjectDestLow = 0x08;
constexpr uintptr_t kObjectDestHigh = 0x78;
// A player's data: its waypoints, one record per difficulty (the waypoint travel 0x5933F0 reads them so).
constexpr uintptr_t kPlayerWaypoints = 0x58;
constexpr uint32_t kMaxWaypoints = 0x70;  // the waypoint test's own bound
constexpr int kActs = 5;
constexpr int kMaxCureLists = 64;          // per tick; more are taken off on the next
// The inventory and an item's place in it (the key use's accessors: 0x388C10, 0x388A70, 0x38ABA0, 0x36CFE0).
constexpr uint32_t kInventoryMagic = 0x01020304;
constexpr uintptr_t kInventoryFirstItem = 0x10;
constexpr uintptr_t kInventoryCursorItem = 0x40;
constexpr uintptr_t kItemDataPage = 0x55;   // 0 the inventory grid, 3 the cube, 4 the stash, 0xFF none
constexpr uintptr_t kItemDataNext = 0xB0;
constexpr uint32_t kItemModeStored = 0;
constexpr int kMaxInventoryItems = 1024;
// The client's map: the room a path is in, and the DRLG room, level and DRLG fields the game's own
// accessors read (0x341C30, 0x192B20, 0x360FC0, 0x328FD0, InitLevel 0x3271C0, the act setup 0x326BA0).
constexpr uintptr_t kPathRoom = 0x20;
constexpr uintptr_t kActiveRoomDrlgRoom = 0x18;
constexpr uintptr_t kRoomLevel = 0x90;
constexpr uintptr_t kRoomNext = 0x48;
constexpr uintptr_t kLevelFirstRoom = 0x10;
constexpr uintptr_t kLevelDrlg = 0x1C8;
constexpr uintptr_t kLevelId = 0x1F8;
constexpr uintptr_t kRoomActive = 0x58;     // a DRLG room: its ActiveRoom once built (CreateActiveRoom answers it)
constexpr uintptr_t kDrlgAutomapCallback = 0x838;
constexpr uintptr_t kLevelDefLayer = 0x08;  // the Levels record: its automap layer (the automap callback reads it)
// The key-action table: the ids of the three keys (the order of the game's key bindings, the ids its .key
// files store), the size of an entry, and where its release function sits in it.
constexpr int kKeyActionIds[kKeyActionCount] = {7, 37, 67};
constexpr uintptr_t kKeyActionEntry = 0x18;
constexpr uintptr_t kKeyActionRelease = 0x08;
// The table's thunks for the Show Items keys: Show Items asks a stub for its index (0) and jumps to the
// handler; Show Items (Unfiltered) loads 1 and jumps. Each shape is followed by the jump's rel32.
constexpr const char* kShowItemsThunk = "48 83 EC 28 E8 ?? ?? ?? ?? 0F B6 C8 48 83 C4 28 E9";
constexpr const char* kUnfilteredThunk = "B1 01 E9";
// The client's unit tables: 128 buckets of unit pointers per unit type, a bucket
// picked by id & 127 (GetUnitByIdAndType's `and edx, 7Fh` / `shl rcx, 0Ah`).
constexpr uint32_t kBuckets = 128;
constexpr uintptr_t kTypeStride = kBuckets * sizeof(uintptr_t);

template <typename Fn>
Fn at(sites::Id id) {
  return sites::found(id) ? reinterpret_cast<Fn>(sites::address(id)) : nullptr;
}

// A jump the loader put in the game's image: `jmp [rip+disp]` to code outside it (D2RCore's own).
bool loader_jump(uintptr_t fn) {
  uint8_t op[2] = {};
  int32_t disp = 0;
  if (!fn || !mem::copy_from(op, fn, sizeof(op)) || op[0] != 0xFF || op[1] != 0x25 || !mem::read_safe(fn + 2, &disp))
    return false;
  const uintptr_t target = mem::read_ptr(fn + 6 + static_cast<intptr_t>(disp));
  return target && !mem::module_range(sites::exe_base()).contains(target);
}

// A thunk whose bytes are `shape` and whose last instruction jumps to `target` (the rel32 after the shape).
bool thunk_to(uintptr_t fn, const char* shape, uintptr_t target) {
  const mem::Pattern p = mem::parse_pattern(shape);
  const size_t n = p.bytes.size();
  uint8_t code[32] = {};
  int32_t rel = 0;
  if (!fn || !target || n + sizeof(rel) > sizeof(code) || !mem::copy_from(code, fn, n + sizeof(rel)) ||
      mem::find_all(code, n, p, 1).empty())
    return false;
  std::memcpy(&rel, code + n, sizeof(rel));
  return fn + n + sizeof(rel) + static_cast<intptr_t>(rel) == target;
}

uintptr_t key_slot(int id, uintptr_t field) {
  const uintptr_t table = sites::derived(sites::dKeyActions);
  uintptr_t v = 0;
  return table && mem::read_safe(table + static_cast<uintptr_t>(id) * kKeyActionEntry + field, &v) ? v : 0;
}

// Each entry must hold its key's function: the game's handler (or thunk), or a loader jump in its place
// where D2RCore wraps the key. The release functions are never wrapped, so they pin the Show Items entries.
void bind_key_actions() {
  const uintptr_t automap = sites::address(sites::kAutomapKey);
  const uintptr_t press = sites::address(sites::kShowItemsPress);
  const uintptr_t release = sites::address(sites::kShowItemsRelease);
  for (int a = 0; a < kKeyActionCount; ++a) {
    g_key_press[a] = 0;
    g_key_wrapped[a] = false;
  }
  if (!sites::derived(sites::dKeyActions)) return;
  // The Automap key has no release function.
  uintptr_t p = key_slot(kKeyActionIds[kKeyAutomap], 0);
  if (automap && p && !key_slot(kKeyActionIds[kKeyAutomap], kKeyActionRelease) && (p == automap || loader_jump(p))) {
    g_key_press[kKeyAutomap] = p;
    g_key_wrapped[kKeyAutomap] = p != automap;
  }
  p = key_slot(kKeyActionIds[kKeyShowItems], 0);
  if (thunk_to(key_slot(kKeyActionIds[kKeyShowItems], kKeyActionRelease), kShowItemsThunk, release) &&
      (thunk_to(p, kShowItemsThunk, press) || loader_jump(p))) {
    g_key_press[kKeyShowItems] = p;
    g_key_wrapped[kKeyShowItems] = !thunk_to(p, kShowItemsThunk, press);
  }
  p = key_slot(kKeyActionIds[kKeyShowItemsUnfiltered], 0);
  if (thunk_to(p, kUnfilteredThunk, press) &&
      thunk_to(key_slot(kKeyActionIds[kKeyShowItemsUnfiltered], kKeyActionRelease), kUnfilteredThunk, release))
    g_key_press[kKeyShowItemsUnfiltered] = p;
}

const char* key_binding_note(KeyAction a) {
  return !has_key_action(a) ? "MISSING" : g_key_wrapped[a] ? "bound (D2RCore's wrapper)" : "bound (the game's)";
}

// The automap's row of the panel gate's rules: what the gate does to the open automap as each panel opens (1 closes
// it). The rules are the game's static data.
void bind_automap_closers() {
  g_automap_closers = 0;
  const uintptr_t rules = sites::derived(sites::dPanelRules);
  const int automap = sites::automap_panel_id();
  uintptr_t row = 0;
  int32_t rule[kPanels] = {};
  if (!rules || automap <= 0 || automap >= kPanels ||
      !mem::read_safe(rules + static_cast<uintptr_t>(automap) * sizeof(uintptr_t), &row) || !row ||
      !mem::copy_from(rule, row, sizeof(rule)))
    return;
  for (int p = 0; p < kPanels; ++p)
    if (rule[p] == 1 && p != automap) g_automap_closers |= 1u << p;
}

}  // namespace

bool bind() {
  g_get = at<GetStatFn>(sites::kGetUnitStat);
  g_get_base = at<GetStatFn>(sites::kGetUnitBaseStat);
  g_set = at<SetStatFn>(sites::kSetUnitStat);
  g_server_lookup = reinterpret_cast<LookupFn>(sites::derived(sites::dServerGetUnit));
  g_client_lookup = reinterpret_cast<LookupFn>(sites::derived(sites::dClientGetUnit));
  g_is_online = at<IsOnlineFn>(sites::kIsOnlineState);
  g_above_head = at<AboveHeadFn>(sites::kProjectAboveHead);
  g_is_hd = at<IsHdFn>(sites::kIsHdGraphics);
  g_render_w = at<DimensionFn>(sites::kRenderWidth);
  g_render_h = at<DimensionFn>(sites::kRenderHeight);
  g_is_hostile = at<IsHostileFn>(sites::kIsHostile);
  g_get_room = at<GetRoomFn>(sites::kUnitGetRoom);
  g_room_removed = at<RoomRemovedFn>(sites::kRoomRemovedUnit);
  g_free_collision = at<UnitFn>(sites::kUnitFreeCollision);
  g_room_unlink = at<UnitFn>(sites::kRoomUnlinkUnit);
  g_free_unit = at<FreeUnitFn>(sites::kFreeUnit);
  g_lf_manager = reinterpret_cast<ManagerFn>(sites::derived(sites::dLootFilterManager));
  g_lf_shown = at<ShownFn>(sites::kLootFilterShown);
  g_respec = at<RespecFn>(sites::kResetStatsAndSkills);
  g_spend_stats = at<PacketHandlerFn>(sites::kAllocateStatPoints);
  g_spend_skills = at<PacketHandlerFn>(sites::kAllocateSkillPoints);
  g_list_by_state = reinterpret_cast<StatListByStateFn>(sites::derived(sites::dStatListByState));
  g_unlink_list = reinterpret_cast<UnlinkStatListFn>(sites::derived(sites::dUnlinkStatList));
  g_free_list = reinterpret_cast<FreeStatListFn>(sites::derived(sites::dFreeStatList));
  g_state_on = reinterpret_cast<StateOnFn>(sites::derived(sites::dStateOn));
  g_toggle_state = reinterpret_cast<ToggleStateFn>(sites::derived(sites::dToggleState));
  g_get_inventory = reinterpret_cast<GetInventoryFn>(sites::derived(sites::dGetInventory));
  g_item_type = reinterpret_cast<ItemTypeTestFn>(sites::derived(sites::dItemTypeTest));
  g_create_room = at<CreateActiveRoomFn>(sites::kCreateActiveRoom);
  g_automap_room = at<AutomapRoomFn>(sites::kAutomapRoomCallback);
  g_level_def = reinterpret_cast<LevelDefRecordFn>(sites::derived(sites::dLevelDefRecord));
  g_name_display[0] = reinterpret_cast<SettingFn>(sites::derived(sites::dItemNameDisplay));
  g_name_display[1] = reinterpret_cast<SettingFn>(sites::derived(sites::dUnfilteredNameDisplay));
  g_setting_value = reinterpret_cast<SettingValueFn>(sites::derived(sites::dSettingValue));
  g_pick_affix = reinterpret_cast<PickAffixFn>(sites::derived(sites::dPickAffix));
  g_set_prefix = reinterpret_cast<SetAffixFn>(sites::derived(sites::dSetPrefix));
  g_set_suffix = reinterpret_cast<SetAffixFn>(sites::derived(sites::dSetSuffix));
  g_affix_table = at<AffixTableFn>(sites::kAffixTable);
  g_affix_fits = at<AffixFitsFn>(sites::kAffixFitsItem);
  g_apply_affix = reinterpret_cast<ApplyAffixFn>(sites::derived(sites::dApplyAffix));
  g_set_object_dest = reinterpret_cast<SetObjectDestFn>(sites::derived(sites::dSetObjectDest));
  g_load_act = at<LoadActFn>(sites::kLoadAct);
  g_warp = at<WarpToLevelFn>(sites::kWarpToLevel);
  g_waypoint_active = at<WaypointActiveFn>(sites::kWaypointActive);
  {
    const sites::CubeFacts& c = sites::cube_facts();
    const bool known = c.known;
    g_page_grid = known ? reinterpret_cast<PageGridFn>(c.page_grid) : nullptr;
    g_grid_spot = known ? reinterpret_cast<GridSpotFn>(c.grid_spot) : nullptr;
    g_inventory_move = known ? reinterpret_cast<InventoryMoveFn>(c.inventory_move) : nullptr;
    g_item_notice = known ? reinterpret_cast<ItemNoticeFn>(c.item_notice) : nullptr;
    g_charm_counts = known ? reinterpret_cast<CharmCountsFn>(c.charm_counts) : nullptr;
    g_refresh_items = known ? reinterpret_cast<RefreshItemsFn>(c.refresh_items) : nullptr;
    g_item_flags = known ? reinterpret_cast<ItemFlagsFn>(c.item_flags) : nullptr;
    g_item_page = known ? reinterpret_cast<UnitValueFn>(c.item_page) : nullptr;
    g_item_body = known ? reinterpret_cast<UnitValueFn>(c.item_body) : nullptr;
    g_unit_mode = known ? reinterpret_cast<UnitValueFn>(c.unit_mode) : nullptr;
    g_unit_x = known ? reinterpret_cast<UnitValueFn>(c.unit_x) : nullptr;
    g_unit_y = known ? reinterpret_cast<UnitValueFn>(c.unit_y) : nullptr;
    g_unit_coords = known ? reinterpret_cast<UnitCoordsFn>(c.unit_coords) : nullptr;
    g_ground_spot = known ? reinterpret_cast<GroundSpotFn>(c.ground_spot) : nullptr;
    g_ground_put = known ? reinterpret_cast<GroundPutFn>(c.ground_put) : nullptr;
    g_first_item = known && c.sockets_free ? reinterpret_cast<FirstItemFn>(c.first_item) : nullptr;
    g_take_out = known && c.sockets_free ? reinterpret_cast<TakeOutFn>(c.take_out) : nullptr;
  }
  g_identify = sites::identify_facts().known ? at<IdentifyFn>(sites::kItemIdentify) : nullptr;
  g_pet_type = sites::revive_facts().known ? at<PetTypeFn>(sites::kPetTypeOf) : nullptr;
  {
    const sites::RequirementFacts& r = sites::requirement_facts();
    g_restricted = r.known ? reinterpret_cast<RestrictedFn>(r.restricted) : nullptr;
    g_pet_of_type = r.known && r.client_pets ? at<PetOfTypeFn>(sites::kPetOfType) : nullptr;
    g_items_refresh = r.known ? reinterpret_cast<RefreshItemsFn>(r.refresh) : nullptr;
  }
  {
    const sites::MapFacts& m = sites::map_facts();
    g_map_put_tile = m.tiles ? reinterpret_cast<MapPutTileFn>(m.tile_add) : nullptr;
    g_map_insert = m.objects ? reinterpret_cast<MapInsertFn>(m.list_insert) : nullptr;
    g_map_point = m.draw ? reinterpret_cast<MapPointFn>(m.to_map) : nullptr;
    g_map_marker = m.draw ? reinterpret_cast<MapMarkerFn>(m.draw_marker) : nullptr;
    g_map_name = m.draw ? reinterpret_cast<MapNameFn>(m.draw_name) : nullptr;
    g_unit_name = m.draw ? reinterpret_cast<UnitNameFn>(m.unit_name) : nullptr;
  }
  bind_key_actions();
  bind_automap_closers();
  g_cure_failed = false;
  g_bound = g_get && g_set && g_server_lookup;
  logf("game: stat helpers %s (base getter %s), unit lookups %s/%s, online check %s", g_get && g_set ? "bound" : "MISSING",
       g_get_base ? "bound" : "missing", g_server_lookup ? "server" : "-", g_client_lookup ? "client" : "-",
       g_is_online ? "bound" : "MISSING");
  logf("game: above-head point %s, graphics mode %s, render size %s, hostility %s, client unit list %s",
       g_above_head ? "bound" : "MISSING", g_is_hd ? "bound" : "MISSING", g_render_w && g_render_h ? "bound" : "MISSING",
       g_is_hostile ? "bound" : "MISSING", has_client_units() ? "known" : "MISSING");
  logf("game: ground item removal %s, loot filter %s, affix picker and slots %s, affix table, fit test and apply %s",
       has_item_removal() ? "bound" : "MISSING", has_loot_filter() ? "bound" : "MISSING",
       has_affix_picker() ? "bound" : "MISSING", has_affix_rows() ? "bound" : "MISSING");
  logf("game: respec %s, point spenders %s, antidote cure %s", has_respec() ? "bound" : "MISSING",
       has_spenders() ? "bound" : "MISSING", has_cure() ? "bound" : "MISSING");
  logf("game: town travel %s (an act made, the move to a level, the waypoint test, an object's destination)",
       has_town_travel() ? "bound" : "MISSING");
  logf("game: item handover %s (the quest reward giver's: into the inventory, else at the feet), socket contents "
       "take-out %s, identify %s, pet lookup %s",
       has_item_handover() ? "bound" : "MISSING", has_socket_takeout() ? "bound" : "MISSING",
       has_identify() ? "bound" : "MISSING", has_pet_lookup() ? "bound" : "MISSING");
  logf("game: item requirements: the restricted-socket test %s, a player's mercenary %s, the items worked out again %s",
       has_restricted_test() ? "bound" : "MISSING", has_pet_of_type() ? "bound" : "MISSING",
       has_items_refresh() ? "bound" : "MISSING");
  logf("game: key search %s (key item type 0x%X), map reveal %s, automap save %s", has_key_check() ? "bound" : "MISSING",
       sites::key_item_type(), has_map_reveal() ? "bound" : "MISSING",
       automap_saves_any_size() ? "has the checked cell count" : "NOT CHECKED");
  logf("game: on the map: a tile's add %s, a cell's insert %s, the unit draw's transform, marker and name %s",
       has_map_tiles() ? "bound" : "MISSING", has_map_objects() ? "bound" : "MISSING",
       has_map_draw() ? "bound" : "MISSING");
  char closers[48] = "no panel";
  if (!sites::derived(sites::dPanelRules)) {
    std::strcpy(closers, "NOT KNOWN");
  } else if (g_automap_closers) {
    std::strcpy(closers, "panel");
    for (int p = 0; p < kPanels; ++p) {
      const size_t n = std::strlen(closers);
      if (g_automap_closers >> p & 1) std::snprintf(closers + n, sizeof(closers) - n, " %d", p);
    }
  }
  logf("game: key actions: Automap %s, Show Items %s, Show Items (Unfiltered) %s; display modes %s; the game closes "
       "the automap as %s opens",
       key_binding_note(kKeyAutomap), key_binding_note(kKeyShowItems), key_binding_note(kKeyShowItemsUnfiltered),
       g_name_display[0] && g_name_display[1] && g_setting_value ? "bound" : "MISSING", closers);
  return g_bound;
}

bool bound() { return g_bound; }

int32_t get_stat(Unit* u, int stat, uint16_t layer) { return u && g_get ? g_get(u, stat, layer) : 0; }
int32_t get_base_stat(Unit* u, int stat, uint16_t layer) { return u && g_get_base ? g_get_base(u, stat, layer) : 0; }
bool has_base_stat() { return g_get_base != nullptr; }
void set_stat(Unit* u, int stat, int32_t value, uint16_t layer) {
  if (u && g_set) g_set(u, stat, value, layer);
}

uint32_t unit_type(Unit* u) {
  uint32_t v = 0xFFFFFFFFu;
  mem::read_safe(reinterpret_cast<uintptr_t>(u), &v);
  return v;
}
uint32_t unit_class(Unit* u) {
  uint32_t v = 0;
  mem::read_safe(reinterpret_cast<uintptr_t>(u) + 0x04, &v);
  return v;
}
uint32_t unit_id(Unit* u) {
  uint32_t v = 0xFFFFFFFFu;
  mem::read_safe(reinterpret_cast<uintptr_t>(u) + 0x08, &v);
  return v;
}
uint32_t unit_mode(Unit* u) {
  uint32_t v = 0;
  mem::read_safe(reinterpret_cast<uintptr_t>(u) + 0x0C, &v);
  return v;
}
bool unit_dead_flag(Unit* u) {
  uint32_t v = 0;
  mem::read_safe(reinterpret_cast<uintptr_t>(u) + kUnitFlags, &v);
  return (v & kFlagDead) != 0;
}

// The game's own test (0x34C2C0): the dead flag, or a monster in its death (0)
// or dead (12) mode, a player in death (0) or dead (17).
bool unit_is_dead(Unit* u) {
  if (!u || unit_dead_flag(u)) return true;
  const uint32_t type = unit_type(u);
  const uint32_t mode = unit_mode(u);
  if (type == kMonster) return mode == 0 || mode == 12;
  if (type == kPlayer) return mode == 0 || mode == 17;
  return false;
}

// The game's getter is 0x34F5A0. The client sets the flag from the monster's MonStats2
// row (isAtt) when it makes the unit, changes its class or revives it, so a hydra never
// has it; a few monster skills clear it for a while.
bool unit_can_be_attacked(Unit* u) {
  uint32_t v = 0;
  return mem::read_safe(reinterpret_cast<uintptr_t>(u) + kUnitFlags, &v) && (v & kFlagCanBeAttacked) != 0;
}

// Players and monsters walk a dynamic path whose position is 16.16 fixed point
// at +0 and +4 (the game's precision getters, 0x349D90 / 0x349DE0).
bool unit_position(Unit* u, float* x, float* y) {
  const uint32_t type = unit_type(u);
  if (type != kPlayer && type != kMonster) return false;
  const uintptr_t path = mem::read_ptr(reinterpret_cast<uintptr_t>(u) + kUnitPath);
  uint32_t px = 0, py = 0;
  if (!path || !mem::read_safe(path, &px) || !mem::read_safe(path + 4, &py)) return false;
  *x = static_cast<float>(px) / 65536.0f;
  *y = static_cast<float>(py) / 65536.0f;
  return true;
}

uint16_t monster_type_flags(Unit* u) {
  if (unit_type(u) != kMonster) return 0;
  const uintptr_t data = mem::read_ptr(reinterpret_cast<uintptr_t>(u) + kUnitData);
  uint16_t v = 0;
  return data && mem::read_safe(data + kMonsterTypeFlags, &v) ? v : 0;
}

uint8_t unit_table_bank(Unit* u) {
  uint8_t v = 0;
  mem::read_safe(reinterpret_cast<uintptr_t>(u) + kUnitBank, &v);
  return v;
}

Unit* server_unit(uint32_t id, uint32_t type) {
  if (!g_server_lookup || id == 0xFFFFFFFFu) return nullptr;
  Unit* u = g_server_lookup(id, type);
  return u && unit_type(u) == type && unit_id(u) == id ? u : nullptr;
}

Unit* client_unit(uint32_t id, uint32_t type) {
  if (!g_client_lookup || id == 0xFFFFFFFFu) return nullptr;
  Unit* u = g_client_lookup(id, type);
  return u && unit_type(u) == type && unit_id(u) == id ? u : nullptr;
}

void set_difficulty(int d) { InterlockedExchange(&g_difficulty, d); }
int difficulty() { return g_difficulty; }

void set_local_player_id(uint32_t id) {
  InterlockedExchange(&g_local_id, static_cast<LONG>(id));
  g_local_server = nullptr;
}
void clear_local_player() {
  InterlockedExchange(&g_local_id, -1);
  g_local_server = nullptr;
}
uint32_t local_player_id() { return static_cast<uint32_t>(g_local_id); }

bool is_local_player(Unit* u) {
  const uint32_t id = local_player_id();
  if (!u || id == 0xFFFFFFFFu) return false;
  if (u == g_local_server) return true;
  return unit_type(u) == kPlayer && unit_id(u) == id;
}

void note_local_server_player(Unit* u) { g_local_server = u; }
Unit* local_server_player() { return g_local_server; }

Unit* local_client_player() {
  const uint32_t id = local_player_id();
  return id == 0xFFFFFFFFu ? nullptr : client_unit(id, kPlayer);
}

bool is_online() { return g_is_online ? g_is_online() : false; }

void set_current_level(int id) { InterlockedExchange(&g_level, id); }
int current_level() { return g_level; }
bool in_town() {
  // Rogue Encampment, Lut Gholein, Kurast Docks, the Pandemonium Fortress, Harrogath.
  const int level = current_level();
  return level == 1 || level == 40 || level == 75 || level == 103 || level == 109;
}

bool has_client_units() { return sites::derived(sites::dClientUnitTable) && sites::unit_next_offset(); }

size_t client_units(uint32_t type, Unit** out, size_t cap) {
  const uintptr_t table = sites::derived(sites::dClientUnitTable);
  const int next = sites::unit_next_offset();
  if (!table || !next || type > kTile) return 0;
  size_t n = 0;
  for (uint32_t b = 0; b < kBuckets && n < cap; ++b) {
    uintptr_t u = mem::read_ptr(table + type * kTypeStride + b * sizeof(uintptr_t));
    // A unit belongs to the bucket its id picks; anything else ends the walk.
    for (int guard = 0; u && guard < 4096 && n < cap; ++guard) {
      uint32_t t = 0, id = 0;
      if (!mem::read_safe(u, &t) || !mem::read_safe(u + 0x08, &id) || t != type || (id & (kBuckets - 1)) != b) break;
      out[n++] = reinterpret_cast<Unit*>(u);
      u = mem::read_ptr(u + static_cast<uintptr_t>(next));
    }
  }
  return n;
}

bool has_projection() { return g_above_head && g_is_hd && g_render_w && g_render_h; }
bool hd_graphics() { return g_is_hd && g_is_hd(); }
bool render_size(int* w, int* h) {
  if (!g_render_w || !g_render_h) return false;
  *w = g_render_w();
  *h = g_render_h();
  return *w > 0 && *h > 0;
}

bool project_above_head(Unit* u, float* x, float* y) {
  if (!g_above_head || !u) return false;
  const uint64_t r = g_above_head(u, 0);
  if (r == ~0ull) return false;
  *x = static_cast<float>(static_cast<int32_t>(r & 0xFFFFFFFFu));
  *y = static_cast<float>(static_cast<int32_t>(r >> 32));
  return true;
}

bool has_hostility() { return g_is_hostile != nullptr; }
bool is_hostile(Unit* me, Unit* other) { return g_is_hostile && me && other && g_is_hostile(me, other, -1) != 0; }

int32_t item_quality(Unit* item) {
  if (unit_type(item) != kItem) return 0;
  const uintptr_t data = mem::read_ptr(reinterpret_cast<uintptr_t>(item) + kUnitData);
  int32_t v = 0;
  return data && mem::read_safe(data, &v) ? v : 0;
}

uint32_t item_flags(Unit* item) {
  if (unit_type(item) != kItem) return 0;
  const uintptr_t data = mem::read_ptr(reinterpret_cast<uintptr_t>(item) + kUnitData);
  uint32_t v = 0;
  return data && mem::read_safe(data + kItemDataFlags, &v) ? v : 0;
}

int32_t item_file_index(Unit* item) {
  if (unit_type(item) != kItem) return -1;
  const uintptr_t data = mem::read_ptr(reinterpret_cast<uintptr_t>(item) + kUnitData);
  int32_t v = -1;
  return data && mem::read_safe(data + kItemDataFileIndex, &v) ? v : -1;
}

int32_t item_level(Unit* item) {
  if (unit_type(item) != kItem) return 0;
  const uintptr_t data = mem::read_ptr(reinterpret_cast<uintptr_t>(item) + kUnitData);
  int32_t v = 0;
  return data && mem::read_safe(data + kItemDataLevel, &v) ? v : 0;
}

uint16_t item_version(Unit* item) {
  if (unit_type(item) != kItem) return 0;
  const uintptr_t data = mem::read_ptr(reinterpret_cast<uintptr_t>(item) + kUnitData);
  uint16_t v = 0;
  return data && mem::read_safe(data + kItemDataVersion, &v) ? v : 0;
}

bool has_affix_picker() {
  return g_pick_affix && g_set_prefix && g_set_suffix && sites::affix_facts().prefix_slots != 0;
}

int affix_id(Unit* item, bool prefix, int slot) {
  const sites::AffixFacts& f = sites::affix_facts();
  if (!has_affix_picker() || slot < 0 || slot >= kAffixSlots || unit_type(item) != kItem) return -1;
  const uintptr_t data = mem::read_ptr(reinterpret_cast<uintptr_t>(item) + kUnitData);
  // The slots sit before the data's +0x00: the offset is negative.
  const intptr_t offset = (prefix ? f.prefix_slots : f.suffix_slots) + slot * 4;
  int32_t id = -1;
  return data && mem::read_safe(data + static_cast<uintptr_t>(offset), &id) && id >= 0 ? id : -1;
}

int pick_affix(Unit* item, bool prefix, int32_t forced) {
  if (!has_affix_picker() || unit_type(item) != kItem) return 0;
  // The rare and crafted steps' own arguments: spawnable rows only, a pick for sure, and the properties applied now
  // (those steps apply theirs after their loop; this pick comes after it).
  const int32_t id = g_pick_affix(item, 1, 1, 1, prefix ? 1 : 0, forced);
  return id > 0 ? id : 0;
}

void set_affix(Unit* item, bool prefix, int slot, int id) {
  if (!has_affix_picker() || slot < 0 || slot >= kAffixSlots || unit_type(item) != kItem) return;
  (prefix ? g_set_prefix : g_set_suffix)(item, id, slot);
}

bool has_affix_rows() { return g_affix_table && g_affix_fits && g_apply_affix; }

bool affix_table(Unit* item, AffixTable* out) {
  *out = AffixTable{};
  if (!has_affix_rows() || unit_type(item) != kItem) return false;
  // The record the picker reads: the rows and their count, then where the suffixes, prefixes and automagic rows
  // start (+0x18, +0x20, +0x28). They must lie in that order inside the rows, on row boundaries.
  const uintptr_t t = reinterpret_cast<uintptr_t>(g_affix_table(unit_table_bank(item)));
  uintptr_t rows = 0, count = 0, suffixes = 0, prefixes = 0, automagic = 0;
  if (!t || !mem::read_safe(t, &rows) || !mem::read_safe(t + 0x08, &count) || !mem::read_safe(t + 0x18, &suffixes) ||
      !mem::read_safe(t + 0x20, &prefixes) || !mem::read_safe(t + 0x28, &automagic))
    return false;
  if (!rows || !count || count > 0x10000) return false;
  const uintptr_t end = rows + count * kAffixRowSize;
  if (suffixes < rows || prefixes < suffixes || automagic < prefixes || automagic > end ||
      (suffixes - rows) % kAffixRowSize || (prefixes - rows) % kAffixRowSize || (automagic - rows) % kAffixRowSize)
    return false;
  out->rows = reinterpret_cast<const uint8_t*>(rows);
  out->count = static_cast<uint32_t>(count);
  out->suffixes = static_cast<uint32_t>((suffixes - rows) / kAffixRowSize);
  out->prefixes = static_cast<uint32_t>((prefixes - rows) / kAffixRowSize);
  out->automagic = static_cast<uint32_t>((automagic - rows) / kAffixRowSize);
  return true;
}

bool affix_fits(Unit* item, const uint8_t* row) {
  return row && has_affix_rows() && unit_type(item) == kItem && g_affix_fits(item, row) != 0;
}

void apply_affix(Unit* item, const uint8_t* row) {
  if (row && has_affix_rows() && unit_type(item) == kItem) g_apply_affix(0, nullptr, item, row, 0, 0);
}

bool item_seeds(Unit* item, ItemSeeds* out) {
  *out = ItemSeeds{};
  if (unit_type(item) != kItem) return false;
  const uintptr_t unit = reinterpret_cast<uintptr_t>(item);
  const uintptr_t data = mem::read_ptr(unit + kUnitData);
  return data && mem::read_safe(unit + kUnitSeed, &out->unit_seed) &&
         mem::read_safe(data + kItemDataSeed, &out->item_seed);
}

bool has_item_removal() { return g_get_room && g_room_removed && g_free_collision && g_room_unlink && g_free_unit; }

bool remove_ground_item(void* game, Unit* item) {
  if (!game || !has_item_removal() || unit_type(item) != kItem) return false;
  if (void* room = g_get_room(item)) {
    g_room_removed(room, kItem, unit_id(item));
    g_free_collision(item);
    g_room_unlink(item);
  }
  g_free_unit(game, item);
  return true;
}

bool has_loot_filter() { return g_lf_manager && g_lf_shown; }

bool loot_filter_shows(Unit* item) {
  if (!has_loot_filter() || !item) return true;
  void* manager = g_lf_manager();
  return !manager || g_lf_shown(manager, item);
}

// The filter's test (0x1C1120) and its rule matcher (0x1BFE30) reach an item through four getters, each one
// field: the unit's type (+0x00), class (+0x04), data (+0x10) and bank (+0x1BD); of the data the matcher reads
// the quality (+0x00) and the flags (+0x18, ethereal | socketed). The stand-in has room before its data, where a
// real item keeps its affix ids.
bool loot_filter_would_show(uint8_t bank, uint32_t item_class, int32_t quality, bool ethereal_or_socketed) {
  if (!has_loot_filter()) return true;
  void* manager = g_lf_manager();
  if (!manager) return true;
  alignas(16) uint8_t unit[0x400] = {};
  alignas(16) uint8_t block[0x200] = {};
  uint8_t* const data = block + 0x80;
  const uint32_t type = kItem;
  const uint32_t flags = ethereal_or_socketed ? static_cast<uint32_t>(kItemEthereal) : 0;
  std::memcpy(unit, &type, sizeof(type));
  std::memcpy(unit + 0x04, &item_class, sizeof(item_class));
  std::memcpy(unit + kUnitData, &data, sizeof(data));
  unit[kUnitBank] = bank;
  std::memcpy(data, &quality, sizeof(quality));
  std::memcpy(data + kItemDataFlags, &flags, sizeof(flags));
  return g_lf_shown(manager, reinterpret_cast<Unit*>(unit));
}

int panel_side() {
  const uintptr_t at = sites::derived(sites::dPanelSide);
  int32_t v = -1;
  if (!at || !mem::read_safe(at, &v) || v < 0 || v > 3) return -1;
  return v;
}

bool owned_by_local_player(Unit* u) {
  const uint32_t me = local_player_id();
  if (me == 0xFFFFFFFFu || unit_type(u) != kMonster) return false;
  const uintptr_t data = mem::read_ptr(reinterpret_cast<uintptr_t>(u) + kUnitData);
  const uintptr_t ai = data ? mem::read_ptr(data + kMonsterAi) : 0;
  uint32_t id = 0xFFFFFFFFu, type = 0xFFFFFFFFu;
  // The game's own lookup only trusts the owner fields while the record names a game.
  if (!ai || !mem::read_ptr(ai + kAiGame) || !mem::read_safe(ai + kAiOwnerId, &id) ||
      !mem::read_safe(ai + kAiOwnerType, &type))
    return false;
  return type == kPlayer && id == me;
}

bool is_mercenary(Unit* u) {
  uint32_t flags = 0;
  return unit_type(u) == kMonster && mem::read_safe(reinterpret_cast<uintptr_t>(u) + kUnitFlags, &flags) &&
         (flags & kFlagMercenary) != 0;
}

size_t own_skills(Unit* u, SkillPoints* out, size_t cap) {
  const uint32_t cls = unit_class(u);
  const uintptr_t list = mem::read_ptr(reinterpret_cast<uintptr_t>(u) + kUnitSkills);
  uintptr_t node = list ? mem::read_ptr(list) : 0;
  size_t n = 0;
  for (int guard = 0; node && guard < kMaxSkillNodes && n < cap; ++guard) {
    const uintptr_t row = mem::read_ptr(node + kSkillRow);
    uint16_t id = 0;
    int8_t row_class = -1;
    int32_t level = 0, owner = 0;
    if (row && mem::read_safe(row, &id) && mem::read_safe(row + kSkillRowClass, &row_class) &&
        mem::read_safe(node + kSkillBaseLevel, &level) && mem::read_safe(node + kSkillOwnerItem, &owner) &&
        owner == -1 && row_class >= 0 && static_cast<uint32_t>(row_class) == cls && level > 0 && level < 256)
      out[n++] = SkillPoints{id, level};
    node = mem::read_ptr(node + kSkillNext);
  }
  return n;
}

int skill_base_level(Unit* u, int skill_id) {
  const uintptr_t list = mem::read_ptr(reinterpret_cast<uintptr_t>(u) + kUnitSkills);
  uintptr_t node = list ? mem::read_ptr(list) : 0;
  for (int guard = 0; node && guard < kMaxSkillNodes; ++guard) {
    const uintptr_t row = mem::read_ptr(node + kSkillRow);
    uint16_t id = 0;
    int32_t owner = 0, level = 0;
    if (row && mem::read_safe(row, &id) && id == skill_id && mem::read_safe(node + kSkillOwnerItem, &owner) &&
        owner == -1)
      return mem::read_safe(node + kSkillBaseLevel, &level) ? level : 0;
    node = mem::read_ptr(node + kSkillNext);
  }
  return 0;
}

bool has_respec() { return g_respec != nullptr; }

bool respec(void* game, Unit* player) {
  if (!g_respec || !game || unit_type(player) != kPlayer) return false;
  return g_respec(game, player);
}

bool has_spenders() { return g_spend_stats && g_spend_skills; }

namespace {
// The client's request as the handlers read it: opcode, the stat or skill id, and how many points
// beyond the first to spend. The handlers stop at the first point the game refuses.
void send_points(PacketHandlerFn handler, uint8_t opcode, void* game, Unit* player, int id, int points) {
  if (!handler || !game || unit_type(player) != kPlayer || points <= 0 || id < 0 || id > 0xFFFF) return;
  const int extra = points - 1 > 0xFFFF ? 0xFFFF : points - 1;
  const uint8_t packet[5] = {opcode, static_cast<uint8_t>(id & 0xFF), static_cast<uint8_t>(id >> 8),
                             static_cast<uint8_t>(extra & 0xFF), static_cast<uint8_t>(extra >> 8)};
  handler(game, player, packet, static_cast<int32_t>(sizeof(packet)));
}
}  // namespace

void spend_stat_points(void* game, Unit* player, int stat, int points) {
  if (stat >= kStrength && stat <= kVitality) send_points(g_spend_stats, 0x3A, game, player, stat, points);
}

void spend_skill_points(void* game, Unit* player, int skill, int points) {
  send_points(g_spend_skills, 0x3B, game, player, skill, points);
}

bool has_cure() {
  return g_list_by_state && g_unlink_list && g_free_list && g_state_on && g_toggle_state && !g_cure_failed;
}

Cured cure_state(void* game, Unit* u, int state) {
  Cured c;
  if (!has_cure() || !game || !u) return c;
  void* list = g_list_by_state(u, state);
  uint8_t ctx = 0;
  if (list && !mem::read_safe(reinterpret_cast<uintptr_t>(game) + kGameDataCtx, &ctx)) return c;
  while (list && c.lists < kMaxCureLists) {
    g_unlink_list(u, list);
    void* next = g_list_by_state(u, state);
    if (next == list) {
      // The unlink is D2RCore's own; a list it left on the unit must not be freed under it.
      g_cure_failed = true;
      log_error("game: the stat list unlink left a list of state %d on the unit - the antidote cure is off", state);
      return c;
    }
    g_free_list(ctx, list);
    ++c.lists;
    list = next;
  }
  if (!list && g_state_on(u, state)) {
    g_toggle_state(u, state, 0);
    c.cleared = true;
  }
  return c;
}

bool has_state_test() { return g_state_on != nullptr; }
bool state_on(Unit* u, int state) { return u && g_state_on && state >= 0 && g_state_on(u, state) != 0; }

void* unit_level(Unit* u) {
  // The room as the game's room getter (0x34B440) finds it: players, monsters and missiles walk a path that keeps
  // it at +0x20, the others stand on one that begins with it.
  const uint32_t type = unit_type(u);
  if (!u || type > kTile) return nullptr;
  const uintptr_t path = mem::read_ptr(reinterpret_cast<uintptr_t>(u) + kUnitPath);
  const bool walks = type == kPlayer || type == kMonster || type == kMissile;
  const uintptr_t active = path ? mem::read_ptr(path + (walks ? kPathRoom : 0)) : 0;
  const uintptr_t room = active ? mem::read_ptr(active + kActiveRoomDrlgRoom) : 0;
  return room ? reinterpret_cast<void*>(mem::read_ptr(room + kRoomLevel)) : nullptr;
}

int level_id_of(void* level) {
  int32_t id = -1;
  return level && mem::read_safe(reinterpret_cast<uintptr_t>(level) + kLevelId, &id) && id > 0 ? id : -1;
}

void* level_drlg(void* level) {
  return level ? reinterpret_cast<void*>(mem::read_ptr(reinterpret_cast<uintptr_t>(level) + kLevelDrlg)) : nullptr;
}

int unit_level_id(Unit* u) { return level_id_of(unit_level(u)); }

void* room_built(void* room) {
  return room ? reinterpret_cast<void*>(mem::read_ptr(reinterpret_cast<uintptr_t>(room) + kRoomActive)) : nullptr;
}

uint8_t game_bank(void* game) {
  uint8_t bank = 0;
  return game && mem::read_safe(reinterpret_cast<uintptr_t>(game) + kGameDataCtx, &bank) ? bank : 0;
}

bool has_town_travel() {
  return g_set_object_dest && g_load_act && g_warp && g_waypoint_active && sites::portal_facts().acts_offset > 0;
}

int object_destination(Unit* object) {
  if (unit_type(object) != kObject) return -1;
  const uintptr_t data = mem::read_ptr(reinterpret_cast<uintptr_t>(object) + kUnitData);
  uint8_t low = 0, high = 0;
  if (!data || !mem::read_safe(data + kObjectDestLow, &low) || !mem::read_safe(data + kObjectDestHigh, &high))
    return -1;
  return low | (high << 8);
}

void set_object_destination(Unit* object, int level) {
  if (g_set_object_dest && unit_type(object) == kObject && level > 0 && level <= 0xFFFF)
    g_set_object_dest(object, static_cast<uint32_t>(level));
}

bool act_loaded(void* game, int act) {
  const int at = sites::portal_facts().acts_offset;
  if (!game || at <= 0 || act < 0 || act >= kActs) return false;
  const uintptr_t record = reinterpret_cast<uintptr_t>(game) + static_cast<uintptr_t>(at);
  const uintptr_t acts = mem::read_ptr(record);
  uint64_t count = 0;
  if (!acts || !mem::read_safe(record + 8, &count) || count > 16 || static_cast<uint64_t>(act) >= count) return false;
  return mem::read_ptr(acts + static_cast<uintptr_t>(act) * sizeof(uintptr_t)) != 0;
}

bool load_act(void* game, int act) {
  if (!has_town_travel() || !game || act < 0 || act >= kActs) return false;
  if (act_loaded(game, act)) return true;
  // The slot must be there and empty, as the act change finds it before it makes the act.
  const uintptr_t record = reinterpret_cast<uintptr_t>(game) + static_cast<uintptr_t>(sites::portal_facts().acts_offset);
  uint64_t count = 0;
  if (!mem::read_ptr(record) || !mem::read_safe(record + 8, &count) || static_cast<uint64_t>(act) >= count || count > 16)
    return false;
  g_load_act(game, static_cast<uint32_t>(act));
  return act_loaded(game, act);
}

void warp_to_level(void* game, Unit* player, int level, int spot) {
  if (has_town_travel() && game && unit_type(player) == kPlayer && level > 0) g_warp(game, player, level, spot);
}

bool has_waypoint(void* game, Unit* player, int number) {
  if (!g_waypoint_active || !game || unit_type(player) != kPlayer || number < 0 ||
      static_cast<uint32_t>(number) >= kMaxWaypoints)
    return false;
  uint8_t difficulty = 0;
  if (!mem::read_safe(reinterpret_cast<uintptr_t>(game) + kGameDifficulty, &difficulty) || difficulty > 2) return false;
  const uintptr_t data = mem::read_ptr(reinterpret_cast<uintptr_t>(player) + kUnitData);
  const uintptr_t waypoints = data ? mem::read_ptr(data + kPlayerWaypoints + difficulty * sizeof(uintptr_t)) : 0;
  return waypoints && g_waypoint_active(reinterpret_cast<const void*>(waypoints), static_cast<uint32_t>(number)) != 0;
}

bool has_item_handover() {
  return g_get_inventory && g_get_room && g_page_grid && g_grid_spot && g_inventory_move && g_item_notice &&
         g_charm_counts && g_refresh_items && g_item_flags && g_item_page && g_item_body && g_unit_mode && g_unit_x &&
         g_unit_y && g_unit_coords && g_ground_spot && g_ground_put;
}

// The quest reward giver's placement (0x517530): the grid of page 0 for the game's bank (classic or not), a free spot
// in it, the item moved there by the inventory's own move, then the client told of the item added (command 2; flags
// 0, as the cube tells it of its products) and the player's items worked out again for a charm whose stats count.
bool put_in_inventory(void* game, Unit* player, Unit* item) {
  if (!has_item_handover() || !game || unit_type(player) != kPlayer || unit_type(item) != kItem) return false;
  uint8_t bank = 0;
  if (!mem::read_safe(reinterpret_cast<uintptr_t>(game) + kGameDataCtx, &bank)) return false;
  void* const inventory = g_get_inventory(player, __FILE__, __LINE__);
  if (!inventory) return false;
  constexpr uint32_t kInventoryPage = 0;
  const uint32_t grid = g_page_grid(player, kInventoryPage, bank != 1 ? 1u : 0u);
  int32_t x = 0, y = 0;
  if (!g_grid_spot(inventory, item, grid, &x, &y, kInventoryPage)) return false;
  const int32_t place[4] = {static_cast<int32_t>(kItemModeStored), static_cast<int32_t>(kInventoryPage),
                            static_cast<int32_t>((static_cast<uint32_t>(x) & 0xFFFFu) |
                                                 ((static_cast<uint32_t>(y) & 0xFFFFu) << 16)),
                            0};
  const uint32_t id = unit_id(item);
  if (!g_inventory_move(player, place, &id, 1, 0)) return false;
  constexpr uint32_t kCommandAdded = 2;
  g_item_notice(game, player, item, 0, kCommandAdded, place[0], static_cast<uint32_t>(place[1]),
                static_cast<uint32_t>(place[3]), static_cast<uint32_t>(place[2]));
  if (g_charm_counts(item, player)) g_refresh_items(game, player, nullptr, 0);
  return true;
}

// The giver's drop (and the drop request's, 0x4BD8F0): the item made one that lies on the ground, a free spot near
// the player in the player's room or one beside it, the item put there.
bool put_at_feet(void* game, Unit* player, Unit* item) {
  if (!has_item_handover() || !game || unit_type(player) != kPlayer || unit_type(item) != kItem) return false;
  constexpr uint32_t kFlagDropClears = 0x4000;  // an item flag every drop takes off (the drop request, the giver)
  constexpr uint32_t kNoPage = 0xFF;
  constexpr uint32_t kModeOnGround = 3;
  g_item_flags(item, kFlagDropClears, 0);
  g_item_page(item, kNoPage);
  g_item_body(item, 0);
  g_unit_mode(item, kModeOnGround);
  g_unit_x(item, 0);
  g_unit_y(item, 0);
  int32_t at[2] = {};
  g_unit_coords(player, at);
  void* const room = g_get_room(player);
  int32_t spot[2] = {};
  void* const spot_room = room ? g_ground_spot(game, room, at, spot, 1) : nullptr;
  if (!spot_room) return false;
  g_ground_put(game, player, item, spot_room, spot[0], spot[1]);
  return true;
}

bool has_socket_takeout() { return g_get_inventory && g_first_item && g_take_out && g_item_page && g_item_body &&
                                   g_unit_mode && g_unit_x && g_unit_y; }

// The cube's own "keep what is in the sockets" (the product routine at 0x5278A2): the first item of the item's
// inventory until there is none, each taken out, its page 0xFF, its place 0, its body location 0, its mode 4.
int take_out_socketed(Unit* item, Unit** out, int cap) {
  if (!has_socket_takeout() || unit_type(item) != kItem || !out || cap <= 0) return 0;
  void* const inventory = g_get_inventory(item, __FILE__, __LINE__);
  if (!inventory) return 0;
  constexpr uint32_t kNoPage = 0xFF;
  constexpr uint32_t kModeLoose = 4;
  int n = 0;
  while (n < cap) {
    Unit* const socketed = g_first_item(inventory);
    if (!socketed || g_take_out(inventory, socketed) != socketed) break;
    g_item_page(socketed, kNoPage);
    g_unit_x(socketed, 0);
    g_unit_y(socketed, 0);
    g_item_body(socketed, 0);
    g_unit_mode(socketed, kModeLoose);
    out[n++] = socketed;
  }
  return n;
}

bool has_pet_lookup() { return g_pet_type != nullptr; }

// The lookup asks the player's data first, and asserts on a unit that is no player with its data.
int pet_type(Unit* player, uint32_t unit_id) {
  if (!g_pet_type || unit_type(player) != kPlayer || !mem::read_ptr(reinterpret_cast<uintptr_t>(player) + kUnitData))
    return 0;
  return g_pet_type(player, unit_id);
}

bool has_restricted_test() { return g_restricted != nullptr; }

// The arguments the requirement test passes it (it walks the unit's worn items and the item's sockets, and asks no
// more than that: a unit without an inventory answers no).
bool restricted_socket(Unit* unit, Unit* item, int body_location) {
  return g_restricted && unit && unit_type(item) == kItem && g_restricted(unit, item, body_location);
}

bool has_pet_of_type() { return g_pet_of_type != nullptr; }

// The lookup asks the player's data first, and asserts on a unit that is no player with its data (as the pet lookup).
Unit* pet_of_type(void* game, Unit* player, int type) {
  if (!g_pet_of_type || !game || type <= 0 || unit_type(player) != kPlayer ||
      !mem::read_ptr(reinterpret_cast<uintptr_t>(player) + kUnitData))
    return nullptr;
  return g_pet_of_type(game, player, type, 0);
}

bool has_items_refresh() { return g_items_refresh != nullptr; }

void refresh_unit_items(void* game, Unit* unit) {
  const uint32_t type = unit_type(unit);
  if (g_items_refresh && game && (type == kPlayer || is_mercenary(unit))) g_items_refresh(game, unit, nullptr, 0);
}

bool has_identify() { return g_identify != nullptr; }

// The game's own identify, with the argument an Identify scroll and Cain pass (1: the player identifies its own item).
void identify_item(void* game, Unit* player, Unit* item) {
  if (g_identify && game && unit_type(player) == kPlayer && unit_type(item) == kItem) g_identify(game, player, item, 1);
}

bool has_key_check() { return g_get_inventory && g_item_type && sites::key_item_type() > 0; }

bool carries_key(Unit* player) {
  if (!has_key_check() || unit_type(player) != kPlayer) return false;
  const uintptr_t inv = reinterpret_cast<uintptr_t>(g_get_inventory(player, __FILE__, __LINE__));
  uint32_t magic = 0;
  if (!inv || !mem::read_safe(inv, &magic) || magic != kInventoryMagic || mem::read_ptr(inv + kInventoryCursorItem))
    return false;
  uintptr_t item = mem::read_ptr(inv + kInventoryFirstItem);
  for (int guard = 0; item && guard < kMaxInventoryItems; ++guard) {
    Unit* u = reinterpret_cast<Unit*>(item);
    const uintptr_t data = mem::read_ptr(item + kUnitData);
    uint8_t page = 0xFF;
    // Anything but an item ends the list, as it ends the key use's own walk.
    if (unit_type(u) != kItem || !data || !mem::read_safe(data + kItemDataPage, &page)) return false;
    if (unit_mode(u) == kItemModeStored && page == 0 && g_item_type(u, sites::key_item_type()) != 0) return true;
    item = mem::read_ptr(data + kItemDataNext);
  }
  return false;
}

bool has_room_build() { return g_create_room != nullptr; }

bool has_map_reveal() {
  return g_create_room && g_automap_room && g_level_def && sites::derived(sites::dAutomapLayer) && g_client_lookup;
}

bool automap_saves_any_size() { return sites::found(sites::kAutomapSaveCheck); }

bool client_level(ClientLevel* out) {
  Unit* me = has_map_reveal() ? local_client_player() : nullptr;
  if (!me) return false;
  const uintptr_t path = mem::read_ptr(reinterpret_cast<uintptr_t>(me) + kUnitPath);
  const uintptr_t active = path ? mem::read_ptr(path + kPathRoom) : 0;
  const uintptr_t room = active ? mem::read_ptr(active + kActiveRoomDrlgRoom) : 0;
  const uintptr_t level = room ? mem::read_ptr(room + kRoomLevel) : 0;
  const uintptr_t drlg = level ? mem::read_ptr(level + kLevelDrlg) : 0;
  int32_t id = -1;
  if (!drlg || !mem::read_safe(level + kLevelId, &id) || id <= 0) return false;
  // The client's DRLG is the one that reveals its rooms on the automap with the game's callback.
  if (mem::read_ptr(drlg + kDrlgAutomapCallback) != reinterpret_cast<uintptr_t>(g_automap_room)) return false;
  out->level = reinterpret_cast<void*>(level);
  out->drlg = reinterpret_cast<void*>(drlg);
  out->id = id;
  out->bank = unit_table_bank(me);
  return true;
}

int automap_layer() {
  const uintptr_t at = sites::derived(sites::dAutomapLayer);
  const uintptr_t layer = at ? mem::read_ptr(at) : 0;
  int32_t id = -1;
  return layer && mem::read_safe(layer, &id) ? id : -1;
}

int level_layer(uint8_t bank, int level_id) {
  if (!g_level_def || level_id <= 0) return -1;
  const uintptr_t record = reinterpret_cast<uintptr_t>(g_level_def(bank, level_id));
  int32_t layer = -1;
  return record && mem::read_safe(record + kLevelDefLayer, &layer) ? layer : -1;
}

// The per-tile add's own question (0x3269B0): the level record's level type.
int level_type(uint8_t bank, int level_id) {
  const int at = sites::map_facts().level_type_at;
  if (!g_level_def || level_id <= 0 || at <= 0) return -1;
  const uintptr_t record = reinterpret_cast<uintptr_t>(g_level_def(bank, level_id));
  int32_t type = -1;
  return record && mem::read_safe(record + static_cast<uintptr_t>(at), &type) ? type : -1;
}

void* level_first_room(void* level) {
  return level ? reinterpret_cast<void*>(mem::read_ptr(reinterpret_cast<uintptr_t>(level) + kLevelFirstRoom)) : nullptr;
}

void* room_next(void* room) {
  return room ? reinterpret_cast<void*>(mem::read_ptr(reinterpret_cast<uintptr_t>(room) + kRoomNext)) : nullptr;
}

bool reveal_room(uint8_t bank, void* room) {
  if (!room || !g_create_room || !g_automap_room) return false;
  void* active = g_create_room(bank, room);
  if (!active) return false;
  g_automap_room(active);
  return true;
}

void* build_room(uint8_t bank, void* room) { return room && g_create_room ? g_create_room(bank, room) : nullptr; }

void reveal_built_room(void* active_room) {
  if (active_room && g_automap_room) g_automap_room(active_room);
}

bool has_map_tiles() { return g_map_put_tile && sites::derived(sites::dAutomapLayer); }
bool has_map_objects() { return g_map_insert && sites::derived(sites::dAutomapLayer); }

// The active layer: what the automap draws, and where the game's own reveal puts what it reveals.
static uintptr_t automap_owner() {
  const uintptr_t at = sites::derived(sites::dAutomapLayer);
  return at ? mem::read_ptr(at) : 0;
}

bool map_put_tile(uint8_t bank, void* tile, void* room) {
  const uintptr_t owner = automap_owner();
  if (!g_map_put_tile || !owner || !tile || !room) return false;
  g_map_put_tile(bank, tile, room, reinterpret_cast<void*>(owner + static_cast<uintptr_t>(sites::map_facts().floor_list)));
  return true;
}

// The key the game's unit cell add makes (0xD52B0): a tag of 0 (1 is a cell read back from the map's save), the cell,
// and where it goes. The insert answers {the node, whether it is new}.
bool map_put_cell(uint16_t cell, int32_t x, int32_t y) {
  struct Key {
    uint16_t tag;
    uint16_t cell;
    int32_t x, y;
  };
  static_assert(sizeof(Key) == 12, "the key the list insert copies");
  const uintptr_t owner = automap_owner();
  if (!g_map_insert || !owner || !cell) return false;
  const Key key{0, cell, x, y};
  alignas(16) uint8_t out[16] = {};
  g_map_insert(reinterpret_cast<void*>(owner + static_cast<uintptr_t>(sites::map_facts().object_list)), out, &key);
  return out[8] != 0;
}

// The automap's view, as its draw of one unit reads it: the rectangle it draws in (x, y, width, height) and the
// scale of its markers.
constexpr uintptr_t kViewRect = 0x18;
constexpr uintptr_t kViewScale = 0x38;

bool has_map_draw() { return g_map_point && g_map_marker && g_map_name && g_unit_name; }

bool map_point(const void* view, int32_t px, int32_t py, uint64_t* point) {
  if (!g_map_point || !view) return false;
  uint64_t out = 0;
  g_map_point(view, &out, static_cast<uint32_t>(px) | static_cast<uint64_t>(static_cast<uint32_t>(py)) << 32);
  int32_t rect[4] = {};
  if (!mem::copy_from(rect, reinterpret_cast<uintptr_t>(view) + kViewRect, sizeof(rect))) return false;
  const int32_t x = static_cast<int32_t>(out), y = static_cast<int32_t>(out >> 32);
  if (x < rect[0] || y < rect[1] || x >= rect[0] + rect[2] || y >= rect[1] + rect[3]) return false;
  *point = out;
  return true;
}

float map_scale(const void* view) {
  float scale = 1.0f;
  return view && mem::read_safe(reinterpret_cast<uintptr_t>(view) + kViewScale, &scale) ? scale : 1.0f;
}

void map_marker(uint64_t point, int marker, float scale) {
  if (g_map_marker && marker >= 0 && marker < 8) g_map_marker(point, static_cast<uint32_t>(marker), scale);
}

void map_name(const char* text, uint64_t point, float scale, int color) {
  if (g_map_name && text && text[0]) g_map_name(text, point, scale, static_cast<uint32_t>(color));
}

int map_name_color() { return sites::map_facts().name_color; }

const char* client_unit_name(Unit* u) { return g_unit_name && u ? g_unit_name(u) : nullptr; }

// The game's getters (0x34AF60 / 0x34AFB0) read a unit's path at +0x08 and +0x0C whatever the unit: the static path of an
// object and the dynamic one of a monster keep their pixels there.
bool unit_pixels(Unit* u, int32_t* x, int32_t* y) {
  const uintptr_t path = u ? mem::read_ptr(reinterpret_cast<uintptr_t>(u) + kUnitPath) : 0;
  return path && mem::read_safe(path + 0x08, x) && mem::read_safe(path + 0x0C, y);
}

bool has_key_action(KeyAction a) {
  if (a < 0 || a >= kKeyActionCount || !g_key_press[a]) return false;
  return a == kKeyAutomap ? sites::derived(sites::dUiVars) && sites::automap_panel_id()
                          : sites::derived(sites::dShowItemsState) && g_name_display[0] && g_name_display[1] &&
                                g_setting_value;
}

bool key_action_wrapped(KeyAction a) { return a >= 0 && a < kKeyActionCount && g_key_wrapped[a]; }

bool press_key_action(KeyAction a) {
  if (!has_key_action(a)) return false;
  // The entry as it is now: what bind checked, or a loader jump put there since.
  const uintptr_t fn = key_slot(kKeyActionIds[a], 0);
  if (!fn || (fn != g_key_press[a] && !loader_jump(fn))) return false;
  reinterpret_cast<KeyPressFn>(fn)();
  return true;
}

int key_action_state(KeyAction a) {
  if (!has_key_action(a)) return -1;
  uint8_t on = 0;
  if (a == kKeyAutomap) {
    const uintptr_t vars = sites::derived(sites::dUiVars);
    return mem::read_safe(vars + static_cast<uintptr_t>(sites::automap_panel_id()), &on) ? on != 0 : -1;
  }
  // The on/off bytes are an array with its count beside the pointer (the setter checks the index against it).
  const uintptr_t state = sites::derived(sites::dShowItemsState);
  const uintptr_t data = mem::read_ptr(state);
  const uint64_t which = a == kKeyShowItems ? 0 : 1;
  uint64_t count = 0;
  if (!data || !mem::read_safe(state + 8, &count) || count <= which || count > 64 || !mem::read_safe(data + which, &on))
    return -1;
  return on != 0;
}

int automap_held_closed() {
  const uintptr_t vars = sites::derived(sites::dUiVars);
  if (!vars || !sites::derived(sites::dPanelRules)) return -1;
  for (int p = 0; p < kPanels; ++p) {
    uint8_t open = 0;
    if (!(g_automap_closers >> p & 1)) continue;
    if (!mem::read_safe(vars + static_cast<uintptr_t>(p), &open)) return -1;
    if (open) return 1;
  }
  return 0;
}

uint32_t automap_closers() { return g_automap_closers; }

int item_name_display(KeyAction a) {
  if (!has_key_action(a) || a == kKeyAutomap) return kDisplayUnknown;
  void* setting = g_name_display[a == kKeyShowItems ? 0 : 1]();
  if (!setting) return kDisplayUnknown;
  const int mode = static_cast<uint8_t>(g_setting_value(setting));
  return mode <= kDisplayTimed ? mode : static_cast<int>(kDisplayUnknown);
}

}  // namespace d2rcc::game

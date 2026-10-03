#include "sites.h"

#include <windows.h>

#include <cstdio>
#include <initializer_list>
#include <vector>

#include "log.h"
#include "mem.h"

namespace d2rcc::sites {
namespace {

// Rel32 and disp32 operands are wildcarded: they move with every build.
const Spec kSpecs[kCount] = {
    // The four stat helpers, past D2RCore's 10/6/10-byte entry patch (native_stat_compat_contract.inc).
    // Table order must follow the Id enum in sites.h.
    {kGetUnitStat, "STATLIST_GetUnitStat",
     "48 89 74 24 20 57 48 83 EC 20 41 0F B7 E8 8B FA 48 8B D9 48 85 C9 75 2A 88 4C 24 30 48 8D 4C 24 30 E8 ?? ?? ?? ?? "
     "84 C0 74 01 CC 33 C0 48 8B 5C 24 38 48 8B 6C 24", 10, 0x2F5020},
    {kSetUnitStat, "STATLIST_SetUnitStat",
     "56 57 41 54 41 56 41 57 48 83 EC 40 45 0F B7 E1 45 8B F0 8B F2 48 8B E9 48 85 C9 75 2B 88 4C 24 70 48 8D 4C 24 70 "
     "E8 ?? ?? ?? ?? 84 C0 74 01 CC 48 8D 4C 24 70 C6", 10, 0x2F7D10},
    {kAddUnitStat, "STATLIST_AddUnitStat",
     "57 41 56 48 83 EC 40 45 0F B7 F1 41 8B F8 8B EA 48 8B F1 48 85 C9 75 13 88 4C 24 70 48 8D 4C 24 70 E8 ?? ?? ?? ?? "
     "84 C0 74 01 CC 85 FF 74 5B 48 8B CE 48 89 5C 24 60", 6, 0x2F34F0},
    {kGetUnitBaseStat, "STATLIST_GetUnitBaseStat",
     "48 89 74 24 20 57 48 83 EC 20 41 0F B7 E8 8B DA 48 8B F9 48 85 C9 75 2A 88 4C 24 30 48 8D 4C 24 30 E8 ?? ?? ?? ?? "
     "84 C0 74 01 CC 33 C0 48 8B 5C 24 38 48 8B 6C 24", 10, 0x2F48C0},
    {kSetEvent, "EVENT_SetEvent",
     "48 83 EC 48 8B 84 24 80 00 00 00 89 44 24 38 8B 44 24 78 89 44 24 30 8B 44 24 70 89 44 24 28 48", 0, 0x48B720},
    {kExecuteEvents, "SUNITDMG_ExecuteEvents",
     "40 55 53 56 57 41 56 41 57 48 8D AC 24 E8 FE FF FF 48 81 EC 18 02 00 00 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 85 00 01 "
     "00 00 48 8B BD 70 01 00 00 41 8B D9 49 8B F0", 0, 0x44CE80},
    {kFinalizeDamage, "SUNITDMG_FinalizeDamage",
     "40 53 55 57 41 56 48 81 EC 88 00 00 00 49 8B D9 49 8B F8 4C 8B F2 48 8B E9 E8 ?? ?? ?? ?? 85 C0 75 0C F7 03 00 10 00 00",
     0, 0x44A9B0},
    {kPlayerStatRegen, "D2GAME_PLAYER_ApplyStatRegen",
     "48 89 5C 24 20 55 48 83 EC 40 41 8B C1 C7 44 24 30 00 00 00 00 44 8B 89 70 01 00 00 48 8B DA 89 44 24 28 41 FF C1", 0,
     0x42E600},
    {kDeathHandler, "D2GAME_PLAYER_DeathPenalties",
     "40 53 56 57 48 83 EC 40 49 8B D8 48 8B F2 48 8B F9 E8 ?? ?? ?? ?? 48 85 DB 74 2D 48 8B CB E8 ?? ?? ?? ?? 85 C0", 0,
     0x424AC0},
    {kClientGetLocalPlayer, "CLIENT_GetLocalPlayer",
     "48 89 5C 24 08 57 48 83 EC 20 83 F9 08 0F 83 85 00 00 00 8B D9 48 89 5C 24 38 48 83 FB 08 72 19", 0, 0x9A480},
    {kGetUnitByIdAndType, "GetUnitByIdAndType (client and server copies)",
     "4C 63 CA 48 8D 05 ?? ?? ?? ?? 8B D1 44 8B C1 49 8B C9 83 E2 7F 48 C1 E1 0A 48 03 C8 E9 ?? ?? ?? ??", 0, 0x9A5A0},
    {kIsOnlineState, "CLIENT_IsOnlineState",
     "48 8B 05 ?? ?? ?? ?? 48 85 C0 74 08 80 78 5C 01 0F 94 C0 C3 C3", 0, 0x8D3B0},
    {kPlayerAddExperience, "PLAYER_AddExperience",
     "48 85 D2 0F 84 90 01 00 00 55 57 41 56 41 57 48 83 EC 68 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 44 24 58 4C 8B F1 41 8B "
     "F9", 0, 0x44F2E0},
    {kGetManaCost, "D2Common_SKILLMANA_GetManaCost",
     "48 89 5C 24 08 57 48 83 EC 20 41 8B F8 33 DB E8 ?? ?? ?? ?? 48 8B D0 48 85 C0 74 41 0F BF 88 2A 02 00 00", 0,
     0x33AA00},
    // Its two callers. These patterns start before the call, so the negative offset
    // makes the site's RVA the call instruction itself (entry_bytes is not used on them).
    {kManaCostServerCall, "SKILLMANA_Consume -> GetManaCost call",
     "0F B6 8E 06 01 00 00 45 8B C6 8B D5 E8 ?? ?? ?? ?? 8B D8 85 C0 7F 04 33 C0 EB 61", -12, 0x4369FB},
    {kManaCostClientCall, "client mana check -> GetManaCost call",
     "E8 ?? ?? ?? ?? 0F B6 C8 44 8B C5 8B D6 E8 ?? ?? ?? ?? 8B F8 85 C0 7E 4E BA 72 00 00 00", -13, 0x218937},
    // Terror zones (D2Common's DesecratedZones.cpp and the server job that applies them).
    {kDesecrateCurrentZone, "DesecrateGetCurrentScheduledZone",
     "48 89 5C 24 18 48 89 74 24 20 57 48 83 EC 40 48 8B D9 33 C9 E8 ?? ?? ?? ?? 48 8B D3 48 8B C8 48 8B F8 E8 ?? ?? ?? "
     "?? 48 8B F0", 0, 0x35B380},
    {kDesecrateConfigForTime, "DesecrateGetConfigForTime",
     "40 53 57 41 57 48 83 EC 30 48 89 6C 24 50 48 8B E9 4C 89 64 24 60 4C 8B E2 4C 89 74 24 68 4C 8B 35 ?? ?? ?? ?? "
     "4D 85 F6 75 0C", 0, 0x35DB10},
    {kDesecrateJob, "DesecrateApplyCurrentZoneJob",
     "40 55 53 48 8D AC 24 38 FF FF FF 48 81 EC C8 01 00 00 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 85 A0 00 00 00 E8 ?? ?? "
     "?? ?? BA 08 01 00 00 41 B8 10 00 00 00", 0, 0x405B30},
    // A Worldstone Shard's use (the item-use callback for pSpell 15; its file's line 650 in r8d): the shard's act
    // (calc1), whether the game has terror zones, the act's manual zone, applied as manual terror, the clients told.
    {kTerrorShardUse, "Worldstone Shard: use (terrorize an act)",
     "48 89 5C 24 18 48 89 6C 24 20 57 48 83 EC 60 49 8B F8 48 8B EA 48 8B D9 48 8D 15 ?? ?? ?? ?? 48 8B CF 41 B8 8A 02 "
     "00 00 E8",
     0, 0x580320},
    // Area levels: the client's level-name getter and the two UI routines that ask it.
    {kClientGetLevelName, "CLIENT_GetLevelName",
     "48 83 EC 38 E8 ?? ?? ?? ?? 48 85 C0 74 4B 48 8D 88 FD 00 00 00 80 39 00 48 89 4C 24 20 74 24 48 C7 C0 FF FF FF FF", 0,
     0xC7A30},
    {kAutomapUpdate, "AutomapPanel_Update",
     "48 89 5C 24 20 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 E0 FE FF FF 48 81 EC 20 02 00 00 0F 29 B4 24 10 02 00 "
     "00 48 8B 05 ?? ?? ?? ??", 0, 0x1471BC0},
    {kAutomapInfoUpdate, "AutomapInfo_Update",
     "4C 8B DC 55 53 57 49 8D AB 28 F9 FF FF 48 81 EC C0 07 00 00 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 85 A0 06 00 00 49 "
     "89 73 10 48 8D B9 98 01 00 00", 0, 0x14751E0},
    {kWaypointPopulate, "WaypointPanel_Populate",
     "48 89 74 24 20 55 57 41 54 41 55 41 56 48 8D AC 24 80 FD FF FF 48 81 EC 80 03 00 00 48 8B 05 ?? ?? ?? ?? 48", 0,
     0x1E9B40},
    // Health bars. The above-head helper fetches the renderer's context ([root+0x20]) and projects
    // with the above-the-head flag (r9b = 1); the game uses it for overhead text.
    {kProjectAboveHead, "UnitAboveHeadToScreen",
     "40 53 48 83 EC 40 48 8B D9 48 89 54 24 20 E8 ?? ?? ?? ?? 48 8B 48 20 48 85 C9 74 ?? 41 B1 01 4C 8D 44 24 60 48 8B "
     "D3 E8 ?? ?? ?? ?? 84 C0 74", 0, 0xCE850},
    {kIsHdGraphics, "IsHdGraphics", "48 8B 05 ?? ?? ?? ?? 48 85 C0 74 08 0F B6 80 B9 00 00 00 C3 E9", 0, 0x846210},
    {kRenderWidth, "GetRenderWidth",
     "48 83 EC 28 E8 ?? ?? ?? ?? 84 C0 74 09 48 83 C4 28 E9 ?? ?? ?? ?? 8B 05 ?? ?? ?? ?? 48 83 C4 28 C3", 0, 0x7F510},
    {kRenderHeight, "GetRenderHeight",
     "48 83 EC 28 E8 ?? ?? ?? ?? 84 C0 74 0E E8 ?? ?? ?? ?? 48 C1 E8 20 48 83 C4 28 C3 8B 05 ?? ?? ?? ?? 48 83 C4 28 C3", 0,
     0x7F4A0},
    {kIsHostile, "CLIENT_IsHostile",
     "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 41 54 41 55 41 56 41 57 48 83 EC 20 41 8B F0 48 8B DA 4C 8B F9 E8 "
     "?? ?? ?? ?? 41 B8 C8 0D 00 00", 0, 0x971E0},
    {kPanelSideSet, "UI_SetPanelSide",
     "48 83 EC 38 44 8B 0D ?? ?? ?? ?? 41 8B C1 99 89 0D ?? ?? ?? ?? 83 E2 03 44 8D 14 02 41 C1 FA 02 85 C9 74", 0, 0xB9C20},
    // The bucket walk behind GetUnitByIdAndType, past the jump D2RCore put at its entry.
    {kUnitListNext, "unit hash bucket walk",
     "48 85 C0 74 ?? 44 39 40 08 75 ?? 44 39 08 74 ?? 48 8B 88 ?? ?? ?? ?? 48 8B C1 48 85 C9 EB", 7, 0x9F270},
    // Loot. The treasure-class walker every monster kill and chest drop runs through (0x441300 and the
    // objects' 0x5930B0 call it); per pick it makes the item on the ground beside the source.
    {kTcDropCore, "TreasureClassDropCore",
     "4C 8B DC 55 53 49 8D AB 08 F7 FF FF 48 81 EC E8 09 00 00 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 85 80 08 00 00", 0,
     0x4404F0},
    // The item generator's quality step: an expansion item with a quality asked for (request+0x40) gets it.
    {kItemQualityStep, "item generation: quality step",
     "48 89 54 24 10 41 55 41 57 48 83 EC 58 4C 8B EA 4C 8B F9 E8 ?? ?? ?? ?? 66 83 F8 01 72 0C 41 8B 45 40 85 C0", 0,
     0x444200},
    // The socket roll (normal and superior items, after the quality's own step): the chance on the item's seed, then
    // the request's flags (`mov ecx, [rbp+0C0h]`): 8 no sockets, 0x10 counts the roll as 0, a restored item's
    // socketed flag (+0x54 & 0x800), else sockets on a roll below 33. Checked, not called: all socketed sets 0x10.
    {kSocketRollFlags, "socket roll: request flags",
     "BA 64 00 00 00 E8 ?? ?? ?? ?? 8B 8D C0 00 00 00 F6 C1 08 75 ?? F6 C1 10 B9 00 00 00 00 0F 45 C1 F7 45 54 00 08 "
     "00 00 75 ?? 83 F8 21 7D ??",
     -10, 0x44211A},
    {kPropertyRoll, "ITEMMODS property roll",
     "48 89 5C 24 10 57 48 83 EC 20 8B DA 48 8B F9 41 3B D0 75 0D 8B C2 48 8B 5C 24 38 48 83 C4 20 5F C3", 0, 0x3D5860},
    // Item creation (CreateItemEx): a classic game's check of the item first (the Items row's version), then the
    // Items table of the game's bank. Its third argument (kept in ebx) makes it take both seeds from the request.
    {kCreateItem, "item creation (CreateItemEx)",
     "40 55 53 56 41 54 41 57 48 8D 6C 24 C9 48 81 EC 90 00 00 00 80 B9 06 01 00 00 01 41 8B D8 48 8B F2 4C 8B F9 75 "
     "19 8B 52 20 B1 01 E8 ?? ?? ?? ?? 48 85 C0 74 3A 66 83 B8 FE 00 00 00 64 73 30 41 0F B6 8F 06 01 00 00 E8",
     0, 0x43CD50},
    {kLootFilterShown, "LootFilter: item shown",
     "48 89 5C 24 18 56 57 41 56 48 83 EC 50 48 8B DA 48 8B F9 48 85 D2 0F 84 ?? ?? ?? ?? E8 ?? ?? ?? ?? 0F B6 C8 BA 08 00 "
     "00 00",
     0, 0x1C1120},
    // The ground labels ask the manager getter, then pass what it returns to the "shown" test.
    {kLootFilterCallPair, "ground labels -> loot filter call pair",
     "E8 ?? ?? ?? ?? 84 C0 75 18 E8 ?? ?? ?? ?? 49 8B D7 48 8B C8 E8 ?? ?? ?? ?? 84 C0", -9, 0x15176EE},
    // Taking an item off the ground: the game's own item-expiry sequence (0x4416E0).
    {kUnitGetRoom, "UNITS_GetRoom",
     "40 53 48 83 EC 20 48 8B D9 48 85 C9 75 13 88 4C 24 30 48 8D 4C 24 30 E8 ?? ?? ?? ?? 84 C0 74 01 CC 8B 0B 83 E9 02 74 "
     "25",
     0, 0x34B440},
    {kRoomRemovedUnit, "room: removed unit notice",
     "48 85 C9 74 69 48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 41 8B F8 8B F2 48 8B D9 E8 ?? ?? ?? ??", 0, 0x2EF4D0},
    {kUnitFreeCollision, "unit: collision footprint",
     "48 89 5C 24 10 48 89 6C 24 18 56 57 41 54 41 56 41 57 48 83 EC 30 48 8B D9 E8 ?? ?? ?? ?? 44 8B E0", 0, 0x348B00},
    {kRoomUnlinkUnit, "room: unlink unit", "48 89 5C 24 10 48 89 74 24 18 57 48 83 EC 20 33 F6 48 8B D9 48 85 C9 75 1E", 0,
     0x38EFE0},
    {kFreeUnit, "SUNIT_FreeUnit", "48 89 74 24 18 57 48 83 EC 20 48 8B FA 48 8B F1 48 85 C9 75 13 88 4C 24 30", 0,
     0x48FAA0},
    // The property roll's choice sites (the call instructions): they pick an id, not a value.
    {kRollPickSkill, "property roll: skill pick (skill-rand)", "6C 44 8B 43 0C 48 8B CF 8B 53 08 E8 ?? ?? ?? ??", -11,
     0x3CCAA1},
    {kRollPickClass, "property roll: class pick (randclassskill)",
     "55 00 00 84 C0 74 01 CC 33 C0 EB 71 44 8B 43 0C 48 8B CF 8B 53 08 E8 ?? ?? ?? ??", -22, 0x3CC8F1},
    {kRollPickStat, "property roll: stat pick", "8D 41 FF 48 8B CE 33 D2 E8 ?? ?? ?? ??", -8, 0x3CD141},
    {kRollPickGroupA, "property roll: group pick A", "8B 4F 18 44 8D 46 FF 33 D2 E8 ?? ?? ?? ??", -9, 0x3D16EB},
    {kRollPickGroupB, "property roll: group pick B", "8B 4F 18 45 8D 46 FF 33 D2 E8 ?? ?? ?? ??", -9, 0x3D19AF},
    // A group entry's param, rolled when its ParMin (+0x0C) and ParMax (+0x10) differ (3.3: skilltab-war, the
    // Warlock's three skill tabs): the same code in both pick routines, told apart by the store before it.
    {kRollPickGroupParam, "property roll: group param (every entry)",
     "8B 44 CE 0C 44 8B 44 CE 10 41 3B C0 74 0A 8B D0 48 8B CF E8 ?? ?? ?? ??", -19, 0x3D4F7F},
    {kRollPickGroupParamA, "property roll: group param A",
     "89 45 CB 41 8B 40 0C 45 8B 40 10 41 3B C0 74 0B 48 8B 4F 18 8B D0 E8 ?? ?? ?? ??", -22, 0x3D17B3},
    {kRollPickGroupParamB, "property roll: group param B",
     "89 44 24 64 41 8B 40 0C 45 8B 40 10 41 3B C0 74 0B 48 8B 4F 18 8B D0 E8 ?? ?? ?? ??", -23, 0x3D1A7A},
    // `or [rcx+rbp+disp32], eax` (rbp = the game): a unique is marked dropped for the rest of the game.
    {kUniqueBitSet, "unique generator: dropped bit",
     "83 E2 1F 48 8D 0C 85 00 00 00 00 8B 84 96 ?? ?? ?? ?? 09 84 29 ?? ?? ?? ??", -18, 0x443ECB},
    // Consumables. The predicate tests for a book (item type 0x12) first: tomes are never removed.
    {kShouldRemoveOnUse, "ITEMS_ShouldRemoveOnUse",
     "48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 18 41 56 48 83 EC 20 BA 12 00 00 00 48 8B F9 E8 ?? ?? ?? ?? 85 C0 0F "
     "85 ?? ?? ?? ?? 48 8B CF",
     0, 0x308E80},
    // Reads the quantity (stat 70) first: `lea edx, [r8+46h]` with r8d zeroed.
    {kSyncItemQuantity, "item quantity update (tomes)",
     "48 89 5C 24 08 48 89 6C 24 10 56 57 41 54 41 56 41 57 48 83 EC 30 49 8B F8 48 8B DA 45 33 C0 48 8B CF 45 8B F9 41 "
     "8D 50 46 E8 ?? ?? ?? ??",
     0, 0x46F090},
    // Inside the item-use effect: `is it a book? then quantity update (game, player, item, -1)`.
    {kItemUseBookDebit, "item use -> tome charge call",
     "BA 12 00 00 00 48 8B CB E8 ?? ?? ?? ?? 85 C0 74 14 41 B9 FF FF FF FF 4C 8B C3 48 8B D5 48 8B CE E8 ?? ?? ?? ??", -32,
     0x5817CC},
    // Reset skills, reset base stats, refresh, then the respec sound (2) on the player.
    {kResetStatsAndSkills, "D2GAME_PLAYER_ResetStatsAndSkills",
     "48 89 5C 24 08 57 48 83 EC 20 48 8B FA 48 8B D9 E8 ?? ?? ?? ?? 48 8B D7 48 8B CB E8 ?? ?? ?? ?? 45 33 C9 45 33 C0 "
     "48 8B D7 48 8B CB E8 ?? ?? ?? ?? 48 8B CF E8 ?? ?? ?? ?? 48 8B CF E8 ?? ?? ?? ?? BA 02 00 00 00 4C 8B C7",
     0, 0x580F20},
    // The two point handlers check the packet size (5) right after their prologues.
    {kAllocateStatPoints, "server: spend attribute points (0x3A)",
     "40 53 55 56 57 41 55 41 56 41 57 48 81 EC C0 00 00 00 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 84 24 A0 00 00 00 49 "
     "63 D9 49 8B F8 48 89 54 24 70 48 8B F2 48 8B E9 83 FB 05 74",
     0, 0x4B3C70},
    {kAllocateSkillPoints, "server: spend skill points (0x3B)",
     "40 55 53 56 57 41 55 41 56 41 57 48 8D 6C 24 D9 48 81 EC D0 00 00 00 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 45 0F "
     "49 63 D9 49 8B F8 48 89 55 CF 48 8B F2 4C 8B E9 83 FB 05 74",
     0, 0x4B3EE0},
    // The antidote's cure, per state: the stat list (`mov rcx, rsi` first), unlink, free (the game's
    // data context byte in ecx), then the state test and `toggle (unit, state, 0)`.
    {kCureStates, "antidote cure -> state routine calls",
     "48 8B CE E8 ?? ?? ?? ?? 48 8B F8 48 85 C0 74 1B 48 8B D0 48 8B CE E8 ?? ?? ?? ?? 41 0F B6 8E 06 01 00 00 48 8B D7 "
     "E8 ?? ?? ?? ?? 0F BF 13 48 8B CE E8 ?? ?? ?? ?? 85 C0 74 0E 0F BF 13 45 33 C0 48 8B CE E8 ?? ?? ?? ??",
     -3, 0x580890},
    // The key use checks its two arguments (the second, the player, in rbp) before it asks for the inventory.
    {kUseKey, "D2GAME_UseKey",
     "48 8B C4 53 55 41 56 48 83 EC 60 48 8B EA 4C 8B F1 48 85 C9 75 11 88 48 08 48 8D 48 08 E8 ?? ?? ?? ?? 84 C0 74 01 "
     "CC 48 85 ED 75 1A 48 8D 8C 24 80 00 00 00 40 88 AC 24 80 00 00 00 E8 ?? ?? ?? ?? 84 C0 74 01 CC 48 89 B4 24 88 00 "
     "00 00",
     0, 0x4712A0},
    // Three room flags in turn (tile libraries, preset units, the room itself), then the room's ActiveRoom.
    {kCreateActiveRoom, "DRLGROOM_CreateActiveRoom",
     "48 89 5C 24 08 57 48 83 EC 20 8B 42 50 48 8B DA 0F B6 F9 0F BA E0 18 72 ?? E8 ?? ?? ?? ?? 8B 43 50 0F BA E0 19 72 "
     "?? 83 7B 74 02 75 ?? 48 8B D3 40 0F B6 CF E8 ?? ?? ?? ?? 8B 43 50 0F BA E0 14 72 ?? 48 8B D3 40 0F B6 CF E8 ?? ?? "
     "?? ?? 48 8B 43 58",
     0, 0x3289A0},
    // The bank, the active layer (`mov rax, [rip+disp]`), the room's level record and its layer (+0x08), the
    // layer, then the reveal with its "whole room" argument (r8d = 1).
    {kAutomapRoomCallback, "automap: reveal a room (client DRLG callback)",
     "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 48 8B F1 E8 ?? ?? ?? ?? 8B C8 E8 ?? ?? ?? ?? 48 85 C0 74 19 E8 ?? ?? "
     "?? ?? 8B C8 E8 ?? ?? ?? ?? 48 8B C8 E8 ?? ?? ?? ?? 0F B6 F8 EB 0C E8 ?? ?? ?? ?? 0F B6 B8 ?? ?? ?? ?? 48 8B 05 ?? "
     "?? ?? ?? 48 85 C0 74 04 8B 18 EB 05 BB FF FF FF FF 48 8B CE E8 ?? ?? ?? ?? 8B D0 40 0F B6 CF E8 ?? ?? ?? ?? 8B 48 "
     "08 E8 ?? ?? ?? ?? 4C 8B C8 41 B8 01 00 00 00 48 8B D6 40 0F B6 CF E8 ?? ?? ?? ??",
     0, 0xD2240},
    // The cell-tree serializer's byte count: `2 * words` in 32 bits, 0 on overflow. The game's own is a
    // sign-extended 16-bit double that a large layer overflows (a crash at the next layer change); D2RCore
    // writes this one over it.
    {kAutomapSaveCheck, "automap serializer: checked cell count",
     "4D 85 FF 74 0D 33 C9 8B 56 08 03 D2 0F 43 CA 41 89 0F", 0, 0xD7E3A},
    // The Show Items key (cl = 0) and Show Items (Unfiltered) (cl = 1): the key's display mode, then Toggle (1)
    // flips the key's on/off byte, anything else turns it on (Timed also restarts its timer); the ShowItemsPanel
    // follows.
    {kShowItemsPress, "Show Items key: press",
     "48 89 5C 24 10 48 89 7C 24 18 55 48 8D AC 24 ?? ?? ?? ?? 48 81 EC ?? ?? ?? ?? 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 "
     "85 ?? ?? ?? ?? 0F B6 D9 84 C9 75 11 E8 ?? ?? ?? ?? 48 8B C8 E8 ?? ?? ?? ?? 8B F8 EB 10 E8 ?? ?? ?? ?? 48 8B C8 E8 "
     "?? ?? ?? ?? 0F B6 F8 0F B6 CB 40 80 FF 01 75 0F E8 ?? ?? ?? ?? E8",
     0, 0xC66A0},
    // ... and its release: the display-mode setting (Show Items or Unfiltered), its value, and in Hold mode
    // (0) the on/off setter with 0, then the panel update.
    {kShowItemsRelease, "Show Items key: release",
     "40 53 48 83 EC 20 0F B6 D9 84 C9 75 07 E8 ?? ?? ?? ?? EB 05 E8 ?? ?? ?? ?? 48 8B C8 E8 ?? ?? ?? ?? 84 C0 75 14 33 "
     "D2 0F B6 CB E8 ?? ?? ?? ?? 48 83 C4 20 5B E9 ?? ?? ?? ??",
     0, 0xC6E90},
    // The Automap key (Tab): unless a controller holds it back, toggle the automap panel (ecx = 10), then ask
    // that panel's UI var (`mov ecx, 0Ah` before the getter) and re-centre a closed map.
    {kAutomapKey, "Automap key",
     "48 83 EC 28 E8 ?? ?? ?? ?? 84 C0 74 11 E8 ?? ?? ?? ?? 48 8B C8 E8 ?? ?? ?? ?? 84 C0 74 23 33 D2 8D 4A 0A E8 ?? ?? "
     "?? ?? B9 0A 00 00 00 E8 ?? ?? ?? ?? 84 C0 75 0B 33 C9 48 83 C4 28 E9",
     0, 0x119D50},
    // A key binding's action goes to the release (jne) or the press (jmp) dispatch with the key-action table in rdx.
    {kKeyActionSelect, "key binding -> key-action table",
     "45 33 C0 84 D2 48 8D 15 ?? ?? ?? ?? 0F 85 ?? ?? ?? ?? E9 ?? ?? ?? ??", 0, 0x11EC0E},
    // No durability loss. A hit's wear asks the item's type twice, any armor (0x32) then any weapon (0x2D), before
    // its roll on the owner's seed (10 % armor, 10 % a throwing weapon, 4 % any other weapon).
    {kItemWear, "item durability loss (a hit's wear)",
     "48 89 6C 24 10 56 57 41 54 41 56 41 57 48 83 EC 30 4C 8B F2 48 8B E9 BA 32 00 00 00 49 8B C8 49 8B F8 E8 ?? ?? ?? "
     "?? BA 2D 00 00 00 48 8B CF 44 8B F8 E8 ?? ?? ?? ??",
     0, 0x441B10},
    // Impale's start function hands it the weapon, the chance (r9d, kept in esi) and the amount: a weapon (0x2D)
    // that is thrown loses one of its stack, any other loses durability.
    {kImpaleWear, "Impale: weapon durability loss",
     "48 89 5C 24 10 48 89 6C 24 18 48 89 74 24 20 57 48 83 EC 40 48 8B FA 48 8B E9 BA 2D 00 00 00 49 8B C8 41 8B F1 49 "
     "8B D8 E8 ?? ?? ?? ?? 85 C0 0F 84 ?? ?? ?? ??",
     0, 0x5590C0},
    // Item level. The item writer's network form (no seed): the Items row (bank, class) and its ShowLevel byte
    // (+0x10E) against zero; set, the `jne` goes to the real level (clamped 1..99), else `mov edx, 1` writes the
    // placeholder. Seven bits either way, so the client reads the same stream. The save form never comes here.
    {kItemLevelSend, "item writer: ShowLevel test (level sent to clients)",
     "8B D3 0F B6 C8 E8 ?? ?? ?? ?? 44 38 A0 0E 01 00 00 0F 85 ?? ?? ?? ?? BA 01 00 00 00 EB", -17, 0x37D726},
    // The inventory's item name (inv.cpp): null item, then its 0x1700-byte frame. Under ShowLevel it appends the
    // string "space" and "(%i)" of the item's level. D2RCore's tooltip and label code call it.
    {kItemNameBuild, "inventory item name (item level)",
     "48 85 C9 0F 84 ?? ?? ?? ?? 55 56 41 54 48 8D AC 24 00 EA FF FF B8 00 17 00 00 E8 ?? ?? ?? ?? 48 2B E0 48 8B 05 "
     "?? ?? ?? ?? 48 33 C4 48 89 85 D0 15 00 00 48 89 9C 24 30 17 00 00 48 8B F2",
     0, 0x1E1D80},
    // Max affixes. The magic step: the request's forced prefix (+0xA8, -1 none allowed) picked with "must" only when
    // forced (`setg r8b`), else on the picker's coin; the rest (the suffix, the setters) is checked below.
    {kMagicAffixes, "item generation: magic affixes",
     "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 41 56 41 57 48 83 EC 30 8B 82 A8 00 00 00 45 33 FF 48 8B F2 48 "
     "8B F9 41 8B EF 45 8B F7 90 41 8D 5F 01 85 C0 79 04 8B EB EB 42 48 8B 09 85 C0 45 8B C7 89 44 24 28 41 0F 9F C0 "
     "89 5C 24 20",
     0, 0x442C60},
    // The rare step (the wrapper 0x58AC70 sends expansion items here): the item's bank, its version, then the two
    // rare names (D2RCore's; a jump-table call either way). Its count, jewel test and picks are checked below.
    {kRareAffixes, "item generation: rare affixes",
     "48 89 54 24 10 55 56 57 41 55 41 57 48 83 EC 60 4C 8B EA 48 8B F9 33 ED 45 33 FF E8 ?? ?? ?? ?? 48 8B CF 88 84 "
     "24 A8 00 00 00 E8 ?? ?? ?? ?? 8D 55 01 48 8B CF 66 3B C2 72 07 E8 ?? ?? ?? ?? EB 05 E8 ?? ?? ?? ??",
     0, 0x58BBA0},
    // The crafted step: the item's version and the two rare names, then its count (checked below).
    {kCraftedAffixes, "item generation: crafted affixes",
     "48 89 54 24 10 57 41 54 41 57 48 83 EC 70 4C 8B E2 4C 8B F9 E8 ?? ?? ?? ?? BA 01 00 00 00 49 8B CF 66 3B C2 72 "
     "07 E8 ?? ?? ?? ?? EB 05 E8 ?? ?? ?? ?? 49 8B CF 48 89 9C 24 90 00 00 00 8B F8 90 E8 ?? ?? ?? ?? 33 D2 49",
     0, 0x58A120},
    // A class item's skill lines: its 0x230-byte frame, the item in r12 and the item level in ebx, then its rolls
    // (checked below).
    {kClassSkillBonus, "item generation: class item skill bonuses",
     "40 55 53 57 41 54 41 57 48 8D AC 24 D0 FE FF FF 48 81 EC 30 02 00 00 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 85 10 "
     "01 00 00 48 8B F9 48 89 4C 24 50 B8 FF FF FF FF 44 89 4C 24 48 48 8B CA 89 85 08 01 00 00 41 8B D8",
     0, 0x58AF50},
    // The superior step: the item's bank and the QualityItems table of it, the item's class (the line of its file in
    // r8d) and its Items row. Its count of kinds and its test of a kind are read below.
    {kSuperiorStep, "item generation: superior kind step",
     "48 89 5C 24 18 55 56 57 41 54 41 55 41 56 41 57 48 83 EC 50 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 44 24 48 4C 8B "
     "EA 4C 8B F1 E8 ?? ?? ?? ?? 0F B6 C8 44 0F B6 E0 E8 ?? ?? ?? ?? 41 B8 D2 05 00 00",
     0, 0x58B9E0},
    // Whether a kind goes on an item: the row's bytes for the item types it names (shield .. belt) onto the stack,
    // then the item's type. The step is its one caller.
    {kSuperiorFits, "item generation: superior kind fits the item",
     "48 89 5C 24 10 48 89 74 24 18 57 48 81 EC 80 00 00 00 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 44 24 70 0F B6 42 02 "
     "48 8B FA",
     0, 0x3D40D0},
    // Best affixes. The affix picker the magic and crafted steps call: the item's version, then an expansion item's
    // pick goes to D2RCore's picker (a jump-table call, with group 0), a classic item's to D2RCore's classic picker.
    {kAffixPicker, "item generation: affix picker",
     "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 83 EC 40 41 8B F9 41 8B F0 8B EA 48 8B D9 E8 ?? ?? ?? ?? 66 "
     "83 F8 01 44 8B CF 8B 44 24 78 44 8B C6 8B D5 48 8B CB 72 ?? C7 44 24 30 00 00 00 00 89 44 24 28 8B 44 24 70 89 "
     "44 24 20 E8",
     0, 0x58AE00},
    // Its twin, which the generator calls last for a base's automatic affix: the same, with its seventh argument (the
    // base's AutoMagic group, [rsp+0x80] here) handed on to D2RCore's picker. The two picker calls are checked below.
    {kAutoAffixPicker, "item generation: automatic affix picker",
     "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 83 EC 40 41 8B F9 41 8B F0 8B EA 48 8B D9 E8 ?? ?? ?? ?? 44 "
     "8B CF 44 8B C6 8B D5 48 8B CB 66 83 F8 01 72 ?? 8B 84 24 80 00 00 00 89 44 24 30 8B 44 24 78 89 44 24 28 8B 44 "
     "24 70 89 44 24 20 E8",
     0, 0x58AE80},
    // Whether an affix row can go on an item (D2RCore's picker asks it of every row): a classic stackable or thrown
    // item takes none; a socket affix (the first property's first stat 194, item_numsockets, at +0x20 of its
    // Properties row) only a base that can have sockets; then the row's five excluded item types (+0x78) and its
    // seven item types (+0x6A), each asked with the item-type test.
    {kAffixFitsItem, "affix fits the item (item types)",
     "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 48 89 7C 24 20 41 56 48 83 EC 20 4C 8B F2 48 8B E9 E8 ?? ?? ?? ?? "
     "66 83 F8 64 73 ?? 48 8B CD E8 ?? ?? ?? ?? 85 C0 0F 85 ?? ?? ?? ?? 48 8B CD E8 ?? ?? ?? ?? 85 C0 0F 85 ?? ?? ?? "
     "?? 48 8B CD E8 ?? ?? ?? ?? 85 C0 74 ?? 48 8B CD E8 ?? ?? ?? ?? 84 C0 75 ?? 49 8D 46 24 48 85 C0 74 ?? 66 83 38 "
     "00 7C ?? 66 83 78 02 00 75 ?? 8B 18 48 8B CD E8 ?? ?? ?? ?? 8B D3 0F B6 C8 E8 ?? ?? ?? ?? 48 85 C0 74 ?? B9 C2 "
     "00 00 00 66 39 48 20 74 ?? 33 DB 49 8D 76 78 8B FB 0F BF 06 66 85 C0 7E ?? 8B D0 48 8B CD E8 ?? ?? ?? ?? 85 C0 "
     "75 ?? FF C7 48 83 C6 02 83 FF 05 7C ?? 49 8D 7E 6A",
     0, 0x3D4220},
    // The affix table of a data-table bank: the bank's data tables plus 0x15E8, where the combined MagicSuffix,
    // MagicPrefix and AutoMagic rows are kept (the picker reads the three kinds' starts at +0x18, +0x20, +0x28).
    {kAffixTable, "affix table (bank)", "48 83 EC 28 E8 ?? ?? ?? ?? 48 05 E8 15 00 00 48 83 C4 28 C3", 0, 0x3142F0},
    // Cannot be cursed. A curse skill's step for one unit in its range: the context in rsi, the unit in rdi, the
    // unit's data-table bank, then the context's "monsters only" (Dim Vision, Terror). What it hands the routine
    // that puts the curse on is checked below.
    {kCurseSkillStep, "curse skill: step for one unit",
     "48 89 5C 24 08 48 89 6C 24 18 48 89 74 24 20 57 41 54 41 55 41 56 41 57 48 81 EC 90 00 00 00 48 8B F2 48 8B F9 "
     "E8 ?? ?? ?? ?? 83 7E 10 00 44 0F B6 F8 74 3E 48 8B CF E8 ?? ?? ?? ?? 83 F8 01 0F",
     0, 0x55A7E0},
    // The Cursed monster modifier's step for one unit near the monster: the level in r15d, the unit in r14, the
    // monster in r12, the game in rsi, then whether the unit is one the monster may curse.
    {kCursedModStep, "Cursed monster modifier: Amplify Damage step",
     "44 89 4C 24 20 55 56 41 54 41 56 41 57 48 8B EC 48 81 EC 80 00 00 00 45 8B F9 4D 8B F0 4C 8B E2 48 8B F1 E8 "
     "?? ?? ?? ?? 85 C0 0F 84 ?? ?? ?? ?? 0F B6 8E 06 01 00 00 48 89 9C 24 B0 00 00 00 E8",
     0, 0x4A06B0},
    // Infinite gold. The payment reads the player's gold (stat 14, `lea edx, [r8+0Eh]` with r8d zeroed) and the
    // stash's (15), and answers 0 when the two together are less than the amount (in esi).
    {kVendorPay, "vendor payment (gold taken)",
     "48 89 5C 24 18 48 89 74 24 20 57 48 83 EC 20 48 8B DA 41 8B F0 45 33 C0 48 8B CB 41 8D 50 0E E8 ?? ?? ?? ?? 45 "
     "33 C0 48 8B CB 8B F8 41 8D 50 0F E8 ?? ?? ?? ?? 03 C7 3B C6 7D 12 33 C0 48 8B 5C",
     0, 0x5416D0},
    // The home town. A town portal's cast: the player's room and its level, then that level's record (no portal
    // where the record forbids it). Its call of the pair maker is checked below.
    {kTownPortalStart, "town portal: cast (the portals made)",
     "40 55 53 56 57 41 54 41 56 41 57 48 8B EC 48 83 EC 60 4C 8B F1 48 8B FA 48 8B CA E8 ?? ?? ?? ?? 48 8B C8 E8 "
     "?? ?? ?? ?? 41 0F B6 8E 06 01 00 00 8B D0 E8 ?? ?? ?? ?? 48 8B D8 48 85 C0 0F 84",
     0, 0x46FA70},
    // The portal at the destination: the game's acts (`lea rbx, [rcx+190h]`), the destination level in r12d and its
    // act, then the act's town portal spot.
    {kPortalTownEnd, "portal: the one made at the destination",
     "48 89 5C 24 10 48 89 74 24 18 55 57 41 54 41 56 41 57 48 8B EC 48 81 EC 80 00 00 00 4C 8B F9 48 8D 99 90 01 00 "
     "00 0F B6 89 06 01 00 00 41 8B D1 45 8B E1 4D 8B F0 E8 ?? ?? ?? ?? 0F B6 F8 48 89",
     0, 0x435DD0},
    // A portal's use: the object (the context's second field) must be open (mode 2), the user a player.
    {kPortalOperate, "portal: use (object operate function)",
     "48 89 4C 24 08 55 53 56 57 41 56 41 57 48 8B EC 48 83 EC 68 48 8B 49 08 48 8D 5D 38 E8 ?? ?? ?? ?? 45 33 FF 83 "
     "F8 02 0F 85 ?? ?? ?? ?? 48 8B 4D 38 48 8B 49 10 E8 ?? ?? ?? ?? 85 C0 74 12 48 8D",
     0, 0x58F680},
    // The move to a level: the spot in r12d, the level in ebp, the player in rdi, the game in rbx (null asserts).
    {kWarpToLevel, "move a player to a level (across acts)",
     "48 8B C4 48 89 58 10 48 89 68 18 56 57 41 54 41 56 41 57 48 83 EC 60 45 8B E1 41 8B E8 48 8B FA 48 8B D9 48 85 "
     "C9 75 11 88 48 08 48 8D 48 08 E8 ?? ?? ?? ?? 84 C0 74 01 CC 0F B6 8B 06 01 00 00",
     0, 0x488180},
    // An act made for the game: the act's town, and the Levels rows' count it must lie in.
    {kLoadAct, "act made for the game",
     "48 89 5C 24 18 48 89 6C 24 20 56 57 41 56 48 83 EC 60 0F B6 EA 48 8B F9 0F B6 89 06 01 00 00 40 0F B6 D5 E8 ?? "
     "?? ?? ?? 0F B6 8F 06 01 00 00 8B F0 E8 ?? ?? ?? ?? 48 8B 98 68 14 00 00 4C 63 C3",
     0, 0x48AA50},
    // The waypoint test, from its check of the number on (its first 0x45 bytes are its twin's, which sets a
    // waypoint): 0x70 waypoints, a {word, mask} pair for each, the word read and masked (`and eax, ecx`).
    {kWaypointActive, "waypoint: the character has it",
     "83 FB 70 72 ?? 48 8D 4C 24 38 C6 44 24 38 00 E8 ?? ?? ?? ?? 84 C0 74 01 CC 33 C0 48 8B 5C 24 30 48 83 C4 20 5F "
     "C3 48 8D 0C 9D 00 00 00 00 48 8B 5C 24 30 48 8D 15 ?? ?? ?? ?? 0F B7 04 11 0F B7 4C 11 02 0F B7 04 47 23 C1 48 "
     "83 C4 20 5F C3",
     0x45, 0x3D6930},
    // A death's wake: the packet's size (r9d) must be 1, the player goes to r13 and the game to r14. What it asks
    // of the player and where it moves it are checked below.
    {kWakeInTown, "wake in town (after a death)",
     "40 55 41 55 41 56 48 8D 6C 24 B9 48 81 EC B0 00 00 00 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 45 17 4C 8B EA 4C 8B "
     "F1 41 83 F9 01 74 12 48 8D 4D E7 C6 45 E7 00 E8 ?? ?? ?? ?? 84 C0 74 01 CC 48 89",
     0, 0x4B6160},
    // The client's trip through a town portal: the player (rcx, its type asked first: a player) and the portal
    // (rdx, to rbx). What it asks of the portal is checked below.
    {kClientPortalTrip, "client: portal trip ahead of the server",
     "40 53 57 48 83 EC 28 48 8B DA 48 8B F9 48 85 C9 74 09 E8 ?? ?? ?? ?? 85 C0 74 14 48 8D 4C 24 40 C6 44 24 40 00 "
     "E8 ?? ?? ?? ?? 84 C0 74 01 CC 48 89 6C 24 50 48 89 74 24 20 48 85 DB 0F 84 ?? ??",
     0, 0xFE1F0},
    // NPC item services. The NPC table's getter: a walk over the records (`lea r10, [rip+table]`), each as long
    // as the `imul` and the `add` say, as many as the `cmp` says, for the one whose first word is the class asked
    // for. The whole routine: it has no frame and no calls.
    {kNpcRecord, "NPC record (by class)",
     "45 33 C0 4C 8D 15 ?? ?? ?? ?? 41 8B C8 4D 8B CA 4D 85 C0 75 ?? 41 0F BF 01 3B C2 75 ?? 8B C1 4C 6B C0 ?? 4D 03 "
     "C2 FF C1 49 83 C1 ?? 83 F9 ?? 7C ?? 49 8B C0 C3",
     0, 0x3971A0},
    // The server's handler of an NPC's item service (the request 0x2E): the NPC by the request's id, within reach
    // (`mov r8d, 32h`), whether it is the second pass in r15b. What it reads of the NPC's record and does for each
    // service is checked below.
    {kNpcItemService, "server: an NPC's item service",
     "40 55 41 54 41 56 41 57 48 8D AC 24 E8 FC FF FF 48 81 EC 18 04 00 00 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 85 F0 "
     "02 00 00 4D 8B D0 48 89 4C 24 58 48 8B C2 4C 89 44 24 68 48 89 54 24 50 41 B8 32 00 00 00 48 8B C8 45 0F B6 F9 "
     "41 8B 52 05 E8",
     0, 0x4FC230},
    // The client's NPC menu as it opens: the NPC in r15 (none closes the menu, the tail jump), the second argument
    // in r13d. Its walk of the quest rows is checked below.
    {kNpcMenuOpen, "client: an NPC's menu opened",
     "40 55 57 41 55 41 57 48 8B EC 48 83 EC 48 44 8B EA 4C 8B F9 E8 ?? ?? ?? ?? 33 FF 4D 85 FF 75 25 8B 0D ?? ?? ?? "
     "?? 89 3D ?? ?? ?? ?? E8 ?? ?? ?? ?? 33 D2 8D 4F 08 48 83 C4 48 41 5F 41 5D 5F 5D E9",
     0, 0x1141D0},
    // Infinite cube ingredients. The product routine: a frame of 0x16F8 bytes (its stack probe), the recipe kept in
    // r15 and at [rbp-48h], the player at [rsp+60h]. Its free of an ingredient and of Clear Sockets' socket
    // contents are checked below.
    {kCubeProducts, "cube: a recipe's products (ingredients used up)",
     "40 55 53 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 08 EA FF FF B8 F8 16 00 00 E8 ?? ?? ?? ?? 48 2B E0 48 8B 05 "
     "?? ?? ?? ?? 48 33 C4 48 89 85 E0 15 00 00 33 C0 4C 89 45 B8 45 33 ED 48 89 54 24 60",
     0, 0x5269C0},
    // The free of an item: it must lie on the ground nowhere (the room getter answers 0, else an assert) and on no
    // inventory (the item's inventory, 0 or an assert), then the unit is freed.
    {kItemFree, "item free (on no inventory, not on the ground)",
     "48 89 5C 24 08 57 48 83 EC 20 48 8B F9 48 8B DA 48 8B CA E8 ?? ?? ?? ?? 48 85 C0 74 14 48 8D 4C 24 40 C6 44 24 "
     "40 00 E8 ?? ?? ?? ?? 84 C0 74 01 CC 48 8B CB E8 ?? ?? ?? ?? 48 85 C0 74 14",
     0, 0x43EC10},
    // The free of what is in an item's sockets: the item's inventory, handed to the free of everything in an
    // inventory. The whole routine.
    {kSocketedFree, "item: what is in its sockets freed",
     "48 89 5C 24 08 57 48 83 EC 20 48 8B DA 48 8B F9 48 85 D2 75 13 48 8D 4C 24 38 88 54 24 38 E8 ?? ?? ?? ?? 84 C0 "
     "74 01 CC 41 B8 ?? ?? 00 00 48 8D 15 ?? ?? ?? ?? 48 8B CB E8 ?? ?? ?? ?? 48 85 C0 74 0B 48 8B D0 48 8B CF E8 ?? "
     "?? ?? ?? 48 8B 5C 24 30 48 83 C4 20 5F C3",
     0, 0x48FA40},
    // The quest reward giver: the Items row of the code asked for (the game's bank byte, `movzx ecx, [rcx+106h]`),
    // the level in edi. Its placement in the inventory and its drop at the feet are read below.
    {kItemGive, "quest reward: an item handed to a player",
     "48 89 6C 24 18 48 89 74 24 20 57 48 81 EC A0 00 00 00 41 8B C0 48 8B F2 48 8B E9 4C 8D 44 24 60 0F B6 89 06 01 "
     "00 00 8B D0 41 8B F9 E8 ?? ?? ?? ?? 48 85 C0 0F 84",
     0, 0x517530},
    // The protected-stat test (stat in ecx, unit type in edx): a player's life (6) while the players' no-damage switch
    // is on, its mana (8) and 0x1A while the mana cheat is on, a monster's life while the monsters' switch is on. The
    // whole routine; derive() checks that its first and last `cmp` read the switches ExecuteEvents reads.
    {kProtectedStat, "stat: may not go down (the no-damage switches)",
     "85 D2 75 23 38 15 ?? ?? ?? ?? 74 05 83 F9 06 74 13 80 3D ?? ?? ?? ?? 00 74 24 83 F9 08 74 05 83 F9 1A 75 1A B0 "
     "01 C3 83 FA 01 75 12 80 3D ?? ?? ?? ?? 00 74 09 83 F9 06 75 04 0F B6 C2 C3 32 C0 C3",
     0, 0x2F2830},
    // A player's Chronicle: the unit must be a player (`cmp dword [rbx], 0`) with its data (`mov rax, [rbx+10h]`);
    // the Chronicle is the data plus an offset (`add rax, imm32`, read below). The whole routine.
    {kChronicleOfPlayer, "Chronicle: a player's",
     "40 53 48 83 EC 20 48 8B D9 48 85 C9 75 13 88 4C 24 30 48 8D 4C 24 30 E8 ?? ?? ?? ?? 84 C0 74 01 CC 83 3B 00 74 "
     "14 48 8D 4C 24 30 C6 44 24 30 00 E8 ?? ?? ?? ?? 84 C0 74 01 CC 48 8B 43 10 48 85 C0 75 1B 48 8D 4C 24 30 88 44 "
     "24 30 E8 ?? ?? ?? ?? 84 C0 74 01 CC 33 C0 48 83 C4 20 5B C3 48 05 ?? ?? ?? ??",
     0, 0x347440},
    // An entry of a player's Chronicle by kind and id: the kind's map (kind 0 the Chronicle itself, the others at
    // `add r9, imm8`), its buckets (`mov rdx, [r9+disp8]`), the id hashed (MurmurHash3's finalizer) and masked by the
    // bucket count (`mov rax, [r9]`), the bucket's chain walked (`cmp r8d, [rax+disp8]`, `mov rax, [rax]`).
    {kChronicleFind, "Chronicle: an entry by kind and id",
     "4C 8B C9 0F B6 CA 84 D2 74 14 83 E9 01 74 0B 83 F9 01 75 ?? 49 83 C1 ?? EB 04 49 83 C1 ?? 4D 85 C9 74 ?? 49 8B "
     "51 ?? 48 85 D2 74 ?? 48 B9 CD 8C 55 ED D7 AF 51 FF 41 8B C0 48 0F AF C1 48 8B C8 48 C1 E9 21 48 33 C8 48 B8 53 "
     "EC 85 1A FE B9 CE C4 48 0F AF C8 48 8B C1 48 C1 E8 21 48 33 C8 49 8B 01 48 FF C8 48 23 C1 48 8D 0C C2 48 8B 04 "
     "C2 48 85 C0 74 ?? 44 3B 40 ?? 74 ?? 48 8B C8 48 8B 00",
     0, 0x3D87D0},
    // The share of a kind's entries a Chronicle has: a branch a kind that counts the entries of the game's own list
    // of the kind that the Chronicle's map holds, over how many the list counts (the lists are read below).
    {kChronicleShare, "Chronicle: the share of a kind found",
     "48 89 5C 24 08 55 56 57 48 83 EC 20 44 0F B6 C2 48 8B F9 84 D2 0F 84 ?? ?? ?? ?? 41 83 E8 01 0F 84 ?? ?? ?? ?? "
     "41 83 F8 01 0F 85 ?? ?? ?? ?? 4C 8B 15 ?? ?? ?? ?? 4D 85 D2 0F 84 ?? ?? ?? ?? 4C 8B 05 ?? ?? ?? ?? 45 33 C9 48 "
     "8B 05 ?? ?? ?? ?? 49 8D 1C C0",
     0, 0x3D8870},
    // The item notice: the game in r13, the flags in edi, the item in r15 and the player in rbx; it keeps the item's
    // flags and command flags (the first two calls), sets what the packet says, sends it and puts them back.
    {kItemNotice, "item notice (the clients told where an item went)",
     "48 89 5C 24 20 55 56 57 41 55 41 57 48 83 EC 60 4C 8B E9 41 8B F9 49 8B C8 4D 8B F8 48 8B DA E8 ?? ?? ?? ?? 49 "
     "8B CF 89 44 24 24 E8 ?? ?? ?? ??",
     0, 0x535F60},
    // The pick-up routine: whether it goes to the inventory (r8b, at [rbp-39h]) and the reach (r9d, to r13d). Its
    // notice of the item's new place is read below.
    {kItemPickup, "item picked up from the ground",
     "48 89 5C 24 18 55 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 E9 48 81 EC A0 00 00 00 45 33 E4 44 88 45 C7 4C 89 "
     "65 FF 45 8B E9 44 89 65 CF",
     0, 0x471950},
    // The identify routine: the item in rbp, the player in r14, the game in r15, its fourth argument in r12b; it
    // returns at once for an item that has the flag it tests (`mov edx, imm32`), else sets it (`lea edx, [rax+imm8]`).
    {kItemIdentify, "item identified",
     "48 89 6C 24 20 41 54 41 56 41 57 48 83 EC 50 49 8B E8 45 0F B6 E1 4C 8B F2 4C 8D 0D ?? ?? ?? ?? 4C 8B F9 BA 10 "
     "00 00 00 48 8B CD 41 B8 ?? ?? 00 00 E8 ?? ?? ?? ?? 85 C0 0F 85 ?? ?? ?? ?? 48 89 5C 24 70 8D 50 10",
     0, 0x46E8C0},
    // The AI dispatcher: the game in r15, the monster in r14; the game's own debug switch for every AI first (`mov
    // eax, [rip+disp]`: 1 every monster idle, 2 every one that is not on the players' side), then the monster's AI
    // record, a target for the AIs that take one, and the AI's own routine.
    {kAiDispatch, "AI: a monster's AI tick (dispatcher)",
     "48 89 54 24 10 48 89 4C 24 08 55 41 56 41 57 48 8D 6C 24 C1 48 81 EC E0 00 00 00 8B 05 ?? ?? ?? ?? 4C 8B F2 4C "
     "8B F9 83 F8 01 0F 84 ?? ?? ?? ?? 83 F8 02 75 11 48 8B CA E8 ?? ?? ?? ?? 83 F8 02 0F 85 ?? ?? ?? ??",
     0, 0x4A2A00},
    // The enemy test: the flag (r9b) kept in r12d, the other unit in rbx and rsi, the unit in rdi; a monster's owner
    // asked for first (`cmp eax, 1` after the unit type, then the owner getter), a missile's after it (type 3).
    {kEnemyTest, "AI: is the other unit an enemy",
     "40 56 57 41 54 48 83 EC 30 48 89 5C 24 50 45 0F B6 E1 4C 89 7C 24 20 49 8B D8 4C 8B F9 48 8B FA 48 8B CA 49 8B "
     "F0 E8 ?? ?? ?? ?? 48 8B CF 83 F8 01 75 16 E8 ?? ?? ?? ?? 48 85 C0 0F 84 ?? ?? ?? ?? 48 8B F8 E9 ?? ?? ?? ?? E8 "
     "?? ?? ?? ?? 83 F8 03",
     0, 0x492790},
    // Its two entry stubs, 16 bytes apart: the flag set (`mov r9b, 1`) or cleared (`xor r9d, r9d`), then `jmp rel32`.
    {kEnemyTestEntries, "AI: the enemy test's two entry stubs",
     "41 B1 01 E9 ?? ?? ?? ?? CC CC CC CC CC CC CC CC 45 33 C9 E9 ?? ?? ?? ??", 0, 0x48E460},
    // The AI's own enemy check: both units a player or a monster (the unit type twice, `cmp eax, 1`), neither dead,
    // then the room, whether the other can be attacked and its state, and a jump to the first stub at its end.
    {kAiEnemyCheck, "AI: enemy check of the unit searches",
     "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 48 8B F1 49 8B D8 48 8B CA 48 8B FA E8 ?? ?? ?? ?? 85 C0 74 09 83 "
     "F8 01 0F 85 ?? ?? ?? ?? 48 8B CB E8 ?? ?? ?? ?? 85 C0 74 05 83 F8 01 75 73",
     0, 0x597710},
    // The kind of the target kept on a monster: the unit must be a monster (`cmp eax, 1`) with its data, whose +0x44
    // is the kind (the id is at +0x40, read by the getter's twin right after it). The whole routine.
    {kKeptTarget, "AI: the kind of the target kept on a monster",
     "40 53 48 83 EC 20 48 8B D9 48 85 C9 74 4A E8 ?? ?? ?? ?? 83 F8 01 75 40 48 8B CB E8 ?? ?? ?? ?? 83 F8 01 74 14 "
     "48 8D 4C 24 30 C6 44 24 30 00 E8 ?? ?? ?? ?? 84 C0 74 01 CC 48 8B CB E8 ?? ?? ?? ?? 83 F8 01 75 12 48 8B 43 10 "
     "48 85 C0 74 09 8B 40 44 48 83 C4 20 5B C3 33 C0 48 83 C4 20 5B C3",
     0, 0x544A00},
    // The killself monster mod's timer: the game in rsi, the monster in rbx, the mod in edi. A dead monster (the first
    // call) is left alone; one with the uninterruptable state (`lea edx, [rax+36h]`, then the state test) has the same
    // event (type 7: `lea r8d, [rax+7]`, the mod its parameter) again 3 frames on (`add r9d, 3` to the game's frame).
    // The rest, the owner and the pet removal with its kill flag (else the death mode), is read below.
    {kKillSelfTimer, "monster mod: killself's timer (a revive's end)",
     "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 70 48 8B F1 41 8B F8 48 8B CA 48 8B DA E8 ?? ?? ?? ?? 85 C0 0F 85 ?? "
     "?? ?? ?? 8D 50 36 48 8B CB E8 ?? ?? ?? ?? 85 C0 74 3A 44 8B 8E 70 01 00 00 33 C0 89 44 24 30 41 83 C1 03 89 44 "
     "24 28 48 8B D3 48 8B CE 89 7C 24 20 44 8D 40 07 E8 ?? ?? ?? ??",
     0, 0x4A1D20},
    // The pet lookup: the unit's id in esi, the player in rdi; the player's data (the first call, which asserts on
    // anything else), its pet lists at +0x98, then each pet type's list walked for the id.
    {kPetTypeOf, "pets: which of its owner's pet lists a unit is in",
     "48 89 5C 24 10 56 57 41 56 48 83 EC 20 8B F2 48 8B F9 E8 ?? ?? ?? ?? 48 8B D8 48 85 C0 75 13 48 8D 4C 24 50 88 "
     "44 24 50 E8 ?? ?? ?? ?? 84 C0 74 01 CC 4C 8B B3 98 00 00 00",
     0, 0x4FF3B0},
    // The automap's draw of one unit: the view kept in rdi, the unit in rbp; the marker decision (the call, with the
    // marker at [rsp+60h] and the name's kind at [rsp+68h]), then the party option (`cmp dword [rip+disp], 0`) for the
    // party's markers 1 and 4.
    {kAutomapUnitDraw, "automap: draw one unit (marker and name)",
     "48 89 6C 24 10 57 48 83 EC 40 48 8B FA 4C 8D 44 24 68 48 8D 54 24 60 48 8B E9 E8 ?? ?? ?? ?? 84 C0 0F 84 ?? ?? "
     "?? ?? 83 3D ?? ?? ?? ?? 00 75 16 83 7C 24 60 01 0F 84 ?? ?? ?? ?? 83 7C 24 60 04 0F 84 ?? ?? ?? ??",
     0, 0xD76E0},
    {kCollisionFlags, "collision: a subtile's flags in its room's map",
     "48 89 6C 24 10 56 57 41 56 48 83 EC 20 41 8B F9 41 8B F0 8B EA 41 BE 27 00 00 00 E8 ?? ?? ?? ?? 48 85 C0 74 ?? "
     "48 8B C8 48 89 5C 24 40 E8",
     0, 0x366250},
    {kWarpTilePreset, "DRLG: a warp tile's preset unit",
     "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 48 89 7C 24 20 41 56 48 83 EC 40 83 7C 24 78 0B 41 8B F0 4C 8B 44 "
     "24 70 41 8B F9 41 B9 6C 00 00 00 B8 72 00 00 00 44 0F 44 C8",
     0, 0x3F4670},
};

Result g_results[kCount];
uintptr_t g_derived[dCount];
uintptr_t g_base = 0;
int g_unit_next = 0;
int g_unique_bits = 0;
int g_key_type = 0;
int g_automap_panel = 0;
AffixFacts g_affix;
SuperiorFacts g_superior;
CurseFacts g_curse;
PortalFacts g_portal;
WakeFacts g_wake;
NpcFacts g_npc;
CubeFacts g_cube;
ChronicleFacts g_chronicle;
IdentifyFacts g_identify;
bool g_protected_checked = false;
bool g_enemy_checked = false;
ReviveFacts g_revive;
MapFacts g_map;

// The bytes of an instruction's RIP-relative operand, as an absolute address.
uintptr_t rip_operand(uintptr_t insn, int disp_at, int len) {
  int32_t disp = 0;
  if (!mem::read_safe(insn + static_cast<uintptr_t>(disp_at), &disp)) return 0;
  return insn + static_cast<uintptr_t>(len) + static_cast<intptr_t>(disp);
}

// Where a `call rel32` at insn goes (0 when insn is not one).
uintptr_t call_at(uintptr_t insn) {
  uint8_t op = 0;
  int32_t rel = 0;
  if (!insn || !mem::read_safe(insn, &op) || op != 0xE8 || !mem::read_safe(insn + 1, &rel)) return 0;
  return insn + 5 + static_cast<intptr_t>(rel);
}

// ... and a `jmp rel32` (0 when insn is not one).
uintptr_t jmp_at(uintptr_t insn) {
  uint8_t op = 0;
  int32_t rel = 0;
  if (!insn || !mem::read_safe(insn, &op) || op != 0xE9 || !mem::read_safe(insn + 1, &rel)) return 0;
  return insn + 5 + static_cast<intptr_t>(rel);
}

// One byte of an instruction (an imm8 or a disp8), -1 when it cannot be read.
int byte_at(uintptr_t at) {
  uint8_t v = 0;
  return at && mem::read_safe(at, &v) ? v : -1;
}

// Where a near conditional jump (`0F 8x rel32`) at insn goes, and a short one (`7x rel8`); 0 when it cannot be read.
uintptr_t near_jump_at(uintptr_t insn) {
  int32_t rel = 0;
  return insn && mem::read_safe(insn + 2, &rel) ? insn + 6 + static_cast<intptr_t>(rel) : 0;
}
uintptr_t short_jump_at(uintptr_t insn) {
  int8_t rel = 0;
  return insn && mem::read_safe(insn + 1, &rel) ? insn + 2 + static_cast<intptr_t>(rel) : 0;
}

// The first match of a short pattern inside [from, from+span), as an address (0 = none).
uintptr_t find_in(uintptr_t from, size_t span, const char* pattern) {
  std::vector<uint8_t> copy;
  if (mem::snapshot(from, span, copy) < span) return 0;
  const std::vector<size_t> hits = mem::find_all(copy.data(), copy.size(), mem::parse_pattern(pattern), 1);
  return hits.empty() ? 0 : from + hits[0];
}



}  // namespace

const Spec& spec(Id id) { return kSpecs[id]; }
const Result& result(Id id) { return g_results[id]; }
uintptr_t exe_base() { return g_base; }
uintptr_t address(Id id) { return found(id) && g_base ? g_base + static_cast<uintptr_t>(g_results[id].rva) : 0; }
uintptr_t derived(Derived d) { return d >= 0 && d < dCount ? g_derived[d] : 0; }
int unit_next_offset() { return g_unit_next; }
int unique_bits_offset() { return g_unique_bits; }
int key_item_type() { return g_key_type; }
int automap_panel_id() { return g_automap_panel; }
const AffixFacts& affix_facts() { return g_affix; }
const SuperiorFacts& superior_facts() { return g_superior; }
const CurseFacts& curse_facts() { return g_curse; }
const PortalFacts& portal_facts() { return g_portal; }
const WakeFacts& wake_facts() { return g_wake; }
const NpcFacts& npc_facts() { return g_npc; }
const CubeFacts& cube_facts() { return g_cube; }
const ChronicleFacts& chronicle_facts() { return g_chronicle; }
const IdentifyFacts& identify_facts() { return g_identify; }
bool protected_stat_checked() { return g_protected_checked; }
bool enemy_test_checked() { return g_enemy_checked; }
const ReviveFacts& revive_facts() { return g_revive; }
const MapFacts& map_facts() { return g_map; }
uintptr_t call_target(Id id) { return call_at(address(id)); }

size_t entry_bytes(Id id, uint8_t* out, size_t capacity) {
  const uintptr_t at = address(id);
  if (!at) return 0;
  const mem::Pattern p = mem::parse_pattern(kSpecs[id].pattern);
  const size_t n = p.bytes.size() + static_cast<size_t>(kSpecs[id].offset);
  if (n > capacity || !mem::copy_from(out, at, n)) return 0;
  return n;
}

bool image_ready(uintptr_t exe_base) {
  const mem::Range text = mem::section(exe_base, ".text");
  if (text.empty()) return false;
  const Spec& probe = kSpecs[kSetEvent];
  const mem::Pattern p = mem::parse_pattern(probe.pattern);
  // Where the research put it first (cheap), then anywhere in .text.
  const uintptr_t at = exe_base + probe.seed_rva + static_cast<uintptr_t>(probe.offset);
  std::vector<uint8_t> head;
  if (text.contains(at) && mem::snapshot(at, p.bytes.size(), head) == p.bytes.size() &&
      !mem::find_all(head.data(), head.size(), p, 1).empty())
    return true;
  std::vector<uint8_t> copy;
  mem::snapshot(text.begin, text.size(), copy);
  return !mem::find_all(copy.data(), copy.size(), p, 1).empty();
}

int resolve(uintptr_t exe_base) {
  g_base = exe_base;
  const mem::Range text = mem::section(exe_base, ".text");
  if (text.empty()) {
    logf("sites: no .text section at base 0x%llX", static_cast<unsigned long long>(exe_base));
    return 0;
  }
  std::vector<uint8_t> copy;
  const size_t got = mem::snapshot(text.begin, text.size(), copy);
  logf("sites: .text at rva 0x%llX, %zu bytes (%zu readable)", static_cast<unsigned long long>(text.begin - exe_base),
       text.size(), got);
  int usable = 0;
  for (int i = 0; i < kCount; ++i) {
    const Spec& s = kSpecs[i];
    const mem::Pattern p = mem::parse_pattern(s.pattern);
    const std::vector<size_t> hits = mem::find_all(copy.data(), copy.size(), p, 8);
    Result& r = g_results[i];
    r.hits = static_cast<int>(hits.size());
    r.rva = hits.empty() ? 0 : static_cast<uint64_t>(text.begin - exe_base + hits[0]) - static_cast<uint64_t>(s.offset);
    for (size_t h = 0; h < hits.size() && h < 4; ++h)
      r.all[h] = static_cast<uint64_t>(text.begin - exe_base + hits[h]) - static_cast<uint64_t>(s.offset);
    // A stat helper with a twin: the one the loader patched (a jmp at its entry)
    // is the one the loader itself calls by that name.
    if (r.hits == 2 && s.offset > 0) {
      for (int h = 0; h < 2; ++h) {
        const size_t off = static_cast<size_t>(r.all[h] - (text.begin - exe_base));
        if (off < copy.size() && copy[off] == 0xFF) {
          r.rva = r.all[h];
          r.hits = 1;
          break;
        }
      }
    }
    if (r.hits == 1) ++usable;
    if (hits.empty()) {
      logf("sites: %-48s  NOT FOUND (research had 0x%llX)", s.name, static_cast<unsigned long long>(s.seed_rva));
    } else {
      char extra[128] = {};
      if (hits.size() > 1) {
        size_t n = 0;
        for (size_t h = 1; h < hits.size() && n < sizeof(extra) - 16; ++h)
          n += static_cast<size_t>(std::snprintf(extra + n, sizeof(extra) - n, " 0x%llX",
                                                 static_cast<unsigned long long>(text.begin - exe_base + hits[h] - s.offset)));
      }
      logf("sites: %-48s  0x%llX  hits=%d  research 0x%llX  moved %+lld%s", s.name,
           static_cast<unsigned long long>(r.rva), r.hits, static_cast<unsigned long long>(s.seed_rva),
           static_cast<long long>(r.rva) - static_cast<long long>(s.seed_rva), extra);
    }
  }
  return usable;
}

void derive() {
  for (uintptr_t& d : g_derived) d = 0;
  if (!g_base) return;
  // The client unit table: CLIENT_GetLocalPlayer looks its player up in it with
  // `lea rcx, [rip+disp]` right before the bucket search.
  if (const uintptr_t f = address(kClientGetLocalPlayer)) {
    if (const uintptr_t lea = find_in(f, 0xA0, "48 8D 0D ?? ?? ?? ?? 45 33 C9")) g_derived[dClientUnitTable] = rip_operand(lea, 3, 7);
  }
  // The two (id, type) lookups differ only by the table they `lea rax` from.
  const Result& lookups = g_results[kGetUnitByIdAndType];
  if (lookups.hits == 2 && g_derived[dClientUnitTable]) {
    for (int h = 0; h < 2; ++h) {
      const uintptr_t fn = g_base + static_cast<uintptr_t>(lookups.all[h]);
      const uintptr_t table = rip_operand(fn + 3, 3, 7);
      if (table == g_derived[dClientUnitTable]) {
        g_derived[dClientGetUnit] = fn;
      } else if (table) {
        g_derived[dServerGetUnit] = fn;
        g_derived[dServerUnitTable] = table;
      }
    }
  }
  // The game's own "no damage" switches: ExecuteEvents tests two adjacent bytes
  // (`cmp byte ptr [rip+disp], 0`) before applying anything to a player or a monster.
  if (const uintptr_t f = address(kExecuteEvents)) {
    const uintptr_t cmp = find_in(f, 0x300, "80 3D ?? ?? ?? ?? 00 74 ?? 45 85 E4 74");
    if (cmp) {
      const uintptr_t players = rip_operand(cmp, 2, 7);
      const uintptr_t cmp2 = find_in(cmp + 7, 0x40, "80 3D ?? ?? ?? ?? 00");
      const uintptr_t monsters = cmp2 ? rip_operand(cmp2, 2, 7) : 0;
      if (players && monsters == players + 1) {
        g_derived[dInvulnerablePlayers] = players;
        g_derived[dInvulnerableMonsters] = monsters;
      } else {
        log_warn("sites: the no-damage switches did not pair up (0x%llX, 0x%llX)",
                 static_cast<unsigned long long>(players ? players - g_base : 0),
                 static_cast<unsigned long long>(monsters ? monsters - g_base : 0));
      }
    }
  }
  // The protected-stat test asks the same two switches: `cmp [players], dl` at +4, `cmp [monsters], 0` at +0x2C.
  g_protected_checked = false;
  if (const uintptr_t f = address(kProtectedStat)) {
    const uintptr_t players = rip_operand(f + 4, 2, 6), monsters = rip_operand(f + 0x2C, 2, 7);
    g_protected_checked = players && players == g_derived[dInvulnerablePlayers] &&
                          monsters == g_derived[dInvulnerableMonsters];
    if (g_protected_checked)
      logf("sites: the protected-stat test reads the players' no-damage switch 0x%llX and the monsters' 0x%llX",
           static_cast<unsigned long long>(players - g_base), static_cast<unsigned long long>(monsters - g_base));
    else
      log_warn("sites: the protected-stat test reads 0x%llX and 0x%llX, not the no-damage switches ExecuteEvents reads",
               static_cast<unsigned long long>(players ? players - g_base : 0),
               static_cast<unsigned long long>(monsters ? monsters - g_base : 0));
  }
  // Which side panels are open: the setter stores its argument (`mov [rip+disp], ecx`) right after the prologue.
  if (const uintptr_t f = address(kPanelSideSet)) g_derived[dPanelSide] = rip_operand(f + 15, 2, 6);
  // The field that links a bucket's units: the walk's `mov rcx, [rax+disp32]` (a unit is well under 4 KiB).
  g_unit_next = 0;
  if (const uintptr_t walk = address(kUnitListNext)) {
    int32_t next = 0;
    const uintptr_t match = walk + static_cast<uintptr_t>(kSpecs[kUnitListNext].offset);
    if (mem::read_safe(match + 19, &next) && next > 0x10 && next < 0x1000 && (next & 7) == 0) g_unit_next = next;
  }
  // The loot filter's manager getter is one of many identical singleton getters; the ground labels
  // call it and hand what it returns straight to the "shown" test, which names it.
  if (const uintptr_t pair = address(kLootFilterCallPair)) {
    const uintptr_t getter = call_at(pair);
    const uintptr_t shown = call_at(pair + 11);
    if (getter && shown && shown == address(kLootFilterShown)) {
      g_derived[dLootFilterManager] = getter;
    } else {
      log_warn("sites: the loot filter call pair does not reach the shown test (0x%llX)",
               static_cast<unsigned long long>(shown ? shown - g_base : 0));
    }
  }
  // The unique generator sets a unique's "dropped this game" bit with `or [rcx+rbp+disp32], eax`
  // (rbp is the game): the disp is where the bits start in the game record.
  g_unique_bits = 0;
  if (const uintptr_t at = address(kUniqueBitSet)) {
    int32_t disp = 0;
    if (mem::read_safe(at + 3, &disp) && disp > 0x100 && disp < 0x100000 && (disp & 3) == 0) g_unique_bits = disp;
  }
  // The antidote's cure calls the routines it needs, in order, from fixed places in its block.
  if (const uintptr_t cure = address(kCureStates)) {
    const uintptr_t calls[5] = {call_at(cure), call_at(cure + 0x13), call_at(cure + 0x23), call_at(cure + 0x2E),
                                call_at(cure + 0x40)};
    if (calls[0] && calls[1] && calls[2] && calls[3] && calls[4]) {
      for (int i = 0; i < 5; ++i) g_derived[dStatListByState + i] = calls[i];
    } else {
      log_warn("sites: the antidote's cure block does not make its five calls");
    }
  }
  // The key use asks for the player's inventory, then walks it asking the item-type test with the key's
  // type (`mov edx, imm32` right before that call).
  g_key_type = 0;
  if (const uintptr_t f = address(kUseKey)) {
    const uintptr_t inv = find_in(f, 0x100, "48 8B CD 4C 89 BC 24 ?? 00 00 00 E8 ?? ?? ?? ?? 48 8B C8 48 8B D8 E8");
    const uintptr_t test = find_in(f, 0x100, "48 8B CF E8 ?? ?? ?? ?? BA ?? 00 00 00 48 8B C8 48 8B D8 E8");
    int32_t type = 0;
    if (inv && test && mem::read_safe(test + 9, &type) && type > 0 && type < 0x1000 && call_at(inv + 11) &&
        call_at(test + 19)) {
      g_derived[dGetInventory] = call_at(inv + 11);
      g_derived[dItemTypeTest] = call_at(test + 19);
      g_key_type = type;
      // ITEMS_ShouldRemoveOnUse asks the same test about a book (`mov edx, 12h` then the call).
      const uintptr_t book = address(kShouldRemoveOnUse);
      if (book && call_at(book + 0x1D) != g_derived[dItemTypeTest])
        log_warn("sites: the key use's item-type test (0x%llX) is not the one the item-use predicate calls",
                 static_cast<unsigned long long>(g_derived[dItemTypeTest] - g_base));
    } else {
      log_warn("sites: the key use does not make its inventory and item-type calls where expected");
    }
  }
  // The automap callback reads the active layer, then asks the room's level record for the level's layer.
  if (const uintptr_t f = address(kAutomapRoomCallback)) {
    g_derived[dAutomapLayer] = rip_operand(f + 0x48, 3, 7);
    g_derived[dLevelDefRecord] = call_at(f + 0x6B);
  }
  // The Show Items release asks the display-mode setting of its key (Show Items first, then Unfiltered),
  // reads its value, and in Hold mode sets the key's on/off byte to 0 (the setter bounds-checks the index
  // against the byte array's count, `cmp rdx, [rip+count]`, then stores through its data, `mov rax, [rip+data]`).
  if (const uintptr_t f = address(kShowItemsRelease)) {
    const uintptr_t filtered = call_at(f + 0x0D), unfiltered = call_at(f + 0x14), value = call_at(f + 0x1C);
    const uintptr_t setter = call_at(f + 0x2A);
    const uintptr_t count_at = setter ? find_in(setter, 0x40, "48 3B 15 ?? ?? ?? ??") : 0;
    const uintptr_t data_at = setter ? find_in(setter, 0x70, "48 8B 05 ?? ?? ?? ?? 88 1C 08") : 0;
    const uintptr_t count = count_at ? rip_operand(count_at, 3, 7) : 0;
    const uintptr_t data = data_at ? rip_operand(data_at, 3, 7) : 0;
    if (filtered && unfiltered && value && data && count == data + 8) {
      g_derived[dItemNameDisplay] = filtered;
      g_derived[dUnfilteredNameDisplay] = unfiltered;
      g_derived[dSettingValue] = value;
      g_derived[dShowItemsState] = data;
    } else {
      log_warn("sites: the Show Items release does not make its setting and on/off calls where expected");
    }
  }
  // The Automap key asks the automap panel's UI var after toggling it: `mov ecx, imm32` then the getter
  // (`movsxd rax, ecx; lea rcx, [rip+vars]; movzx eax, byte [rax+rcx]`).
  g_automap_panel = 0;
  if (const uintptr_t f = address(kAutomapKey)) {
    int32_t panel = 0;
    const uintptr_t getter = call_at(f + 0x2D);
    const uintptr_t lea = getter ? find_in(getter, 0x10, "48 63 C1 48 8D 0D ?? ?? ?? ?? 0F B6 04 08 C3") : 0;
    if (lea == getter && lea && mem::read_safe(f + 0x29, &panel) && panel > 0 && panel < 0x40) {
      g_derived[dUiVars] = rip_operand(getter + 3, 3, 7);
      g_automap_panel = panel;
    } else {
      log_warn("sites: the Automap key does not ask a UI var where expected");
    }
  }
  if (const uintptr_t at = address(kKeyActionSelect)) g_derived[dKeyActions] = rip_operand(at + 5, 3, 7);
  // All areas terrorized. A Worldstone Shard's use asks whether the game has terror zones, picks its act's manual
  // zone (`mov ecx, edi` the act, then the pick), applies it as manual terror (`mov r8b, 2`) and tells the clients.
  // The zone job's rotation takes its own terror off (`mov dl, 1`), asks the same check, applies the next zone
  // (`mov r8b, 1`) and tells the clients: its calls must reach the shard's. terror.cpp reads what these routines
  // walk, so the layouts are checked in their code too: the pick walks the config's manual groups (+0x268, count
  // +0x270, 0x20 bytes each, the zone count at +0x10); the removal walks the game's waiting levels (+0x7728, 0x790
  // bytes each, the kind at +0x8C), then its terrorized ones (+0x5820, 0x90 bytes each, the kind at +0x8C) and
  // takes each off by its level id (the entry's first dword) with a routine that reads their count (+0x5828) and
  // the level runtimes (+0x1D0).
  if (const uintptr_t f = address(kTerrorShardUse)) {
    const uintptr_t job = address(kDesecrateJob);
    const uintptr_t check = find_in(f, 0x80, "48 8B CB E8 ?? ?? ?? ?? 84 C0 0F 84");
    const uintptr_t pick = find_in(f, 0x100, "4C 8D 44 24 40 8B CF E8 ?? ?? ?? ?? 48 85 C0 75");
    const uintptr_t apply = find_in(f, 0x120, "41 B0 02 48 8B D0 48 8B CB E8 ?? ?? ?? ?? 48 8B CB E8");
    const uintptr_t rotate = job ? find_in(job, 0x200, "B2 01 48 8B CB E8 ?? ?? ?? ?? 48 8B CB E8 ?? ?? ?? ?? 84 C0") : 0;
    const uintptr_t reapply = job ? find_in(job, 0x240, "41 B0 01 48 8B D7 48 8B CB E8 ?? ?? ?? ?? 48 8B CB E8") : 0;
    const uintptr_t enabled = check ? call_at(check + 3) : 0;
    const uintptr_t picker = pick ? call_at(pick + 7) : 0;
    const uintptr_t applier = apply ? call_at(apply + 9) : 0;
    const uintptr_t update = apply ? call_at(apply + 17) : 0;
    const uintptr_t remove = rotate ? call_at(rotate + 5) : 0;
    const bool same = enabled && applier && update && remove && call_at(rotate + 13) == enabled && reapply &&
                      call_at(reapply + 9) == applier && call_at(reapply + 17) == update;
    const uintptr_t take_off = remove ? find_in(remove, 0x160, "8B 14 C8 49 8B CF E8") : 0;
    const uintptr_t level_off = take_off ? call_at(take_off + 6) : 0;
    const bool layouts =
        picker && find_in(picker, 0x80, "48 8B 80 68 02 00 00 48 8B 8E 70 02 00 00 48 C1 E1 05") &&
        find_in(picker, 0xE0, "48 83 78 10 00") && remove && find_in(remove, 0x30, "48 8D B9 28 77 00 00") &&
        find_in(remove, 0x80, "48 69 ?? ?? 90 07 00 00 48 8B 07 44 38 B4 01 8C 00 00 00") &&
        find_in(remove, 0xC0, "49 8D BF 20 58 00 00") &&
        find_in(remove, 0x120, "48 8D 0C C0 48 8B 07 48 03 C9 44 38 B4 C8 8C 00 00 00") && level_off &&
        find_in(level_off, 0x30, "48 83 B9 28 58 00 00 00") && find_in(level_off, 0x30, "48 8B 89 D0 01 00 00 E8");
    if (same && layouts) {
      g_derived[dTerrorEnabled] = enabled;
      g_derived[dTerrorApply] = applier;
      g_derived[dTerrorClientUpdate] = update;
      g_derived[dTerrorRemoveKind] = remove;
    } else {
      log_warn("sites: %s", !same ? "a Worldstone Shard's use and the zone job do not make the same terror calls"
                                  : "the terror zone lists are not where the game's code has them");
    }
  }
  // Max affixes. The magic step's prefix pick and prefix setter, then its suffix (+0xB4: -1 skips it beside a prefix, a
  // must when no prefix came, `cmovg ebp, ebx`), its pick and the suffix setter, name the affix picker (both picks
  // reach the same one: the game's wrapper that sends an expansion item to D2RCore's picker, a classic one to D2RCore's
  // classic picker, the kAffixPicker site) and the two slot setters, each storing the id at the item data plus a small
  // offset plus four times the slot (`mov [rax+rdi*4+disp8], esi`), the three prefixes right before the three
  // suffixes. The rare step's picks go to the expansion picker straight: its prefix pick must reach the one the wrapper
  // calls and write with the same setter. The crafted step's first pick must call the wrapper. A step that picks some
  // other way gets no counts below (its top-up stays off).
  g_affix = AffixFacts{};
  bool rare_same = false, crafted_same = false;
  const uintptr_t magic = address(kMagicAffixes);
  const uintptr_t magic_rest =
      magic ? find_in(magic + 0x4E, 0x70,
                      "44 8B CB 8B D3 E8 ?? ?? ?? ?? 48 8B 0F 45 33 C0 8B D0 90 44 8B F0 90 E8 ?? ?? ?? ?? 45 85 F6 90 75 "
                      "04 8B EB EB 08 39 AE B4 00 00 00 7C 44 8B 86 B4 00 00 00 44 8B CB 48 8B 0F 85 C0 89 44 24 28 8B D3 "
                      "0F 4F EB 44 89 7C 24 20 44 8B C5 E8 ?? ?? ?? ?? 48 8B 0F 45 33 C0 8B D0 90 8B E8 90 E8")
            : 0;
  if (const uintptr_t f = magic_rest && magic_rest == magic + 0x4E ? magic : 0) {
    const uintptr_t pick = call_at(f + 0x53), set_prefix = call_at(f + 0x65), set_suffix = call_at(f + 0xAC);
    const uintptr_t expansion_picker = pick && pick == address(kAffixPicker) ? call_at(pick + 0x4D) : 0;
    const char* const kStore = "89 74 B8 ?? 90 48 8B 5C 24 38";
    const uintptr_t prefix_store = set_prefix ? find_in(set_prefix, 0xA0, kStore) : 0;
    const uintptr_t suffix_store = set_suffix ? find_in(set_suffix, 0xA0, kStore) : 0;
    int8_t prefix_disp = 0, suffix_disp = 0;
    const uintptr_t rare = address(kRareAffixes);
    const uintptr_t rare_pick =
        rare ? find_in(rare, 0x300, "C7 44 24 20 01 00 00 00 E8 ?? ?? ?? ?? 85 C0 90 74 ?? 45 8B C6 8B D0 90 48 8B CF E8")
             : 0;
    const uintptr_t crafted = address(kCraftedAffixes);
    const uintptr_t crafted_pick = crafted ? find_in(crafted, 0x200, "C7 44 24 20 01 00 00 00 49 8B CF 45 33 E4 E8") : 0;
    if (expansion_picker && call_at(f + 0x9B) == pick && set_prefix && set_suffix && set_prefix != set_suffix &&
        prefix_store && suffix_store && mem::read_safe(prefix_store + 3, &prefix_disp) &&
        mem::read_safe(suffix_store + 3, &suffix_disp) && prefix_disp < 0 && (prefix_disp & 3) == 0 &&
        suffix_disp == prefix_disp + 12) {
      g_derived[dPickAffix] = pick;
      g_derived[dSetPrefix] = set_prefix;
      g_derived[dSetSuffix] = set_suffix;
      g_affix.prefix_slots = prefix_disp;
      g_affix.suffix_slots = suffix_disp;
      rare_same = rare_pick && call_at(rare_pick + 8) == expansion_picker && call_at(rare_pick + 27) == set_prefix;
      crafted_same = crafted_pick && call_at(crafted_pick + 14) == pick;
      if (rare && !rare_same)
        log_warn("sites: the rare affix step does not pick and write its affixes the way the magic step does");
      // Best affixes. The rare step's suffix pick goes to the same picker and writes with the suffix setter. After its
      // loop the step applies each slot's affix, prefixes then suffixes: the row getter's row in r9, zeros in ecx,
      // edx and both stack arguments, then the apply routine, the same call both times.
      const uintptr_t rare_suffix =
          rare_same ? find_in(rare, 0x300, "89 4C 24 20 44 8B C2 48 8B CF E8 ?? ?? ?? ?? 85 C0 90 74 ?? 44 8B C6 8B D0 "
                                           "90 48 8B CF E8")
                    : 0;
      const char* const kApply = "48 85 C0 74 ?? 89 6C 24 28 4C 8B C8 4C 8B C7 89 6C 24 20 33 D2 33 C9 E8";
      const uintptr_t apply_prefixes = rare_same ? find_in(rare, 0x400, kApply) : 0;
      const uintptr_t apply_suffixes = apply_prefixes ? find_in(apply_prefixes + 1, 0x60, kApply) : 0;
      const uintptr_t apply = apply_prefixes ? call_at(apply_prefixes + 23) : 0;
      if (rare_suffix && call_at(rare_suffix + 10) == expansion_picker && call_at(rare_suffix + 29) == set_suffix &&
          apply && apply_suffixes && call_at(apply_suffixes + 23) == apply) {
        g_derived[dRarePickSuffix] = rare_suffix + 10;
        g_derived[dRarePickPrefix] = rare_pick + 8;
        g_derived[dApplyAffix] = apply;
      } else if (rare_same) {
        log_warn("sites: the rare affix step's suffix pick or its affix apply is not where expected");
      }
      if (crafted && !crafted_same)
        log_warn("sites: the crafted affix step does not call the affix picker the magic step calls");
    } else {
      log_warn("sites: the magic affix step does not make its picker and slot setter calls where expected");
    }
  } else if (magic) {
    log_warn("sites: the magic affix step's suffix half is not what was expected");
  }
  // Best affixes' automatic affix: the affix picker's twin must send an expansion item's pick to the same D2RCore
  // picker (the call after the stack arguments) and a classic item's to the same classic one, so that what it
  // answers is an id of the same combined table.
  if (const uintptr_t twin = address(kAutoAffixPicker)) {
    const uintptr_t picker = address(kAffixPicker);
    const uintptr_t expansion = call_at(twin + 0x50), classic = call_at(twin + 0x67);
    g_affix.auto_same = picker && expansion && classic && expansion == call_at(picker + 0x4D) &&
                        classic == call_at(picker + 0x60);
    if (!g_affix.auto_same)
      log_warn("sites: the automatic affix picker does not call the affix picker's two pickers where expected");
  }
  // The counts' tops, from the steps themselves. The rare step asks the item's type and, for its jewel type (`cmp
  // eax, imm8`), counts a coin (`and r12d, 1`) plus a base; any other item draws a power-of-two roll (`mov edx, n`)
  // into the table the `lea r12` names. Then it walks the request's forced ids, the prefixes' at +0xA8 and the
  // suffixes' at +0xB4, one per affix it makes on that side. The crafted step walks the same ids; its count is the
  // higher of a roll of one more than its top item-level minimum (the last `mov eax, imm32`, then `lea edx, [rax+1]`)
  // and the item level's minimum. The class skill step gives three lines above its first threshold (a roll of 100 plus
  // the bonus), and each line +3 at its second (another roll of 100 plus half the bonus, `cdq; sub eax, edx; sar eax,
  // 1`).
  if (const uintptr_t rare = rare_same ? address(kRareAffixes) : 0) {
    const uintptr_t m = find_in(rare, 0x180,
                                "E8 ?? ?? ?? ?? 83 F8 ?? 75 ?? 8B 0B 4C 8B CB 4C 69 E1 C5 90 C6 6A 8B 4B 04 4C 03 E1 49 "
                                "8B CC 44 89 23 48 C1 E9 20 41 83 E4 01 89 4B 04 41 83 C4 ?? EB ?? BA ?? 00 00 00 48 8B "
                                "CB E8 ?? ?? ?? ?? 48 63 C8 4C 8D 25 ?? ?? ?? ?? 4C 8B CB 45 8B 24 8C");
    const uintptr_t forced = find_in(rare, 0x180, "49 81 C5 B4 00 00 00 48 05 A8 00 00 00");
    uint8_t type = 0, base = 0, n = 0;
    int32_t table[16] = {};
    const uintptr_t at = m ? rip_operand(m + 67, 3, 7) : 0;
    if (m && forced && mem::read_safe(m + 7, &type) && mem::read_safe(m + 48, &base) && mem::read_safe(m + 52, &n) &&
        n > 0 && n <= 16 && (n & (n - 1)) == 0 && at && mem::copy_from(table, at, n * sizeof(int32_t))) {
      int top = 0;
      for (int i = 0; i < n; ++i) top = table[i] > top ? table[i] : top;
      // Three slots a side: no count past six means anything.
      if (type > 0 && base > 0 && base < 6 && top > 0 && top <= 6) {
        g_affix.jewel_type = type;
        g_affix.jewel_max = base + 1;
        g_affix.rare_max = top;
      }
    }
    if (!g_affix.rare_max) log_warn("sites: the rare affix step's count is not where expected");
  }
  if (const uintptr_t crafted = crafted_same ? address(kCraftedAffixes) : 0) {
    const uintptr_t m = find_in(crafted, 0x180,
                                "41 8B 4C 24 18 4C 8B F0 83 F9 ?? 48 89 44 24 40 B8 ?? 00 00 00 41 8B DD 0F 9F C3 FF C3 "
                                "83 F9 ?? 0F 4F D8 B8 ?? 00 00 00 83 F9 ?? 49 8B CE 0F 4F D8 8D 50 01 E8");
    const uintptr_t forced = find_in(crafted, 0x180, "49 8D 9C 24 A8 00 00 00 8B C8 48 89 5C 24 30 4D 8D AC 24 B4 00 00 00");
    int32_t top = 0;
    if (m && forced && mem::read_safe(m + 36, &top) && top > 0 && top <= 6) g_affix.crafted_max = top;
    else log_warn("sites: the crafted affix step's count is not where expected");
  }
  if (const uintptr_t f = address(kClassSkillBonus)) {
    // The count roll right after the signature: a roll of 100 (`mov edx, 64h`), plus the bonus ([rbp+188h]), then `cmp
    // eax, imm8; jle` before the three-line branch (`mov qword [rsp+60h], 3`).
    const uintptr_t count = find_in(f + 70, 0x40,
                                    "48 C7 85 00 01 00 00 FF FF FF FF 4C 8B E2 E8 ?? ?? ?? ?? 48 8B C8 BA 64 00 00 00 E8 "
                                    "?? ?? ?? ?? 8B 8D 88 01 00 00 BA 02 00 00 00 03 C1 44 8D 7A FF 83 F8 ?? 7E 0B 48 C7 "
                                    "44 24 60 03 00 00 00");
    const uintptr_t level = find_in(f, 0x800,
                                    "BA 64 00 00 00 E8 ?? ?? ?? ?? 8B C8 8B 85 88 01 00 00 99 2B C2 D1 F8 03 C8 83 F9 ?? "
                                    "7C ?? BF 03 00 00 00");
    uint8_t lines = 0, plus3 = 0;
    if (count == f + 70 && level && mem::read_safe(count + 51, &lines) && mem::read_safe(level + 27, &plus3) &&
        lines > 0 && lines < 100 && plus3 > 0 && plus3 < 100) {
      // Both rolls are 0..99: the lowest bonus that clears both thresholds even on a 0.
      g_affix.skill_bonus = lines + 1 > 2 * plus3 ? lines + 1 : 2 * plus3;
    } else {
      log_warn("sites: the class item skill step's rolls are not where expected");
    }
  }
  // A superior item's kind. The step takes the count of the kinds from the table (`mov rbx, [rbx+8]`), or a small
  // one (`mov ebx, imm32`) for a base that is thrown (the test it calls, which answers with a byte of the ItemTypes
  // row of the item's type: `movzx eax, byte [rax+disp8]`) or has no durability (a byte of the Items row, `cmp
  // [rdi+disp32], al`). In its loop the row it rolled goes to the test that is hooked (`call`), and a kind the test
  // refuses is marked tried (`mov byte [rsi], 1`) and the roll made again.
  g_superior = SuperiorFacts{};
  if (const uintptr_t step = address(kSuperiorStep)) {
    const uintptr_t count = find_in(step, 0x100,
                                    "48 8B 5B 08 33 C0 49 8B CE 48 89 44 24 38 66 89 44 24 40 E8 ?? ?? ?? ?? 85 C0 75 08 "
                                    "38 87 ?? ?? ?? ?? 74 05 BB ?? ?? ?? ?? 4C 63 FB");
    const uintptr_t ask = find_in(step, 0x1C0,
                                  "48 8B E8 48 85 C0 74 ?? 48 8B D0 49 8B CE E8 ?? ?? ?? ?? 85 C0 75 ?? 33 C0 C6 06 01");
    const uintptr_t thrown = count ? call_at(count + 19) : 0;
    const uintptr_t type_byte = thrown ? find_in(thrown, 0x80, "48 85 C0 74 ?? 0F B6 40 ?? 48 83 C4 20 5F C3") : 0;
    const int thrown_at = type_byte ? byte_at(type_byte + 8) : -1;
    int32_t no_durability = 0, few = 0;
    if (count && ask && address(kSuperiorFits) && call_at(ask + 14) == address(kSuperiorFits) &&
        mem::read_safe(count + 30, &no_durability) && mem::read_safe(count + 37, &few) && no_durability > 0 &&
        no_durability < 0x1C0 && few > 0 && few <= 10 && thrown_at > 0 && thrown_at < 0x80) {
      g_superior.known = true;
      g_superior.few = few;
      g_superior.no_durability_at = no_durability;
      g_superior.thrown_at = thrown_at;
    } else {
      log_warn("sites: the superior step does not count and test its kinds where expected");
    }
  }
  // Cannot be cursed. A curse skill's step copies its context into the effect it hands the routine that puts the
  // curse on: the state (`mov eax, [rsi+disp8]`) into the effect's state field (+0x24), the unit (rdi, the step's
  // first argument) into its target (+8). The Cursed monster modifier's step builds the same effect for one skill
  // (`mov r9d, imm32`, Amplify Damage), the monster (r12) its source, the unit (r14) its target, the state the one
  // the skill's row names (`movsx eax, word [rdi+disp32]`), and calls the same routine.
  g_curse = CurseFacts{};
  if (const uintptr_t step = address(kCurseSkillStep)) {
    const uintptr_t m = find_in(step, 0x240,
                                "8B 46 ?? 89 84 24 ?? 00 00 00 49 8B C5 48 0F 45 C1 48 89 7C 24 ?? 48 8D 4C 24 ?? 48 "
                                "89 84 24 ?? 00 00 00 89 9C 24 ?? 00 00 00 E8");
    const uintptr_t cursed = address(kCursedModStep);
    const uintptr_t skill =
        cursed ? find_in(cursed, 0x200, "41 B9 ?? 00 00 00 4C 89 65 ?? 49 8B D4 4C 89 75 ?? C7 45 ?? ?? 00 00 00") : 0;
    const uintptr_t call = cursed ? find_in(cursed, 0x200, "0F BF 87 ?? ?? 00 00 89 45 ?? 33 C0 48 89 45 ?? E8") : 0;
    const uintptr_t apply = m ? call_at(m + 42) : 0;
    int32_t state_field = 0, skill_id = 0, skill_id2 = 0, row_state = 0;
    const int effect = byte_at(m + 26), target = byte_at(m + 21), state_at = byte_at(m + 2);
    const bool step_ok = m && apply && effect > 0 && target - effect == 8 && mem::read_safe(m + 6, &state_field) &&
                         state_field - effect == 0x24 && state_at > 0 && state_at < 0x80;
    // The modifier's effect sits on its frame (rbp - disp8): the fields are told by how far apart they are.
    const auto frame = [](uintptr_t at) { return static_cast<int8_t>(byte_at(at)); };
    const bool cursed_ok = skill && call && mem::read_safe(skill + 2, &skill_id) &&
                           mem::read_safe(skill + 20, &skill_id2) && skill_id == skill_id2 && skill_id > 0 &&
                           frame(skill + 16) - frame(skill + 9) == 8 && frame(skill + 19) - frame(skill + 9) == 0x10 &&
                           frame(call + 9) - frame(skill + 9) == 0x24 && mem::read_safe(call + 3, &row_state) &&
                           row_state > 0 && row_state < 0x2EC && call_at(call + 16) == apply;
    if (step_ok) {
      g_derived[dCurseApply] = apply;
      g_curse.state_offset = state_at;
    } else {
      log_warn("sites: a curse skill's step does not hand its curse on the way expected");
    }
    if (step_ok && cursed_ok) {
      g_curse.amplify_skill = skill_id;
      g_curse.skill_state = row_state;
    } else if (cursed) {
      log_warn("sites: the Cursed monster modifier's step does not put its curse on the way a curse skill does");
    }
  }
  // The home town. A cast makes its portals with the pair maker (the object class on the stack, `mov dword
  // [rsp+38h], imm32`), which makes the one beside the player and calls the town end maker for the other. The town
  // end maker asks for its act's town portal spot (`mov r9d, imm32`) and sets the new portal's destination with a
  // routine of D2RCore's. A portal's use tests the object's class in its context (`cmp dword [rax+disp8], imm8`) and
  // the state a player has right after a use (`mov edx, imm32`, then the state test the antidote's cure calls too).
  // The move to a level calls the act change last, which makes an act the game has not yet (`cmp qword
  // [rcx+rax*8], 0` on the game's acts, `lea rdi, [rsi+disp32]`) with the routine found as kLoadAct.
  g_portal = PortalFacts{};
  if (const uintptr_t start = address(kTownPortalStart)) {
    const uintptr_t town_end = address(kPortalTownEnd);
    const uintptr_t operate = address(kPortalOperate);
    const uintptr_t warp = address(kWarpToLevel);
    const uintptr_t pair_call = find_in(start, 0x360,
                                        "C7 44 24 38 ?? 00 00 00 4C 8B C0 48 89 4C 24 30 48 8B D7 44 89 7C 24 28 49 8B "
                                        "CE 89 74 24 20 E8");
    const uintptr_t pair = pair_call ? call_at(pair_call + 31) : 0;
    const uintptr_t end_call =
        pair ? find_in(pair, 0x2A0, "89 44 24 20 49 8B D6 48 8B CD E8 ?? ?? ?? ?? 48 8B D8 48 85 C0 74") : 0;
    const uintptr_t spot = town_end ? find_in(town_end, 0xA0, "41 B9 ?? 00 00 00 C7 44 24 30 03 00 00 00 45 8B C4") : 0;
    const uintptr_t setter =
        town_end ? find_in(town_end, 0x300, "48 8B CB E8 ?? ?? ?? ?? 48 8B CB E8 ?? ?? ?? ?? 0C 03 48 8B CB 0F B6 D0 E8")
                 : 0;
    const uintptr_t class_test = operate ? find_in(operate, 0x300, "48 8B 45 ?? 83 78 ?? ?? 0F 85") : 0;
    const uintptr_t used = operate ? find_in(operate, 0x400, "48 8B 48 10 BA ?? 00 00 00 E8 ?? ?? ?? ?? 85 C0 0F 85") : 0;
    const uintptr_t change_call = warp ? find_in(warp, 0x100, "45 8B CC 44 8B C5 48 8B D6 48 8B CB E8") : 0;
    const uintptr_t change = change_call ? call_at(change_call + 12) : 0;
    const uintptr_t acts = change ? find_in(change, 0xC0, "48 8D BE ?? ?? 00 00") : 0;
    const uintptr_t load = change ? find_in(change, 0x100, "48 83 3C C1 00 75 ?? 41 0F B6 D5 48 8B CE E8") : 0;
    int32_t cast_class = 0, town_spot = 0, used_state = 0, acts_at = 0;
    const bool cast_ok = pair && end_call && town_end && call_at(end_call + 10) == town_end &&
                         mem::read_safe(pair_call + 4, &cast_class) && cast_class > 0 && cast_class < 0x1000;
    const bool end_ok = spot && setter && call_at(setter + 3) && mem::read_safe(spot + 2, &town_spot) &&
                        town_spot > 0 && town_spot < 0x40;
    const bool use_ok = class_test && used && byte_at(class_test + 7) == cast_class && byte_at(class_test + 6) > 0 &&
                        mem::read_safe(used + 5, &used_state) && used_state > 0 && used_state < 0x400 &&
                        call_at(used + 9) && call_at(used + 9) == g_derived[dStateOn];
    // The town end maker reads the same acts (`lea rbx, [rcx+disp32]`, in its signature).
    const uintptr_t end_acts_at = town_end ? find_in(town_end, 0x30, "48 8D 99 ?? ?? 00 00") : 0;
    int32_t end_acts = 0;
    const bool move_ok = acts && load && address(kLoadAct) && call_at(load + 14) == address(kLoadAct) &&
                         mem::read_safe(acts + 3, &acts_at) && acts_at > 0 && acts_at < 0x10000 && end_acts_at &&
                         mem::read_safe(end_acts_at + 3, &end_acts) && end_acts == acts_at;
    // The client's trip ahead of the server is for an object of the cast's class (`cmp eax, imm8` after the class
    // getter, called with the source file and line as the game's getters are), and takes the level of the
    // portal's other end from the unit (`movzx edx, word [rbx+disp32]`, for the level's record).
    const uintptr_t trip = address(kClientPortalTrip);
    const uintptr_t trip_class =
        trip ? find_in(trip, 0xA0, "41 B8 ?? ?? 00 00 48 8D 15 ?? ?? ?? ?? 48 8B CB E8 ?? ?? ?? ?? 83 F8 ?? 0F 85") : 0;
    const uintptr_t trip_level = trip ? find_in(trip, 0x1A0, "0F B7 93 ?? ?? 00 00 40 0F B6 CE E8") : 0;
    int32_t other_level = 0;
    const bool trip_ok = trip_class && trip_level && byte_at(trip_class + 23) == cast_class &&
                         mem::read_safe(trip_level + 3, &other_level) && other_level > 0 && other_level < 0x1000;
    if (cast_ok && end_ok && use_ok && move_ok && trip_ok) {
      g_derived[dPortalPair] = pair;
      g_derived[dSetObjectDest] = call_at(setter + 3);
      g_portal.portal_class = cast_class;
      g_portal.town_spot = town_spot;
      g_portal.used_state = used_state;
      g_portal.class_offset = byte_at(class_test + 6);
      g_portal.acts_offset = acts_at;
      g_portal.other_level_offset = other_level;
    } else {
      log_warn("sites: %s", !cast_ok   ? "a town portal's cast does not make its portals the way expected"
                            : !end_ok  ? "the town end of a portal is not made the way expected"
                            : !use_ok  ? "a portal's use does not make its tests where expected"
                            : !move_ok ? "the move to a level does not make an act the way expected"
                                       : "the client's trip through a portal is not decided the way expected");
    }
  }
  // A death's wake. The handler asks for the player's mode (`cmp eax, imm8` after its first call past the size
  // test: the dead mode) and, once the player is whole again, moves it to its act's town with the move to a level
  // (the act's town in eax from the call before, `xor r9d, r9d` for the spot: 0; the player in r13, the game in
  // r14), the routine found as kWarpToLevel.
  g_wake = WakeFacts{};
  if (const uintptr_t wake = address(kWakeInTown)) {
    const uintptr_t mode = find_in(wake, 0x90, "49 8B CD 48 89 B4 24 ?? 00 00 00 48 89 BC 24 ?? 00 00 00 4C 89 A4 24 ?? "
                                               "00 00 00 4C 89 BC 24 ?? 00 00 00 E8 ?? ?? ?? ?? 83 F8 ?? 0F 84");
    const uintptr_t move = find_in(wake, 0x5B0, "E8 ?? ?? ?? ?? 45 33 C9 44 8B C0 49 8B D5 49 8B CE E8");
    const int dead = mode ? byte_at(mode + 42) : -1;
    if (mode && move && dead > 0 && address(kWarpToLevel) && call_at(move + 17) == address(kWarpToLevel)) {
      g_wake.known = true;
      g_wake.dead_mode = dead;
      g_wake.town_spot = 0;
    } else {
      log_warn("sites: a death's wake does not move the player to a town the way expected");
    }
  }
  // NPC item services. The NPC table's getter names the table, the size of a record and their number. The server's
  // handler takes the record of the NPC the request names (the getter's call), wants the record's service to be the
  // request's (`cmp [rcx+1], edx`) and reads the quest that asks for it (`movzx ecx, byte [rax+disp8]`, at most
  // `cmp cl, imm8`). For a quest (the byte is its number plus one: `lea edx, [rdi-1]`) it asks the player's quest
  // record for a reward to be had (the flag, `mov r8d, imm32`). Its first pass is made only for a record with a
  // quest (`cmp byte [rax+disp8], sil`, sil zero): the second pass, the third service and a record without a quest
  // all go straight on to the service. The services are told by what is done for each: for 1 (`sub ecx, 1; je`) the
  // item is made again as a rare one (the request's flag 0x20, quality 6), for 2 the socketed flag is set (0x800),
  // for 3 (what is left: `cmp ecx, 1`) the personalized flag (0x1000000). The client's menu opener walks its rows
  // (`lea rbx, [rip+rows]`, as many as `movsxd rsi, [rip+count]` says) and for a row of the NPC's class (`cmp word
  // [rbx-0Ch], r12w`) asks its copy of the quest record with the row's quest and flag, by the routine the handler
  // asks with, and calls the row's routine (`call qword [rbx+4]`) when the answer is what the row wants.
  g_npc = NpcFacts{};
  if (const uintptr_t getter = address(kNpcRecord)) {
    const uintptr_t handler = address(kNpcItemService);
    const uintptr_t opener = address(kNpcMenuOpen);
    const uintptr_t records = rip_operand(getter + 3, 3, 7);
    const int size = byte_at(getter + 34), step = byte_at(getter + 43), count = byte_at(getter + 46);
    const uintptr_t read =
        handler ? find_in(handler, 0x600,
                          "E8 ?? ?? ?? ?? 48 89 45 ?? 48 85 C0 0F 84 ?? ?? ?? ?? 0F B6 48 ?? 84 C9 0F 84 ?? ?? ?? ?? "
                          "8B D1 48 8B 4C 24 ?? 39 51 01 0F 85 ?? ?? ?? ?? 0F B6 48 ?? 80 F9 ?? 0F 86")
                : 0;
    const uintptr_t asked =
        handler ? find_in(handler, 0x800, "8D 57 FF 41 B8 ?? 00 00 00 48 8B 4C C8 ?? E8 ?? ?? ?? ?? 85 C0 0F 85") : 0;
    const uintptr_t gate =
        handler ? find_in(handler, 0x1800,
                          "48 8B 45 ?? 45 84 FF 0F 85 ?? ?? ?? ?? 0F B6 48 ?? 80 F9 ?? 0F 84 ?? ?? ?? ?? 40 38 70 ?? "
                          "0F 84")
                : 0;
    const uintptr_t dispatch =
        handler ? find_in(handler, 0x1800, "0F B6 48 ?? 83 E9 01 0F 84 ?? ?? ?? ?? 83 E9 01 74 ?? 83 F9 01 0F 85") : 0;
    const uintptr_t walk =
        opener ? find_in(opener, 0x400,
                         "48 63 35 ?? ?? ?? ?? 44 8B E0 4C 8B 35 ?? ?? ?? ?? 48 85 F6 7E ?? 48 8D 1D ?? ?? ?? ?? 66 44 "
                         "39 63 ?? 75 ?? 49 8B CE 44 8B 43 ?? 8B 53 ?? 39 3B 74 ?? E8 ?? ?? ?? ?? 85 C0 74 ?? EB ?? E8 "
                         "?? ?? ?? ?? 85 C0 75 ?? FF 53 ?? 48 83 C3 ?? 48 83 EE 01 75")
               : 0;
    const int service_at = read ? byte_at(read + 21) : -1, quest_at = read ? byte_at(read + 49) : -1;
    const int quest_max = read ? byte_at(read + 52) : -1, flag = asked ? byte_at(asked + 5) : -1;
    const uintptr_t quest_state = asked ? call_at(asked + 14) : 0;
    const bool table_ok = records && size > 2 && size == step && count > 0;
    const bool read_ok = read && call_at(read) == getter && service_at > 1 && service_at < size && quest_at > 1 &&
                         quest_at < size && quest_at != service_at && quest_max > 0 && quest_state && flag > 0 &&
                         flag < 16;
    // The three ways out of the first pass come to one place, where the service is done.
    const uintptr_t service = gate ? near_jump_at(gate + 7) : 0;
    const bool gate_ok = service && near_jump_at(gate + 20) == service && near_jump_at(gate + 30) == service &&
                         byte_at(gate + 16) == service_at && byte_at(gate + 19) == 3 && byte_at(gate + 29) == quest_at;
    const uintptr_t imbue = dispatch ? near_jump_at(dispatch + 7) : 0;
    const uintptr_t sockets = dispatch ? short_jump_at(dispatch + 16) : 0;
    const uintptr_t personalize = dispatch ? dispatch + 27 : 0;
    const bool services_ok = dispatch && byte_at(dispatch + 3) == service_at && imbue && sockets &&
                             find_in(imbue, 0x80, "83 C9 20 09 8D") &&
                             find_in(imbue, 0x80, "C7 85 ?? ?? ?? ?? 06 00 00 00") &&
                             find_in(sockets, 0x20, "BA 00 08 00 00 41 B8 01 00 00 00 E8") &&
                             find_in(personalize, 0x40, "BA 00 00 00 01 41 B8 01 00 00 00 E8");
    // A row, from the register the walk keeps in it: the class before the quest, the flag, the value it stands
    // on, the routine.
    const auto field = [](uintptr_t at) { return static_cast<int8_t>(byte_at(at)); };
    const uintptr_t rows = walk ? rip_operand(walk + 22, 3, 7) : 0;
    const int class_at = walk ? field(walk + 33) : 0;
    const bool walk_ok = walk && rows && class_at < 0 && field(walk + 45) - class_at == 4 &&
                         field(walk + 42) - class_at == 8 && -class_at == 12 && field(walk + 72) - class_at == 16 &&
                         byte_at(walk + 76) == 24 && call_at(walk + 50) == quest_state &&
                         call_at(walk + 61) == quest_state && rip_operand(walk, 3, 7);
    if (table_ok && read_ok && gate_ok && services_ok && walk_ok) {
      g_npc.known = true;
      g_npc.records = records;
      g_npc.record_size = size;
      g_npc.record_count = count;
      g_npc.service_at = service_at;
      g_npc.quest_at = quest_at;
      g_npc.quest_max = quest_max;
      g_npc.pending_flag = flag;
      g_npc.imbue = 1;
      g_npc.sockets = 2;
      g_npc.personalize = 3;
      g_npc.menu_rows = rows + static_cast<intptr_t>(class_at);
      g_npc.menu_count = rip_operand(walk, 3, 7);
      g_npc.menu_row_size = 24;
    } else {
      log_warn("sites: %s", !table_ok      ? "the NPC table is not what its getter was taken to say"
                            : !handler     ? "the server's handler of an NPC's item service was not found"
                            : !read_ok     ? "an NPC's item service does not read the NPC's record the way expected"
                            : !gate_ok     ? "an NPC's item service does not pass a record without a quest on"
                            : !services_ok ? "an NPC's item services are not told apart the way expected"
                                           : "the client's NPC menu does not walk its quest rows the way expected");
    }
  }
  // Infinite cube ingredients. The quest reward giver hands its item over the game's way: the grid of inventory page
  // 0 (`setne bl`: a bank that is not the classic one; `xor edx, edx`: page 0), the player's inventory, a free spot
  // in the grid for the item, the item moved there (`mov r9d, 1`: one item), the client told (`mov dword [rsp+20h],
  // 2`), then whether a charm's stats count, and if so what the player's items give worked out again. With no room:
  // its flag 0x4000 off (`mov edx, 4000h`), its page 0xFF, its body location 0, its mode 3 (the ground), its place 0,
  // the player's place and room, a free spot on the ground near the player (`mov byte [rsp+20h], 1`), the item put
  // there; with no spot either, the item freed. The product routine tells the client of each item that was in the
  // cube with the same routine (`mov r9d, 20h`, `mov dword [rsp+20h], 4`) before it frees the ingredients it used up
  // (`cmp rdi, r13; jae`: only those below their count), and for Clear Sockets (`test byte [r15+5Eh], 10h`) frees
  // the socket contents of the item it changes. Its other way with them, when that flag is clear, takes each out of
  // the item and keeps it: the first item of the item's inventory, taken out, its page 0xFF, its place 0, its body
  // location 0 and its mode 4, with the setters the giver's drop calls.
  g_cube = CubeFacts{};
  {
    const uintptr_t give = address(kItemGive), products = address(kCubeProducts);
    const uintptr_t free_item = address(kItemFree), socket_free = address(kSocketedFree);
    const uintptr_t place =
        give ? find_in(give, 0x300,
                       "80 BD 06 01 00 00 01 48 8B CE 0F 95 C3 33 D2 44 8B C3 E8 ?? ?? ?? ?? 41 B8 ?? ?? 00 00 48 8D "
                       "15 ?? ?? ?? ?? 48 8B CE 8B D8 E8 ?? ?? ?? ?? 48 8D 4C 24 ?? C6 44 24 28 00 48 89 4C 24 20 4C "
                       "8D 4C 24 ?? 48 8B C8 44 8B C3 48 8B D7 E8")
             : 0;
    const uintptr_t move =
        give ? find_in(give, 0x300, "41 B9 01 00 00 00 C6 44 24 20 00 4C 8D 44 24 ?? 48 8B CE 48 8D 54 24 ?? E8 ?? "
                                    "?? ?? ?? 84 C0")
             : 0;
    const uintptr_t notice =
        give ? find_in(give, 0x300,
                       "C7 44 24 20 02 00 00 00 E8 ?? ?? ?? ?? 45 33 C9 4C 8B C7 48 8B D6 48 8B CD E8 ?? ?? ?? ?? 48 "
                       "8B D6 48 8B CF E8 ?? ?? ?? ?? 85 C0 74 ?? 45 33 C9 45 33 C0 48 8B D6 48 8B CD E8")
             : 0;
    const uintptr_t drop =
        give ? find_in(give, 0x300,
                       "45 33 C0 BA 00 40 00 00 48 8B CF E8 ?? ?? ?? ?? B2 FF 48 8B CF E8 ?? ?? ?? ?? 33 D2 48 8B CF "
                       "E8 ?? ?? ?? ?? BA 03 00 00 00 48 8B CF E8 ?? ?? ?? ?? 33 D2 48 8B CF E8 ?? ?? ?? ?? 33 D2 48 "
                       "8B CF E8 ?? ?? ?? ?? 48 8D 54 24 ?? 48 8B CE E8 ?? ?? ?? ?? 48 8B CE E8 ?? ?? ?? ?? 48 8B D0 "
                       "C6 44 24 20 01 4C 8D 8C 24 ?? ?? 00 00 48 8B CD 4C 8D 44 24 ?? E8 ?? ?? ?? ?? 48 85 C0 74 ?? "
                       "8B 8C 24 ?? ?? 00 00 4C 8B C8 89 4C 24 28 4C 8B C7 8B 8C 24 ?? ?? 00 00 48 8B D6 89 4C 24 20 "
                       "48 8B CD E8 ?? ?? ?? ?? 48 8B C7 EB ?? 48 8B D7 48 8B CD E8")
             : 0;
    const uintptr_t consume =
        products ? find_in(products, 0x1800,
                           "44 8B 84 BD ?? ?? 00 00 BA 04 00 00 00 49 8B CC E8 ?? ?? ?? ?? 8B 4B FC 41 B9 20 00 00 00 "
                           "89 4C 24 40 4C 8B C0 8B 0B 49 8B D7 89 4C 24 38 48 8B F0 8B 4B F8 89 4C 24 30 8B 4B F4 89 "
                           "4C 24 28 49 8B CC C7 44 24 20 04 00 00 00 E8 ?? ?? ?? ?? 49 3B FD 73 0B 48 8B D6 49 8B CC "
                           "E8 ?? ?? ?? ?? 48 FF C7")
                 : 0;
    const uintptr_t unsocket =
        products ? find_in(products, 0x1800,
                           "41 F6 47 5E 10 0F 84 ?? ?? ?? ?? 48 63 54 24 ?? 49 8B CC 48 8B 94 D5 ?? ?? 00 00 E8 ?? ?? "
                           "?? ?? E9")
                 : 0;
    const uintptr_t inventory = place ? call_at(place + 0x29) : 0;
    const uintptr_t room = drop ? call_at(drop + 0x55) : 0;
    const uintptr_t giver_free = drop ? call_at(drop + 0xAE) : 0;
    const uintptr_t item_notice = notice ? call_at(notice + 8) : 0;
    CubeFacts c;
    if (place && move && notice && drop) {
      c.page_grid = call_at(place + 0x12);
      c.grid_spot = call_at(place + 0x4B);
      c.inventory_move = call_at(move + 0x18);
      c.item_notice = item_notice;
      c.charm_counts = call_at(notice + 0x24);
      c.refresh_items = call_at(notice + 0x39);
      c.item_flags = call_at(drop + 0x0B);
      c.item_page = call_at(drop + 0x15);
      c.item_body = call_at(drop + 0x1F);
      c.unit_mode = call_at(drop + 0x2C);
      c.unit_x = call_at(drop + 0x36);
      c.unit_y = call_at(drop + 0x40);
      c.unit_coords = call_at(drop + 0x4D);
      c.ground_spot = call_at(drop + 0x72);
      c.ground_put = call_at(drop + 0x9E);
    }
    // The routines the giver shares with the other sites must be theirs: its inventory getter the key use's, its room
    // getter the one found by name, its free the item free, and the product routine's notice and free the giver's.
    const bool calls_ok = c.page_grid && c.grid_spot && c.inventory_move && c.item_notice && c.charm_counts &&
                          c.refresh_items && c.item_flags && c.item_page && c.item_body && c.unit_mode && c.unit_x &&
                          c.unit_y && c.unit_coords && c.ground_spot && c.ground_put && inventory &&
                          (!g_derived[dGetInventory] || inventory == g_derived[dGetInventory]) &&
                          (!address(kUnitGetRoom) || room == address(kUnitGetRoom)) && free_item &&
                          giver_free == free_item && call_at(free_item + 0x13) == room;
    const bool consume_ok =
        consume && call_at(consume + 0x4A) == item_notice && call_at(consume + 0x5A) == free_item;
    if (calls_ok && consume_ok) {
      c.ingredient_free = consume + 0x5F;
      c.known = true;
      if (!g_derived[dGetInventory]) g_derived[dGetInventory] = inventory;
      // Clear Sockets: its free must be the socket contents' free (which frees what is in the item's inventory,
      // asked with the same getter), and the other branch the routine's own way of keeping them.
      const uintptr_t keep = unsocket ? near_jump_at(unsocket + 5) : 0;
      const uintptr_t kept =
          keep ? find_in(keep, 0x100,
                         "48 8B CF E8 ?? ?? ?? ?? 48 8B D8 48 85 C0 0F 84 ?? ?? ?? ?? 4C 8B 65 ?? 8B 75 ?? 0F 1F 00 "
                         "48 8B D3 48 8B CF E8 ?? ?? ?? ?? 48 85 C0 75 13 48 8D 4C 24 ?? 88 44 24 ?? E8 ?? ?? ?? ?? "
                         "84 C0 74 01 CC B2 FF 48 8B CB E8 ?? ?? ?? ?? 33 D2 48 8B CB E8 ?? ?? ?? ?? 33 D2 48 8B CB "
                         "E8 ?? ?? ?? ?? 33 D2 48 8B CB E8 ?? ?? ?? ?? BA 04 00 00 00 48 8B CB E8")
               : 0;
      if (unsocket && socket_free && call_at(unsocket + 0x1B) == socket_free &&
          call_at(socket_free + 0x38) == inventory &&
          kept == keep && call_at(kept + 0x46) == c.item_page && call_at(kept + 0x50) == c.unit_x &&
          call_at(kept + 0x5A) == c.unit_y && call_at(kept + 0x64) == c.item_body &&
          call_at(kept + 0x71) == c.unit_mode && call_at(kept + 3) && call_at(kept + 0x24)) {
        c.sockets_free = unsocket + 0x20;
        c.first_item = call_at(kept + 3);
        c.take_out = call_at(kept + 0x24);
      } else {
        log_warn("sites: Clear Sockets' free of the socket contents is not the way expected - what is in the sockets "
                 "is destroyed as the game has it");
      }
      g_cube = c;
    } else if (give || products) {
      log_warn("sites: %s", !calls_ok ? "the quest reward giver does not hand an item over the way expected"
                                      : "the cube's product routine does not free its ingredients the way expected");
    }
  }
  // The Chronicle. A player's is its data plus the accessor's `add rax, imm32`. The find takes the kind's map: kind
  // 0 (set items) the Chronicle itself (its `je` skips both adds), kind 1 (uniques) `add r9, imm8`; a map's buckets
  // `mov rdx, [r9+disp8]`, their count `mov rax, [r9]`, an entry's id `cmp r8d, [rax+disp8]`, the next entry `mov
  // rax, [rax]`. The share's branch for uniques loads the game's list of them (`mov r8, [rip+disp32]`, its count
  // `mov rax, [rip+disp32]`, 8 bytes an entry: `lea rbx, [r8+rax*8]`), reads each row's id (`movsx edx, word
  // [rax]`) and looks it up in the uniques' map (`mov r11, [rcx+disp8]` its buckets, `mov rax, [rdi+disp8]` their
  // count), over the count it keeps (`mov r10, [rip+disp32]`); its branch for set items hands the list of them
  // (`mov rcx, [rip+disp32]`, its count `mov rdx, [rip+disp32]`, `shl rdx, 4`: 16 bytes an entry, a row and a tag)
  // and the Chronicle itself (`mov r8, rdi`: the set items' map) to its counter, over the count it keeps (`cmp
  // qword [rip+disp32], 0`).
  g_chronicle = ChronicleFacts{};
  {
    const uintptr_t of = address(kChronicleOfPlayer), find = address(kChronicleFind), share = address(kChronicleShare);
    int32_t of_player = 0;
    const uintptr_t sets_branch = share ? near_jump_at(share + 0x15) : 0;
    const uintptr_t uniques_branch = share ? near_jump_at(share + 0x1F) : 0;
    const uintptr_t u = uniques_branch
                            ? find_in(uniques_branch, 0x40,
                                      "4C 8B 15 ?? ?? ?? ?? 4D 85 D2 0F 84 ?? ?? ?? ?? 4C 8B 05 ?? ?? ?? ?? 45 33 C9 48 8B "
                                      "05 ?? ?? ?? ?? 49 8D 1C C0 4C 3B C3 0F 84 ?? ?? ?? ?? 4C 8B 59 ?? 48 BE CD 8C 55 ED "
                                      "D7 AF 51 FF")
                            : 0;
    const uintptr_t row_id = u == uniques_branch && u ? find_in(u + 0x40, 0x20, "49 8B 00 0F BF 10") : 0;
    const uintptr_t mask = u == uniques_branch && u ? find_in(u + 0x40, 0x60, "48 8B 47 ?? 48 FF C8 48 23 C1") : 0;
    const uintptr_t s = sets_branch ? find_in(sets_branch, 0x40,
                                              "48 83 3D ?? ?? ?? ?? 00 0F 86 ?? ?? ?? ?? 48 8B 15 ?? ?? ?? ?? 4C 8B C7 "
                                              "48 8B 0D ?? ?? ?? ?? 48 C1 E2 ?? 48 03 D1 E8")
                                    : 0;
    ChronicleFacts c;
    c.uniques_at = find ? byte_at(find + 0x1D) : -1;
    c.buckets_at = find ? byte_at(find + 0x26) : -1;
    c.key_at = find ? byte_at(find + 0x78) : -1;
    c.count_at = 0;  // `mov rax, [r9]`
    c.next_at = 0;   // `mov rax, [rax]`
    c.sets_at = 0;   // kind 0's map is the Chronicle itself
    const int entry_shift = s == sets_branch && s ? byte_at(s + 0x22) : -1;
    const bool find_ok = find && short_jump_at(find + 8) == find + 0x1E && c.uniques_at > 0 && c.uniques_at < 0x80 &&
                         c.buckets_at > 0 && c.buckets_at < 0x20 && c.key_at > 0 && c.key_at < 0x20;
    const bool of_ok = of && mem::read_safe(of + 0x60, &of_player) && of_player > 0x10 && of_player < 0x10000;
    const bool uniques_ok = u && u == uniques_branch && row_id && mask &&
                            byte_at(u + 0x31) == c.uniques_at + c.buckets_at &&
                            byte_at(mask + 3) == c.uniques_at + c.count_at;
    const bool sets_ok = s && s == sets_branch && entry_shift == 4;
    if (find_ok && of_ok && uniques_ok && sets_ok) {
      c.of_player = of_player;
      c.unique_total = rip_operand(u, 3, 7);
      c.unique_list = rip_operand(u + 0x10, 3, 7);
      c.unique_count = rip_operand(u + 0x1A, 3, 7);
      c.set_total = rip_operand(s, 3, 8);
      c.set_count = rip_operand(s + 0xE, 3, 7);
      c.set_list = rip_operand(s + 0x18, 3, 7);
      c.set_entry = 1 << entry_shift;
      c.known = c.unique_total && c.unique_list && c.unique_count && c.set_total && c.set_count && c.set_list;
    }
    if (c.known) g_chronicle = c;
    else if (of || find || share)
      log_warn("sites: the Chronicle is not kept the way expected (%s) - its entries cannot be read",
               !of_ok       ? "a player's Chronicle"
               : !find_ok   ? "its maps"
               : !uniques_ok ? "the uniques' list"
               : !sets_ok   ? "the set items' list"
                            : "the lists' addresses");
  }
  // Identify on pickup. The pick-up routine's last call tells the clients where the item went: the command by the
  // place's mode (0 stored 0x80, 1 the body 0x200, 2 the belt 0x2000, else the cursor 0x40, into ebx), flags 0
  // (`xor r9d, r9d`), then the notice; after it `mov edx, 1` and `mov al, 1`, a pick-up made. The identify routine
  // tells them with the same routine, flags 1 and command 0 (`mov r9d, 1`, `mov dword [rsp+20h], 0`). The flag it
  // tests is its `mov edx, imm32` (+0x24), the one it sets its `lea edx, [rax+imm8]` (+0x45). The quest reward giver's
  // notice, read for the cube, must be the same routine too.
  g_identify = IdentifyFacts{};
  {
    const uintptr_t notice = address(kItemNotice), pickup = address(kItemPickup), identify = address(kItemIdentify);
    const uintptr_t told =
        pickup ? find_in(pickup, 0x600,
                         "8B 4D ?? 85 C9 74 ?? 83 E9 01 74 ?? 83 F9 01 74 ?? BB 40 00 00 00 EB ?? BB 00 20 00 00 EB "
                         "?? BB 00 02 00 00 EB ?? BB 80 00 00 00 49 8B CE E8 ?? ?? ?? ?? 48 8B C8 45 33 C9 8B 45 ?? "
                         "4C 8B C6 89 44 24 40 49 8B D6 8B 45 ?? 89 44 24 38 8B 45 ?? 89 44 24 30 8B 45 ?? 89 44 24 "
                         "28 89 5C 24 20 E8 ?? ?? ?? ?? BA 01 00 00 00 49 8B CE E8 ?? ?? ?? ?? B0 01")
               : 0;
    const uintptr_t updated =
        identify ? find_in(identify, 0x180,
                           "89 5C 24 40 41 B9 01 00 00 00 89 74 24 38 4C 8B C5 89 7C 24 30 49 8B D6 89 44 24 28 49 "
                           "8B CF C7 44 24 20 00 00 00 00 E8")
                 : 0;
    uint32_t tested = 0;
    const bool flag_ok = identify && mem::read_safe(identify + 0x24, &tested) && tested &&
                         (tested & (tested - 1)) == 0 && tested < 0x80 &&
                         byte_at(identify + 0x45) == static_cast<int>(tested);
    const bool told_ok = told && notice && call_at(told + 0x5F) == notice;
    const bool updated_ok = updated && call_at(updated + 0x27) == notice;
    const bool giver_ok = !g_cube.known || g_cube.item_notice == notice;
    if (flag_ok && told_ok && updated_ok && giver_ok) {
      g_identify.pickup_notice = told + 0x64;
      g_identify.identified = tested;
      g_identify.known = true;
    } else if (notice || pickup || identify) {
      log_warn("sites: %s - items are not identified as they are picked up",
               !flag_ok      ? "the identify routine does not test and set an item flag the way expected"
               : !told_ok    ? "the pick-up routine does not tell the clients of the item's new place the way expected"
               : !updated_ok ? "the identify routine does not tell the clients with the item notice"
                             : "the quest reward giver tells the clients with another routine than the item notice");
    }
  }
  // Passive mercenary and minions. The enemy test's two entry stubs must both jump to it: they are its only callers
  // (the one with the flag set is the one 39 calls go to). The AI's own enemy check, the filter of the unit searches
  // that find a target, and the routine right after it, the other searches' filter, must each end in a jump to that
  // stub (`jmp rel32`, the only one in each): so every target an AI looks for is put to the hooked test.
  g_enemy_checked = false;
  {
    const uintptr_t test = address(kEnemyTest), stubs = address(kEnemyTestEntries), check = address(kAiEnemyCheck);
    const bool stubs_ok = test && stubs && jmp_at(stubs + 3) == test && jmp_at(stubs + 0x13) == test;
    int tails = 0;
    std::vector<uint8_t> code;
    constexpr size_t kChecks = 0x1A0;  // the two filters together (0x130 and 0x70 bytes in 3.3)
    if (stubs_ok && check && mem::snapshot(check, kChecks, code) == kChecks) {
      for (size_t i = 0; i + 5 <= code.size(); ++i)
        if (code[i] == 0xE9 && jmp_at(check + i) == stubs) ++tails;
    }
    g_enemy_checked = stubs_ok && tails == 2;
    if (!g_enemy_checked && (test || stubs || check))
      log_warn("sites: %s - your mercenary and minions cannot be made passive",
               !stubs_ok ? "the enemy test's entry stubs do not jump to it"
                         : "the AI's enemy checks do not end in a jump to the enemy test");
  }
  // Permanent revives. The killself timer, for a monster whose owner is a player, calls the pet removal with its kill
  // flag (`mov r9d, 1`), the owner (`mov rdx, rdi`) and the game (`mov rcx, rsi`). The pet removal's first question,
  // past the owner's data and its pet lists (+0x98), is the pet lookup with the unit's id (`mov edx, esi`) and the
  // owner (`mov rcx, rdi`): it must be the routine found above, so its answer is what the removal goes by.
  g_revive = ReviveFacts{};
  {
    const uintptr_t timer = address(kKillSelfTimer), lookup = address(kPetTypeOf);
    const uintptr_t kill = timer ? find_in(timer, 0x100, "41 B9 01 00 00 00 48 8B D7 48 8B CE E8") : 0;
    const uintptr_t removal = kill ? call_at(kill + 12) : 0;
    const uintptr_t asks =
        removal ? find_in(removal, 0x80, "48 8B 9B 98 00 00 00 48 85 DB 0F 84 ?? ?? ?? ?? 8B D6 48 8B CF E8") : 0;
    if (lookup && asks && call_at(asks + 21) == lookup) {
      g_revive.pet_removal = removal;
      g_revive.known = true;
    } else if (timer || lookup) {
      log_warn("sites: %s - revives time out as the game has them",
               !removal ? "the killself timer does not remove a player's pet the way expected"
                        : "the pet removal does not ask the pet lookup first");
    }
  }
  // Named enemies and landmarks on the map. The automap callback's last call (`mov r8d, 1`: the whole room) is the
  // reveal of a built room. It walks the room's floor tiles (the getter: the room's +0x08, its tiles at +0x20 and their
  // count at +0x28; 0x48 bytes a tile, its flags at +0x18, 8 one never put on) and puts each on the layer's floor list
  // with the per-tile add, which skips a tile already on the map (flag 0x40000), asks the tile lookup with the level's
  // type and the tile's type, style and sequence (three getters of the tile's graphics record) and puts the cell on
  // with the list insert. A tail jump then walks the room's units: an object's icon is its Objects row's cell, the
  // stash's only in two acts, the sewer stairs' only open, the Arcane Sanctuary's waypoint only there; the unit cell
  // add puts it on the layer's units list at the unit's pixels / 10 plus (1, -3), with the same list insert.
  g_map = MapFacts{};
  {
    const uintptr_t callback = address(kAutomapRoomCallback);
    const uintptr_t whole = callback ? find_in(callback, 0xB0, "41 B8 01 00 00 00 48 8B D6 40 0F B6 CF E8") : 0;
    const uintptr_t reveal = whole ? call_at(whole + 13) : 0;
    const uintptr_t floor_get = reveal ? find_in(reveal, 0x60, "48 8D 54 24 68 49 8B CD 89 7C 24 68 4C 8B F8 E8") : 0;
    const uintptr_t floor_tiles = floor_get ? call_at(floor_get + 15) : 0;
    const bool tile_list = floor_tiles && find_in(floor_tiles, 0x30, "48 8B 71 08 48 85 F6 74 ?? 48 8B 76 28") &&
                           find_in(floor_tiles, 0x60, "89 37 48 8B 43 08 48 8B 40 20") &&
                           find_in(reveal, 0xA0, "48 8D 14 C9 41 8B 4C D4 18 49 8D 14 D4 F6 C1 08");
    const uintptr_t floor_add = reveal ? find_in(reveal, 0xA0, "4C 8D 4D ?? 4D 8B C7 41 0F B6 CE E8") : 0;
    const uintptr_t tile_add = floor_add ? call_at(floor_add + 11) : 0;
    const uintptr_t tail = reveal ? find_in(reveal, 0x140,
                                            "48 8D 55 ?? 49 8B CD 48 8B 5C 24 50 48 8B 6C 24 58 48 8B 74 24 60 48 83 "
                                            "C4 20 41 5F 41 5E 41 5D 41 5C 5F E9")
                                  : 0;
    const uintptr_t unit_pass = tail ? jmp_at(tail + 35) : 0;
    // The per-tile add: the on-the-map flag, the four questions it asks the lookup with, and its list insert.
    const bool add_skips = tile_add && find_in(tile_add, 0x40, "8B 42 18 4D 8B F9 4D 8B F0 48 8B EA 0F BA E0 12 0F 82");
    const uintptr_t asks = tile_add ? find_in(tile_add, 0x90,
                                              "8B 90 F8 01 00 00 E8 ?? ?? ?? ?? 48 8B 4D 20 8B F0 E8 ?? ?? ?? ?? 48 8B 4D "
                                              "20 8B F8 E8 ?? ?? ?? ?? 48 8B 4D 20 8B D8 E8 ?? ?? ?? ?? 44 8B C8 44 8B C3 "
                                              "8B D7 8B CE E8")
                                    : 0;
    const uintptr_t level_type = asks ? call_at(asks + 6) : 0;
    const uintptr_t lookup = asks ? call_at(asks + 54) : 0;
    const auto field_of = [](uintptr_t getter) {
      const uintptr_t at = getter ? find_in(getter, 0x40, "8B 41 ?? 48 83 C4 20 5B C3") : 0;
      return at ? byte_at(at + 2) : -1;
    };
    const int tile_type = asks ? field_of(call_at(asks + 17)) : -1;
    const int tile_style = asks ? field_of(call_at(asks + 28)) : -1;
    const int tile_sequence = asks ? field_of(call_at(asks + 39)) : -1;
    const bool type_of_level = level_type && find_in(level_type, 0x14, "48 83 EC 28 E8 ?? ?? ?? ?? 8B 40 ?? 48 83 C4 28 C3") &&
                               call_at(level_type + 4) == derived(dLevelDefRecord);
    const uintptr_t insert_at =
        tile_add ? find_in(tile_add, 0x140, "4C 8D 44 24 38 49 8B CF 48 89 44 24 3C 48 8D 54 24 28 E8") : 0;
    const uintptr_t list_insert = insert_at ? call_at(insert_at + 18) : 0;
    // The lookup: by level type the range of records it walks, and in it the first whose level type and tile type are
    // the tile's, whose style is the tile's or 0xFF, and whose sequences are 0xFF or hold the tile's; one of its cells.
    const uintptr_t index_at = lookup ? find_in(lookup, 0x60, "48 8D 0D ?? ?? ?? ?? 42 8B 3C F9 42 8B 74 F9 04") : 0;
    const uintptr_t records_at = lookup ? find_in(lookup, 0xC0, "48 8B 05 ?? ?? ?? ?? 48 C1 E1 05 44 39 3C 01") : 0;
    const uintptr_t count_at = lookup ? find_in(lookup, 0xC0, "48 3B 1D ?? ?? ?? ??") : 0;
    const uintptr_t records = records_at ? rip_operand(records_at, 3, 7) : 0;
    bool record_layout = records && count_at && rip_operand(count_at, 3, 7) == records + 8;
    for (const char* p : {"44 39 64 01 04", "80 7C 01 08 FF", "0F B6 4C 01 08 41 3B CD", "80 7C 01 09 FF",
                          "0F B6 4C 01 09 41 3B CE 7F", "0F B6 4C 01 0A 41 3B CE 7D", "44 8B 4C 01 1C 45 85 C9 7F",
                          "8B 44 97 0C"})
      record_layout = record_layout && find_in(lookup, 0x360, p);
    if (tile_list && add_skips && type_of_level && list_insert && index_at && record_layout && tile_type >= 0 &&
        tile_style >= 0 && tile_sequence >= 0 && byte_at(floor_add + 3) > 0) {
      g_map.tile_add = tile_add;
      g_map.list_insert = list_insert;
      g_map.floor_list = byte_at(floor_add + 3);
      g_map.lookup_records = records;
      g_map.lookup_index = rip_operand(index_at, 3, 7);
      g_map.tile_type_at = tile_type;
      g_map.tile_style_at = tile_style;
      g_map.tile_sequence_at = tile_sequence;
      g_map.level_type_at = byte_at(level_type + 11);
      g_map.tiles = true;
    } else if (callback) {
      log_warn("sites: the automap's reveal of a room does not put its floor tiles on the way expected (%s) - no "
               "waypoint is put on the map ahead",
               !reveal ? "no reveal" : !tile_list ? "the tile list" : !add_skips ? "the per-tile add"
               : !type_of_level ? "the level type" : !list_insert ? "the list insert"
               : !record_layout || !index_at ? "the tile lookup" : "the tile getters");
    }
    // The units: the object branch (the Objects row's cell, then the three rules), and the unit cell add's key.
    const uintptr_t branch = unit_pass ? find_in(unit_pass, 0x100,
                                                 "E8 ?? ?? ?? ?? 48 8B F0 8B 90 ?? ?? ?? ?? 85 D2 0F 84 ?? ?? ?? ?? 81 "
                                                 "EF ?? ?? ?? ?? 74 ?? 83 EF ?? 74 ?? 83 FF ?? 0F 85 ?? ?? ?? ?? 49 8B "
                                                 "CF E8 ?? ?? ?? ?? 83 F8 ?? 0F 85")
                                       : 0;
    int32_t cell_at = 0, stash = 0;
    const uintptr_t stash_rule = branch ? short_jump_at(branch + 28) : 0;
    const uintptr_t stairs_rule = branch ? short_jump_at(branch + 33) : 0;
    const bool rules = branch && mem::read_safe(branch + 10, &cell_at) && mem::read_safe(branch + 24, &stash) &&
                       stash_rule && stairs_rule &&
                       find_in(stash_rule, 0x20, "49 8B CF E8 ?? ?? ?? ?? 8B D0 41 0F B6 CE E8 ?? ?? ?? ?? 2C ?? 3C ?? 0F 87") ==
                           stash_rule &&
                       find_in(stairs_rule, 0x10, "48 8B CB E8 ?? ?? ?? ?? 83 F8 ?? 0F 85") == stairs_rule;
    const uintptr_t unit_add_at = unit_pass ? find_in(unit_pass, 0x280, "4C 8B C5 48 8B CB E8") : 0;
    const uintptr_t unit_add = unit_add_at ? call_at(unit_add_at + 6) : 0;
    const uintptr_t unit_insert = unit_add ? find_in(unit_add, 0x90, "48 8B CD 89 44 24 34 E8") : 0;
    const bool unit_key = unit_add && find_in(unit_add, 0x90, "41 FF C1") && find_in(unit_add, 0x90, "83 C2 FD") &&
                          unit_insert && list_insert && call_at(unit_insert + 7) == list_insert;
    if (rules && unit_key && byte_at(tail + 3) > 0 && cell_at > 0 && cell_at < 0x1000) {
      g_map.object_list = byte_at(tail + 3);
      g_map.object_cell_at = cell_at;
      g_map.stash_class = stash;
      g_map.stash_act = byte_at(stash_rule + 20);
      g_map.stash_acts = byte_at(stash_rule + 22) + 1;
      g_map.stairs_class = stash + byte_at(branch + 32);
      g_map.stairs_mode = byte_at(stairs_rule + 10);
      g_map.arcane_class = g_map.stairs_class + byte_at(branch + 37);
      g_map.arcane_level = byte_at(branch + 54);
      g_map.list_insert = list_insert;
      g_map.objects = true;
    } else if (callback) {
      log_warn("sites: the automap's reveal of a room does not put its objects' icons on the way expected (%s) - no "
               "object's icon is put on the map ahead",
               !unit_pass ? "no unit pass" : !rules ? "the object rules" : "the unit cell add");
    }
    // CreateActiveRoom's preset step (a preset room, type 2, not made yet): the room's +0x40 points to its preset part
    // (+0x08), whose units it makes from the room's DS1 file (+0x10) once: each a copy of the file's, linked at the
    // part's +0x58 by +0x10, with its type (+0x20: 1 a monster, 2 an object), class (+0x04) and place (+0x08 x, +0x24
    // y, subtiles of the level's map: the file's plus the room's).
    const uintptr_t create = address(kCreateActiveRoom);
    const uintptr_t step_at = create ? find_in(create, 0x40, "83 7B 74 02 75 ?? 48 8B D3 40 0F B6 CF E8") : 0;
    const uintptr_t step = step_at ? call_at(step_at + 13) : 0;
    const uintptr_t make_at = step ? find_in(step, 0x80, "4D 8D 46 30 48 8B D7 40 0F B6 CE E8") : 0;
    const uintptr_t make = make_at ? call_at(make_at + 11) : 0;
    const uintptr_t link_at =
        make ? find_in(make, 0x340, "E8 ?? ?? ?? ?? 49 8B 4F 58 48 89 48 10 49 89 47 58 48 8B 76 10") : 0;
    const uintptr_t copy = link_at ? call_at(link_at) : 0;
    if (step && find_in(step, 0x40, "48 8B 42 40 4C 8B F2 0F B6 F1 48 8B 78 08 48 83 7F 10 00") && copy &&
        find_in(copy, 0x80, "8B 47 20 89 43 20 8B 47 04 89 43 04") &&
        find_in(copy, 0x90, "8B 4F 08 41 03 CC 89 4B 08 8B 4F 24 41 03 CF 89 4B 24")) {
      g_map.presets = true;
    } else if (create) {
      log_warn("sites: CreateActiveRoom does not keep a room's preset units the way expected - no landmark or named "
               "enemy is put on the map ahead");
    }
    // Then its build step (once, flag bit 20): the room's near rooms and warp links (when it has no near rooms yet),
    // then its static grids, which for a preset room move the part's units that stand in the room into the room's own
    // list (+0x98, by +0x10), their places made the room's (less its subtiles: tile x and y at +0x60 / +0x64, times
    // five). Every other preset unit a room gets (a maze's, a warp tile's) the room preset add puts there too, the same
    // way. So a built room's preset units are in its own list.
    const uintptr_t build_at = create ? find_in(create, 0x60, "0F BA E0 14 72 ?? 48 8B D3 40 0F B6 CF E8") : 0;
    const uintptr_t build = build_at ? call_at(build_at + 13) : 0;
    const uintptr_t steps_at = build && find_in(build, 0x20, "48 83 7A 18 00")
                                   ? find_in(build, 0x40, "75 05 E8 ?? ?? ?? ?? 48 8B D3 40 0F B6 CE E8")
                                   : 0;
    const uintptr_t near_links = steps_at ? call_at(steps_at + 2) : 0;
    const uintptr_t grids = steps_at ? call_at(steps_at + 14) : 0;
    const uintptr_t to_room_at = grids ? find_in(grids, 0x40,
                                                 "8B 53 74 83 EA 01 74 ?? 83 FA 01 75 ?? 48 8B CB 48 8B 5C 24 30 48 83 "
                                                 "C4 20 5F E9")
                                       : 0;
    const uintptr_t to_room = to_room_at ? jmp_at(to_room_at + 26) : 0;
    const uintptr_t moved = to_room ? find_in(to_room, 0x2C0,
                                              "48 8B 86 ?? ?? ?? ?? 48 89 43 10 48 89 9E ?? ?? ?? ??")
                                    : 0;
    int32_t own_list = 0, own_list2 = 0;
    const bool room_moves = moved && mem::read_safe(moved + 3, &own_list) && mem::read_safe(moved + 14, &own_list2) &&
                            own_list == own_list2 && own_list > 0 && own_list < 0x400 &&
                            find_in(to_room, 0x2C0, "8B 46 60 48 8B 5D 58 44 8D 34 80 8B 46 64 44 8D 3C 80") &&
                            find_in(to_room, 0x2C0, "44 29 73 08 44 29 7B 24");
    // The warp tile's preset unit: of type 5, the warp's id (its record +0x2C) for its class, placed at the tile less
    // the room's tile x and y, times five, plus the record's offset; put on by the room preset add, whose list is the
    // same.
    const uintptr_t warp_tile = address(kWarpTilePreset);
    const uintptr_t warp_add_at =
        warp_tile ? find_in(warp_tile, 0xD0, "44 8B 4D ?? C7 44 24 20 00 00 00 00 E8") : 0;
    const uintptr_t room_add = warp_add_at ? call_at(warp_add_at + 12) : 0;
    const uintptr_t warp_type_at = warp_tile ? find_in(warp_tile, 0xD0, "41 B8 ?? 00 00 00 03 4D 48") : 0;
    const bool warp_room = warp_tile && find_in(warp_tile, 0x80, "2B 73 60 2B 7B 64");
    int32_t add_list = 0;
    const uintptr_t add_link = room_add ? find_in(room_add, 0x80, "48 8B 86 ?? ?? ?? ?? 49 89 40 10 49 8B C0 4C 89 86") : 0;
    const bool add_places = room_add && find_in(room_add, 0x60, "89 58 20 89 78 04 89 08 8B 44 24 58 41 89 40 08 8B 44 24 "
                                                                 "60 41 89 40 24");
    const bool room_list = room_moves && add_link && add_places && mem::read_safe(add_link + 3, &add_list) &&
                           add_list == own_list;
    if (room_list) {
      g_map.room_presets = true;
      g_map.room_presets_at = own_list;
    } else if (create) {
      log_warn("sites: a built room does not keep its preset units in a list of its own the way expected (%s) - no "
               "landmark or named enemy is put on the map ahead",
               !build ? "no build step" : !grids ? "no static grids" : !room_moves ? "the move" : "the room preset add");
    }
    // Area names at exits. The near links: the room's near rooms of its own area first (the array at +0x10, its count
    // at +0x18 cleared; the room's area at +0x90), then for each area its flags link it to, that area's rooms near it
    // (pushed onto the same array) and, for a warp, a link node at +0x78 {+0 the room at the other end, +8 the next,
    // +0x10 1, +0x20 the warp's record, whose id (+0x2C) the record getter compares}.
    const uintptr_t same_area = near_links ? find_in(near_links, 0x30, "48 8B CA E8") : 0;
    const uintptr_t own_near = same_area ? call_at(same_area + 3) : 0;
    const bool near_reset = own_near && find_in(own_near, 0x20, "48 8B 81 90 00 00 00") &&
                            find_in(own_near, 0x20, "48 C7 41 18 00 00 00 00");
    const uintptr_t area_link_at = near_links ? find_in(near_links, 0x220,
                                                        "4D 8B 4E 10 44 0F B6 C5 44 89 6C 24 28 48 8B D6 40 0F B6 CF "
                                                        "88 5C 24 20 E8")
                                              : 0;
    const uintptr_t link = area_link_at ? call_at(area_link_at + 24) : 0;
    const bool link_near = link && find_in(link, 0x200, "48 8D 4E 10 E8");
    const uintptr_t node_at = link ? find_in(link, 0x200,
                                             "48 89 38 48 8B 96 90 00 00 00 E8 ?? ?? ?? ?? 48 89 43 20 B8 01 00 00 00 "
                                             "C7 43 10 01 00 00 00 48 8B 4E 78 48 89 4B 08")
                                   : 0;
    const uintptr_t warp_record = node_at ? call_at(node_at + 10) : 0;
    const uintptr_t id_at = warp_record ? find_in(warp_record, 0x90, "41 39 7A 2C") : 0;
    const uintptr_t tile_record_at = warp_tile ? find_in(warp_tile, 0x60, "41 80 E0 3F E8") : 0;
    const bool same_record = warp_record && tile_record_at && call_at(tile_record_at + 4) == warp_record &&
                             find_in(warp_tile, 0xD0, "44 8B 4D 2C");
    // The collision map: an ActiveRoom's +0x38 (the getter), {+0 x, +4 y, +8 width, +0x0C height, +0x20 the flags,
    // a u16 a subtile, row by row}.
    const uintptr_t flags_at = address(kCollisionFlags);
    const uintptr_t coll_get = flags_at ? call_at(flags_at + 45) : 0;
    const bool collision = coll_get && find_in(coll_get, 0x08, "48 8B 41 38 C3") == coll_get &&
                           find_in(flags_at, 0x90, "48 8B 53 20 48 85 D2 74 ?? 2B 73 04 0F AF 73 08 2B 2B 48 63 C5 48 "
                                                   "63 CE 48 03 C8 44 0F B7 34 4A");
    if (room_list && near_reset && link_near && id_at && same_record && warp_room && warp_type_at && collision) {
      g_map.near_at = 0x10;
      g_map.links_at = 0x78;
      g_map.warp_id_at = byte_at(id_at + 3);
      g_map.warp_preset_type = byte_at(warp_type_at + 2);
      g_map.collision_at = 0x38;
      g_map.exits = g_map.warp_preset_type > 0 && g_map.warp_id_at > 0;
    }
    if ((create || warp_tile || flags_at) && !g_map.exits)
      log_warn("sites: a built room is not linked to the areas next to it the way expected (%s) - no area name is "
               "written at an exit",
               !room_list ? "its preset units" : !near_reset || !link_near ? "the near rooms"
               : !id_at || !same_record ? "the warp links" : !warp_room || !warp_type_at ? "the warp tile's preset"
                                                                                        : "the collision map");
    // The automap's draw of one unit: the unit's pixels through the transform (`mov rcx, rdi`: the view), the view's
    // rectangle (+0x18 x, +0x1C y, +0x20 width, +0x24 height), the marker with the view's scale (+0x38), then the name
    // by its kind; the stash's (a string of the game's) and an NPC's (the client's unit name, in its color) go to the
    // same name draw.
    const uintptr_t draw = address(kAutomapUnitDraw);
    const uintptr_t to_map_at = draw ? find_in(draw, 0x90, "48 8D 54 24 20 4C 8B 44 24 20 48 8B CF E8") : 0;
    const uintptr_t marker_at = draw ? find_in(draw, 0xD0, "F3 0F 10 57 38 48 8B CB 8B 54 24 60 E8") : 0;
    const uintptr_t stash_at =
        draw ? find_in(draw, 0x200, "B9 ?? ?? ?? ?? E8 ?? ?? ?? ?? 45 33 C9 0F 28 D6 48 8B D3 48 8B C8 E8") : 0;
    const uintptr_t name_draw = stash_at ? call_at(stash_at + 22) : 0;
    const bool rect = draw && find_in(draw, 0xB0,
                                      "8B 4F 18 3B D9 0F 8C ?? ?? ?? ?? 8B 57 1C 44 8B 44 24 24 44 3B C2 0F 8C ?? ?? "
                                      "?? ?? 03 4F 20 3B D9 0F 8D ?? ?? ?? ?? 8B 4F 24 03 CA 44 3B C1 0F 8D");
    uintptr_t npc_at = 0;
    for (uintptr_t from = draw, end = draw + 0x200; from && from < end && !npc_at;) {
      const uintptr_t at = find_in(from, end - from,
                                   "F3 0F 10 77 38 48 8B CD E8 ?? ?? ?? ?? 41 B9 ?? ?? ?? ?? 0F 28 D6 48 8B D3 48 8B C8 E8");
      if (!at) break;
      if (name_draw && call_at(at + 28) == name_draw) npc_at = at;
      from = at + 1;
    }
    int32_t color = -1;
    if (to_map_at && marker_at && rect && name_draw && npc_at && mem::read_safe(npc_at + 15, &color) && color >= 0 &&
        color < 32) {
      g_map.to_map = call_at(to_map_at + 13);
      g_map.draw_marker = call_at(marker_at + 12);
      g_map.draw_name = name_draw;
      g_map.unit_name = call_at(npc_at + 8);
      g_map.name_color = color;
      g_map.draw = g_map.to_map && g_map.draw_marker && g_map.unit_name;
    }
    if (draw && !g_map.draw)
      log_warn("sites: the automap's draw of one unit is not made the way expected (%s) - no named enemy is drawn on "
               "the map",
               !to_map_at ? "the transform" : !marker_at ? "the marker" : !rect ? "the view's rectangle"
               : !name_draw ? "the name draw" : "an NPC's name");
  }
  static const char* const kNames[dCount] = {"client unit table",       "server unit table",
                                             "server GetUnitByIdAndType", "client GetUnitByIdAndType",
                                             "players take no damage",  "monsters take no damage",
                                             "open side panels",        "loot filter manager",
                                             "stat list by state",      "stat list unlink",
                                             "stat list free",          "state test",
                                             "state toggle",            "inventory getter",
                                             "item type test",          "active automap layer",
                                             "level record",            "Item Name Display setting",
                                             "Unfiltered Name Display setting", "setting value",
                                             "Show Items on/off bytes", "UI vars",
                                             "key-action table",        "terror zones on (game)",
                                             "terror zone apply",       "terror client update",
                                             "terror removal (kind)",   "affix picker",
                                             "prefix slot setter",      "suffix slot setter",
                                             "rare step: suffix pick",  "rare step: prefix pick",
                                             "affix apply",             "curse apply",
                                             "portal pair maker",       "object destination setter"};
  for (int d = 0; d < dCount; ++d)
    logf("sites: %-28s %s0x%llX", kNames[d], g_derived[d] ? "" : "not derived ",
         static_cast<unsigned long long>(g_derived[d] ? g_derived[d] - g_base : 0));
  logf("sites: %-28s %s+0x%X", "unit list link", g_unit_next ? "" : "not derived ", g_unit_next);
  logf("sites: %-28s %s+0x%X", "unique dropped bits", g_unique_bits ? "" : "not derived ", g_unique_bits);
  logf("sites: %-28s %s0x%X", "key item type", g_key_type ? "" : "not derived ", g_key_type);
  logf("sites: %-28s %s%d", "automap panel id", g_automap_panel ? "" : "not derived ", g_automap_panel);
  logf("sites: %-28s %s%d / %d", "affix slots (prefix/suffix)", g_affix.prefix_slots ? "" : "not derived ",
       g_affix.prefix_slots, g_affix.suffix_slots);
  logf("sites: %-28s %srare %d, jewel %d (item type %d), crafted %d", "most affixes",
       g_affix.rare_max && g_affix.crafted_max ? "" : "not derived ", g_affix.rare_max, g_affix.jewel_max,
       g_affix.jewel_type, g_affix.crafted_max);
  logf("sites: %-28s %s%d", "class skill top bonus", g_affix.skill_bonus ? "" : "not derived ", g_affix.skill_bonus);
  logf("sites: %-28s %s", "automatic affix picker",
       g_affix.auto_same ? "calls the affix picker's two pickers, with its group" : "not checked");
  logf("sites: %-28s %sthe first %d for a base that is thrown (ItemTypes +0x%X) or has no durability (Items +0x%X), "
       "else all of them",
       "superior kinds picked among", g_superior.known ? "" : "not derived ", g_superior.few, g_superior.thrown_at,
       g_superior.no_durability_at);
  logf("sites: %-28s %sstate at context +0x%X; the Cursed modifier casts skill %d, a skill's state at row +0x%X",
       "curse steps", g_curse.state_offset && g_curse.amplify_skill ? "" : "not derived ", g_curse.state_offset,
       g_curse.amplify_skill, g_curse.skill_state);
  logf("sites: %-28s %sobject class %d, town spot %d, state after a use %d, class at context +0x%X, acts at game "
       "+0x%X, the other end's level at a client's portal +0x%X",
       "town portal", g_portal.portal_class ? "" : "not derived ", g_portal.portal_class, g_portal.town_spot,
       g_portal.used_state, g_portal.class_offset, g_portal.acts_offset, g_portal.other_level_offset);
  logf("sites: %-28s %sa player in mode %d, moved to its act's town at spot %d", "a death's wake",
       g_wake.known ? "" : "not derived ", g_wake.dead_mode, g_wake.town_spot);
  logf("sites: %-28s %s%d records of %d bytes at 0x%llX, the service at +0x%X (imbue %d, sockets %d, personalize "
       "%d), its quest at +0x%X (at most %d, asked for flag %d); the client's menu rows of %d bytes at 0x%llX",
       "NPC item services", g_npc.known ? "" : "not derived ", g_npc.record_count, g_npc.record_size,
       static_cast<unsigned long long>(g_npc.records ? g_npc.records - g_base : 0), g_npc.service_at, g_npc.imbue,
       g_npc.sockets, g_npc.personalize, g_npc.quest_at, g_npc.quest_max, g_npc.pending_flag, g_npc.menu_row_size,
       static_cast<unsigned long long>(g_npc.menu_rows ? g_npc.menu_rows - g_base : 0));
  const auto rva_of = [](uintptr_t a) { return static_cast<unsigned long long>(a ? a - g_base : 0); };
  logf("sites: %-28s %sthe ingredient free returns to 0x%llX, Clear Sockets' to %s0x%llX; an item handed over: grid "
       "0x%llX, spot 0x%llX, move 0x%llX, notice 0x%llX, charm 0x%llX, refresh 0x%llX; at the feet: place 0x%llX, "
       "spot 0x%llX, put 0x%llX; socket contents kept: first 0x%llX, taken out 0x%llX",
       "cube ingredients", g_cube.known ? "" : "not derived ", rva_of(g_cube.ingredient_free),
       g_cube.sockets_free ? "" : "(not derived) ", rva_of(g_cube.sockets_free), rva_of(g_cube.page_grid),
       rva_of(g_cube.grid_spot), rva_of(g_cube.inventory_move), rva_of(g_cube.item_notice),
       rva_of(g_cube.charm_counts), rva_of(g_cube.refresh_items), rva_of(g_cube.unit_coords),
       rva_of(g_cube.ground_spot), rva_of(g_cube.ground_put), rva_of(g_cube.first_item), rva_of(g_cube.take_out));
  logf("sites: %-28s %sa player's at its data +0x%X; set items' map +0x%X, uniques' +0x%X (buckets +0x%X, their count "
       "+0x%X; an entry's next +0x%X, its id +0x%X); the uniques' list 0x%llX (count 0x%llX, counted 0x%llX), the "
       "set items' 0x%llX (count 0x%llX, counted 0x%llX, %d bytes an entry)",
       "Chronicle", g_chronicle.known ? "" : "not derived ", g_chronicle.of_player, g_chronicle.sets_at,
       g_chronicle.uniques_at, g_chronicle.buckets_at, g_chronicle.count_at, g_chronicle.next_at, g_chronicle.key_at,
       rva_of(g_chronicle.unique_list), rva_of(g_chronicle.unique_count), rva_of(g_chronicle.unique_total),
       rva_of(g_chronicle.set_list), rva_of(g_chronicle.set_count), rva_of(g_chronicle.set_total),
       g_chronicle.set_entry);
  logf("sites: %-28s %sthe pick-up's notice of the item's new place returns to 0x%llX; the identify routine tests "
       "and sets item flag 0x%X and tells the clients with the same notice",
       "identify on pickup", g_identify.known ? "" : "not derived ", rva_of(g_identify.pickup_notice),
       g_identify.identified);
  logf("sites: %-28s %s", "enemy test",
       g_enemy_checked ? "its two entry stubs jump to it, and the AI's own two enemy checks end in a jump to the first"
                       : "not checked");
  logf("sites: %-28s %sthe killself timer kills a player's pet with the pet removal 0x%llX, which asks the pet "
       "lookup first",
       "permanent revives", g_revive.known ? "" : "not derived ", rva_of(g_revive.pet_removal));
  logf("sites: %-28s %swaypoint tiles: the per-tile add 0x%llX onto a layer's +0x%X, the tile lookup's records 0x%llX "
       "(by level type 0x%llX; a tile's type +0x%X, style +0x%X, sequence +0x%X; a level's type +0x%X); %sobject "
       "icons: the list insert 0x%llX onto a layer's +0x%X, an Objects row's cell +0x%X, the stash (class %d) in acts "
       "%d..%d, the sewer stairs (class %d) in mode %d, the Arcane waypoint (class %d) in level %d; %sa room's preset "
       "units; %sthe unit draw's transform 0x%llX, marker 0x%llX, name 0x%llX (an NPC's color %d), unit name 0x%llX",
       "map: landmarks, named enemies", g_map.tiles ? "" : "(not derived) ", rva_of(g_map.tile_add), g_map.floor_list,
       rva_of(g_map.lookup_records), rva_of(g_map.lookup_index), g_map.tile_type_at, g_map.tile_style_at,
       g_map.tile_sequence_at, g_map.level_type_at, g_map.objects ? "" : "(not derived) ", rva_of(g_map.list_insert),
       g_map.object_list, g_map.object_cell_at, g_map.stash_class, g_map.stash_act + 1,
       g_map.stash_act + g_map.stash_acts, g_map.stairs_class, g_map.stairs_mode, g_map.arcane_class,
       g_map.arcane_level, g_map.presets ? "" : "(not checked) ", g_map.draw ? "" : "(not derived) ",
       rva_of(g_map.to_map), rva_of(g_map.draw_marker), rva_of(g_map.draw_name), g_map.name_color,
       rva_of(g_map.unit_name));
  logf("sites: %-28s %sa built room's own preset units at +0x%X; %sits near rooms at +0x%X, its warp links at +0x%X "
       "(a warp's id at +0x%X, its tile's preset unit of type %d), an ActiveRoom's collision map at +0x%X",
       "map: area names at exits", g_map.room_presets ? "" : "(not checked) ", g_map.room_presets_at,
       g_map.exits ? "" : "(not checked) ", g_map.near_at, g_map.links_at, g_map.warp_id_at, g_map.warp_preset_type,
       g_map.collision_at);
}

size_t dump_image(uintptr_t exe_base, const wchar_t* path) {
  const mem::Range image = mem::module_range(exe_base);
  if (image.empty()) return 0;
  std::vector<uint8_t> copy;
  const size_t got = mem::snapshot(image.begin, image.size(), copy);
  FILE* f = _wfopen(path, L"wb");
  if (!f) {
    logf("sites: dump: cannot write %ls", path);
    return 0;
  }
  const size_t written = std::fwrite(copy.data(), 1, copy.size(), f);
  std::fclose(f);
  logf("sites: dumped %zu bytes of the image (%zu readable) to %ls", written, got, path);
  return written;
}

}  // namespace d2rcc::sites

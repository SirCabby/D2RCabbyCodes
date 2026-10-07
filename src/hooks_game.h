#pragma once

// The hooks into the game's own code, installed through D2RLoader so the loader
// tracks them and other plugins can chain them. Every cheat acts from here, on
// the game's server thread:
//   D2GAME_PLAYER_ApplyStatRegen  every frame per player: the tick (refills, speed, status)
//   SUNITDMG_ExecuteEvents        a hit is about to be applied: exit-before-death clamps it, the damage
//                                 multiplier scales yours and your minions', a protected pet's is
//                                 emptied, freeze and poison are taken off hits on you, and under god
//                                 mode everything a hit on you does but its damage
//   SUNITDMG_FinalizeDamage       the kill decision: exit-before-death, god mode (and pet protection) undo it
//   protected-stat test           god mode: a player's life may not go down, whoever lowers it, but to its top when
//                                 it is above it (any thread: the test D2RCore's stat writers ask before they lower
//                                 a value; a thunk hands on the unit the writer keeps in rdi)
//   death penalties               reached only if the above failed: leave the game
//   PLAYER_AddExperience          the multiplier scales every gain
//   hireling experience award     ... and the mercenary's, so it levels with the character
//   CLIENT_GetLevelName           the area level after an area's name (while the two UI routines below run)
//   AutomapPanel_Update / WaypointPanel_Populate  mark the names they ask for
//   SKILLMANA_GetManaCost         infinite mana: the local player's skills cost nothing
//   TreasureClassDropCore         a monster's or object's drop: loot filter only, every pick, extra drops for all
//                                 or for champions, uniques, bosses and Heralds only (loot.cpp)
//   item generation quality step  new items superior / ethereal / socketed where the game allows it
//   property roll                 perfect rolls: every value at the top of its range, the picks left random
//   item creation                 perfect rolls: a new armor is made from a unit seed (the seed a save keeps) that
//                                 rolls its top base defense; nothing is written to the item (itemseed.cpp)
//   class item skill bonuses      perfect rolls: a class item's three skill lines, each +3 (which skills stays random)
//   superior kind test            perfect rolls: a superior item's kind is the best of those that go on the item
//                                 (the superior step is told no for the others; superior.h, loot.cpp)
//   magic / rare / crafted affixes  max affixes: after the game's own pass, as many affixes as the quality can have,
//                                 from the game's own picker (loot.cpp)
//   affix picker                  best affixes: each affix picked for a fresh item is the best version of that affix
//                                 the item could have had at its level (loot.cpp)
//   automatic affix picker        ... and so is a base's own automatic affix (a paladin shield's all res), which the
//                                 generator picks last through the picker's twin
//   ITEMS_ShouldRemoveOnUse       infinite scrolls and potions: a used one stays (asked on the UI thread
//                                 too, by the client's use request; consumables.cpp)
//   item quantity update          infinite tomes: the item-use effect's charge is not taken
//   key use                       infinite keys: a locked chest or door opens and the key stays
//   item durability loss          no durability loss: a hit's wear on the local player's weapon or armor is not made
//   Impale's weapon wear          ... nor is Impale's wear on the weapon it was used with
//   inventory item name           item level: " (N)" after an item's name (UI thread; itemlevel.cpp)
//   curse skill step              cannot be cursed: a curse skill's step for one unit of its range is not made for
//                                 the local player (curses.cpp says which states are curses)
//   Cursed monster modifier step  ... nor is the Cursed modifier's Amplify Damage
//   vendor payment                infinite gold: what a vendor takes its price with takes nothing
//   town portal cast              the home town: marks the portals made inside a cast (hometown.cpp)
//   portal town end               ... the portal at the destination is made in the home town
//   portal use                    ... a portal that leads into another act takes its user there, by the game's own
//                                 move to a level
//   wake in town                  ... after a death the player, woken by the game in the town of the act it died
//                                 in, is taken on to the home town by the same move
//   client's portal trip          ... the one hook on the client (UI thread): a portal whose other end stands in
//                                 another act is not gone through ahead of the server
//   client's NPC menu             infinite imbues, sockets and personalizing (UI thread): as an NPC's menu opens,
//                                 the item service of its quest is put in it whatever the quest says (npcservice.cpp)
//   cube products / item free / socket contents free  infinite cube ingredients (cube.cpp)
//   item notice                   identify on pickup: the pick-up's notice of the item's new place (autoid.cpp)
//   AI dispatcher                 passive mercenary and minions: a passive pet's AI tick marks the thread (passive.cpp)
//   enemy test                    ... and while it is marked, no unit is an enemy (every way an AI finds a target asks
//                                 the test; any other question gets the game's answer)
//   kept target                   ... nor is there a target kept on the monster (the Warlock demons' own, one a
//                                 summoning skill set)
//   killself timer                permanent revives: a revive's end is not run (revive.cpp)
//   resistance of a hit           no enemy immunities: a monster's immunity counts as no resistance (enemies.cpp)
//   client's mod loop             ... and the hover lists none: the client's copy of an enemy's immunities lowered
//                                 after its mod loop (UI thread)
//   monster mod add / test / mods run  no enemy affixes: a monster that is not yours gets none of the unique affixes:
//                                 not added, refused to the rollers (D2RCore's too), taken out of what a maker wrote in
//                                 before the makers run (enemies.cpp)
//   item requirement test         ignore item requirements (any thread: the client's, the server's, D2RCore's): for
//                                 the local player, or its mercenary, level, strength and dexterity count as met; the
//                                 tick has the game work the unit's items out again when a switch changes
//                                 (requirements.cpp)
//   automap's draw of one unit    named enemies on the map (the thread that draws the automap): after the game's own
//                                 marker and name, a named enemy's red cross and name, and at the local player's own
//                                 call the spawn spots of the super uniques and bosses (mapmarks.cpp)
// and two patches, not hooks: the item writer sends the client every item's real level, not only for rows
// with ShowLevel (so the name has the right number), and the rare affix step's two picks call the affix picker
// the other steps call (it called D2RCore's picker straight), so best affixes sees them.
// The tick also carries out the character's respec and presets (character.cpp), under cannot be
// poisoned ends a poison already on you with the antidote potion's own cure (under cannot be cursed a
// curse, the same way), and under all areas terrorized applies every act's zone the way a Worldstone
// Shard does (terror.cpp), and takes the quest out of the record of an NPC whose item service is switched on (one
// byte of the game's NPC table, npcservice.cpp). The map reveal and the landmarks hook nothing (mapreveal.cpp,
// mapmarks.cpp, on the UI thread).
namespace d2rcc::hooks {

bool install();    // after sites::derive() and game::bind(); sets each cheat's why_not
void uninstall();  // the loader restores the code; this forgets the originals

void game_left();  // forget per-game state (the Levels table is read again next game)

unsigned life_losses_kept();  // god mode: the times a player's life was about to go down and did not

// Exit before death asked to leave the game (from the server thread). plugin.cpp
// acts on it from its watchdog thread and clears it.
bool exit_requested();
const char* exit_reason();
void clear_exit_request();

}  // namespace d2rcc::hooks

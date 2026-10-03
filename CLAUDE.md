# D2RCabbyCodes - working notes

CabbyCodes for Diablo II: Resurrected (offline), built as a **D2RLoader plugin** (`d2rl-cabbycodes.dll`) and
shipped as a **D2RMM mod folder** that carries the plugin in `d2rloader/`, with no D2RMM settings and no game data
of its own (`mod/mod.js` writes nothing). Sibling projects: RE0/RE1/RE2/RE3/Fear3 CabbyCodes (same author, same house
style; those proxy a game DLL, this one cannot - `D2R.exe` imports only Blizzard's protector, so the community
loader is the injection point every runtime mod uses).

## Rules

- **Never patch the game on disk.** Everything is runtime, through the loader. The D2RMM mod has no settings page
  and writes no game data: its one option, the rune numbers (asked for 2026-09-27, a D2RMM data change in
  `mod/mod.js`), was taken out 2026-10-01 (Joshua: "we shouldn't have this setting anymore and no settings page").
  Options go in the in-game panel, or nowhere.
- **No absolute addresses in code.** Every routine is found by byte signature (`src/sites.cpp`) and checked
  with the loader's expected-bytes contract before a hook. RVAs live only in this file, as seeds and record.
- **All game-code hooks go through D2RLoader** (`InstallInlineHook`) so they are tracked, chained and
  restored. MinHook is used only for the DXGI Present/ResizeBuffers/CreateSwapChain entry hooks of the overlay
  (the way the other overlay plugins do it).
- **Game memory is written only from the game's server thread**, inside our hooks (the regen tick, the damage
  hooks). The present thread draws and flips atomics; the watchdog thread saves settings and drives the exit.
  The UI thread (the loader's per-frame UI callback) only reads the client's side: the health bars, and a
  preset save (the client's copy of the character's attributes and skills). Two exceptions (approved): the map
  reveal builds the client's rooms and puts them on the automap, only through the game's own routines, on the
  thread the game itself does both on; and keeping the automap and Show Items between games presses the Automap
  and Show Items keys at a game's load, through the game's key-action table (D2RCore's wrappers where it has
  them), on the thread the game runs its key bindings on (and, from 2026-10-03, the same presses once a cinematic of
  the game's that closed the automap is over: asked as "when changing acts it seems the map gets closed even if it's
  set to stay open"; no new call, the panel gate's rules read). The item level (approved 2026-09-27) adds to the name the
  game's item-name builder returns, in that hook on the thread that asks (UI), and brings the first of the plugin's
  two code patches: a branch of the server's item writer turned into a jump through the loader (`PatchJmpRel32`), for
  the session. The second (best affixes, approved 2026-09-27) points the rare affix step's two calls of D2RCore's
  affix picker at the game's picker wrapper (`PatchCallRel32`, the same arguments), for the session. The NPC item
  services (approved 2026-09-29) put a service in its NPC's menu with the game's own routine for that entry, called
  on the UI thread inside the hook on the menu's opener, where the game calls it itself; and on the server thread
  the tick writes one byte of the game's own NPC table (the quest of the NPC's record), put back when the switch
  goes off or the plugin unloads, as god mode sets the game's no-damage switch. Infinite cube ingredients (approved
  2026-09-29) moves the items a transmute gave up into the inventory or onto the ground only through the game's own
  routines, inside the hook on the cube's product routine, on the server thread that runs it.
- **New game calls or hooks need Joshua's approval** before they go in. Approved 2026-09-28, all on the server
  thread: for cannot be cursed the two curse steps hooked; for infinite gold the vendor payment hooked; for the home
  town (asked for as the town a town portal leads to) three hooks (a town portal's cast, the town end maker, a
  portal's use) and, inside them, four of the game's routines called: an act made for the game, the move to a level,
  the waypoint test, and D2RCore's setter of an object's destination (the one write: the portal beside the player is
  told where it leads). Approved 2026-09-29: a fourth hook for the home town, the server's handler of a dead
  player's wake request, after whose own work the move to a level is called as a portal's use calls it (the act made
  first); and, after the crash of that day, a fifth, the plugin's one hook on a routine of the client (UI thread,
  it only reads): whether the client goes through a town portal ahead of the server. Approved 2026-09-29 for the
  NPC item services (asked as "larzuk will socket items without completing the quest for it, allowing for infinite
  socketing"; Joshua chose "menu hook + table byte" and all three services): one hook, on the client (UI thread),
  the NPC menu's opener, and inside it one routine of the game's called, the one the client's quest row of the
  NPC names (it writes the NPC's menu); on the server no hook and no call, the byte above. Approved 2026-09-29 for
  infinite cube ingredients (asked as "items will no longer be consumed when creating the new item ... place them
  back into the player's inventory, or drop onto the ground if no space permits it"; Joshua chose "ingredients +
  sockets"), all on the server thread: three hooks, the cube's product routine (it marks a transmute), the item free
  (it acts only on the product routine's free of an ingredient used up) and the free of what is in an item's sockets
  (only on Clear Sockets' call), and, inside the first once the game's own is through, the routines the quest reward
  giver hands an item over with: the grid of an inventory page, a free spot in it, the inventory's move, the client
  notice, D2RCore's charm test and the refresh of what the player's items give; with no room its drop at the feet
  (the item's flag, page, body location, mode and place set, the player's place and room, a free spot on the ground,
  the item put there); and in the socket hook the product routine's own way to take socket contents out and keep
  them (the first item of the item's inventory, taken out, and the same setters). Approved 2026-09-29 for a
  superior item's kind under perfect rolls (asked as "I don't think superior items are always rolling with max
  stats added to them"; Joshua chose the hook, "Damage + attack rating" for a weapon, and perfect rolls as its
  switch), on the server thread: one hook, the game's test whether a superior kind goes on an item, whose one
  caller is the superior step; inside it the test's own original is asked about the other kinds the step picks
  among. Nothing else is called, nothing written. Approved 2026-09-30 for god mode (asked as "we should hook the
  methods in the game that decrease player health and negate the effect. This should not need to be scanning every
  frame for performance reasons, and we don't need the refill logic"): one hook, the protected-stat test that
  D2RCore's four stat writers ask before they lower any value (any thread; answered "may not go down" for a
  player's life under the switch, else the original). God mode no longer writes the game's no-damage switch and does
  nothing in the tick. The same day (Joshua: "the life should be allowed to go down if life is above the current
  max", then "I don't like work having to be done on every tick"): a life above its top goes down to it, the game's
  own setter called inside the hook on the unit the writer is at (its rdi) and on the writer's thread (the one
  write: that unit's life, to its top); nothing is noted in the tick. Approved 2026-09-30 for best affixes (asked as
  "the perfect rolls paired with best affixes cheats didn't produce a sacred targe with +45 all res, instead giving
  me 30 all res"; Joshua chose the hook with the game's kind kept, for paladin shields, orbs and grimoires alike):
  one hook, on the server thread, the automatic affix's picker (the affix picker's twin with an AutoMagic group),
  whose one caller is the generator; it calls its original and answers another id of the same group, which the
  generator writes and applies itself. Nothing else is called, nothing written. Approved 2026-10-01 for identify on
  pickup (asked as "add a new cheat to automatically identify items on pickup"; Joshua chose the notice hook, and
  pick-ups only): one hook, on the server thread, the item notice (what tells the clients where an item went, 124
  callers), which acts only on the pick-up routine's own call (its return address); inside it one routine of the
  game's called, the identify routine an Identify scroll and Deckard Cain call, with their argument: before the
  notice for an item the pick-up put on the cursor, after it for any other place. It writes what identifying an item
  writes (the item's flag, a unique or set item's Chronicle entry), nothing else. Approved 2026-10-01 for passive
  mercenary and minions (asked as "add a new cheat that turns merc and minions passive so they don't attack enemies";
  Joshua chose the enemy test gate and two switches): three hooks, all on the server thread: the AI dispatcher (every
  monster's AI tick; for a pet of the local player's whose switch is on it marks the thread for the length of the
  tick), the enemy test (every way an AI finds a target asks it; answered "not an enemy" while the mark is up) and the
  getter of the kind of target kept on a monster (answered none while the mark is up). Nothing is called but the
  originals, nothing written. Approved 2026-10-02 for permanent revives (asked as "add a cheat so that necromancer
  revived minions don't time out and stay until death"; Joshua chose the timer dropped, and Revives only): one hook,
  on the server thread, the killself monster mod's timer (the end of a revive's time; a Decoy's and a Reanimate As
  monster's too), whose original is not run for a revive of the local player's under the switch; inside it one routine
  of the game's called, the pet lookup the original's pet removal asks first (read only). Nothing written. Approved
  2026-10-02 for named enemies and landmarks on the map (asked as "add a new display option to show named enemies on
  the map as well as shrines and other landmarks that usually require a close distance to appear on the map"; Joshua
  chose the unit-draw hook with spawn spots, uniques, super uniques, bosses and Heralds, two switches, and for
  landmarks "only the landmarks normally shown on the map that usually require distance to draw, such as waypoints",
  then waypoints and object icons from the area's layout only, no look at the objects the client knows): named
  enemies, one hook on the client, the automap's draw of one unit (the thread that draws the automap, the UI thread),
  after whose original the game's own routines are called: the transform of a point to the automap's screen, the
  marker draw, the name draw and the client's unit name (which rebuilds the name it caches in the monster's data, as
  it does for the game's own NPC names); nothing written. Landmarks, no hook: on the UI thread, inside the map
  reveal's area pass (whose rooms are built with CreateActiveRoom also when Reveal the map is off), the game's per-tile
  add for the floor tiles the automap's tile table gives the waypoint's cell (it marks the tile as on the map), and the
  automap object list's insert for a layout object's icon with the key the game gives a seen object: the game's own
  routines, on the thread the game itself does both on, writing what its own reveal writes (saved with the map). The
  same day, "the named on the map should also show regardless of distance like the landmarks cheat works": a named
  monster the automap has drawn keeps its mark where it was last drawn, however far the player goes, until it is seen
  dead (no new hook or call: the same draw hook and drawing calls). A random unique or a Herald cannot be shown before
  the server makes it: a room's monsters are made as the room is first activated near a player (0x503790). Then
  "looks like we're missing champions from the boss list": champions are marked too (their monster's name, in the
  game's champion blue, text color 3, once for a pack's members close together in a draw). Asked the same day for
  area names at exits ("let's add another display option to show area names for transition points, such as cave
  entrances or area zone lines"; a third switch, `map_exit_names`): no new hook and no new routine called - the area
  pass's room building (the rooms CreateActiveRoom builds link themselves to the areas next to them), reads of what it
  makes (a room's near rooms, warp links, preset units, collision map), and the named enemies' draw hook with its
  transform and name draw; nothing written. And, reported the same day ("a sparkle chest icon that didn't show up on
  the map until I got close to it in the Flayer Dungeon Level 3", "the entrance to sewers level 2 also isn't showing
  up on the map"): the landmarks read a built room's own preset list (they read the preset part's, which the build
  empties of the room's units: Khalim's Brain chest and most other rooms' never came), and the sewer stairs of a
  layout go on ahead (the game's rule shows them only once the lever opened them). Approved 2026-10-03 for the shrines
  the game rolls ("we need to make sure we're not changing shrine placement beyond vanilla, but all areas need to be
  able to get shrines, and I can't tell you each area one by one, you need to solve this yourself for all areas";
  Joshua chose "Fill on area entry"): a rolled object exists nowhere before the game fills its room, and which ones an
  area rolls goes by the order its rooms are filled in (the record: object groups), so no reading can say them
  ahead. On the server thread (the tick), as the character enters an area whose object groups can roll an object with
  an icon, each of its rooms not built yet is built with the game's CreateActiveRoom (the call the map reveal makes on
  the client's side, here on the server's DRLG), a few a server frame; the game's own fill pass then fills them (no
  fill is called). Their objects are read, and those with an icon put on the automap as the landmarks put theirs on.
  Placement is the game's rolls and rules; the order is the area's room list's. The server keeps a room it built for
  the rest of the game (its DRLG frees none: the record), so these rooms end as the rooms walked past do. Approved
  2026-10-03 for the experience multiplier (asked as "update the experience cheat to go higher, 1000 if possible.
  Also make sure the exp also works for the mercenary so they level up with me better"): the cap raised from 50 to
  1000, and one hook, on the server thread, the hireling experience award (0x44F480; the kill exp region's three
  calls are its only callers), which scales the local player's mercenary's gains under the same switch and
  multiplier; nothing of the game's is called inside it but the original and a read of the receiver's experience
  through the stat helpers. Both writers add the gain to a 32-bit total (the player's caps it at the level-99
  figure with an unsigned compare, the hireling's doubles the amount and has no cap of the total), so both hooks
  keep a scaled gain below the room the total has left: a sum past 2^32 would wrap and lower it. The hireling's
  gain also keeps its callers' own ceiling (0x7FFFFF): its writer walks the level up with no bound but 98 and the
  level setter none at all, so an unbounded award would take a fresh mercenary to 98 in one kill; within the
  ceiling one award bursts at most a few levels past the player, where the game's own entry gate (no gain at or
  above the player's level) holds it until the player passes it. Approved 2026-10-03 for ignore item requirements
  (asked as "add a new cheat to ignore requirements from items so that a level 1 with no stats could use anything";
  Joshua chose the hook with a refresh at a switch, class-only items kept with their class, and the mercenary too,
  under its own switch): one hook, on any thread (the client's calls, the server's, D2RCore's), the game's requirement
  test (0x36BC50), which for the local player (the server's unit or the client's copy), or its mercenary (the server's
  by its AI record's owner, the client's copy by the client's pet list), under its switch answers as if level,
  strength and dexterity were met: the three flags it hands back say met, and a no is replaced by the routine's later
  tests asked again the way it asks them (the identified flag, a tome's quantity, the restricted-socket test 0x376110
  called as the routine calls it, the class against the item type's row and, for a hireling, its Hireling row: reads
  of the game's data tables). Nothing written. When a switch changes in a game, the tick has the game work that unit's
  items out again with the cube's refresh (0x470C90), the mercenary found by the server's lookup of a player's pet of
  a type (0x4FF1A0, the one its handlers of the mercenary's gear ask).
- **Nothing looks at every frame for what an event announces, and no thread of the game writes a log line**
  (Joshua, 2026-09-28: "things that run every frame ... a game hook we could watch instead or event based
  functionality"). A piece of the UI callback waits for what the loader or the game says happened (a lifecycle
  event, a UI message, a switch of ours) behind one read of a flag, with a look once a second for what nothing
  announces (`src/gate.h`); only the health bars work at every frame, and only while they have monsters to follow.
  A log line takes the loader 5 to 9 ms: the game's threads hand theirs to the writer thread (`src/log.cpp`). See
  What runs when.
- **Every item a loot switch makes must be one a vanilla game can make** (asked 2026-09-27): its affixes, values
  and flags within the game's own rules for that base, level and quality, and nothing the seed saved with it
  contradicts (see Item seeds).
- **Only the saved seed is chosen, and nothing is looked up while playing** (Joshua, 2026-09-28). A save keeps one
  seed with an item, the unit's; the item's own seed is in no save, so nothing is tied to it later. The loot
  switches act in the generator; what the saved seed rolls (an armor's base defense) is never written to an item:
  the item is made from a unit seed that rolls it. No search threads, no waits, no seed files: a seed is a handful
  of multiplications inside the item's creation. Every item still gets everything its switches ask for.
- **Joshua commits and pushes.** Do not `git commit`/`push` unless asked.
- **No self-driven game testing.** Build, `make install`, and hand over a short checklist with the log lines
  to look for. Joshua plays and reports; `d2rloader/logs/cabbycodes.log` is the evidence.
- Offline only. D2RLoader cannot run Battle.net play; `game::is_online()` exists for a gate if that changes.

## Layout

```
src/plugin.cpp        entry points, settings load/save, lifecycle + UI-message listeners (which also say when
                      the pieces of the UI callback have something to look at), console command, the worker
                      (resolve sites -> bind -> hook), the watchdog (saves, Save and Exit ladder; woken, not polling)
src/log.*             the log: the game's threads hand their lines to a writer thread, the plugin's own threads,
                      warnings and trace = true write at once; the switch that leaves it out and the least severe
                      kind of line it takes (tests/test_log.cpp)
src/gate.h            when a piece of the UI callback works: after something happened, and at a safety pace
                      (tests/test_gate.cpp)
src/perf.*            what the plugin's own work costs, timed in the game (the console's perf; tests/test_perf.cpp)
src/watchdog.h        waking the watchdog
src/sites.*           signature table + resolver over the decrypted image; derived data addresses
src/game.*            typed wrappers over the stat helpers, unit readers (guarded), local-player identity, the
                      antidote's state cure
src/hooks_game.*      the loader-tracked hooks: tick, damage (multiplier, pets, freeze/poison), kill decision, death
                      penalties, mana cost, exp (the player's award and the mercenary's), item use (should-remove
                      predicate, tome quantity), key use, item
                      wear (a hit's, Impale's), item name (item level), the drop core, the quality step, the
                      property roll, the class item skill step, the superior step's test of a kind, the magic,
                      rare and crafted affix steps, the affix picker and
                      the automatic affix picker (loot), item creation (the unit seed), the two curse steps, the vendor payment, a town
                      portal's cast, town end and use, a death's wake, the client's portal trip, the client's NPC
                      menu, the cube's product routine, the item free and the socket contents' free, the protected-
                      stat test (god mode), the item notice (identify on pickup), the AI dispatcher, the enemy test
                      and the kept target's getter (passive pets), the killself timer (permanent revives), the
                      automap's draw of one unit (named enemies on the map), the requirement test (ignore item
                      requirements); the item writer patch, the rare step's picker calls
src/cheats.*          switches (atomics), Status snapshot, why-not reasons for the panel
src/movespeed.h       movement speed: the bonus in the player's base velocitypercent, put on top of the game's own
                      again wherever the base is not what the tick wrote (each game's character starts at 100, with
                      the same id; the rule, tests/test_movespeed.cpp)
src/terror.*          all areas terrorized: every act's manual zone (and the Moo Moo Farm's) applied as a Worldstone
                      Shard applies it, from the tick; switched off, the manual terror taken off
src/arealevel.*       Levels table (loader data-table service) + localized names (loader localization service) for area levels
src/loot.*            the loot switches: drop sessions (loot filter only, Chronicle only - either will do with both -,
                      every pick, extra drops: for every kill
                      and chest, or for champions, uniques, bosses and Heralds only; a drop made only in a form
                      the filter shows, from the item creation hook, and the ones it hides made up for by picks
                      among what it shows; the console's dropcheck), superior/ethereal/
                      socketed at the item generator's quality step, perfect rolls (property roll, class item
                      skills, a superior item's kind), max affixes (the magic, rare and crafted affix steps topped
                      up), best affixes (each pick of those steps kept at the best version of its affix), counters
src/superior.h        a superior item's kind under perfect rolls: which of the kinds that go on an item is the best
                      (the rule; loot.cpp asks it from the hook on the game's test of a kind;
                      tests/test_superior.cpp)
src/dropodds.*        what a treasure class can drop that the loot filter shows: the drop core's walk as how often
                      it comes to each item, the quality roll as chances, the forms the generator makes of a quality
                      (a unique that came, ethereal, sockets), the picks among what is shown; tables through the
                      loader, the game asked only through what loot.cpp hands it; a unique or set item of a row
                      that is wanted whatever the filter says (the Chronicle's) kept too (tests/test_dropodds.cpp)
src/chronicle.*       the Chronicle (3.3's record of the uniques, set items and runewords found): the game's own lists
                      of its unique and set item entries and the local player's maps of those found, read where the
                      game keeps them (server thread); which rows it misses, less the ones kept as drops in this
                      game (the map and list readers in the header, tests/test_chronicle.cpp)
src/itemseed.*        the seed a save keeps with an item (the unit's): under perfect rolls a new armor is made from
                      one that rolls its top base defense (the item creation hook), and checked; the arithmetic is in
                      the header (tests/test_itemseed.cpp)
src/healthbars.*      monster health bars and the boss bar: MonStats copy (game thread), monster walk + projection
                      (UI thread, from the UI pump), ImGui drawing (present thread)
src/consumables.*     infinite town portal / identify / potions: Items rows classified once per game (game thread),
                      asked by the item-use hooks on the UI and game threads; infinite keys (asked by the key-use hook)
src/curses.*         cannot be cursed: the States rows classified per bank (game thread: a curse that can be cured),
                      asked by the two curse hooks; the curses already on the player ended from the tick (the
                      rule is in the header, tests/test_curses.cpp)
src/hometown.*        the home town: each act's town and its waypoint (ActInfo and Levels rows, game thread), the
                      town end made in the home town, a portal into another act used, a death's wake taken on to
                      the home town, the client kept from going through such a portal ahead of the server (the
                      choice and what a use comes to are in the header, tests/test_hometown.cpp)
src/npcservice.*      imbues, sockets and personalizing without their quests: the NPC of each service found in the
                      game's NPC table and in the client's quest rows (at bind), the quest taken out of its record
                      while the switch is on (the tick), the service put in its menu by the game's own routine (the
                      hook on the client's menu opener, UI thread); the rule is in the header
                      (tests/test_npcservice.cpp)
src/cube.*            infinite cube ingredients: a transmute marked (the product routine's hook), the ingredients it
                      uses up and Clear Sockets' socket contents kept from their frees, then handed back to the
                      inventory, or at the feet, the way the quest reward giver hands an item over (game.cpp); what is
                      kept and the order it goes back in are in the header (tests/test_cube.cpp)
src/autoid.*          identify on pickup: the item the pick-up routine has just moved identified by the game's own
                      routine, before or after the pick-up's notice to the clients (the hook on the item notice,
                      server thread); when, by where the item went, is in the header (tests/test_autoid.cpp)
src/passive.*         passive mercenary and minions: the thread marked for the length of a passive pet's AI tick (the
                      hook on the AI dispatcher, server thread), and while it is, no unit is an enemy (the enemy test's
                      hook) and no target is kept on the monster (the kept target getter's); which pets, and the
                      mark, are in the header (tests/test_passive.cpp)
src/revive.*          permanent revives: the killself monster mod's timer not run for a revive of the local player's
                      (the hook on the timer, server thread), which pets by the game's own pet lookup; Revive's pet
                      type read from its Skills row (game thread, the tick); the rule is in the header
                      (tests/test_revive.cpp)
src/requirements.*    ignore item requirements: the requirement test's hook (any thread) takes level, strength and
                      dexterity as met for the local player, or its mercenary, and asks the routine's later tests
                      again (the game's data tables read, the restricted-socket test called); a switch changed in a
                      game, the tick has the game work that unit's items out again; the rule is in the header
                      (tests/test_requirements.cpp)
src/itemlevel.*       item level after an item's name: Items and ItemTypes rows classified per bank (game thread),
                      the level added by the item-name hook (UI thread)
src/mapreveal.*       the area pass: the current area's rooms built and, as the switches ask, revealed on the automap
                      and handed to mapmarks (UI thread, no hook; started by the loader's area and game events)
src/mapmarks.*        named enemies, landmarks and area names at exits on the map: the Objects, MonStats, SuperUniques
                      and Levels rows and the levels' names (game thread, the tick), a room's layout read in the area
                      pass (its own preset units: the objects' icons and the waypoint's floor tiles put on the automap
                      by the game's own routines, the super uniques' and bosses' spawn spots noted, the warps' tiles;
                      its warp links and its edges with other areas' rooms, walked over its collision map, for the
                      exits), and the hook on the automap's draw of one unit (a named enemy's red cross and name, its
                      mark kept where it was last drawn, the spawn spots, the exits' names of the player's area); the
                      rules are in the header (tests/test_mapmarks.cpp)
src/roomfill.*        the shrines the game rolls, for the landmarks: an area entered (server thread, the tick), its rooms
                      built for the game's own fill pass to fill, a few a frame; then the objects in them read and
                      handed to mapmarks, which puts those with an icon on the automap (UI thread); each area once a
                      game, only those whose object groups can roll an object with an icon
src/remember.*        the automap and the two Show Items keys kept between games: read while playing (when a key,
                      a button or a panel says so), put back at a load by pressing the keys through the key-action
                      table (UI thread, no hook); what a cinematic of the game's does to the automap not recorded,
                      and undone once it is over
src/character.*       respec and skill/attribute presets: requests from the panel, carried out by the tick, the
                      presets file (cabbycodes.presets.txt beside the settings) written by the watchdog
src/presets.*         the presets file format (text in, text out; tests/test_presets.cpp)
src/config.*          TOML read/rewrite of [cabbycodes]
src/overlay.*         ImGui panel, window subclass, visibility rule (pause menu / F7)
src/overlay_d3d12.cpp DX12 host: probe swap chain, MinHook Present, render
src/mem.*             guarded reads (VEH), PE sections, pattern search, image snapshot
src/dev.*             DEV builds: command file poller (d2rloader/logs/cabbycodes.cmd)
tools/                RE helpers over the runtime dump, a CASC reader for the game's data files, the seeds in the
                      stash files against their items (see below)
tools/seedmodel/      research, not in the plugin: a model of the item generator (what an item's own seed makes,
                      checked against the game), its checks and a search over all seeds (host builds)
mod/                  the D2RMM package: mod.json (no settings), mod.js (writes no game data; logs one line)
res/                  manifest (ABI 4), default TOML (embedded + shipped), version resource
```

Build: `make` (DEV=1, `build/dev/`), `make DEV=0` or `make dist` (release, `build/release/`), `make install`
(global plugin dir), `make install-mod`, `make install-d2rmm` (copies the dist folder into D2RMM's mods),
`make package`, `make test` (wine; test_itemseed runs over the armor
rows of the loader's compiled tables under `GAME_DIR`, or `D2RCC_EXCEL`; test_dropodds has the game's quality roll,
drop core walk and generator steps written a second time, and a small game of its own, with a Chronicle;
test_chronicle builds a map of the game's layout and reads it back), `make seedfinder` and
`make seedmodel-test`
(host builds of tools/seedmodel). `config.mk` holds `GAME_DIR`, `MOD_NAME`, `D2RMM_MODS_DIR`.
Launch: `scripts/d2r-loader.sh` (umu-run, Battle.net prefix, Proton verb `run`), or D2RMM's Run Game.

## Loader facts learned

- Build 3.3.0 / 93847, exe base 0x140000000, image decrypted before plugins load (a probe signature is
  polled; `EVENT_SetEvent` is the probe because D2RCore does not patch it).
- D2RCore patches the entries of `GetUnitStat`, `SetUnitStat`, `AddUnitStat`, `GetUnitBaseStat` (10/6/10-byte
  `FF 25` jumps) and of `SKILLMANA_Consume`, `SetStatListStat`, `PostStatList`. They can be called (our
  signatures start past the patch; twins are told apart by the patched entry) but not hooked.
- `PatchCallRel32`/`PatchJmpRel32` targets must be inside `D2R.exe` (SDK contract). A call site cannot be
  redirected into the plugin; only `InstallInlineHook` gets plugin code in.
- The loader's Native Hooks (`InstallInlineHook`) are MinHook's: D2RCore.dll carries it (its `MH_ERROR_*` strings,
  HDE64's opcode table at RVA 0x6FBEB0; the installer is D2RCore 0x47D960). So a routine whose entry has a short
  Jcc or a RIP-relative operand can be hooked: the trampoline turns the Jcc into an absolute one and moves the
  displacement. Checked for the protected-stat test with the plugin's own MinHook over the dump mapped at the
  game's base (scratchpad harness/hookcheck.cpp, 2026-09-30): the trampoline answered as the routine for all 672
  combinations of stat, unit type and switches. Its limits are MinHook's: no LOOP/JRCXZ out of the first five
  bytes, no jump back into them.
- `ThreadService::runOnUiThread` returns `OwnerInactive` (4) while `LoadPlugin` runs. Queue from a thread
  after load; the watchdog retries once a second until accepted. UI callbacks then run every root-widget update:
  the pump re-queues itself, so it is a once-a-frame callback on the game's UI thread, the thread and phase where
  the game's own widgets read client units and project them (the health bars run there, no hook needed).
- The loader's tables are the game's own memory: D2RCore's `getTable` reads the bank's data tables (TreasureClasses:
  the rows at +0xD78, their count at +0xD80, 0x150 bytes a row; its code at file offset 0x409EE8 of D2RCore.dll), so
  a treasure class the drop core is handed lies among the TreasureClasses rows. `TableView.revision` is the table
  load's. The compiled `treasureclassex.bin` is another thing (0x304 bytes a row, names and chances as in the txt).
- `DataTableService::getTable` answers only on the game (server) thread (`Busy` elsewhere); the pointers expire at
  the next table load. Copy what is needed there (the tick does, once per game).
- Widget calls (`dispatchUiAction`) must run on the UI thread. Save and Exit = `PausePanelMessage` /
  `ExitGame` (learned from the UI message log), preceded by `PanelManager`/`OpenPanel`/`PauseLayoutGarden`.
- Pause menu: `PanelManager` `OpenPanel`/`ClosePanel`/`UnloadPanel` with text `PauseLayoutGarden`;
  `PausePanelMessage` `Close`; leaving: `FrontEndNavigation`/`ToFrontEnd`.
- UI messages that come every frame: `InputMessage`/`MouseMove` and `MonsterHealth`/`ClearInfo` or `SetInfo` (the
  hover bar), one after the other, so a log that only collapses repeats of one message writes them all (the run of
  2026-09-28: 1,805 of the DEV log's 2,000 lines). They are told apart first and never logged. The others of a game:
  `InputMessage` `KeyDown`/`KeyUp` and, for a key that is bound, `BindingDown`/`BindingUp` right after it,
  `LeftMouseButtonDown`/`Up`, `RightMouseButtonDown`, `CharacterTyped`; `PanelManager` `LoadPanel`/`OpenPanel`/
  `ClosePanel`/`UnloadPanel` with the panel's name, sent inside the call that opens or closes it (`AutoMap` and
  `ShowItemsPanel` among them: a key pressed through the key-action table logs its panel's message before the
  press returns); `ScrollController`/`ManualScrollInput`; `Waypoints` `SelectTab`/`SelectWaypoint`.
- The loader's lifecycle events come on the UI thread, `LevelChanged` before the loading screen of the area goes
  (a second before, at a waypoint between acts), so an area's work can start with its first frame.
- Under Proton the game presents from its UI thread (one thread id for the present hook, the window and the UI
  callback), so whatever the UI callback, the UI message listener or the present hook does is a frame's time.
- Lifecycle `LocalPlayerReady.playerId` is the unit id (1 offline). The global server unit table does not
  list the player; the server unit is recognised by id in the hooks (every player passes the regen tick).
- Character badges ("soft modded", "plugins") come from the loader's `<char>.d2rl` sidecar, not the save.
- The sidecar `<char>.d2rl` (read in D2RCore 2026-09-28, RVAs of D2RCore.dll, whose own routines have names in the
  Ghidra project): "D2RL", u16 version 2, u16 header length 0x2C, u32 payload length, the payload's SHA-256 (+0x0C),
  then the payload: +0x08 the .d2s's checksum, +0x0C its size, +0x10 its SHA-256, times, a history (a count of
  sidecars, the SHA-256 of the one before), the game's version and build, the mod, the plugins with their versions.
  It is sealed: an ECDSA P-256 signature (64 bytes, the public key beside it) over SHA-256("D2RL-SEAL-V2\0" and the
  serialized record), verified at 0x4F3C80 (the digest: 0x8FD0). The character list's reader
  (ReadOfflineCharacterSummaryWithEnvironment 0x82DE70) gives a character a status: 0 no sidecar, 1 one that does
  not read or whose seal fails, 2 one that names another .d2s (0xB450 compares checksum, size and SHA-256), 3 one
  that names this .d2s. Only with 3 are the recorded mod and plugins compared with the running ones (0x3B7DA0: the
  badges, and issues from 0x20 up ask before the launch, LaunchFrontendCharacterWithEnvironmentWarning 0x834D80);
  with the others there are no badges and no question. A save writes a new one after the .d2s is closed
  (CloseD2sFileWithEnvironment 0x82D040), its history taken from the one that is there when that one's seal holds
  (0x4F3E40), whatever .d2s it names. A .d2s changed by hand is therefore status 2 until the next save.
- Under Proton the `waitforexitandrun` verb waits for the prefix's wineserver (Battle.net) forever; use `run`.
- A log line takes the loader about 5 ms (2,458 lines of a run: 7 gaps under 5 ms, most 5 to 9; again 2026-09-28:
  1,891 pairs of lines, none closer than 4 ms, half of them 8 or more): nothing that is timed may count it
  (`log_spent_us`), nothing that happens per item may log without a limit, and a thread of the game hands its
  lines to the writer thread (see What runs when).

## Reverse-engineering record (D2R 3.3.93847; RVAs, never used as fixed offsets)

| Routine | RVA | ABI / notes |
|---|---|---|
| STATLIST_GetUnitStat | 0x2F5020 | (unit, statId, u16 layer) -> int32 |
| STATLIST_SetUnitStat | 0x2F7D10 | (unit, statId, value, u16 layer) |
| STATLIST_AddUnitStat | 0x2F34F0 | (unit, statId, delta, u16 layer) |
| STATLIST_GetUnitBaseStat | 0x2F48C0 | (unit, statId, u16 layer) -> int32; twin at 0x2F50C0 (unpatched) |
| EVENT_SetEvent | 0x48B720 | probe only |
| SUNITDMG_ExecuteEvents | 0x44CE80 | (game, attacker, defender, int computeTotals, D2Damage* on the stack): applies the amounts; the no-damage switches are read here. computeTotals 1 (most callers: missiles, skills) runs CalculateTotalDamage inside; 0 (the melee record 0x44B3FA, 0x5F2621) has the totals in already. A missile's owner is the attacker (0x436E90) |
| SUNITDMG_CalculateTotalDamage | 0x44DF10 | (game, attacker, defender, D2Damage*): the PvM/PvP percentage, then resistances in place per damage type (the loader's `.maho` resolver), "cannot be frozen" (stat 153 zeroes chill/freeze lengths, 118 halves them), shrine states 0x85/0x83 zero poison/burn lengths; life total = phys + fire + light + magic + cold + poison, + leech for a monster attacker |
| damage effects | 0x4509B0 / 0x451570 / 0x4518C0 / 0x451380 / 0x451C30 / 0x451820 | chill (state 11) / freeze (state 1) / poison (state 2, hpregen stat) / burning (state 115) / stun (state 21) / absorb heal; all called from ExecuteEvents only |
| SUNITDMG_FinalizeDamage | 0x44A9B0 | (game, attacker, defender, D2Damage*): the kill decision |
| D2GAME_PLAYER_ApplyStatRegen | 0x42E600 | (game, unit, a3, a4): every server frame per player (~25/s) |
| a character's stats for a game | 0x52D770 / 0x420280 / 0x5311F0 | the base stats a character's new unit gets as a game takes it in: a new character's from its CharStats row (0x52D770: attributes, life, mana, stamina, level 1), a character read from its save (0x420280, the quests' "Woo!" header checked; life and mana), and 0x5311F0 on the load D2RCore runs (0x52E910, named only in D2RCore's routine table; gold capped, stamina full, the skills' hot keys). Each sets base stats 68 attackrate, 67 velocitypercent and 69 other_animrate to 100. No other routine of the game's writes a player's 67 (its writers: those three, the monsters' init 0x495500, a hireling's 0x543E90 (75), the debug routine 0x402BB0 on its own unit, and the client's 0xFED70, 0x1A33B0, 0x1A3940, 0x1A4150). Read: movement speed |
| D2GAME_PLAYER_DeathPenalties | 0x424AC0 | (game, player, killer) -> u64: gold and experience loss |
| PLAYER_AddExperience | 0x44F2E0 | (game, player, currentLevel, amount): players only (0x34B9D0); the gain added to the 32-bit experience total (GetUnitBaseStat 13, `lea esi, [rdi+rax]` at 0x44F34B), capped at the level-99 total with an unsigned compare (`cmova` at 0x44F377: a sum past 2^32 wraps and lowers the total), sets stats 29/13, levels up via 0x52DA30 + event 0xC; callers in 0x44A3B0..0x44A823 (kill exp; amount from 0x44ECE0, capped 0x7FFFFF). Hooked: experience multiplier (the scaled gain kept below the room the total has left) |
| D2Common_SKILLMANA_GetManaCost | 0x33AA00 | (u8 dataCtx, skillId, level) -> cost in 256ths: `(mana + lvlmana*(lvl-1)) << manashift`, min `minmana<<8`. No unit argument |
| SKILLMANA_Consume -> GetManaCost | 0x4369FB | call site inside 0x436830 (D2RCore-patched entry); unit in rsi; return value ignored by both callers |
| client mana check -> GetManaCost | 0x218937 | call site inside 0x2188B0; unit in rbx; cost <= 0 skips the check |
| CLIENT_GetLocalPlayer | 0x9A480 | (dataContext) -> client unit; `lea` of the client unit table inside |
| GetUnitByIdAndType | 0x9A5A0 / 0x9A5D0 | (id, type); server and client copies, told apart by the table each `lea`s |
| CLIENT_IsOnlineState | 0x8D3B0 | () -> bool (ctx+0x5C == 1) |
| no-damage switches | 0x2AA6200 (players) / 0x2AA6201 (monsters) | bytes, derived from `cmp byte [rip+x],0` in ExecuteEvents. Read by ExecuteEvents (nothing of the hit applied to a player / monster, the killed flag cleared; the events before it still run), FinalizeDamage (0x44AEE7: no player's death), the protected-stat test 0x2F2830, a hit of a percentage of the target's life (0x5F2270), and client code. Written by the game's setter 0x419E60 (the console's raw command 0x40F580 and D2RCore's `god` 0x410A90, which also sets the mana and stamina cheats), cheatsettings.json at start-up (0x123A0E; no such file in the install), 0x40F5C0 (monsters), and **D2RCore, which clears both with all the game's debug cheats at every game's load**: its ordinal 108 (0x1CEF40, the loader's game-load call) ends with 0x1F34C0, which clears them when D2RCore's flag bit (.data 0x716A38 & 1) is set or the game's byte 0x2A96102 is 1, which D2RCore's own init (ordinal 100, 0x1CE910 -> 0x1F3090) sets. Read: the protected-stat test's check (god mode held it from the tick for a day, 2026-09-30; see Dead ends) |
| protected-stat test | 0x2F2830 | (stat, unit type) -> al: a player's life (6) while the players' switch is on, its mana (8) and 0x1A while the mana cheat 0x2AA6A6A is on, a monster's life while the monsters' switch is on. The game's SetUnitStat body refuses a lower value of such a stat, and D2RCore's SetUnitStat and AddUnitStat (0x3D9BF0 / 0x3D9EE0 of D2RCore.dll, behind the `FF 25` entries) call it from its routine table for a value lowered, and so do AddWideListStat and AddWideListStats (0x3D9990 / 0x3DB590): whatever lowers a stat asks it (the exe's own AddUnitStat body calls it at 0x2F336E, behind D2RCore's entry). At the call the unit writers keep the unit in rdi, the list writers the delta (AddWideListStat) or the target list (AddWideListStats), and each returns at once on a "may not go down" but AddWideListStats, which goes on to its next stat and reads its list again. Its entry (`test edx, edx; jne short; cmp byte [rip+x], dl`) is hookable (MinHook). Hooked: god mode (through a thunk that hands on rdi) |
| player death | 0x42D020 | the mode setter with mode 0: from FinalizeDamage (0x44AF4A, the killed flag, not while the players' switch is on) and the `killme` console command (0x411E70) only; 0x42D2C0, the other setter, has no caller that gives it 0 as a constant (two pass the mode of a client's request, 0x4F3C70 and 0x4FDB40). Read: god mode |
| damage returned | 0x4395E0 | (game, attacker, defender, D2Damage*, stat): Thorns, Iron Maiden and the Heralds' thorns (HeraldThorns, skill 428: missile_thorns_percent too): the stat's percentage of the hit's physical damage (+0x18, after the damage multiplier scaled it) goes back to the attacker through ExecuteEvents and FinalizeDamage, the attacker the defender. Run between a melee hit's ExecuteEvents and FinalizeDamage (0x44B2B0 -> 0x436FC0). Read: god mode |
| 0x34B9D0 | | (unit) -> the unit type (so nonzero for non-players; used by Consume, AddExperience) |
| hireling experience award | 0x44F480 | (game, player, hireling, the hireling's level, amount; the fifth argument on the stack): nothing for an amount <= 0 or a hireling in none of the player's pet lists (0x4FF2D0 by its id), or whose hireling record (0x3965D0) is missing; only while its level is below the player's (GetUnitStat 12) and below max level - 1 (0x300C70: at 98 nothing is written); the amount doubled into the 32-bit experience total (GetUnitBaseStat 13, `lea ebp, [rax + rbx*2]`, SetUnitStat with **no cap of the total**), the client told (0x47BED0), the new level walked up the hireling record's exp curve (0x3C1AC0, record +0x20) and set (0x544F60, then 0x545530, unit event 0x62, the level-up event 0xC via 0x5881E0). Three callers, all in the kill exp region (0x44A565 / 0x44A7C8 / 0x44A980): the amount from 0x44ECE0 with the hireling's level, x 86/256 when the killer is not the hireling itself. The level set (0x544F60) clamps nothing: it writes the level the walk reached. Hooked: experience multiplier (the mercenary's gains; the scaled amount kept below half the room, as the writer doubles it, and at the callers' ceiling 0x7FFFFF, so one award bursts a few levels at most) |
| 0x544F60 / 0x544AC0 | | hireling level set (not the player) |
| 0x550C20 | | level-up-by-chunks utility (console/cheat path), not the award |
| DesecrateGetCurrentScheduledZone | 0x35B380 | (bool* changed) -> zone record or 0: `_time64`, config for now, slot = (now - start) / ((duration + break) * 60), index from a seeded weighted pick (0x35C6B0, cached per slot), record = zones + index * 1000. Also called with 0 by the server at a game's join (0x483D0A, then apply kind 1). Called: all areas terrorized (switched off, the rotation's zone applied again) |
| DesecrateGetConfigForTime | 0x35DB10 | (int64 utc, bool* changed) -> config record: +0 start, +0x10 duration min, +0x14 break min, +0x20 seed, +0x250 zones, +0x258 count; +0x18 / +0x1C the manual thresholds (deprioritize / removal), +0x268 the manual zone groups, +0x270 their count; records 0x298 apart in the loaded list (globals 0x2A9AE78/80). Called: all areas terrorized |
| DesecrateApplyCurrentZoneJob | 0x405B30 | (): for every game (0x400 slots, 0x4062F0), the kind-1 terror off (0x488DF0 (game, 1) at 0x405CCD), then if 0x488C70: resolver + apply (game, zone, 1) (the apply call at 0x405CFE goes through the loader's `.maho` jump table, D2RCore owns it) + client update (0x4F22C0). Run by the client loop at rotations and by the native `desecrated` console command (0x88990 -> 0x35C220 sets the override, then the job); 0x4073C0 is its twin. Read: its calls, checked against a shard's (all areas terrorized) |
| zone record | | 1000 bytes: +0 FNV-1a hash of the id, +4 char id[128], +0x88 levels pointer, +0x90 level count; level 0x38 bytes: +0 level id, +4 waypoint level id, +8 difficulty data |
| Worldstone Shard use | 0x580320 | item-use callback 15 (game, player, item) -> bool: the act = the Items row's calc1 (0x3B4880), terror zones on? (0x488C70), two sets of what the game has (0x582310: levels, zone hashes), the pick (0x35B760), then apply (game, zone, 2) + client update. Its can-use check 0x5800A0 (the table before, 0x1D3EA08 + 8 * pSpell) refuses when the group has no zone left (0x35BAE0). The shards are misc xa1..xa5 (Western, Eastern, Southern, Deep, Northern Worldstone Shard), pSpell 15, calc1 the act, usable in Hell only (UsageConditionCalc). Read (its calls): all areas terrorized |
| DesecrateGetManualZoneForZoneGroup | 0x35B760 | (group id, seed, sets...) -> zone: the config for now, its manual groups (+0x268, count +0x270, 0x20 bytes: +0 int32 group id, +8 zones, +0x10 zone count), the group's zones filtered by the thresholds against what the game has, one picked by the seed. 3.3: one zone per group, Act1-Manual .. Act5-Manual (37 / 34 / 27 / 5 / 22 levels: every terrorizable level of the act; only the Moo Moo Farm (39) is in no group). Checked (layout) |
| IsDesecratedZonesEnabled | 0x488C70 | (game) -> bool (al): game +0x120 (a byte of the game's creation settings, beside the ladder flag at +0x108: its terror zones setting), not the classic bank (+0x106 != 1), the config on for the version, difficulty and ladder (0x35B5E0), the game's quest record (+0x21D8, 0x5174F0) with quest 40 (Eve of Destruction) done on the difficulty (0x514FD0, bits 0 / 0xF / 0xD). Called: all areas terrorized |
| GameDesecrateZone | 0x4885F0 | (game, zone, u8 kind: 1 the rotation's, 2 manual; 0 asserts): the same test as 0x488C70 inline, the queue retried (0x488D00), then LevelDesecrate for each of the zone's levels with the level's difficulty data (0x35B4F0). D2RCore sends all five callers (the job 0x405CFE, its twin 0x40750E, a game's join 0x483D1D, Baal's kill 0x54FC7B, the shard 0x58041A) through `.maho` 0x3E2B430 to its RVA 0x2B1570: the original first (through its routine table, .data 0x6FD378: {pointer set at run time, game RVA, 0} entries 0x18 apart), then its 3.2 Herald state for the game rebuilt from the list (the kind is not looked at). Called (the `.maho` slot): all areas terrorized |
| LevelDesecrate | 0x488FE0 | (game, kind, zone, level id, waypoint level id, level config) -> bool: no runtime for the level (game +0x1D0 -> [level id], 0x508740) -> nothing; listed with the same kind -> taken off and applied again; with the other kind -> queued (game +0x7728 {entries, count}, 0x790 bytes; a queued manual one is refreshed); else applied (0x508F60 on the runtime: +0x2E8 on, +0x2EC the data) and listed (game +0x5820 {entries, count}, 0x90 bytes: +0 level id, +4 waypoint level id, +8 the zone's hash and id, +0x8C kind). Not called |
| GameUndesecrateKind | 0x488DF0 | (game, u8 kind; 0 all): the queued of the kind off, the listed of the kind off (0x489C10 by level id: the runtime cleared, 0x5096C0, the entry erased), then the queue retried (a tail call of 0x488D00: LevelDesecrate per queued entry, the applied ones erased). Callers: the job and its twin, Baal's kill 0x54FA70, all with kind 1: nothing in the exe or D2RCore takes kind 2 off, so a shard's terror lasts the game. Called: all areas terrorized (kind 2, switched off) |
| desecration client update | 0x4F22C0 | (game): every client in the game (0x4040C0) gets 0x478D30: the listed levels in packets of 63 (level, waypoint level, difficulty, kind). Also sent to a client as it joins (0x408EA0). Called: all areas terrorized |
| CLIENT_GetLevelName | 0xC7A30 | (u8 dataCtx, levelId) -> UTF-8 name: `DATATBLS_GetLevelsTxtRecord` (0x32C4A0), the LevelName key at record+0xFD, then the loader-owned string lookup (`.maho` 0x3E2B736). 11 callers: the automap update (0x1471DCD), the waypoint fill (0x1EA1A6), the HUD terror list, the game list. Hooked: area levels |
| AutomapPanel update | 0x1471BC0 | (panel, ...): builds the area / party text with GetLevelName and SetText (0x86DD50). Hooked: marks its calls |
| WaypointPanel populate | 0x1E9B40 | (panel): fills the waypoint buttons; GetLevelName at 0x1EA1A6, SetText at 0x1EA1E1. Builder 0x1EADF0 (templates "waypoint1".."waypoint9"). Hooked: marks its calls |
| LevelNamePanel::Update | 0x14E51A0 | (panel): the *entering an area* notification, not the automap text: record+0x14D key, SetText on panel+0x198 / +0x1A0 (DesecratedText). Not hooked |
| UI_TextWidget_SetText | 0x86DD50 | (widget, StringView*{utf8, len}): assigns widget+0x88 then refreshes (0x86E570); 325 callers. Not hooked (a first attempt; the name getter is the better choke point) |
| Game difficulty | Game+0x104 | byte 0/1/2, read by SUNITDMG at 0x44DF45. The loader's lifecycle events carry difficulty 0 for the join events, so the tick reads it from the game record |
| UnitAboveHeadToScreen | 0xCE850 | (unit, float2 offset packed x \| y<<32) -> rounded point x \| y<<32, or -1: `[GetRenderThreadContextRoot()+0x20]` (0x685750, a static-object getter despite the name), then ProjectUnitToScreen 0x76A7D0 (ctx, unit, float2* out, bool above_head) with above_head = 1 (the top of the unit's bounds plus a margin; out.x from the feet, out.y from the top). Offset scaled by 0x8460F0 (UI scale). Called by 0xC0420 only after IsHdGraphics; 0x76A7D0 also by the hover label 0x1FA9F0 and a debug HP drawer 0x6D7AC0 ("HP: %0.2f%%"). Called: health bars |
| IsHdGraphics | 0x846210 | () -> bool: `[renderer]+0xB9`. Legacy graphics project with the 2D camera (UpdateCamera 0xB9B90: player pixel pos 0x34AF60/0x34AFB0 minus half the legacy size 0x22A0D68/6C) |
| GetRenderWidth / GetRenderHeight | 0x7F510 / 0x7F4A0 | () -> int: the size ProjectUnitToScreen's points are in (legacy: the legacy size globals). Scale to the window by display / render |
| CLIENT_IsHostile | 0x971E0 | (unit, other, -1) -> nonzero when other is an enemy: pets resolved to their owners (0x13ABA0), players by party hostility, monsters by alignment (0x2F32D0 / 0x2F4190). In town (0x2F0750: levels 1/40/75/103/109) it answers 1 for NPCs, so the MonStats NPC flag is checked too |
| can be attacked (unit flag 0x4) | 0x34F5A0 / 0x34DB10 | getter / setter of unit+0x124 bit 2, among one-bit accessors (0x34EE20 sets 0x2 selectable, 0x34E5A0 0x8). The client's monster init (0x1A3940) and class change (0x1A33B0) set it from MonStats2 isAtt (flags byte +5 & 2; isSel byte +4 & 8 gives 0x2), a revive (0x99900) again; four entries of the client's skill function table 0x235F570 clear and set it; objects take Objects+0xE8 (0x1CB410). Off in the data (killable, not NPC): hydra1..3 (351..353, cast by the Council Members and baalhighpriest, on the caster's side), the Assassin's traps, the Druid's vines and hawk, critters, the trap-* missile monsters. The hover's life (0xD0640) and the life percentage (0x97490) read it too. Read (the field): health bars |
| UI_SetPanelSide | 0xB9C20 | (side): stores the open side panels at 0x2A26BB0: 0 none, 1 right (inventory, skill tree), 2 left (character, quests, waypoints, stash, cube, NPC trade), 3 both; also shifts the legacy view. Its callers 0xC00B0 / 0xC01D0 test the classic UI vars (bytes at 0x2A2ADA0 + UI id) |
| GetUnitByIdAndType bucket walk | 0x9F270 | D2RCore replaces its first 7 bytes with a jump (adds an id == -1 check); the walk links units at +0x158. Client tables: `dClientUnitTable + type * 0x400`, 128 buckets by id & 0x7F |
| CLIENT_GetUnitName | 0x9A1B0 | (unit) -> UTF-8 name: a monster's own name (0x985A0, uniques) or LANG_GetStringById(MonStats+0x36); objects and items from their records. 0x1E1CF0 then drops a leading "[xx]" grammar tag (localized names) |
| MonsterHealthPanel | 0x1F8320..0x1F8F6A | the hover bar (HUDMonsterHealth, layout hudmonsterhealthhd.json): TargetAttached (controller) or ScreenAnchored (keyboard); fed by the hover info builder 0xCEA50 (name, kind, immunities, life via 0xD0640) |
| LANG_GetStringById / ByKey | (loader) | the game's calls go through the `.maho` jump table (0x3E2B730/0x3E2B736): D2RCore owns string lookup, so strings cannot be hooked or patched in the exe |
| TreasureClassDropCore | 0x4404F0 | (game, source, killer, tc, forcedQuality, ilvl, skipNoDrop, Unit** out, int* outCount, outMax): the TC walker (a 64-deep stack of TC levels). Monsters: 0x441300 (GenerateDrops, TC upgrade by level) ← 0x447B80 / 0x4479A0; objects: 0x5930B0 (a 6-slot array); the droptest/dropsim commands 0x40B730/0x415310 pass a monster and an array. Per pick RollItemQuality 0x4421B0 then 0x444680 (makes the item on the ground beside the source, mode 3), stored in out, stops at outMax (6 with no array). skipNoDrop ignores NoDrop at every level unless the TC has flag 0x20. Hooked: loot sessions |
| monster drops | 0x441300 / 0x447B80 | GenerateDrops (game, monster, killer, u16 tc id, forcedQuality, ilvl, skipNoDrop, out, outCount, outMax): the TC record by id and level (0x397290 (bank, id, level)), then the drop core with the monster as its source. The level is the monster's (stat 12), taken outside the classic bank and Normal for a row that is neither noRatio nor boss, and for any terrorized monster (state 0xBD, 0x3AF200); a Herald or a Herald's minion (kinds 0x600) adds its TC level boost (0x542EA0 copies the Herald record, monster data +0x78, 0x38 bytes: the boost at +0x98). 0x447B80 (game, monster, skipNoDrop, record: the killer at +0x10) takes the id from D2RCore's chooser (`.maho` 0x3E2B45A (game, monster, difficulty, terrorized)), or the quest TC (MonStats +0xC6 + difficulty * 0x10) for a killer (a pet's owner) whose quest (row +0xF0) is not done (0x325C50 on the player's quest record). Read: extra drops for elites |
| drop item maker | 0x444680 | (game, source, class, quality, file index (stack), request flags (stack)) -> item or 0, the drop core's one way to make an item: the source's position and room (0x34B440), a free spot beside it (D2RCore's, `.maho` 0x3E2B2EC), then a request on its own stack (source, game, class, mode 3, version Game +0x128, item level 0x43E8A0 (source, 0), quality, the file index at +0x50, the flags at +0xC0) to CreateItemEx (game, request, 0), whose answer it returns; 0 when there is no spot. The drop core skips a 0 (`if item != 0`: gold's amount, the out array, the count). The quality comes from RollItemQuality 0x4421B0 (game, source, killer, ilvl, class, the TC's ratios) unless the drop core was given one, or the TC entry names a unique or set row (entry flags 1 / 2: quality 7 / 5 and the row + 1). Not hooked (CreateItemEx is): the loot filter asked first |
| monster kind test | 0x38E870 | MonsterUniqueTestFlag (unit, u16 flags) -> bool (al): a monster (type 1) whose unit data +0x1A has one of the flags; 125 callers. The game's debug panel 0x167130 names the kinds: 0x10 Minion, 0x400 Herald Minion, 4 Champion, 8 Unique, 2 Super Unique, 0x200 Herald, and Boss by 0x3AEFF0. Masks the game asks: 0xE (super unique, champion or unique), 0xC, 0xA, 0x600 (a Herald or its minion), 0x410 (either kind of minion); 0x20, 0x40, 0x80, 0x100 only alone. A TC condition's `MonsterTestElite` takes unique / champion / super / minion / herald / heraldminion (parsed at 0x35F590). D2RCore's kill counting leaves out kinds 0x600 too (monster data +0x1B & 6). Not called: the plugin reads the field (health bars, extra drops for elites) |
| monster kind makers | 0x38E710 / 0x49AB40 / 0x49EDB0 / 0x499BA0 / 0x499210 | the setter (unit, u16 flags, u8 on), 44 callers. A champion (0x49AB40 (game, monster, mod)): unless it is one already, the unique bit 8 (0x49EED0, which also counts it in its level's runtime +0x2C8) and then 5, so a champion's kind is 0xD, and its mod goes into the first free of its 9 mods (0x38E310). A random unique (0x49EDB0): 8 and 1. A super unique (0x499BA0): 2 and 8. A minion (0x499210): 0x10, and 0x400 when its boss is a Herald, the boss's 9 mods copied and their makers run (table 0x2395FE0 by mod). Read: extra drops for elites |
| boss test | 0x3AEFF0 | (MonStats row or 0, unit) -> the row's (0x976E0 (bank, class)) flags byte +0x3C & 0x40. The masks come from a dword table of single bits (0x1D996D0 + 4 * bit; GenerateDrops reads 4 noRatio and 0x40 boss there). Not called: the plugin reads the row through the loader |
| MonStats field table | 0x39DB30 | the builder: descriptors 0x20 bytes on the stack {name, type, bit, offset}: type 4 a byte (PartyMin +0x62, Rarity +0x64, sparsePopulate +0x67), 0x1D a flag bit of +0x3C (primeevil 7, CannotHerald 31). The loader's compiled monstats.bin (a "D2RLBIN" file with two tables: the ids, 2 bytes a row, then the rows) lists every column's offset, the same layout. Read: extra drops for elites (Rarity) |
| item generation | 0x43CD50 / 0x43EF10 / 0x442D60 | CreateItemEx (game, request, flag) allocates into the room, then init 0x43EF10 (quantity, durability, 0x442900 armor defense), then the quality/affix generator 0x442D60: quality step 0x444200, "must be" fixes (ItemTypes+0x16 normal-only, always-magic), per-quality generators (0x442C60 magic, 0x58BBA0 rare, 0x58A120 crafted, 0x58A5A0 superior, 0x443910 unique, 0x58ACD0 set; a failed one falls back to normal, 0x442820, which runs the quality step again), then the ethereal gate, then sockets 0x441FC0 (qualities 1..3, at 0x443578). Hooked (CreateItemEx): the unit seed of a new armor under perfect rolls |
| item quality step | 0x444200 | (item, request) -> quality; the requested quality (request+0x40) wins for expansion items. Request: +0x3C restore (re-make an existing item), +0x40 quality, +0x54 item flags (restore), +0xC0 flags (2 never ethereal, 4 ethereal for sure, 8 no sockets, 0x10 sockets for sure; 0x10 is read by the socket roll only). The helper 0x43D530 (..., p6 no sockets, p7 never ethereal, ...) builds a request with 8 / 2 and calls CreateItemEx: vendor 0x541125, gamble 0x541CEC and quest 0x5175B9 pass never-ethereal, starting items 0x425B34 (and 0x4FB843, 0x542CDC) both. The generator calls it for every item (an expansion item with a quality asked for gets that back at once, without a roll) and takes the request's quality after it when there is one (+0x40 != 0). Hooked: all superior / all ethereal / all socketed |
| socket roll | 0x441FC0 | (game, item, request): quality < 2 returns (so normal and superior); the base must have Items +0x147 (0x373610) set, +0x142 stackable (0x374660) clear and max sockets (0x36EAD0) > 0; n = min(that, 3 / 4 / 6 by Game+0x104 difficulty); a classic item (item version, data +0x40 via 0x3730D0, < 100) of type 3 (body armor) never. Then a roll of 100 on the item's seed (0x36CC80, 0x153B00) and the request flags (0x44211A): 8 none, 0x10 counts the roll as 0; sockets when the restore's socketed flag (+0x54 & 0x800) is set or the roll is below 33. Item flag 0x800, count = item data +0x10 (0x36CEF0; the item's initial seed, set by 0x36DD10) % n + 1 (version 0: min(n, 3), a helm 2), then 0x375560. Debug count 0x2AA6A78. Checked (signature): all socketed |
| max sockets / set sockets | 0x36EAD0 / 0x375560 | (item) -> u8: the item type's ItemTypes row (0x36A4B0) +0x18/+0x19/+0x1A MaxSockets1..3 by the item level (data +0x38) against +0x1B/+0x1C (thresholds 25/40), capped by Items +0x148 gemsockets. (item, count): cap Items w*h (+0x11E * +0x11F, at most 6), by quality (magic 4, rare 2, crafted 3, set/unique 1 or their stat 194), then max sockets; count clamped to 1..cap, item flag 0x800, stat 194 (SetUnitStat). Not called |
| property roll | 0x3D5860 | (unit, a, b) -> value in [min, max] (item seed; debug switch 0x2A9BD90; display items flag 0x80000000 -> max). Dispatcher 0x3D4620 (Properties func ids at record+0x18, 37-entry table 0x2386AB0; funcs 11/19 jump into D2RCore); property groups (kind 1) 0x3D4DB0 -> 0x3D1630 / 0x3D18F0. Choice call sites (a pick, not a value): 0x3CCAA1 skill-rand, 0x3CC8F1 randclassskill, 0x3CD141 func 25, 0x3D16EB / 0x3D19AF group picks, 0x3D4F7F / 0x3D17B3 / 0x3D1A7A a group entry's param (pick modes 0 / 1 / 2; not among them until 2026-09-29, when every Wraithstep came out "+1 to Chaos Skills", the top of the tabs 21..23). No step when a = b. Hooked: perfect rolls |
| property groups | 0x3D4DB0 / 0x3D1630 / 0x3D18F0 | the dispatcher for a mod whose property's high word is 1 (the group id its low word): the group record (0x2D9670 (bank, id)): +0x04 u8 pick mode, eight entries 0x18 bytes apart from +0x08 (+0x00 i16 property, below 0 none; +0x04 / +0x08 ParMin / ParMax; +0x0C / +0x10 ModMin / ModMax; +0x14 Chance, at least 1), listed with their chances added up by 0x3D2FD0. Mode 0 every entry in order; modes 1 (0x3D1630) and 2 (0x3D18F0) the mod's min..max picks (0x3D16A3 / 0x3D1962; at least 1 when the two are equal), each a roll of the chances together (0x3D16EB / 0x3D19AF): mode 1 takes the entry out, mode 2 marks it (a pick that comes to it again makes nothing). Per entry the param is rolled in ParMin..ParMax when they differ (0x3D4F7F / 0x3D17B3 / 0x3D1A7A), then the entry goes to the property dispatcher 0x3D4620 as {property, param, ModMin, ModMax}. 0x3D3F10 rolls a group's count alone (its answer unused). 3.3 (rotw and base alike): 33 groups, modes 1 and 2; one entry has a param range, skilltab-war's skilltab 21..23 (Wraithstep, UniqueItems 413): the Warlock's Demon, Eldritch and Chaos tabs (charstats StrSkillTab1..3; func 10 makes the stat's param tab + 5 x (tab / 3), the class x 8 plus its tab). The others: magdam-rand (Opalvein), roguesbow-affix1 (Piercerib: Fire Arrow or Cold Arrow), the six sunder kinds' Affix1..6 (the Crafted sunder uniques 1..1, the sunder magic prefixes 0..1, not spawnable). Read: perfect rolls (the picks and the params are choices; a count and an entry's value go to their tops) |
| armor defense roll | 0x442900 | (item): minac (Items+0xD4) + roll(maxac (+0xD8) - minac + 1), inline copy of the roll on the unit's RNG, stat 31; it asserts that the item is type 4. Not hooked (it was until 2026-09-28, stat 31 written after it): the unit seed is chosen to roll the top |
| item affix steps | 0x442C60 / 0x58AC70 / 0x58A120 | the generator's (0x442D60) steps that fill the item's affix slots, by quality: 4 magic, 6 rare, 8 crafted. A failed set (durability x2) or rare falls back to magic through 0x442750 (quality 4, then a tail jump into 0x442C60), then superior 0x442680, then normal 0x442820; a failed unique becomes rare (durability x3, quality 6) through 0x58AC70. Magic (Unit** item, request): the prefix (request +0xA8; -1 skips it) picked with must when forced (> 0), else on the picker's coin; then the suffix (+0xB4) with must when no prefix came, else a coin (-1 skips it beside a prefix): 25 % both, 25 % prefix only, 50 % suffix only. Rare (item, request): 0x58AC70 tests ItemTypes +0x15 (can be rare), then version >= 1 -> 0x58BBA0, else the classic 0x58BEE0 (4..6 as seed % 3 + 4, its own group check, 250 tries). 0x58BBA0: two rare names (D2RCore's SelectRareName, `.maho` 0x3E2B03A), then the count: a jewel (item type 58, Items +0x12E via 0x372C90) 3 + seed & 1, else the table 0x1D3EE70 {3,4,4,5,5,5,6,6} by a roll of 8 (0x367160); per count a coin for the side (0x367190), at most 3 a side, a failed pick closes its side without using up the count; picks straight to the `.maho` picker with must, no apply, the request's forced ids walked per side (+0xA8 / +0xB4, one per affix made); then every slot's mods (0x3D3670) and 0x58A050 (class skills). Crafted (item, request): rare names, count = max(roll of 5, a minimum by the request's item level +0x18: 1, 2 above 30, 3 above 50, 4 above 70), a coin per affix, its own group check (250 tries), a failed pick loses its count. Hooked: max affixes, best affixes (which item a step makes). The rare step's two picks (`call` the `.maho` slot: 0x58BD3B suffix, 0x58BDBC prefix) are patched to call the wrapper 0x58AE00: best affixes |
| affix picker | 0x58AE00 | (item, spawnable only, must, apply, prefix, forced id) -> the 1-based id in the combined MagicSuffix/MagicPrefix/AutoMagic table, or 0: item version >= 1 -> `.maho` 0x3E2B02E = D2RCore's SelectExpansionMagicAffix (D2RCore RVA 0x815400), else 0x3E2B034 = SelectClassicMagicAffix (0x815CD0). Expansion: the item's seed (data +4) advances, and without must only a low bit of 1 goes on (the magic coin); alvl from ilvl, Items +0x10D qlvl and +0x150 magic lvl (the usual formula, 1..99); the side's range of the affix table (data tables +0x15E8 rows 0x8C, +0x1600 suffixes, +0x1608 prefixes, +0x1610 automagic): spawnable +0x54, expansion rows (+0x22 >= 100) on expansion items only, level +0x58 <= alvl <= maxlevel +0x60 (0 none) unless forced, rare +0x64 for rare/crafted/tempered items, itype/etype (0x3D4220), frequency +0x82, classspecific +0x66 against the item's class (0x374130, 8 none), group +0x5C not among the item's prefixes' and suffixes'; weight frequency (x level with a magic lvl), a roll of total + 1 (the last candidate one more). apply -> 0x3D3670 (0, 0, item, row, 0, 0). Classic: alvl = ilvl + 2, uniform, no group or max-level test. 0x58AE80 is its automagic twin: a seventh argument, the AutoMagic group, handed on to D2RCore's picker (0x58AE00 passes 0), which then picks among the automagic rows of that group (the seed model's port, checked against the game: no spawnable test, the rest as above; the automagic groups are no prefix's or suffix's). Its one caller is the generator's automatic affix (0x4435D6: spawnable 0, must 1, apply 0, prefix 1, forced 0, the Items row's +0x100 group), which writes the id answered (0x36D7B0) and applies its row (0x314250 (bank, id): the combined table's row, id 1..count; 0x3D3670 (0, 0, item, row, 0, 0)). Hooked (0x58AE80): best affixes. D2RCore's class test (asm): a row passes when classspecific is 0xFF, the item's class is 8, or they are equal; the group test applies to every row. The rolls: a step for the coin (the pick goes on when the low bit is 1, or with must); the candidates in table order; a second step, lo % (total + 1); the walk takes each weight off until it is below 0, the last candidate when none is. Hooked: best affixes; called: max affixes |
| affix slots | 0x36DB40 / 0x36DBE0 | set prefix / set suffix (item, id, slot 0..2): `mov [data + slot*4 - 0x18]` / `- 0x0C`: the six ids precede the item data's +0x00 (getters 0x36CD50 / 0x36CDE0; rare names 0x36E030 / 0x36E0B0). Item version: data +0x40 (u16, 0x3730D0). Called: max affixes |
| affix table | 0x3142F0 | (u8 bank) -> the bank's data tables (0x300A90) + 0x15E8: +0x00 rows (0x8C bytes each), +0x08 count, +0x18 / +0x20 / +0x28 where the suffixes, prefixes and automagic rows start, in that order (an id is the row index + 1). The exe's own picker 0x58C940 (no callers now: D2RCore replaced it) and D2RCore's read the three starts; the row getter 0x314250 (bank, id) reads +0x00 / +0x08. Called: best affixes |
| affix fits item | 0x3D4220 | (item, row) -> int: a classic item (version < 100) that stacks (0x374660) or is thrown (0x374710) takes none; a socket affix (the first property's Properties row, 0x3D3210, stat1 +0x20 = 194) only a base that can have sockets (0x373610, 0x36EAD0); then the row's etypes +0x78 (i16[5]) and itypes +0x6A (i16[7]), each through the item-type test 0x373890. D2RCore's picker asks it of every row (through its routine table). Called: best affixes |
| affix apply | 0x3D3670 | (kind, source, item, row, 0, 0): a jump table on kind 0..5; kind 0 (a magic affix) walks the row's 3 mods (+0x24, 0x10 apart; a property below 0 skipped) through 0x3D5060 (the property functions, which roll with 0x3D5860). Called by D2RCore's picker when asked to apply, and by the rare and crafted steps' final loops (the row from 0x314250). Derived from the rare step's loop (both calls, 0x58BE5D / 0x58BE8F). Called: best affixes |
| affix row | | 0x8C bytes, MagicSuffix / MagicPrefix / AutoMagic alike: +0x00 name char[32], +0x22 u16 version (100+ expansion), +0x24 three {int32 property, param, min, max} (property -1 unused), +0x54 spawnable, +0x58 level, +0x5C group, +0x60 maxlevel, +0x64 rare, +0x65 levelreq, +0x66 classspecific (0xFF none), +0x6A itypes i16[7], +0x78 etypes i16[5], +0x82 frequency (0 for every classic-only row). Matches the loader's compiled magicsuffix.bin / magicprefix.bin. Read: best affixes |
| Properties row | | 0x30 bytes: +0x00 u16 id, +0x02 set u8[7], +0x0A val u16[7] (class skills: the class), +0x18 func u8[7] (0 unused), +0x20 stat u16[7] (0xFFFF none). Matches the loader's properties.bin; via the loader (TableId::Properties). Read: best affixes |
| game RNG | 0x367110 / 0x153B00 / 0x367160 / 0x367190 / 0xF32B0 / 0x2EF880 | multiply with carry on {u32 lo, u32 hi}: t = lo * 0x6AC690C5 + hi, lo = low 32 bits, hi = high 32. Seed (0x367110): lo = the seed, hi = 666. Roll (seed, n) 0x153B00: n <= 0 answers 0 without a step; else one step, lo & (n - 1) for a power of two, lo % n otherwise. 0x367160 (seed, n): a step, lo & (n - 1). 0x367190 (seed, base, n): base + (lo & (n - 1)). 0xF32B0 (seed, base, n): base + roll. 0x2EF880 reads lo. Inline copies everywhere (the defense roll, the pickers) |
| unit and item seeds | 0x34EDD0 / 0x34B4E0 / 0x34A1E0 / 0x36DD10 / 0x36CEF0 / 0x36CC80 | a unit's seed: unit +0x30 (set / get), its RNG unit +0x28. An item's own seed: item data +0x10 (set / get), its RNG item data +0x04. Unit creation 0x4905A0 (wrapper 0x490410 (type, class, x, y, game, room, ...)) steps the game's RNG (Game +0x1A8 {lo, hi}) once for the unit seed of every unit but a player, and once more for an item's seed: two outputs in a row, U then S = low32(U * a + carry), carry < a. 0x36DD10 has six callers: unit creation, CreateItemEx, the generator's fallbacks; the item readers never call it |
| CreateItemEx seeds | 0x43CD50 | (game, request, use seeds): with the third argument or request +0x3C (restore) set, the unit seed is request +0x58 and the item seed request +0x5C, both RNGs seeded again; nothing else reads the argument. The request is 200 bytes (0x43DAC0 (game, request, item) builds one from an item: both seeds, level, class, mode, quality, quantity, durability, file index, flags, name; restore left 0 for its caller 0x4FC230). 0x421680 builds a restore from a 0x71-byte record that has both seeds (+0x28, +0x2C). Unit creation draws its two numbers from the game's RNG either way, so the stream moves on as it would. Request: +0x00 source unit, +0x10 game, +0x18 item level, +0x20 class, +0x24 mode (3 the ground, 4 a hand: vendors, the cube), +0x28 / +0x2C x / y, +0x30 room, +0x3A u16 version, +0x3C restore, +0x40 quality, +0x44 quantity, +0x48 / +0x4C durability, +0x50 file index asked for, +0x54 item flags (restore), +0x58 unit seed, +0x5C item seed, +0xA8 / +0xB4 prefix / suffix ids asked for, +0xC0 flags (1 set 29 allowed, 2 never ethereal, 4 ethereal, 8 no sockets, 0x10 sockets, 0x20 the item level as class skill bonus). A set or unique item of 3.3 also gets where and when it was found (item flag 0x10000000, the source's name, the minute). Hooked: the unit seed (for a new armor under perfect rolls the original is called with 1 and the chosen seeds in the request, which gets its own back after) |
| item init | 0x43EF10 | (game, Unit** item, request, run the generator): rolls on the unit's RNG, not the item's: gold (type 4) its amount; for armor (0x373890 with type 0x32) durability = maxdur / 2 + roll(maxdur / 2) (Items +0x121; stats 72 / 73; stats 20 and 67 set before), then the defense roll 0x442900; for weapons (0x2D) a stack's quantity, then the durability; other stackables (0x374660) quantity; then the picture variant (0x372F80 count, 0x36D9B0). A low quality item's step (0x58A5D0) rolls its lowered durability on the unit's RNG once more and keeps 75 % of the base defense (at least 1). Then the generator 0x442D60, whose steps roll on the item's RNG: quality step, affix picks, property values (0x3D5860: no step when min = max), class skills, ethereal, sockets, automagic. A drop's quality is rolled before, on the source's RNG (0x4421B0) |
| what a save keeps of the seeds | 0x37D140 / 0x378860 | the writer's save form writes 32 bits of unit +0x30 (0x34B4E0 at 0x37D68A); the reader puts them back (0x34EDD0 at 0x378C55) and seeds the unit's RNG. Neither touches the item's own seed, and the network form has no seed. D2RCore loads items: its routine table has the reader's entry 0x374BF0, unit creation 0x490410 and CreateItemEx, not the item seed's setter (a direct write from D2RCore is not excluded) |
| class item skills | 0x58AF50 | (game, item, ilvl, first skill, skill count, bonus): from 0x58A050 (after the magic, rare and crafted steps, and in the normal step 0x443670) for an item with a class (0x372B80 < 8); the bonus is the request's item level when its flags have 0x20, else 0. Count: roll 100 + bonus > 90 -> 3, > 70 -> 2, > 30 (or any bonus) -> 1, else none. Per line: a tier by ilvl (1, 2 above 11, 3 above 18, 4 above 24, 5 above 36 (4 classic)) moved by a roll of 100 (> 80 +1, <= 30 -1, <= 10 -2; min 1, inferior at most 4), a skill of that tier (Skills +0x214 reqlevel lists), 6 tries, no repeats; its level: roll 100 + bonus/2 >= 90 -> +3, >= 60 -> +2, else +1 (an expansion inferior item +1); stat 107. With Game+0x106 = 3 the skill lists go by reqlevel 1 / 6 / 12 / 18 / 24 / 30. Hooked: perfect rolls |
| superior step | 0x58A5A0 / 0x58B9E0 | (item, request) -> 1 when the item got a kind: the wrapper asks the item's version (unused) and jumps to the body. The QualityItems table of the item's bank (0x313DE0: data tables +0x1640 {rows, count}, 0x2C bytes a row) and the Items row; the count N is the table's, or 4 (`mov ebx, 4`) for a base whose type is thrown (0x374710: ItemTypes +0x10 of the Items row's type, +0x12E) or that has no durability (Items +0x122). Then the loop, inline on the item's RNG: a roll of N (one step; `& (N - 1)` for a power of two, else `% N`), again while the kind it came to was tried (a byte a kind on the stack, ten of them), the row (0x313D70), the test 0x3D40D0; a kind that does not go on the item is marked tried, and with all N tried the step answers 0 (the generator then makes a normal item). The first that goes on it: the file index (0x36DC80: item data +0x34) is the kind, its two mods are applied (0x3D3670 with 1), then the class skills (0x58A050). Two callers: the generator (0x443024) and the fallback of a failed magic step (0x442680, which seeds the item again, runs the quality step and jumps here). 3.3's eight kinds, by row: 0 attack rating 1..3, 1 enhanced damage 5..15 %, 2 enhanced defense 5..15 %, 3 attack rating and enhanced damage, 4 durability 10..15 %, 5 attack rating and durability, 6 enhanced damage and durability, 7 enhanced defense and durability; 0, 1, 3, 5 and 6 go on weapons, 2 and 7 on armor, 4 on both. So an armor has one of 2, 4 and 7 (a third each), a weapon one of six, and a bow, crossbow, throwing weapon or Phase Blade (no durability: 79 of the 306 weapon bases) one of 0, 1 and 3. Read (signature; its count and its call of the test are checked): a superior item's kind |
| superior kind test | 0x3D40D0 | (item, QualityItems row) -> int: a row has a byte per sort of item, +0 armor, +1 weapon, +2 shield, +3 scepter, +4 wand, +5 staff, +6 bow, +7 boots, +8 gloves, +9 belt, then two mods from +0x0C ({int32 property, param, min, max}; -1 none). A weapon (0x373890 with 0x2D) whose type (Items +0x12E, 0x372C90) is none of 0x18..0x1B (scepter, wand, staff, bow) and not 0x23 (crossbow) goes by the weapon byte, an armor (0x32) whose type is none of 2, 0xF, 0x10 and 0x13 (shield, boots, gloves, belt) by the armor byte, any other item by the byte of its own type. One caller, the step (0x58BB16). The item type test 0x373890 reads a bit per type of what a type counts as (data tables +0x1360 the dwords a type, +0x1368 {dwords, count}; the single-bit masks 0x1D996D0), for the Items row's type and then its type2. The data tables of a bank: a pointer in the array 0x2A9A580 (16 bytes a bank; the getter 0x300A90), with ItemTypes at +0x1348, Items at +0x15A0 and QualityItems at +0x1640 ({rows, count} each). Hooked: a superior item's kind |
| enhanced damage | 0x3D0720 | property function 7 (dmg%): the value is rolled (0x3D5860) when the dispatcher hands it none. On a weapon for which that percentage of its top damage (the greater of Items +0x110 and +0x114) comes to nothing, the maximum damage function (6, 0x3D0250) is run with 1 instead: "+1 to Maximum Damage" on a superior wand or dagger (a top damage of 6 or less at 15 %). Else stats 17 and 18. Read: a superior item's kind |
| generator: the order of the rolls | 0x442D60 | all on the item's RNG, from (S, 666). The quality step 0x444200 rolls only for a request without a quality (or a classic item; else it answers the request's at once). "Must be": ItemTypes +0x14 magic -> at least 4; +0x15 rare = 0 and quality 6 -> 4; Items +0x139 -> 7; ItemTypes +0x16 normal -> 2. Then the quality's step; one that fails starts the stream again from (S, 666) and the next lower step runs: 7 -> 6 -> 4 -> 3 -> 2, 5 -> 4 -> 3 -> 2, 6 -> 4 -> 3 -> 2, 8 -> 2. Then ethereal: for an expansion item that is a weapon (0x2D) or armor (0x32), uses durability (Items nodurability +0x122 = 0, durability +0x121 != 0, not indestructible, stat 152), not quality 1 or 5, not quest, request flag 2 clear: roll(100) <= 4. Then sockets (quality 2 / 3): roll(100) <= 32, count = S % n + 1. Then the automatic affix (Items +0x100 auto prefix group != 0; qualities 1, 2, 3, 4, 6, 8, 9: not set or unique) through 0x58AE80. Read: the seed model (tools/seedmodel) |
| unique, set, superior steps | 0x443910 / 0x58ACD0 / 0x58A5A0 | unique: the rows of the base (UniqueItems +0x28 code) that may come (not disabled +0x2C bit 2, version +0x24, level +0x38 <= item level, ladder +0x8C); among them the row the request names (+0x50 - 1) is taken as it is, else the ones picked among (spawnable bit 0, DropConditionCalc +0x90 none or holding for the request's source) by one roll of the rarities' total (+0x34, read as int32, at least 1 each) and the walk. Rows that came already are picked among too: the step fails when the row it came to has its bit in Game +0x3638 (a quest item's does not count), and sets the bit of one it makes unless the row is nolimit (bit 1). Nothing to pick among: it fails, but for an item that must be unique (Items +0x139, 0x374870), which stays unique without a row. Then the row's 12 mods (+0x98). Set: SetItems +0x28 code, +0x34 level, +0x38 rarity (i32), +0x30 set (29 needs request flag 1), 9 mods (+0x98) and 10 bonus mods (+0x128); in its walk of the rows (expansion items) a row the request names (+0x50 - 1) is taken where the walk comes to it, past the code, version, disabled, ladder, level and set 29 tests but before the spawnable and calc ones (the jump to 0x58C589), else one roll of the candidates' rarities (0 counts 1) on the item's seed; none: nothing set (the generator makes it magic). Superior: roll(N) (N the QualityItems rows, 4 for a throwable or no-durability base) until a row that fits (0x3D40D0) and was not tried, its 2 mods (+0x0C), class skills. Rows: UniqueItems 0x15C, SetItems 0x1CC, QualityItems 0x2C. Read: the seed model (tools/seedmodel) |
| rare names | 0x314390 / D2RCore 0x816270 | the rare name table (u8 bank) -> data tables + 0x1618: +0x00 rows (0x48 bytes), +0x08 count, +0x18 the suffix names, +0x20 the prefix names (to the end); a row +0x0E version, +0x10 itypes i16[7], +0x1E etypes i16[4]. SelectRareName (`.maho` 0x3E2B03A): per list with a name for the item, one step, lo % count; the prefix list first. Not called |
| property functions | 0x2386AB0 | the table by Properties func; the dispatcher 0x3D4620 stops at a property's first empty function, and hands each function the first one's value. 1, 2, 13, 21, 22 roll; 3..10 and 24 use the first's value (they roll only when it is 0); 11 D2RCore's, no roll; 12 and 36 a pick; 14 sockets (the param, else a roll); 15 the min, 16 the max, 17 the param (else a roll); 18, 20 (indestructible, sets stat 152), 23 no roll; 19 a charged skill (D2RCore 0x833CF0: one step, lo % (n - n / 8)); 25 and a property group (the mod's high word 1) roll picks the model does not follow. Read: the seed model (tools/seedmodel) |
| imbue (NPC item services) | 0x4FC230 / 0x36AED0 | the server's handler of an NPC's item service (game, player, packet, ...): the NPC's record (0x3971A0) +0x0B says which, 1 the imbue (others add sockets, item flag 0x800, or a name). The imbue: the game's test of the item 0x36AED0 (not gold (Items +0x12E = 4), item flag 0x1000 clear, Items +0xE4 bitfield1 bit 0 (all armor, all weapons but the throwing potions and the Malus), in the classic bank no throwable type, no quest item but Wirt's Leg (code "leg "), nothing in its sockets (0x376AB0), not socketed (0x800), quality below 4); then a request built from the item (0x43DAC0), its flags \|= 0x20 (the class skill bonus) and 4 for an ethereal item (0x3740A0) or 2, version Game +0x128, quality 6, item level = the character's level (0x43E8A0: stat 12) + 4; the item removed (0x43EC10) and CreateItemEx (game, request, 0): new seeds from the game's stream. The only caller that sets 0x20. Not called |
| who makes items, and with which flags | | drops 0x444680: 0. The helper 0x43D530 (vendors 0x541125, gambling 0x541CEC, quest rewards 0x5175B9, starting items): 8 (no sockets) and 2 (never ethereal) by its arguments. The cube 0x5269C0: by the recipe's output (+0x5E bit 2 with no socket count: 0x10, else 8; bit 4: 4, else 2), affix ids asked for from +0x6B on. The imbue: 0x20 with 4 or 2. 0x4121B0 (a console command) any. Nothing makes a magic, set or unique item ethereal for sure |
| enhanced defense | 0x3CFA60 / 0x3D5AC0 | property function 2 (ac%) calls 0x3D5AC0 (bank, item, stat) before its roll: for stat 16 or 31 on an armor (0x32) whose Items maxac (+0xD8) is not 0, a base defense (stat 31) not above maxac becomes maxac + 1. (Stats 17 / 22 and 18 / 21 on a weapon: the base damage stats from the Items row.) So an armor with enhanced defense among its properties (a superior one, "Sturdy" and the like, most uniques) has its top base defense plus one, whatever the unit's seed rolled; an ethereal one half as much again after. Seen in the game 2026-09-28 (24 of 24). Read: the unit seed's check |
| cube: an item changed | | "useitem,mod,exc / eli" recipes change the item's class in place and run item init 0x43EF10 again without the generator: durability and armor defense are rolled again on the unit's RNG; affixes and values stay. So what the item's seed rolled is rolled once, at creation |
| treasure class record | | 0x150 bytes, the rows of the bank's data tables +0xD78 (count +0xD80), the id the row: +0x00 group, +0x02 level (i16), +0x04 / +0x06 first and last ladder season, +0x08 / +0x0C quest (classic / the others), +0x10 / +0x14 the entries' chances together (classic / the others), +0x18 picks (below 0: the entries in their order), +0x1C NoDrop, +0x20 condition calc, +0x24 flags (0x20 noAlwaysSpawn: keeps its NoDrop, 0x40 has a condition, 0x80 has entries with one), +0x25 what the condition is (1 ladder season, 2 quest, 4 calc), +0x26 u16[6] mods (magic, rare, set, unique; ethereal and sockets, chances of 1024 the txt has no column for), +0x38 entries, +0x40 their count, +0x50 name. Entry 0x20 bytes: +0x00 / +0x04 where its share of the chances starts (classic / the others), +0x08 item class or treasure class id, +0x0C flags (1 names a unique row, 2 a set item row, 4 a treasure class, 0x10 not in classic, 0x40 has a condition), +0x0D (4: the item's DropConditionCalc, Items +0x104), +0x0E u16[6] mods (classic only), +0x1C the row named or gold's multiplier (256ths). 3.3: 41 rows have a calc (the Worldstone Shard and Terrorize consumables, the Heralds' extra items, Sunder Charms), none of the 7 noAlwaysSpawn rows has a NoDrop. Read: drop odds |
| drop core: the walk | 0x4404F0 | a stack of 64 frames {treasure class, picks left, mods, depth}; a treasure class's picks max(1, \|picks\|), a treasure class among the entries walked before the next pick (in the same frame when the picks are used up). A treasure class with a condition (flag 0x40) is walked only when 0x444920 holds. With flag 0x80 each pick lists the entries taken: one with flag 0x40 only when its treasure class's condition holds (0x444920 on the record by id, 0x397290) or its item's calc (0x3B5380), the others' shares moved up. Picks below 0: the pick's number (0, 1, ..) is the roll, and the walk of the treasure class ends when it reaches the chances together. Else NoDrop by the players (0x427AE0 a debug override, 0x506D10 the party near the killer, 0x4251E0 the game's count or the players setting 0x2AA6A30; NoDrop' = total x p / (1 - p), p = (NoDrop / (NoDrop + total)) ^ players) and a roll of total + NoDrop' on the source's seed; skipNoDrop rolls the total alone unless flag 0x20. Mods of a nested treasure class: the greater of its own and the frame's above. An item entry: the quality 7 / 5 and the row + 1 for an entry that names one, else the quality the caller gave, else RollItemQuality with the frame's mods; a roll of 1024 each against the ethereal and socket mods when they are not 0 (request flags 4 / 0x10); then the item maker, gold's multiplier and gold find. Read: drop odds (tests/test_dropodds.cpp has it a second time) |
| RollItemQuality | 0x4421B0 | (game, source, killer, ilvl, class, u16* mods) -> quality, on the source's seed: ItemTypes +0x16 -> 2; Items +0x139 -> 7; ItemTypes +0x14 with Items +0x13A -> 7; the ItemRatio row (0x313F20 (bank, class, difficulty, 100): the row whose Class Specific +0x43 says whether the type is a class's own (ItemTypes +0x20 < 8) and whose Uber +0x42 whether the base is an exceptional or elite one (a weapon or armor whose ubercode +0x88 or ultracode +0x8C is its code, not type 0x26, no quest item), the highest Version +0x40 up to 100; rows 0x44 bytes, 16 int32 in the order unique, rare, set, magic (value, divisor, min), superior, normal (value, divisor)); d = ilvl - Items +0x10D; magic find = stat 80 of the killer (a player or monster; a debug override 0x427AA0) plus its owner's (0x4A53C0), through 0x2F5C60 (a D2RCore jump); below -99: no magic item at all. Per quality in the order unique, set, rare (only ItemTypes +0x15), magic: chance = (value - d / divisor) x 128, with magic find x 100 / (100 + mf') where mf' is the magic find up to 10 and above it mf x k / (mf + k) (k 250 unique, 500 set, 600 rare; magic takes it whole); not below min; less mod x chance / 1024; the quality when the chance is below 1 or a roll of it is below 128. An always-magic type is magic after rare. Then superior and normal the same way without magic find, min or mods; else low quality. Read: drop odds (tests/test_dropodds.cpp has it a second time) |
| treasure class conditions | 0x444920 / 0x3B5380 / 0x403FE0 / 0x397290 | (game, tc, source, expansion) -> bool: by record +0x25: 1 the ladder test 0x403FE0 (game, first, last: false in a game of type 0 (Game +0x101) for a season that is set unless the game is a ladder one (+0x108) and first <= 0), 2 the quest (+0x08 / +0x0C below 42: done in one of three ways, 0x514FD0), 4 the calc (bank, source, calc id: the calcs at data tables +0xD90, run with the source's level). 0x397290 (bank, id, level) is the record by id, moved up its group by the level when one is given (the drop core gives none). Not called: asked through the drop core |
| generator: what a quality becomes | 0x442D60 | the quality is the quality step's, or the request's when it has one. Must be: an always-magic type (0x374270) -> 7 for a quest item, else 4 unless magic or better already; never rare (0x36AAC0 = 0) and 6 -> 4; Items +0x139 -> 7; normal only (0x374450) -> 2. Then the quality's step: low (0x58A5D0) and superior (0x58A5A0) only make weapons and armor (superior: a QualityItems row that fits, 0x3D40D0: there is one for every weapon and armor), else the normal step 0x442820 (seed set again, quality 2, request +0x40 = 2); magic fails to superior, then normal; set fails to magic (0x442750, durability x2 before); rare to magic; unique to rare with durability x3 (request +0x40 = 6), then magic, superior, normal. Then ethereal (flags & 2 clear, a weapon or armor with durability 0x373540, not quality 1 or 5, no quest item: flag 4, the restore's 0x400000, or roll(100) <= 4), sockets (quality 1..3 call 0x441FC0), the automatic affix. Read: drop odds |
| loot filter | 0x1C1120 / 0x1BF9D0 | IsShown (manager, item) -> bool (D2Client LootFilter.cpp): feature 8, the active profile's rules (0x108 bytes each, matcher 0x1BFE30): a show rule wins, hide only when a hide rule matches and no show rule; quest items (Items+0x13A) always shown. Of the item it reads four fields through one-line getters, the unit's type (+0x00, 0x34B9D0; 4 asserted), class (+0x04, 0x349860), bank (+0x1BD, 0x34A0E0) and data (+0x10, 0x34A500), and of the data the quality (+0x00) and the flags (+0x18 & 0x400800); the Items row (0x314110) gives the code (+0x80), the type (+0x12E) and through 0x372E80 the category record (+0 category, +8 equipment, +10 has tiers; tier 0x371D80). Gold alone (type 4, category 0, a rule with +0x100) is judged by a stat (14, against rule +0x104: above it for a show rule, below for a hide rule). Rule: +0x00 enabled, +0x30 kind (0 show), +0xB8 filterEtherealSocketed. So what it says of an item goes by class, quality and ethereal-or-socketed: asked about a stand-in (the loot filter asked first). The manager getter is one of 40 identical singleton getters: derived from the ground labels' call pair 0x15176EE. Profiles are `<Saved Games>\Diablo II Resurrected\<name>.fltr`, lootfilter.json maps characters to them |
| ground item removal | 0x34B440 / 0x2EF4D0 / 0x348B00 / 0x38EFE0 / 0x48FAA0 | get room, room removal notice (room, type, id), collision footprint, room unlink, SUNIT_FreeUnit (game, unit): the item-expiry routine 0x4416E0's sequence (dropsim 0x415310 does the same) |
| unique dropped bits | Game+0x3638 | a dword per 32 UniqueItems rows (row <= 0x1000); set by the unique generator at 0x443ECB (`or [rcx+rbp+disp]`), cleared by the cube path 0x527880. Item data: +0x00 quality, +0x18 flags (0x800 socketed, 0x400000 ethereal), +0x34 unique/set row |
| Levels table | | via the loader's DataTableService (bank rotw on 3.3): 138 rows of 396 bytes, LevelName key at +0xFD, the six MonLvl columns as int16 at +0x18 (found at run time by scanning the cow level's row for 28/53/78/28/64/81; the loader's `levels.bin` has a "D2RLBIN" header and the same rows) |
| GetMinionOwner | 0x4A53C0 | (monster) -> owner unit: monster data +0x30 (AI record) +0x60 game, +0x68 owner id, +0x6C owner type (0 player -> the game's player table, game+0x2230). Not called: the plugin reads the fields |
| ITEMS_ShouldRemoveOnUse | 0x308E80 | (item) -> bool in al: books (type 0x12) never; else the pSpell descriptor (0x236FE30, 12 bytes: {?, callback index, keep}) keep byte == 0. The client's use request carries the answer (8 senders, e.g. the belt 0x1C7A30, the TP hotkey 0x119B60 -> 0x161F20) and the server checks it (0x4F4196). Hooked: infinite scrolls and potions |
| HandleUseItemPacket | 0x4F40C0 | (game, player, packet, retry): the 0x26 request (36 bytes, item id +0x0A, remove flag +1). Identify runs in two passes (the first simulates a -1/+1 on a tome and defers). Removes the item only when the flag is set; a potion's belt row collapses only then |
| ExecuteItemUseEffect | 0x581680 | (game, player, item, target): callback table 0x1D3EA90 by pSpell (1 identify, 2 town portal, 3/4/5 stat potions, 6 cure states (antidote/thawing), 9 state potion (stamina), 10/11/12 quest rewards, 13 the respec), then a book's charge: `call` 0x5817CC to the quantity update with -1 |
| item quantity update | 0x46F090 | (game, player, item, delta): stat 70 plus the client update and the bound book skill. Callers: the use effect (-1), scroll into tome (+1, 0x4C1780), tome merge (+n/-n, 0x4C1940), stack merge (0x4759E0), shop refill (0x53DD05). Hooked: infinite tomes (only the use effect's call, by return address) |
| D2GAME_UseKey | 0x4712A0 | (game, player) -> int (eax): the inventory (0x34A360), none while an item is on the cursor (0x388A70), then the first item (0x388C10) whose type is key (0x373890 with 0x29), mode 0 (stored) and page 0 (0x36CFE0: the inventory grid, not stash/cube): quantity (stat 70) - 1 through AddUnitStat + the client update 0x47A190, or the last one removed (0x535F60, 0x43EC10); 1 when a key was taken. Callers: the chest 0x58E2F0 and the door 0x58E090 only. Hooked: infinite keys |
| object operate functions | 0x23A51C0 | table by objects.txt OperateFn (6 = TowerTome 0x5DC570): 4 chest 0x58E2F0 (mode 0 only; locked = the interact byte's bit 7 (0x34AD40): `class != 6 (Assassin) && !UseKey` -> sound 0x17 "I need a key"; else opens, 0xB the unlock sound), 8 door 0x58E090 (mode 6 asks UseKey, no class test) |
| inventory accessors | 0x34A360 / 0x388C10 / 0x388A70 / 0x38ABA0 | GetInventory (unit, file, line): unit+0x98 under a refcounted control block at +0xA0, else unit+0x90. Inventory +0x00 magic 0x1020304, +0x10 first item, +0x40 cursor item; item data (unit+0x10) +0x55 page (0 inventory, 3 cube, 4 stash, 0xFF), +0xB0 next item. The accessors `int3` on a wrong unit type (assert helpers), so the plugin reads the fields |
| item wear (a hit's) | 0x441B10 | (game, owner, item): the item's type (0x373890: any armor 0x32, any weapon 0x2D; neither -> nothing) and "has durability" (0x373540, whose entry is a D2RCore `.maho` jump), then a roll on the owner's seed (0x34A1E0, then 0x153B00 (seed, 100)): 10 % armor, 10 % a throwing weapon (ItemTypes +0x10 Throwable, 0x374710; no wear at all when Game+0x106 is 1), 4 % any other weapon. Durability (stat 72) - 1, capped at the item's max (0x2F4B60), set (0x2F7940) + the client update 0x47A190. At 0: armor not yet broken (item flag 0x100) goes to the break routine 0x46E680; a weapon whose 0x3730D0 is nonzero (a stack) loses one of its quantity (70) and gets its durability back (the last one zeroes both, then 0x42BDE0); another weapon stays at 0. Hooked: no durability loss |
| per-hit wear | 0x4526C0 | (game, attacker, defender): a player attacker's weapon (0x4242B0), and for a player defender one armor piece picked by weight from the body locations (7 {location, weight} at 0x2390C90, the count at 0x2390CC8), each to 0x441B10. Players only (type 0): a mercenary's gear never wears. Called only by the melee hit resolution 0x44B2B0 (game, attacker, defender, a4, u8 wear), which takes the queued melee record, runs ExecuteEvents (computeTotals 0, the call at 0x44B3FA), then the wear when its fifth argument is set (1 at most of its 36 callers: skill do functions, 0x446000); missile hits do not come through it. Not hooked |
| Impale's wear | 0x5590C0 | (game, player, weapon, chance %, amount (on the stack)): a weapon (0x2D) whose Items row +0x142 is set (0x374660: javelins and the like) loses one of its quantity on the chance (0x439320); any other with durability loses the amount (0 -> the break routine). Called only by Impale's start function 0x558590 (srvstfunc 7: the server start-function table 0x238E710 is indexed by srvstfunc; 4 the arrow skills 0x558250, 8 Strafe 0x558EE0), with Impale's calc2 (durability loss chance %) and calc3 (amount), skill row +0x194 / +0x198. Hooked: no durability loss |
| item break | 0x46E680 | (game, player, item): item flag 0x100 (broken) set, durability 0 sent to the client. Callers: 0x441B10, 0x5590C0, 0x42BCB0 (a weapon on the body at 0 durability and not broken; run by several skill start functions) and 0x42EAA0 (an item in a hand at quantity 0). Not called |
| timed state effect ("curse apply") | 0x433D20 | (effect) -> stat list or 0: a state with up to six stats put on a unit for a time. Its entry is D2RCore's (`FF 25` -> `.maho` 0x3E2A440 -> D2RCore ApplyWideTimedStatEffect 0x8339D0, body 0x3E7650): called, never hooked. Effect, 0x30 bytes: +0x00 source unit, +0x08 target, +0x10 skill, +0x14 skill level, +0x18 length in frames, +0x1C stat (-1 none), +0x20 its value, +0x24 state, +0x28 end callback. For a state the game's curse mask has (0x335A40 (bank, state): data tables +0x328, a bit per state): the target's curse resistance (stat 109, Fade's alone) at 100 or more answers 0, below that shortens the length; a target under Attract (state 57) answers 0; the list the target has with flag 0x20 (its one curse) is taken off first. 34 callers: every skill that puts a timed state on (most take the state from the Skills row, +0xA2 auratargetstate or +0xA0 aurastate), the shrines (0x591E20 .. 0x5920D0), the two curse steps. Called (through its callers): cannot be cursed |
| curse skills | 0x55C840 / 0x5794B0 / 0x434390 | server do-functions (table 0x238EA00, 8 bytes an entry, by skills.txt srvdofunc): 30 (0x55C840: the necromancer's curses, Defense Curse, Blood Mana, so every curse a monster casts by name: Weaken the Summoner and the succubus witches, Decrepify the Oblivion Knights (doomknight3, dkmag1..3) and Baal on his throne, Defense Curse and Blood Mana the succubi, their witches and Baal, Amplify Damage the witches) and 112 (0x5794B0 MonCurseCast, the Oblivion Knights': one of five by a roll on the caster's seed, table 0x1D3E798). Both build a context and walk the units in range (0x434390 (game, aura filter, caster, range, step, context)) with the step 0x55A7E0. Context: +0x00 game, +0x08 caster, +0x10 monsters only (Dim Vision, Terror), +0x18 skill, +0x1C level, +0x24 length, +0x28 six stats, +0x40 their values, +0x58 state. 59 (Attract) and 61 (Confuse) walk with steps of their own, 71 (Taunt) calls 0x55A7E0 itself: monsters only. Not hooked |
| curse skill step | 0x55A7E0 | (target, context) -> 1 when cursed: the target must be one the curse takes (a monster whose MonStats flags allow it, not a kind 0x20; can be attacked, alive, an enemy of the caster: 0x48E460), then the effect from the context and the routine above; on success the context's stats onto the list (0x2F7C00) and the clients told (0x438370). Hooked: cannot be cursed |
| Cursed monster modifier | 0x4A06B0 / 0x4346F0 / D2RCore 0x8208A0 | the modifier's cast is D2RCore's (CastAmplifyDamage, `.maho` slot 0x3E2AD28; the exe's own 0x4A0970 is its twin): a chance from MonUMod, the level from the monster's (level / 5 + 1), then the game's walk 0x4346F0 (game, 3, monster, level, range, step) with the step 0x4A06B0 (game, monster, target, level), both from its routine table. The step: skill 0x42 (Amplify Damage), its row's length calc (+0x80) and state (+0xA2), the effect, the routine above. Hooked (the step): cannot be cursed |
| States rows | | 0x44 bytes, the row the state id (232 in 3.3): +0x10 flags, a bit per states.txt column in the game's own order (nosend 0, aura 1, hide 2, hidedead 3, transform 4, pgsv 5, active 6, remhit 7, damblue 8, damred 9, attblue 10, attred 11, curse 12, curable 13, plrstaydeath 14, ...), +0x18 stat, +0x1E group. curse is on 26 states: the 13 curses that can be cured (9 amplifydamage, 19 weaken, 23 dimvision, 27 taunt, 55 ironmaiden, 56 terror, 57 attract, 58 lifetap, 59 confuse, 60 decrepify, 61 lowerresist, 113 defense_curse, 114 blood_mana), the ten shrine bonuses (128 .. 137), battlecry 89, bloodlust 141 and cloaked 156, none of which can. Curable without curse: freeze 1, poison 2, cold 11, the Warlock's three hex debuffs (199, 201, 217). Read (the loader's table): cannot be cursed |
| vendor payment | 0x5416D0 | (game, player, amount) -> 1 when paid, 0 when gold (stat 14) and stash gold (15) together are less: the inventory's gold first, the rest from the stash. Nine callers, every one of which has made the same test inline before (refusing with result 0xC) and asserts on a 0: buying 0x53D850 (three: a scroll into its tome, a stack filled, the item), Cain's identify-all 0x53C2B0 (100 a piece, free once he is rescued), a mercenary hired 0x53D350 and brought back 0x53EEF0, repairs 0x53FE00 (three). Hooked: infinite gold |
| vendor handlers | 0x53D850 | buy (game, player, npc, item id, ..., cost, gamble, fill, ...): the NPC's record (0x3971A0: +3 identifies, +4 revives, +7 hires, +8 sells, +9 gambles, +0x0B item service, +0x0C the level it travels to, +0x10 the quest that asks), the price (0x36F0B0 ITEMS_GetTransactionCost (player, item, difficulty, quest flags, npc class, kind: 0 buy, 1 sell, 2 gamble, 3 repair)) must be the request's and not 0, the gold test, then the item handed over and the payment. Every handler answers with packet 0x2A (0x480750 (client, 0x2A, result, gold, item id, kind): 15 bytes; results 0 bought, 1 sold, 3 identified, 5 done, 9 refused, 0xA no room, 0xC not enough gold). The client keeps the packet (0x10F310 -> 0x2A487DC) for its panel and sounds and never changes its own gold: that comes with the stat updates. It sends a buy request without a look at the gold (0x1114C0: 0x32 buy, 0x33 sell, 0x35 repair, for which it reads the gold to say how much to pay). Server packets 3.3: 0x32 buy 0x4C4650, 0x33 sell 0x4C5380, 0x34 identify 0x4C6C90, 0x35 repair 0x4C5EA0, 0x36 hire 0x4A9520, 0x37 an NPC's travel 0x4A9880, 0x3F wake in town 0x4B6160, 0x52 revive 0x4A9600. Not hooked |
| item use: start | 0x5817F0 / 0x236FE30 | the start dispatcher (game, player, item, target): the pSpell descriptor (12 bytes: start callback, use callback, keep) and the start table 0x1D3EA30 (11 entries; the use table is 0x1D3EA90): 1 identify 0x580A50, 2 town portal 0x580F00 (a jump to the cast). The use callback runs when the cast is over. Read |
| town portal: cast | 0x46FA70 / 0x470AE0 | (game, player) -> al. No portal in a town (0x2F0750) or in a level whose Levels row has PreventTownPortal (+0x184); a free spot beside the player (0x3632F0); the act's town (0x2EF990 (bank, act): ActInfo +0) must have its portal spot (0x2EF8B0, spot 0xB); then the pair maker with the town and object class 0x3B, the new portal's id kept in the player data (+0xA4). Callers: the start table, and 0x5822B0 (a skill's). The use callback 0x57ECA0 -> 0x470AE0 finishes: the player's portal before (player data +0xA0) and its partner taken away (0x424C20), the new one its own (+0xA0), both given their owner (0x510E50). 0x4C8650 takes both away when a cast is broken off. Hooked (the cast): the home town |
| portal pair maker | 0x432CE0 | (game, unit, room, x, y, destination level, Unit** out, object class, no spot search, in town allowed) -> 1: in a town only a permanent portal (0x3C) to level 0x27 or 0x85 .. 0x89; **the destination's act must be the room's** (else 0); the portal made (0x490410 (2, class, x, y, game, room, 1, 1, 0, 0)), mode 1, its opening event; the town end maker; both rooms flagged (0x2F0530: DRLG room flags 0x400000, a room with a portal). 15 callers: the cast, an NPC's travel inside an act 0x4F2E40 (Tyrael's portal to Lut Gholein), quests, the cube's portals. Not hooked |
| portal town end maker | 0x435DD0 | (game, unit, portal, destination level, source level) -> the portal made or 0: the destination's act from the game's acts (no test that it is there), its portal spot (0x2EF8B0 with 0xB), a quest's say (0x515030: a portal from level 0x49, Tal Rasha's Chamber, with Duriel dead stands at spot 0xC of level 0x28 whatever the destination), a free spot (0x363370), the object of the first one's class made there, its destination the source level (`.maho` 0x3E2B020), mode 2, each told where the other stands (0x491750: object data +0x24 / +0x28 the other's room, +0x2C / +0x30 its place) and linked (0x34EF50: unit +0xE8 type, +0xEC id of the other). When it cannot, the first portal is taken away and 0 answered. Its other caller 0x593CF0 (the red portals of Act 5, no unit). Hooked: the home town |
| portal partner | 0x490070 | (game, portal) -> the other portal: the act of the portal's destination, the room the other stands in (object data +0x24 / +0x28) found in that act or made again (0x2F0810, 0x4042A0: a room's units made, its sleeping units woken), then the unit by the link's type and id. So a portal's destination must name the act its partner stands in. Callers: the use, the cast's two ends, the object's update to a client (0x592F00: the partner's level and place sent), the unit sent to a client (0x538690). Not called |
| object destination | 0x34AD40 / `.maho` 0x3E2B020 | the getter: object data +0x08 (low byte) and +0x78 (high); the setter is D2RCore's (SetObjectInteraction16, 0x8118D0: the two bytes). A town portal's init (0x50ED70, objects.txt InitFn 11) sets the town of the act the object stands in; the client's init (0x1CB410) does the same, then takes what the server sends (0x99510). Called (the setter): the home town |
| portal use | 0x58F680 | object operate function 15 (table 0x23A51C0; both town portal classes), (context) -> 1: context +0x00 game, +0x08 object, +0x10 user, +0x20 object class. The object open (mode 2), the user a player; in a game of 0x424A60's kind only the owner; 5 s since the user last turned hostile (player data +0x1E8); the partner; no fifth act in a classic game; for class 0x3B and a user who is not the owner the destination's quest (LevelDefs +0 / +4) done; not just through a portal (state 0x66 just_portaled). Then the move inside the act (0x491CA0 (game, unit, room, x, y, 0, 0)) to a free spot beside the partner, the quests told when a town is left (0x5155C0), and for the owner through the town end both portals taken away; state 0x66 for 75 frames. Hooked: the home town |
| move to a level | 0x488180 / 0x487D20 | LEVEL_WarpUnit (game, player, level, spot): the level's act against the client's (client +0x2B8, 0x484BF0): the same, the spot's room (0x2EF8B0) and the move inside the act; another, the towns told (0x502D00) and the act change 0x487D20 (game, client, level, spot): the act made when the game has it not (acts at game +0x190 {acts, count}; 0x48AA50), the spot, the player out of its room and into the new one, the client told (0x05, the act's load 0x03 with the game's seed, 0x53), its act set (0x4860C0, unit 0x34D730), the pets brought along (0x500A40). Callers: waypoints 0x5933F0 (spot 0, 0xD for a town), waking in town 0x4B6160 (0), an NPC's travel 0x4F2E40, the Hellgate 0x58F0A0, the Arcane Sanctuary's portal 0x58EFA0, debug commands. Spots: 0x3DA650 (bank, DRLG, level, spot): a preset level (LevelDefs +0x90) its special tile of that number (0x3DAC20; 0xD the waypoint), another its waypoint's room, a room with a warp, the middle, any. Called: the home town (a town: spot 0xB for a portal's use, 0 after a death's wake) |
| wake in town (after a death) | 0x4B6160 | the server's handler of packet 0x3F (its entry in the packet table 0x1D2A790), (game, player, packet, size: 1 asserted) -> 0: what a dead player's client sends to go on. The player's mode (0x34AB60) must be 0x11, dead, and the game no hardcore one (game +0x5658 & 0x800); either failing is logged by the game as suspect and nothing is done. Then 0x4388B0, life, mana and stamina to their tops (0x2F4D20 / 0x2F4E20 / 0x2F4F20, SetUnitStat 6 / 8 / 10, the client told 0x4F3AD0), calls not read (0x5472A0 (game, player), 0x598E90, 0x598F70 (player, game +0x1AA48), 0x5989C0 (game, client, player)), selectable again (0x34EE20), state 0x36 on and off, then the act of the level the player is in (`.maho` 0x3E2B5C8), that act's town (0x2EF990) and the move to a level with spot 0 (the call at 0x4B6620), the mode changed to 1 (0x42D2C0 (game, player, 0, 1, ...)) and both skills sent again (0x438A70). Hooked: the home town |
| client: portal trip ahead of the server | 0xFE1F0 / 0x892F0 / 0x86E20 | the client's player state machine (0xFF010, case 2: an object reached) asks 0xFE1F0 (player, portal) -> al for an object of class 0x3B when the player stands in a town (0x2F0750; a portal that stands outside one is refused, sound 0x19): the client's copy of the server's tests (the owner from the portal list 0x136630, else the party 0x136A70; the Levels record of the other end's level; no fifth act in a classic game; the quest for one who is not the owner). One caller. D2RCore patched its middle (a `jmp` to 0x3E2DF40 at 0xFE2DC, the 16-bit level). On a yes the trip is begun, 0x892F0 (object id): the trip record (0x2A236E0: +0x6C state 1, +0x70 time, +0x74 / +0x78 the player's place, +0x7C the object) and 0x86E20 (portal): **the room at the other end made in the act the client is in** (0x2EF320 (bank, the client's act 0x2A233B0, level, x / 5, y / 5, 0)), its near rooms, the client's player put there (0x9BF00). The update 0x89400 (from 0x8B3C0) waits in state 1 until what was made is loaded, then sends the request (0xFA180 (2, id): 0x41, and 0x43) and waits in state 2, 1 s at most; the move packet's handler (0x15, 0x12AB40) ends the trip (0x89260) or takes the player back (0x89050) when the server put it in a town. On a no the request is sent at once (0xFA180) and the server's packets move the player: what happens for every portal used outside a town. Hooked (0xFE1F0): the home town |
| portal packets | 0x47F620 / `.maho` 0x3E2B760 / 0x3E2B75A | what a client is told of a portal, from the unit sent (0x538690) and the object's update (0x592F00): 0x60 (14 bytes: flags, the destination level (16 bits), the object's id, the other end's x, y and level: the level of the room the partner stands in, 0 for another class than 0x3B) and 0x82, D2RCore's (SendLinkTownPortal16: the owner's id and name, both ids, level and place). The client's handler of 0x60 is D2RCore's too (HandleSetWarpDestination16): the destination into the object data (+0x08 / +0x78), the other end into the unit: +0x1BE level, +0x1B6 / +0x1B8 place. Only the trip ahead of the server reads them (0xFE1F0, 0x86E20). Read |
| client packet table | 0x22BBC40 | 24 bytes an id {handler, size, 0}: 0x03 the act's load 0x129930 (the act setup 0x86BC0, at once), 0x05 the act let go 0x129AD0 (0x86A50), 0x07 a room added 0x129B80 (the server's 0x47D2D0 (client, level, x, y)), 0x08 one taken away 0x129C30, 0x15 a unit moved 0x12AB40, 0x51 an object 0x129D70, 0x60 `.maho` 0x3E2B75A, 0x2A 0x12C3B0. An act's packets are taken in their order, each done before the next. Read |
| level of a DRLG | 0x3267C0 / 0x2EF320 | GetLevel (bank, DRLG, level id): the DRLG's list (+0x868), and a level that is not in it is made: its record's DRLG type (LevelDefs +0x30) and level type (+0x34), its seed, the maze, preset or outdoor part, **no place and no size** (those come with an act's own levels). So a level asked of another act's DRLG is an empty one, and an outdoor one stops the game in D2RCore's outdoor generator (its size test 0x374C40 of D2RCore.dll, called by PlaceOutdoorShrines / RandomOutdoorPreset / OutdoorPresetGroup / OutdoorWaypoint: "Outdoor map generation stopped because the outdoor dimensions are too small to contain the map border", then RaiseFailFastException: no dump, the line is in d2rloader.log). 0x2EF320 (bank, act, level, x, y, room) makes the room of that level at the place: the client's room packet and its trip ahead of the server call it. Read |
| act made for the game | 0x48AA50 | (game, u8 act): the act's town (ActInfo), DUNGEON_AllocAct (0x2EF1C0 (bank, act, the game's seed +0x12C, 0, game, difficulty, town, 0, 0)) into the game's acts, then the act's own setup (0x59EE20). One caller, the act change, for an act whose slot is empty. Called: the home town |
| waypoints | 0x3D6930 / 0x3D6890 / 0x3D6670 | has (waypoint data, number) / set / number of a level (bank, level, u16* out: Levels +0xEC, 0xFF none). 0x70 waypoints, a {u16 word, u16 mask} pair each (0x2389010). A character's data: player data +0x58 + 8 * difficulty. The towns' are 0, 9, 18, 27 and 30; an NPC's travel sets the new town's (0x4A9880). Called (has): the home town |
| ActInfo / Levels rows | | ActInfo 0x8C bytes, 5 rows: +0x00 town (1, 40, 75, 103, 109), +0x04 start, +0x68 nine waypoint levels. Levels 0x18C bytes (the loader's compiled levels.bin has them twice: as the game has them, and 0x194 wide with a column of the loader's at +0x10 and the rest 8 further on): +0x04 id, +0x0D act, +0xEC waypoint, +0xFD LevelName, +0x184 PreventTownPortal. The game's town test (0x2F0750 a room, 0x2F0780 a level) has the five ids in its code. Read (the loader's tables): the home town |
| DRLGROOM_CreateActiveRoom | 0x3289A0 | (u8 bank, DrlgRoom*) -> ActiveRoom (+0x58): tile libraries (flag bit 24, 0x3F3970), preset units (bit 25, type 2, 0x3DE0E0), then 0x328FD0 (near links 0x3608A0, static grids 0x3F38D0, map tiles 0x3F3930 (bit 20), ActiveRoom 0x326480). No refcount: the room stays built until the DRLG frees it. The bank byte is the client player's data-table bank (the act setup 0x86BC0 gets it the same way). Called: map reveal |
| room activation | 0x328710 / 0x328F60 / 0x328BF0 | the client's room streaming: (ctx, old, new) moves a room reference; four refcount levels (DrlgRoom +0x28 short[4], +0x70 the level it is at), activate/deactivate tables 0x2372700 / 0x2372720 (the rooms in per-level lists at drlg+0x130 + level*0x1C0; level 3's deactivate releases the room data 0x3F3AA0). 0x2EF2F0 wraps it for ActiveRooms (callers 0x485AB0, 0x4871A0). Not called |
| automap room callback | 0xD2240 | (ActiveRoom*): the client DRLG's automap callback (drlg+0x838, set by the act setup 0x86BC0 -> 0x2EF1C0 -> 0x326BA0; +0x878 is 0xD22F0): bank, the active layer (0x2A2CF68), the room's level record (0x32C200) +0x08 = its layer, GetOrCreateLayer (0xD5360), AUTOMAP_RevealActiveRoom (bank, room, 1, owner), then GetOrCreateLayer (previous) back. Called: map reveal |
| AUTOMAP_RevealActiveRoom | 0xD6550 | (bank, room, force, owner): floor (0x2EFB70) and wall (0x2EFDF0) tiles (0x48 bytes, flags +0x18) into owner+0x08 / +0x30 via 0xD5160, skipping flag 8, only seen tiles (0x20000) unless force or the reveal-all byte 0x2A2ADD4; units in the room (0x2EFD90) to owner+0x58 via 0xD52B0 (monsters by MonStats2 +0x118, objects by Objects +0x164) when seen or reveal-all. Callers: the callback (force 1) and the automap update 0xD4310 (force 0: the rooms near the player on its layer, after a move of 0x50) |
| automap layers | 0xD5360 / 0x2A2CF68 | GetOrCreateLayer (layer id) -> owner (0xB0 bytes: +0 layer id, trees +0x08 floor, +0x30 wall, +0x58 objects, +0x80): another layer than the active one (0x2A2CF68) is saved (0xD1710 -> 0xD6230 -> 0xD7CE0 per tree) and freed first, so asking for foreign layers in a loop churns saves. A tree: +0x08 leftmost, nodes parent/left/right +0x00/+0x08/+0x10, key at +0x20 (u16 tag, i16 frame, i32 x, i32 y; tag 1 = restored from the save, not written again), count +0x20 |
| automap serializer | 0xD7CE0 / 0xD5FE0 | the tree walker emits 3 words per tag-0 cell; its byte count at 0xD7E3A is D2RCore's checked 32-bit `2*words` (vanilla: a sign-extended 16-bit double that a layer past 5461 cells overflows, a crash at the next layer change; RuffnecKk's Automap Serialization Fix). The writer 0xD5FE0 keeps 32-bit sizes. Checked (signature): map reveal |
| GetLevelDefRecord | 0x32C200 | (u8 bank, level id) -> the Levels record (+0x08 the automap layer, +0x30 the DRLG type InitLevel switches on: 1 maze, 2 preset, 3 outdoor): asserts on an id out of range. Called: map reveal (the current level's layer) |
| Blizzard's debug reveal | 0xD42B0 / 0x1FB050 | the QA menu's "Reveal Automap" (0x1D3AF3) and the automap debug panel's button (0x14E46C4) call it through the `.maho` slot 0x3E2B148 (D2RCore's `revealmap`, "Reveal the current map", owns them): a flood fill from the player's room over the near-room lists (0x2EFDE0: ActiveRoom +0x00 list, +0x40 count), up to 2500 rooms, activating each (0x328710) for the callback 0xD2240, then releasing them; then it toggles the reveal-all byte 0x2A2ADD4. Not called |
| D2GAME_PLAYER_ResetStatsAndSkills | 0x580F20 | (game, player) -> bool (al): ResetSkills 0x4360F0 (refunds base levels into stat 5), ResetBaseStats 0x52DDF0 (CharStats base values, refund into stat 4), refresh, sound 2. The Token's use callback 13 and Akara's 0x39 handler. Called: respec |
| spend points (0x3A / 0x3B) | 0x4B3C70 / 0x4B3EE0 | (game, player, packet, 5): {opcode, u16 stat or skill, u16 extra}; 1 + extra points through 0x52DD10 (stat 0..3, needs stat 4) / 0x438670 (class 0x436BD0, prerequisites 0x339330, level 0x339C00, max level Skills+0x184, stat 5), stopping at the first refusal. Server packet table 0x1D2A790 (8 bytes per opcode). Called: presets |
| antidote cure (use callback 6) | 0x5807B0 | (game, player, item) -> bool: per Items cure state (+0x9A/+0x9C): list by state, unlink, free (Game+0x106), then state on? -> toggle (state, 0); then the item's own state (0x57FD80). Its block at 0x58088D (`mov rcx, rsi` .. the toggle call 0x5808D0) names the five routines. Called (its routines): cannot be poisoned |
| stat lists by state | 0x2F5940 / 0x2F7920 / 0x2F4180 | GetStatListFromUnitAndState (unit, state) -> the first list: statlistex (unit+0x88) chains +0x90 then +0x98, linked at list+0x68 (prev +0x70); list +0x00 unit, +0x1C flags (2 expires), +0x20 state, +0x24 expire frame (float), +0x78 parent, +0x80 end callback. Unlink (unit, list): the bank (0x34A0E0), then 0x2F6E50, a D2RCore `FF 25` jump (.maho 0x3E2A278 -> D2RCore 0x3DA6E0, same logic as the original body left behind it): off the chains, then a tail call of the end callback (u8 ctx, unit, state, list) unless flags & 0x2000. Free (u8 ctx, list): 0x2F9EB0 for a plain list (unlinks again, frees) |
| states | 0x3351B0 / 0x3354C0 | StateOn (unit, state) -> the bit in statlistex+0xAF0 (players, monsters, missiles only); Toggle (unit, state, on) via 0x2F7EA0: the bit and its changed bit, then 0x38EBF0 queues the unit on its room's update list (unit flag 0x2000), which is how the clients hear |
| poison | 0x4518C0 | (attacker, defender, per frame, length, source x3): a state 2 list with hpregen (74) = -per frame, expiring at frame + length (event 0xC); a stronger poison refreshes the list. End callback 0x436240 (D2RCore .maho 0x3E2A408 first, then state off, anim rate 0x350B40, auras 0x33F830) or 0x4505E0 when the States row's +0x3C byte is set: one list per source (0x2F5AB0), the state off with the last (0x2F5F70 counts them) |
| Show Items key press | 0xC66A0 | (u8 which: 0 Show Items, 1 Unfiltered): the key's display mode (below); Toggle (1) flips its on/off byte (0x1FAF00), anything else sets it (Timed (2) also sends ShowItemsPanelMessage ResetTimer / ResetTimerUnfiltered); then the panel update 0xCE450 (UI var 0xC, the ShowItemsPanel, open while either byte is on). Called: through the key-action table |
| Show Items key release | 0xC6E90 | (which): in Hold (0) the byte to 0 (0x1FAE90), then the panel update. Its calls name the two mode getters, the value reader and the setter. Not called |
| Show Items on/off bytes | 0x235D360 | {u8* data, u64 count}: [0] Show Items, [1] Unfiltered; get 0x1FAE30, set 0x1FAE90, flip 0x1FAF00, either on 0x1FAD80; zeroed (0x1FA780 via 0x14FE370) with the ShowItemsPanel at a game's load. Timed mode: 0x14FE670 turns a key off after its delay. The close-all 0xC8240 turns off only Hold-mode ones. Read |
| Item Name Display settings | 0xE1780 / 0xE2600 | getters of the function-local setting objects (0x2A33CC0 / 0x2A33E30, raw value at +0xC, range 0..2) named "Item Name Display" / "Unfiltered Item Name Display" (the Settings.json keys; the game's options); value 0xD2E490 (setting) -> the value through its two converters (the key handlers compare the low byte): 0 Hold, 1 Toggle, 2 Timed. Called: the display modes |
| automap settings at load | 0xD34C0 | reads AutoMapMode (0xE06A0: 0x2A2ADD0 = mode != 0, 0x2A2ADD5 = mode == 1), the fade/centre/party settings and "AutoMap Open On Load" (0xE05D0 -> 0x2A2CFA0) into globals; 0xD25B0 (called at load, 0xCBE52) opens panel 10 when Open On Load is set, and re-centres (the pan at 0x2A2CF80, two int32) |
| panels (classic UI vars) | 0x2A2ADA0 | a byte per panel id (10 the automap, 0xC the ShowItemsPanel); open 0xCD7C0 (id, x), close 0xC7D30, toggle 0xCDE00, get 0xCE500 (`movsxd; lea; movzx; ret`, one of three identical getters over other tables). They refuse while the client player is dead or not a player, and past the conflict gate 0xD00B0 (matrix 0x229E930: closes panels the new one excludes); they post PanelManager Open/ClosePanel with the name from 0xC7AA0. Close-all 0xC8240 (the automap too with cl = 1). Read |
| panel ids | 0xC7AA0 | (id) -> the D2R panel's name, a jump table of 32: 0 HUDPanel, 1 the inventory, 2 CharacterStatsPanel, 3 SkillSelect, 4 SkillsTreePanel, 5 ChatPanel, 9 PauseLayout(Garden), 10 AutoMap, 11 VendorPanelLayout, 12 ShowItemsPanel, 13 ImbueItemsPanel, 14 the quest log, 15 ScrollOfInifuss, 19 the waypoints, 20 MiniMenuPanel, 21 PartyPanel, 22 TradeLayout, 23 the message log, 24 the stash, 25 HoradricCubeLayout, 27 HelpPanel, 28 HelpButtonPanel, 30 HirelingInventoryPanel, 31 LootFilterOptionsPanel; 6..8, 16..18, 26 and 29 none (8 the NPC menu, 17 the cinematics'), so no PanelManager message for them. Read |
| panel gate | 0xD00B0 | (new id, mode: 0 open, 2 toggle, force) -> al, asked by the open 0xCD7C0 and the toggle 0xCDE00 (`mov r8b, 1; mov edx, 2; mov ecx, ebx` at 0xCDE5D) before they change a UI var: a few panels by name first (chat, the pause menu while dead), then for each of the 32 panels that is open (the UI vars, `lea rdi` at its start) the rule of its row for the new one: the rules 0x229E930, a pointer per open panel to 32 int32 (static data, base-relocated), 0 none, 1 the open one closed (0xC7D30, with force), 2 and 3 the new one refused, 4 an NPC talk ended (0x10F150 / 0x10DE80). The automap's row (10): 1 only for 17, the cinematics' panel; the panels whose rows refuse the automap: 9 (2), 10 (3), 17 (3), 22, 24, 25, 27, 31 (2). Read (the rules, derived from the Automap key's toggle): kept between games |
| close all panels | 0xC8240 | (cl: the automap too, dl: handed to each close) -> al whether one was: the 21 panels of its list 0x1CC2880 (1..5, 9, 11, 12, 14, 15, 17, 19, 21..27, 30, 31; Show Items' panel 12 only for a key in Hold mode), the automap only with cl = 1. Callers with cl = 1: leaving a game (0x8A610, 0x8AA90 the end screen), the Esc key 0x8DFA0 (others first; the automap only when nothing else was open), and the client's packet 0x62 for unit types 2, 4 and 6 (0x10EFF0). Not called |
| cinematics in a game | 0xEF630 | ProgressionVideoHelperWidget::OnMessage (D2Client Video.cpp): "ProgressionMessage" / "PlayVideo" (the act let go 0x86A50, at an act change, asks the client's quest record for flag 0 of quests 6 and 7, 14 and 15, 22 and 23, 27 and 28: the first of a pair set, the second not) plays a video of the table 0x22A0FF0 (24 bytes: 2 act2/act02start, 3 act3/act03start, 4 act4/act04start, 5 act4/act04end, 6 d2x_intro, 7 act5/d2x_out; 0, 1 the menu's) and opens UI var 17 (0xCD7C0 at 0xEF8D0), whose gate closes the automap, then broadcasts "MoviePlayerMessage" / "Play"; "Cinematics" / "MovieComplete" closes var 17 (0xEFE86, 0xF0243), except after act04end in an expansion game, where d2x_intro follows with var 17 still open. The game never opens the automap again: a waypoint between acts plays none and leaves it open (the trace of 2026-09-29, 00:23:15). Read (var 17, through the gate's rules): kept between games |
| packet 0x62 | 0x12B050 / 0x4799D0 | the client's handler (7 bytes: unit type, id) -> 0x10EFF0: a monster (1) selectable again and an NPC talk ended; types 2, 4 and 6 close every panel, the automap too. The server's builder 0x4799D0 (client, 0x62, type, id): from 0x545800 (type 1, units it walks; 7 callers, among them the move to a level's 0x488180 -> 0x502D00 -> 0x503290) and 0x424F30 (type 6, id 0), whose callers are a debug command 0x4189A0 and 0x5E5E80, the callback a quest's update 0x5E60F0 runs for every player of a classic game (Game +0x106 == 1) after a delay (then, with quest 26's flag 13, the move to level 103, the Pandemonium Fortress). Not called |
| Automap key | 0x119D50 | (): unless a controller holds it back, toggle panel 10, then `mov ecx, 0Ah` + get, and a closed map is re-centred (0xD25E0). Id 8's key (0x119DA0) re-centres (keyboard) or opens the map and cycles AutoMapMode (controller). Called: through the key-action table |
| key-action table | 0x22A7930 | 0x18 bytes per action id (the ids a .key file stores): +0 press, +8 release, +0x10/+0x11 flags, +0x12 held. Press dispatch 0x11FF30 (binding, table, force), release 0x1203A0 (the binding's first dword is the id); 0x11EC0E hands a binding to one or the other with the table in rdx. Ids: 7 Automap (no release), 8 the automap centre/mode, 37 Show Items (press thunk 0x11B460: a stub returning 0 into cl; release thunk 0x11C400), 67 Show Items (Unfiltered) (`mov cl, 1` thunks 0x11C450 / 0x11C460); ids 0..68. Called (press): kept between games |
| D2RCore's key wrappers | ids 7 / 37 | the table entries hold `.maho` jumps (0x3E2B16C / 0x3E2B172) into D2RCore (RVA 0x2BFA60 / 0x2C1DF0 in D2RCore.dll, loaded at its base 0xC0DE5000000): the original press, then it remembers the automap's UI var / ((Item Name Display == Toggle) && byte [0]) for the session (D2RCore .data 0x7B0D38 / 0x7B15B0), the automap one clearing its pending restore. At a game's load (0x1CF032; also 0x2BF980 / 0x2C1CC0, callers not found) the restores run once the client player exists (0x2BF990 toggles with the original key until the UI var matches; 0x2C1CD0 in Toggle mode sets byte [0] and updates the panel). The memory starts empty, so on the first game of a run D2RCore closes the map the game's Open On Load just opened. Read (the entries are checked, then called) |
| skills | 0x33DCD0 / 0x33D1E0 | GetSkill (unit, id, owner) walks unit+0x100 -> list+0 -> nodes (+0x00 Skills row, +0x08 next, +0x40 base level, +0x4C granting item id or -1); GetSkillLevel's entry is a D2RCore jump (base +0x40, plus item bonuses when asked). Skills rows 0x2EC bytes: +0 u16 id, +0x2C charclass |
| item writer (bitstream) | 0x37D140 | (item, bitstream, ...): one caller (0x3800F0). After a 3-bit count, the network form (flag 0: what a client is sent) takes the Items row (0x314110 (bank, class)) and tests ShowLevel (`cmp byte [rax+0x10E], r12b`, `jne` 0x37D726 to the real level, else `mov edx, 1`); the save form writes the item's seed (32 bits) and jumps to the real level itself. Real level: item data +0x38, 1..99. Seven bits either way (0xA1B710 (stream, value, bits)). Patched: item level (the `jne` -> `jmp`) |
| item reader (bitstream) | 0x378860 | the counterpart: 3 bits, the seed in the save form, then the 7-bit level, kept unconditionally (item data +0x38 via 0x36E880). ShowLevel matters only at the socket count (0x37A7FE, ItemStatCost row 194's bits): with it 0 the level is set to 99 while the sockets are set and put back, so the placeholder 1 cannot clamp them. Not hooked |
| item reader: the entry | 0x374BF0 | (item, bytes, size, save form, int* socketed items, format, int* failed) -> bytes read. The format is the save's (a .d2s header's version, a stash section's): 96 the 1.10 to 1.14 games', 105 D2R 3.3's; below 97 an item begins with "JM". The flags (32 bits, D2RCore's read, `.maho` 0x3E2B05E) go to item data +0x18 without 0x80000 and without 0x2000000, which says "low quality and nothing after the code" (level 1, quality 1). A compact item (0x200000) is read by 0x3780D0, another by 0x378860. Read: tools/stash_seeds.py |
| item in a save: the layout | 0x378860 / 0x3780D0 | bits from each byte's low end; format 105, format 96 in brackets. The item's version: 1 bit, then 2 (+99 when the first is set) [10 bits]; mode 3; modes 3 and 5: x and y, 16 each, else body location 4, x 4, y 4, page + 1 in 3; the code: four characters by the Huffman tree 0x3763E0 walks (0x1D03330: 8 bytes a node {?, child for a 1, child for a 0, character or -1}, the root 510, all 256 byte values) [32 bits]. A compact item has after it only gold's amount (0x37BCC0), a quest item's difficulty (stat 356's bits; Items quest and questdiffcheck), the 128-bit id and the stash count, and gets seed 0, level 1, quality 2. Another: socketed items 3 (they follow the item, outside its list's count); the unit seed 32 (save form only; the unit's RNG seeded from it); level 7; quality 4; picture 1 + 3; automatic affix 1 + 11; by quality low and superior 3, normal (a charm's affix 1 + 11, a body part's monster 10, a scroll's or book's spell 5), magic 11 + 11, set and unique 12, rare and crafted 8 + 8 and six times 1 + 11, tempered (9) 8 + 8; a runeword's id 16 (flag 0x4000000); an ear (0x10000) 3 + 7 and a name, else a personalized item's (0x1000000) name, 8 bits a character to a 0 [7]; the 128-bit id as 1 + 128 (0x37BD50: item data +0x1C, its flag +0x2C) [1 + 96]; armor (0x32) the base defense, the max durability and, when that is not 0, the durability by ItemStatCost's save bits (31: 11 bits add 10; 73: 8; 72: 9), weapons (0x2D) the two durabilities, gold its amount; a quantity as 1 + 9 [9 bits for a stackable base, Items +0x142, or the shards xa1..xa5 by code]; sockets 4 (flag 0x800, stat 194's bits); a set item's 5 bits of bonus lists; the stat lists (9-bit ids to 0x1FF, a value and a parameter by the save bits; 17, 48, 50, 52, 54 and 57 bring the next one or two along; D2RCore's reader, 0x3E2A198): the item's, one per bonus bit, a runeword's; where and when it was found (flag 0x10000000: 16 bits, then unless 0x20000000 32 bits and a count in 4, at most 8, then 64 bits each, 0x377EB0) [none]; the count in the stash's materials tab as 1 + 8 (0x378000, formats from 104) [none]; then to the byte. Read: tools/stash_seeds.py (every list of Joshua's stashes and characters ends on its section's last byte) |
| character file (.d2s) | | +0x00 0xAA55AA55, +0x04 the format (105), +0x08 the file's size, +0x0C the checksum (every byte added to the sum turned left by one bit, its own four bytes as 0: the 1.10 games'), +0x20 the time of the save, +0x12B the name; 3.3 has the worn items' codes in the header too (from +0xF8). After the skills ("if") the character's list ("JM", a count, the items), "JM" and the count of corpses (12 bytes and a list each), "jf" and the mercenary's list when there is one, "kf", a byte for a golem and its item; 3.3 has more after that ("lf", "gf"). Read: tools/stash_seeds.py |
| duplicate items at a load | 0x4229C0 / 0x541FA0 / 0x36D280 | (game, player, item, format), formats from 93, off while the dword 0x238D810 is 0 (1 in the dump): the item's 128-bit id (item data +0x1C, whether it has one +0x2C) is looked for in the game's sorted ids (Game +0x1AA68; 0x541FA0 adds one that is not there and answers 1, as it does for an item without an id, 0 for one that is there): "G:%u Detected duplicate item for player %s (%llu), removing it", which D2RCore reports as `[D2R:LOADDELETEDUPE]`. The seed is not read: two items may have one seed. In Joshua's saves (2026-09-28) every item of the modern stash and of Xeen.d2s has an id, no two alike; Vacate.d2s's have none. Not called |
| inventory item name | 0x1E1D80 | (item, char name[0x400]) (D2Client inv.cpp): the base name, a personalized name, then under ShowLevel the string "space" (keybinds.json: a space, empty in jaJP; D2RCore's lookup) and "(%i)" (0x1CDD740) of 0x36CCF0 (item data +0x38, 1 below that), strcat_s with 0x400; quest and requirement colors after. No caller in the exe: D2RCore calls it from 15 places (its tooltip code) through its own table of game routines (RVA entries 0x18 apart in its .data). Hooked: item level |
| Items / ItemTypes columns | 0x315FD0 | the Items field table builder: descriptors {name, type, offset, ...} 0x20 bytes on the stack, `lea` of each column name ("ShowLevel" 0x1CFB058): +0x10C rarity, +0x10D level, +0x10E ShowLevel (bytes). ItemTypes rows 0xE8: +0 code, +4 / +6 Equiv1 / Equiv2 (u16; 0 none, row 0 is the blank "Any"); weap 0x2D and armo 0x32 are the rows the game's own type tests use |
| NPC table | 0x3971A0 / 0x237EA60 | the getter (u8 bank, NPC class) -> the NPC's record or 0: a walk over 43 (0x2B) records of 0x1C bytes, static in .data (the bank is not looked at); 22 callers. A record: +0x00 int16 class, +0x02 repairs, +0x03 identifies, +0x04 revives, +0x05 resets stats and skills (Akara), +0x07 hires, +0x08 sells, +0x09 gambles, +0x0A the act (0 .. 4), +0x0B the item service (1 imbue, 2 sockets, 3 personalize), +0x0C int32 the level it travels to, +0x10 the quest that asks for the service (for the reset, the hire), plus one (0: none asks), +0x18 int32 its column among the Items rows' vendor columns (-1 none). With a service: Charsi 154 (record 2: service 1, quest byte 4), Larzuk 511 (38: 2, 0x24), Anya 512 (37: 3, 0x27). Only the service handler, the reset handler (0x4B2530, packet 0x39) and the two hire routines read the quest byte. The getter's first ten bytes hold a RIP-relative `lea`: not hooked, not called. Read (the table), written (the quest byte of a service's NPC): NPC item services |
| NPC item service (server) | 0x4AE280 / 0x4FC230 / 0x4B6050 | the request 0x2E (0x4AE280, its entry in the packet table, wants 0x15 bytes: +1 u32 the service, +5 the NPC's id, +9 the item's id, +0xD whether the client found the item a place, +0x11 the place) is handled by 0x4FC230 (game, player, packet, u8 second pass) -> int. The NPC by id within reach (0x53B8F0 (player, id, 0x32)), the item (identified, item flag 0x10), the NPC's record: its service must be the request's and its quest byte at most 0x2A; for a quest (the byte less one) the player's quest record of the game's difficulty (player data +0x40 + 8 x difficulty) must have flag 1 for it (0x325C50), else the request is logged and dropped; then the game's test of the item for the service (0x36AED0 imbue, 0x36B410 sockets, 0x36B1D0 personalize). The first pass (`test r15b, r15b`: not the second; not service 3; a record with a quest): the quest's routine for a reward used is run with flag 0xF set around it, the item's place tried (0x472590), every character saved (0x406E10 -> 0x41E0D0, D2RCore's writer), flag 0 taken back unless it was set before and flag 1 set again, and 4 answered, for which 0x4AE280 queues the request (game +0x5808) until the save is done (0x4B6050 runs the handler as the second pass). So a reward is saved as used before the item gets anything. The service: 1 the imbue (the row above); 2 item flag 0x800 set (0x36D8F0), the most sockets (0x36EAD0) for a quality below 4, for magic (4) a roll of min(that, 2) on the item's seed plus one, for 5 .. 9 one, then set sockets 0x375560; 3 item flag 0x1000000 and the player's name (0x36DFA0). After each, for a record with a quest, the quest's routine (0x5DA1C0 imbue, 0x548B60 sockets, 0x547C60 personalize: flag 0 set, flag 1 cleared), the item put back, packet 0x58 {item id, 6} to the client, 0 answered. A record without a quest: no test, no first pass, no save, no quest routine. Read (signature; how it reads the record and tells the services apart is checked): NPC item services |
| quest record | 0x325C50 / 0x325C00 / 0x325990 | get / set / clear (record, quest, flag): the record's first field points to the bits, two bytes a quest (bit quest x 16 + flag; 0x60 bytes): flag 0 the reward was taken, 1 a reward is to be had. 1,546 call sites on both sides of the game. A player's records: player data +0x40 + 8 x difficulty. The client keeps a copy of the record it talks under (its pointer 0x2A48778), filled as a talk begins (0x325AD0 copies the packet's 0x60 bytes). Not called, not hooked |
| client: a talk begins | 0x12B030 / 0x10E6E0 | packet 0x28 (103 bytes: +1 the unit's type, +2 its id, +7 the player's quest record) -> 0x10E6E0 (type, id, record): the record copied into the client's, the NPC menus reset (0x115460: every quest entry taken out again), then the NPC's speech when it has one (0x10DA20 opens the menu after it) or the menu's opener at once. Read |
| client: NPC menus | 0x2A48830 / 0x360D0 / 0x1147A0 | {menus, count, capacity}: a heap array, 0x48 bytes a menu, 48 built by the static initialiser 0x360D0 (0x10B6E0 adds one for a row of the bank's table at +0x1510, 0x10E bytes a row, whose class has none): +0x00 int16 the NPC's class, +0x04 int32 entries (the closing one counted), +0x08 u16[5] the entries' strings (0xD35 talk, 0xD44 trade, 0xD06 trade and repair, 0xD46 gamble, 0xD45 hire, 0xFB4 identify), +0x18 [5] the entries' routines, +0x40 a byte. The menu of an NPC, by class: 0x113990. The builder 0x1147A0 (bank) makes the menu of the NPC talked to (its id 0x2A4875C, its class 0x2A48764). Read (as a menu opens: the menu a quest routine writes is its NPC's): NPC item services |
| client: NPC menu opener | 0x1141D0 | (NPC, int after its speech): the panels closed, then the quest rows walked: 9 rows of 0x18 bytes at 0x22A70E0 (their count at 0x22A71B8), {int32 NPC class, quest, flag, value, routine}; for a row of the NPC's class the client's quest record is asked (0x325C50) and the row's routine called when the answer is the value (1 set, 0 not set). Rows: Warriv 155 / quest 6 / flag 0, Meshif 210 / 14 / 0, Kashya 150 / 2 / 0, Charsi 154 / 3 / 1, Akara 148 / 1 / 0, Larzuk 511 / 35 / 1, Anya 512 / 38 / 1, Qual-Kehk 515 / 36 / 0, Tyrael 367 / 28 / 0. Then the mercenary's entry, the panel (0xCD7C0 with 8) and the builder. Two callers: a talk's start and the end of a speech. Hooked: NPC item services |
| client: quest menu routines | 0x108E00 .. 0x109600 | nine routines 0x100 apart, no arguments, al = 1, named only by the quest rows. Each writes one entry of one menu, the menu's index an imm8 (`cmp qword [count], imm8`): the entries counted (+4), the entry's routine, its string. 0x109100 Charsi's menu 3: 4 entries, entry 2 the string 0xFB1; 0x109300 Larzuk's menu 39: 4, entry 2 the string 0x58DC; 0x109400 Anya's menu 41: 5, entry 3 the string 0x58DD; the entry's routine 0x10DD30 for all three. Called (those three): NPC item services |
| client: an item service picked | 0x10DD30 / 0x1E3E20 / 0x1E58F0 / 0x1E4630 / 0x117510 | the entry's routine closes the menu and opens the inventory for the service (0x1E3E20: 0x2A96960 = 1); a click on an item (0x1E58F0; 0x1E37F0 says which items it may be) asks the game's test that goes with the NPC's class (0x10D150: 154 the imbue's, 511 the sockets', 512 the name's) and has the player confirm; the confirm (0x1E4630) sends the request (0x117510 (item id): 0x2E, the service taken from the NPC's record). No quest is asked on the way. Read |
| transmute (the client's request 0x20) | 0x4C1590 / 0x4F2490 / 0x4ADAD0 | the packet table's 0x20 entry (0x4C1590 (game, player, packet, size): 8 bytes and 5 an item: +1 the cube's id, +5 its page, +6 its place, +7 the count of items less one, +8 {u32 item id, u8 place} each) runs 0x4F2490 (game, player, packet, pass): the player alive, not in state 0x36, nothing on the cursor; the items where the client says they are (0x472590), as many page-3 items as the packet names, the cube's code "box "; the recipe matched (0x529090 (&{index, found}, game, player, &ingredients)). The first pass: the cube's contents taken out of the inventory (0x472590), every character saved (0x406E10), the contents put back (0x471500), 4 answered, and 0x4C1590 queues the request (game +0x5808) until the save is done, when 0x4ADAD0 runs the second pass: the recipe's row (0x3E3F60 (bank, index)) to the product routine. So the save made at a transmute has the character without the cube's contents. D2RCore's routine table lists the product routine but nothing of D2RCore calls it. Read |
| cube: a recipe's products | 0x5269C0 | (game, player, recipe, ingredients: {item, class, level} 0x10 bytes each), nothing answered. The cube's items (page 3, mode 0) listed with their places (0x382D20) and ids and all taken out of the inventory at once (0x472590); per output (three, 0x5B bytes from recipe +0x5A: +0x5E u16 flags, +0x64 quality, +0x65 quantity or sockets, +0x67 kind, +0x6B the mods): kinds 1..4 (Cow Portal, Pandemonium Portal, Pandemonium Finale Portal, Red Portal) run their routine (table 0x1D3BFC0; its answer is whether anything was made); "useitem" (kind -2, or flag 1 "mod") takes the ingredient itself as the product (its id moved past the count of those used up; "mod" changes its class, 0x34DE10, and runs item init again, 0x43EF10); else CreateItemEx (mode 4; request flags 0x10 / 8 and 4 / 2 by the output's flags 2 and 4). Flags 0x30 (Clear Sockets; the parser at 0x3E6570: "uns" 0x10, "rem" 0x20; 3.3's cubemain has useitem,uns): the runeword taken off (0x43EC90), then with 0x10 the socket contents freed (0x48FA40, the call at 0x5270FA), else taken out and kept (0x5278A2: first item 0x388C10, taken out 0x389820, page 0xFF, x and y 0, body location 0, mode 4) and put in the cube after the products. Nothing made: the items put back (0x471500). Else the transmute's unit event (0x491960 (player, 4)), then every item that was in the cube: the client told it is gone (0x535F60 flags 0x20, command 4) and, for those used up (`cmp rdi, r13; jae`), freed (0x43EC10, the call at 0x527D0A); then each product put in the cube (0x3865B0 on page 3, 0x471500, identified, 0x535F60 command 2), freed when it does not fit; the quest items' events (hst 0x5A0360, qf2 0x5EB370). Hooked: infinite cube ingredients |
| item free | 0x43EC10 | (game, item): asserts that the item lies on the ground nowhere (0x34B440) and on no inventory (0x38AC50), then SUNIT_FreeUnit (0x48FAA0; an item's socket contents with it: 0x43DEC0 -> 0x48FA40). 47 callers in the exe, one in D2RCore. Hooked: infinite cube ingredients (acts only on the product routine's call, which returns to 0x527D0F) |
| socket contents free | 0x48FA40 / 0x535E60 | (game, item): the item's inventory (0x34A360) to 0x535E60 (game, inventory): the cursor item and each item taken out (0x389820 -> 0x38BA30, which also takes the item's stats off its owner's, 0x2F8290) and freed (0x48FAA0). 7 callers: an item's own free, the product routine's Clear Sockets (0x5270FA) and others. Hooked: infinite cube ingredients (acts only on the product routine's call, which returns to 0x5270FF) |
| quest reward giver | 0x517530 | (game, player, item code, level, quality, at the feet) -> the item or 0; 11 callers, all quest code. The item made (0x43D530, mode 4), then into the inventory: the grid of page 0 (0x34A410 (player, page, bank not classic): page 0 the class's grid), the player's inventory (0x34A360), a free spot (0x3865B0 (inventory, item, grid, int32* x, int32* y, page)), the place {0 stored, the page, x \| y << 16, 0} (0x382E10), the inventory's move (0x471500 (player, places, ids, 1, 0) -> al), the client told (0x535F60 flags 1, command 2), identified (0x46E8C0), and when D2RCore's IsCharmUsable (`.maho` 0x3E2B61C, D2RCore export 1054, its 0x816850) says the item counts where it lies, the player's items worked out again (0x470C90 (game, player, 0, 0)). With no room and its last argument set, at the feet: flag 0x4000 off (0x36D8F0), page 0xFF (0x36DE20), body location 0 (0x36D730), mode 3 (0x34E5F0), x and y 0 (0x34E210 / 0x34E280), the player's place (0x34A040: {int32 x, y}) and room (0x34B440), a free spot near it (0x43DE10 (game, room, at, out, 1) -> its room), the item put there (0x43DD00 (game, player, item, room, x, y): the room's lists, mode 3, the drop time, the clients told 0x535C60, the quests 0x5154E0); else freed (0x43EC10). The drop request (0x17, 0x4BD8F0) drops a cursor item the same way. Read (its calls): infinite cube ingredients |
| inventory transactions | 0x472590 / 0x471500 | the player's items (and its pets') copied into a layout (0x473210), changed, checked (0x46E050) and applied (0x470CA0), not while the player has state 0xBA or 7. 0x472590 (player, places checked, places taken out, their ids, counts, flag) takes items out (and checks that the others are where the client says); 0x471500 (player, places, ids, count, flag) -> al puts items at places (an item on the body, in the belt or in the grid has what it gives counted, 0x475E00; the quests told, 0x515400). A place: {int32 mode (0 stored, 4 the cursor), page (0xFF none), x \| y << 16, body location}; 0x382E30 makes the cursor's, 0x382D20 an item's own. Called (0x471500): infinite cube ingredients |
| item notice | 0x535F60 | (game, player, item, flags, command, mode, page, body location, x \| y << 16): the item's flags (data +0x18, 0x2E9 cleared first), its command flags (0x36D830), mode, page, body location and place set for the packet (0x536410 to every client that sees the player), then put back. Commands: 2 an item added (the cube's products, a quest reward), 4 gone (the cube's items at a transmute, with flags 0x20), 0x80 / 0x200 / 0x2000 / 0x40 a pick-up into the grid, the body, the belt, the cursor (0x471950). What a client is sent (0x536410 (player, item, 0, client)) goes by the command flags first (0x40: to the owner's client 0x47D540 -> 0x479CD0, packet 0x9C; 2 or 0x80: 0x479820, packet 0x9D action 4; ...); with none, an item with flag 0x100 (broken) gets packet 0x7D (0x47EF00), and one with flag 1 (the identify routine's update) the update 0x479E50 (packet 0x9D, action 0x14) when it is stored (the owner's client, and others in some states), worn (everyone) or in the belt (the owner's), **nothing in any other mode: an item on the cursor (4) is not updated**. The item packets (0x9C, 0x9D) carry the whole item, written by D2RCore's SerializeItem (`.maho` 0x3E2B6A0, its 0x82FE80); the game's own writer 0x37D140 branches on item flag 0x10 (identified) in several places. 124 callers; D2RCore's routine table names it too (.data 0x6FFDA8). Called: infinite cube ingredients. Hooked: identify on pickup (acts only on the pick-up's call, which returns to 0x471E6E) |
| item pick-up | 0x471950 | (player, item id, to the inventory, reach, path test, into the cube) -> al: the item by its id among the units of the rooms near the player, the path to it free (0x350550 (player, item, 0x804) = 0) when asked, the item selectable (0x3500E0: unit flag 2), nothing on the player's cursor (0x388A70), within reach (0x325140), on the ground (mode 3, body location 0, page 0xFF); the clients told it left the ground (0x535D50: packet 0x0A), its footprint and room let go (0x348B00, 0x38EFE0). Gold (type 4): its amount to the player (0x475C90), the pile freed, 1. Else a place: to the inventory, a stack it merges into (0x4759E0: the item freed, 1), the body for an item the player can wear there (0x36B6A0, which asks the identified flag first: an unidentified item never), the belt (0x373E40 / 0x373D70 / 0x3862D0), else the grid of page 0 (0x34A410 / 0x3865B0), and with no room back on the ground (0x43DD00), event 0x18, 0; into the cube (the last argument) the grid of page 3; else the cursor (0x382E30). Then the inventory's move (0x471500; refused: back on the ground, event 0x14, 0), and last the notice (the call at 0x471E69, back at 0x471E6E): flags 0, the command by the place's mode (0 stored 0x80, 1 the body 0x200, 2 the belt 0x2000, else the cursor 0x40); 1. Callers: the request 0x16 (0x4B9DF0, 17 bytes: +1 the item's id, +5 / +9 where it lies, +0xD to the inventory, else the cursor; reach 4, the path tested), the request 0x5F (0x4BB600, 25 bytes: straight into the Horadric Cube, the cube's id and a spot in it checked first), Telekinesis (0x554880, its do function: to the inventory, reach 0x32, no path test), four routines among the debug commands' (0x40F670, 0x411280, 0x415E20, 0x4170B0); not D2RCore. The server's packet table 0x1D2A790 has a pointer an opcode (0x17 the drop 0x4BD8F0). Read (its notice's return address): identify on pickup |
| item identified | 0x46E8C0 | (game, player, item, u8 the player's own) -> nothing. An item with the identified flag (0x10 at item data +0x18, the test 0x36E2D0) is left alone; else the flag set (0x36D8F0), a stored item that D2RCore's IsCharmUsable (`.maho` 0x3E2B61C) says counts where it lies has its stats added (0x475140 (bank, item, player, 0)) and the player's items worked out again (0x470CA0, 0x34DBE0 (player, 1)), item flag 0x4000 cleared, the clients told (0x535F60 flags 1, command 0: the update, sent for a stored, worn or belt item and not for one on the cursor), 0x34DBE0 (player, 1), unit event 6 (0x491960 (player, 6, player)), and a drop's set or unique item (flag 0x10000000) recorded: with the fourth argument, every player of the game (0x424970 -> 0x490D00 over game +0x2230, players in state 7 or 0xBA left out, 0x4260B0 each: in a game of type 2 or 3, Game +0x101, any; in another only one with 0x43CC70 (its client's +0x18, the item) at most 8), then the item's flags 0x10000000 / 0x20000000 cleared (0x3751C0); without it the player's client alone (0x4247D0). Callers: an Identify scroll's use callback 0x57EC30 (1), Cain's identify-all 0x53C2B0 (1: the stored items of page 0, and of page 3 with its last argument; none with an item on the cursor), the quest reward giver 0x517710 (0), the buy handler 0x53E6AD (0), a console command 0x4103F0 (0). 0x46EA70 (the item service handler's: the flag and a charm's stats, no notice) sets the flag too. Called: identify on pickup |
| Chronicle of a player | 0x347440 | (player) -> its Chronicle: the player data (unit +0x10) + 0x220, after asserts that the unit is a player with data. 11 callers (the item found below, the console's fills, the save, the client's panel). Read (its offset): Chronicle drops |
| Chronicle: an entry | 0x3D87D0 | (Chronicle, u8 kind: 0 set item, 1 unique, 2 runeword, u32 id) -> the entry + 0xC, or 0. The kinds' maps at +0x00 / +0x20 / +0x40, 0x20 bytes each: {u64 buckets (a power of two), Node** buckets, u64 size, float max load (1.0)}; the bucket = MurmurHash3's finalizer of the id (x * 0xFF51AFD7ED558CCD, ^ x >> 33, * 0xC4CEB9FE1A85EC53, ^ x >> 33) & (buckets - 1); a node (0x18 bytes, made by the insert 0x3D79D0, which doubles the buckets above the load; the erase 0x3D9570): +0x00 next, +0x08 u32 id, +0x0C the id again, +0x10 u8 kind, +0x12 u16 a value (0xBF4 from the console's fills; the item's 0x36C390 otherwise), +0x14 u32 minutes (the time it was found). The id is the item's file index (item data +0x34: the UniqueItems or SetItems row) or a runeword's Runes row +0x46. 0x3D86A0 asks the same of an item; the add 0x3471A0 / 0x3470B0 only for an id not there. Read (the layout, not the hash: the maps are walked): Chronicle drops |
| Chronicle: the share found | 0x3D8870 | (Chronicle, u8 kind) -> float: the entries of the game's list of the kind that the kind's map holds, over the list's total (at most 1). The lists are made at the data load (0x3D8F80, jumped to from 0x3148D0) over bank 3's tables, the totals their builders' returns: uniques (0x3D8FC0) {row*, count} at 0x2A9BDB0 / 0x2A9BDB8, total 0x2A9BDA0 = the count; set items and sets (0x3D8DD0) {row*, u64 tag} 16 bytes an entry at 0x2A9BDC8 / 0x2A9BDD0 (tag 0 a SetItems row, 1 a Sets row: counted only tag 0, 0x3D77F0), total 0x2A9BD98 = the tag-0 entries; runewords (0x3D8B00) at 0x2A9BDE0 / 0x2A9BDE8 (Runes rows, the id at +0x46), total 0x2A9BDA8. An entry's id is its row's +0x00 u16 (the row's index, filled at the load; the loader's compiled rows have 0 there). A row is taken when the row test 0x375F80 passes its flags (+0x2C: neither of the single-bit masks 0x1D996D8 (4, disabled) and 0x1D996DC (8, disableChronicle)) and its code has an Items row (0x313EF0), a unique's no quest item (Items +0x13A); a set item among the six of a Sets row (+0x130; Sets rows 0x160 bytes at data tables +0x1388, count +0x1390; SetItems +0x13A8 / +0x13B0; UniqueItems +0x13C8 / +0x13D0). In 3.3 (worked out over the loader's txt copies): 403 of the 438 unique rows (14 have no code: the dividers and nine rows of old items; 15 more are disableChronicle: the 2.x Sunder Charms and their crafted versions, a second Azurewrath, Constricting Ring, Darkfear ...; 6 have a quest item for a base: the Viper amulet, the Staff of Kings, the Horadric Staff, the Hell Forge Hammer, Khalim's two flails), and 135 of the 140 set item rows (Warlord's Glory is disableChronicle). Read (the lists and totals): Chronicle drops |
| Chronicle: an item found | 0x4247D0 / 0x425F60 | (client, item), from the identify 0x46E8C0 and 0x46EB00 (an item put in another's socket, which may make a runeword): an item with flag 0x10000000 (where and when it was found: a drop's set or unique item) or a runeword (0x4000000) gets its entry (0x425F60: kind 1 for quality 7, 0 for 5, 2 a runeword; the id; the minutes now), added when the player's Chronicle has it not, the client told (packet 0x87, sub-command 0 add, 1 remove, 2 clear), and the item's flags 0x10000000 and 0x20000000 cleared (0x3751C0). So an item counts as found once it is identified. 0x4260B0 does the same for each player of the game, the callback of the identify routine's other branch (its fourth argument set, as a scroll and Cain call it; the record: item identified). Called (through the identify routine): identify on pickup |
| Chronicle in the save | 0x530900 | (game, player, buffer): the shared stash's section of kind 2 ({0xAA55AA55, 2, 105, 0, u16 size, u16 stat 0xB8, u32 2} then 0x40 bytes of header in all), its body D2RCore's (`.maho` 0x3E2B4D2): u32 0xC0EAEDC0, u16 version 1, u16 set items, u16 uniques, u16 runewords, 8 bytes, then 10 bytes an entry {u32 id, u16 value, u32 minutes} kind by kind, then the reward selection. The player's maps are filled from it (0x52FAE0 -> 0x3470B0) as the stash is read. Read by hand (Joshua's modern stash, 2026-09-30: 61 set items, 317 uniques, 7 runewords; 86 uniques and 74 set items missing) |
| Chronicle console commands | 0x414990 .. 0x414FD0 | the game's own, named by D2RCore's command table: chronicleFill 0x414990 (all three), chronicleFillUniques 0x414BB0, chronicleFillSets 0x4149E0 (tag-0 entries only), chronicleFillRunewords 0x414D70 (each with a percent chance, a random time in the last 20 years), chronicleWipe 0x414F30, chronicleRemoveUnique / Set / Rune 0x414F50 / 0x414F90 / 0x414FD0 (an id). Not called |
| AI dispatcher | 0x4A2A00 | (game, monster; its two callers, 0x4449E0 and 0x444D40 (the AI think event, type 2), pass three zeros after them): the game's AI debug switch first (dword 0x2AA6204: 1 every monster idle, 2 every one not on the players' side; the idle tactic 0x4A6D10 for 15 frames), the monster's AI record (monster data +0x30: +0x00 the AI mode (0 the monster's own AI, its MonStats +0x52; 1 .. 17 a mode of the second table, 0x23981F0, laid out alike: 4 the mercenary's think, 7 NecroPet's, ...; the queued changes switch to them), +0x08 its think routine, +0x10 a state, +0x14 a wrapper index (0 / 1: the two AiPet wrappers at 0x2396E70, 16 bytes {flags, routine}), +0x20 queued AI changes {entries, count +0x28}, +0x3C / +0x40 a unit to go to (id, type; set by 0x5971B0), +0x44.. an AI's own params, +0x68 / +0x6C the owner's id and type), a pre-step (0x4A3A20: doors, the unit to go to), the AI table's entry (0x4A36C0 (unit, mode) -> for mode 0 0x2396E90 + 0x20 x the MonStats AI, 155 entries, monai.txt's rows; else 0x23981F0 + 0x20 x mode (10 .. 12 only for a unit 0x34C730 passes): +0x00 a target mode, +0x08 its init (run by 0x4A2690 when the AI is set), +0x10 its think, +0x18 an alternative think), the tick's target by the mode (1 / 3 0x4A69A0, 2 the general finder 0x595750, 5 / 6 0x4A6760; 0 none), then the think (game, monster, tick: {AI record, ?, the target, ?, its distance +0x20, in melee range +0x24, MonStats +0x28, MonStats2 +0x30, difficulty +0x38}), or the wrapper with it. Hooked: passive pets (the thread marked for a passive pet's tick) |
| pet AIs | | the think of each, by monai id: 61 Hireable 0x5BEAC0 (every mercenary: the finder 0x595F80, then its attack 0x5BF6E0), 67 NecroPet 0x5C1400 (golems, Valkyrie, skeletons, mages, the old wolf and bear: the owner's nearest enemy by 0x595750 called with the owner, its own by 0x4A6720, 0x595750 within a range), 86 Hydra 0x5CBD80 (mode 2), 101 AssassinSentry 0x5CD370, 102 BladeCreeper 0x5C7900, 103 InvisoPet 0x5CBE80, 104 DeathSentry 0x5C8A80 (0x595F80; Corpse Explosion only on a corpse beside the target found), 105 ShadowWarrior 0x5CEEA0 (mode 2), 106 ShadowMaster 0x5CD740 (mode 2, and a unit search with its own filter 0x5D1360), 107 Raven 0x5CCA50 (mode 2, and the player's own target, player data +0x1D8 / +0x1DC, checked by 0x5956D0, which asks 0x597710), 108 DruidWolf 0x5CA610, 109 Totem 0x5CF810 (the Druid's spirits, no attack), 110 Vines 0x5D0630 (mode 2), 111 CycleOfLife 0x5C83C0 (mode 2; eats a corpse near its owner, found by 0x584CE0, no enemy asked), 112 DruidBear 0x5C95D0, 148 GenericPet 0x5D2F70 (the Warlock's three demons, warlock goatman, bighead and putrid defiler; AiPet.cpp, new in 3.x). Every one, a monster's own AI or a mode of the second table, is run by the dispatcher. Read: passive pets |
| AiPet.cpp (GenericPet) | 0x5D2F70 / 0x5D3530 / 0x5D4960 / 0x5D4F30 / 0x5D2170 | the Warlock demons' AI: the pet's skills listed (0x5D5680, 0x28 bytes a skill), the units near put to the filter 0x5D4F30 (the enemy test first: a unit that is no enemy is passed over) and each skill given a target and a score, the best used; the target it took kept as the monster's kept target at the end of the tick (0x5D2170: cleared by 0x5449A0, set by 0x5459A0) and read back at the next (0x5D5A70, through the kind getter). The two AI wrappers 0x5D7E10 / 0x5D7B60 (a monster with AI 0 whose record's +0x10 is 1, chosen by its +0x14) run the same around a think. Read: passive pets |
| enemy test | 0x492790 / 0x48E460 / 0x48E470 | (game, unit, other, u8 flag in r9b) -> nonzero when other is an enemy: both taken to their owners (a monster's by 0x4A53C0, a missile's by its owner fields +0xE8 / +0xEC), not the same unit; two players by their party relation (with the flag: the relation's bit 8), anything else by the alignment test (not on the same side). Reached only through its two entry stubs: `mov r9b, 1` 0x48E460 (39 callers: the AI, skills, the curse steps, auras, thorns) and `xor r9d, r9d` 0x48E470 (Confuse's step). The AI's own checks 0x597710 (both units a player or a monster, neither dead, the other not in a town room, attackable, not in state 0x92) and 0x597840 (the other a live, attackable player or monster), the unit searches' filters, end in a jump to the first stub. Hooked: passive pets |
| alignment test | 0x2F32D0 | (a, b) -> 1 on the same side: the same unit, both of alignment 0 (the monsters') or both 2 (the players'); 1 (neutral) is no one's side. The alignment getter 0x2F4190 is D2RCore's (`.maho`). 12 callers, the client's hostility test among them. Read |
| AI unit search | 0x596340 / 0x1D3F910 | (game, unit, &result, filter, mode on the stack, 1..10): the units of the rooms near the unit, each put to a filter: the mode's own (table 0x1D3F910, 16 bytes {how the rooms are walked, filter}), or the one given (modes 1, 2). 3 0x597D60 (no enemy by the enemy test: an ally; only Baal's throne asks), 4 0x597E00 (an enemy by 0x597840; no town rooms), 5 0x597BB0 / 6 0x597CD0 (an enemy by 0x597710), 7 objects (doors), 8 the unit's own dead minions (0x5979A0), 9 0x5978B0 (an enemy by 0x597840), 10 a class counted. Own filters: the Warlock demons' 0x5D4F30, the Shadow Master's 0x5D1360, the Overseer's 0x5C2700 (whose minions are the ones no enemy), ... Read: passive pets |
| AI target finders | 0x595750 / 0x595F80 / 0x598010 | 0x595750 (game, unit, AI record, &distance, &in melee range, difficulty) -> the target: the kept target first (0x598010), then for a unit on the monsters' side the players near (game +0x21E0), for any other the searches of modes 4 and 6; the dispatcher's for mode 2, and the pet AIs' with the pet or its owner. 0x595F80 (game, unit, &distance, &in range): the kept target, then modes 5 and 6 (the mercenary, most monsters' AIs). 0x598010 (game, unit, ...) -> the kept target by its kind (the getter below): 1 a player, 2 a monster, 4 a missile, by id; 3 (Confuse) a search with the unit's alignment changed for it (0x48E600); one dead or out of reach cleared (0x5449A0). Read: passive pets |
| kept target | 0x544A00 / 0x544A60 / 0x5459A0 / 0x5449A0 | a monster's kept target: monster data +0x44 its kind, +0x40 its id. The kind's getter (monster) -> u32 (callers 0x598010 and AiPet's 0x5D5A70), the id's 0x544A60, the setter 0x5459A0 (monster, kind, id: only for a monster whose MonStats has switchai, flags +0x3C bit 16, which every pet has but the Valkyrie, the invisible pet and the three vines), the clear 0x5449A0. Set by Attract's step 0x55ABB0 (only on a monster of the monsters' side, 0x559E40) and Confuse's 0x55AC40 (only on an enemy of the caster): never on a pet; by a summoning skill's spawn helper 0x520930 (server do function 155), the init of AI mode 11 (0x5CD2F0) and the Warlock demons' AI (0x5D2170). Hooked (the kind's getter): passive pets |
| game events | 0x48B720 / 0x48BE80 / 0x48B890 / 0x48CC10 | an event: +0x00 u8 type (0..14), +0x02 flags, +0x04 the frame it is for, +0x08 its unit, +0x18 / +0x1C / +0x20 three parameters, +0x28 .. +0x38 list links, +0x48 a callback or 0. The add 0x48B720 (game, unit, type, frame, p1, p2, p3; no callback) goes to 0x48BE80 (game, unit, type, frame, callback, p1, p2, p3): a frame not after the game's (+0x170) is made the next one. 0x48B890 (game, unit, type, 0) walks the unit's events (0x48FE50) for that type: Revive calls it before it puts the AI's think event again. 0x48CC10 (game, event) frees one. Read |
| monster events | 0x48C790 / 0x447420 / 0x1D1A7A0 | the events are run by an executor of the unit's type (five of them, 0x48C4D0 .. 0x48CA80, each with its own handler); the monsters' 0x48C790 hands each event of the frame to its callback, else to 0x447420 (game, monster, type, p1, p2, p3; it asserts a monster), then frees it (0x48CC10). 0x447420: nothing while the byte 0x2AAEA9C is set; types 6, 7, 10 and 11 are skipped while the monster is frozen (state 1) and not dead, and the event is freed all the same (a revive frozen as its time ends stays: the game's own); type 2 (the AI's think) is put off 25 frames when 0x446E40 says so (asked with 0x4A3010's answer); else the table 0x1D1A7A0 by type: 0 0x446EB0, 1 0x446FA0, 2 0x4A2A00 (the AI dispatcher), 3 0x448C00, 5 0x435520, 6 0x4485F0, 7 0x498EB0 (a monster mod's event), 8 0x437460, 9 0x437230, 10 0x447E30, 12 0x42EA80. Read |
| monster mods | 0x4995E0 / 0x2395FE0 / 0x498EB0 / 0x49D240 / 0x2396210 | a mod added (game, monster, mod, flag): into the first free of the monster's 9 (0x38E310), then the mod's maker (table 0x2395FE0, a pointer a mod; MonUMod 21 has none). A mod's event, type 7 with the mod its first parameter: 0x498EB0 -> 0x49D240 (game, monster, 0, kind 2, mod, p2), which runs the mod's routine of that kind when the monster has the mod: records 0x30 bytes a mod at 0x2396210, six routines by kind (0 none; kind 2 the timer at +0x10, kind 5 at +0x28 handed the third argument). MonUMod 21 is killself (monumod.txt): its record has only the timer 0x4A1D20, the one pointer to it in the image (0x2396610); D2RCore names none of these. Read |
| killself timer | 0x4A1D20 | (game, monster, mod, the monster's unique flag (0x38E870 (monster, 8))): a dead monster (0x34C2C0) nothing; one with state 0x36 (uninterruptable) the same event again at the game's frame + 3; else its owner (0x4A53C0): a player -> the pet removal 0x4FFD30 (game, owner, the monster's id, 1); no owner, or another kind -> the death mode (0x4471E0 (monster, 0, record), 0x4475C0 (game, record, 1)). Hooked: permanent revives |
| killself's users | 0x55E7E0 / 0x556560 / 0x5710C0 / 0x584480 / 0x5A8750 | each adds mod 21 (0x4995E0) and its event (type 7, the mod its parameter) at the game's frame + a length. Revive (srvdofunc 58, 0x55E7E0): the corpse (0x48FE20) through 0x55A510, 0x55EDB0 and 0x55F8E0, its life (stats 7 and 6) and level, the owner (0x4A5800 (game, pet, owner id, owner type)), the unit to go to (0x5971B0), the AI's think event (type 2) 15 frames on, alignment 2 (0x48E600), the kept target cleared, unit flag 0x80000000 (0x34E190), state 0x60 revive (0x3354C0), then the skill's calc2 (Skills row +0x194, 0x3B5160: 4500 frames) above 0 -> killself and its event, then the pet registration 0x4FEB00 (game, owner, pet, the row's pet type +0x112 (a byte, below 0 none), the petmax calc +0x114). Decoy (srvdofunc 15, 0x556560: calc2 ln12, 250 frames and 125 more a level). The shadows (srvdofunc 49, 0x5710C0): only with a length (Skills row +0x80, auralencalc), none in 3.3. Reanimate As (ItemStatCost item_reanimate, itemevent kill, itemeventfunc 31 0x584320 in the item event table 0x238E5C0, which hands 0x584480 to 0x588550 as a callback): state 0x60, killself at frame + 1500, an owner but no pet registration, so in no pet list; Tomb Reaver and the Faith runeword in 3.3. 0x5A8750 (its callers 0x5A80C0 .. 0x5A85A0): monsters it spawns, killself at a random time. Read |
| pet lists | 0x4FF3B0 / 0x4FFD30 / 0x5013F0 / 0x501550 / 0x500D80 / 0x501BC0 | a player's data (0x34B240, which asserts on anything else) +0x98: the pet lists, {entries, ...}; an entry 0x20 bytes a pet type (0x5013F0 (bank, lists, type): +0x00 the first node, +0x08 count, +0x0C max, +0x10 a group record; the number of types is the PetType rows', data tables +0x12E0); a node 0x20 bytes: +0x00 flags, +0x04 the unit's id, +0x18 the next. The lookup 0x4FF3B0 (player, id) -> the type whose list has the id, 0 none (types 1 up). The removal 0x4FFD30 (game, owner, id, kill): the lookup first (0 and kill: the unit killed by id, 0x500FB0), the node unlinked, the clients told (0x490D00), the unit killed (kill, 0x500FB0) or its flag 0x80000000 cleared. The registration 0x4FEB00 -> 0x501550 (game, owner, pet, type, max): the other types of its group out, the type's max set (0x501830), then 0x500D80: at the max the list is trimmed from its head (0x501BC0: 0x4FFD30 with kill), the new node appended at its tail, so the oldest gives way. PetType rows (pettype.txt order): 0 none, 1 single, 2 valkyrie, 3 golem, 4 skeleton, 5 skeletonmage, 6 revive, 7 hireable, 8 dopplezon ... 21 binddemon; a Skills row's +0x112 names its pets' (the loader's compiled rows, tests/test_revive.cpp). Called (the lookup): permanent revives |
| automap: the units' pass | 0xD2600 | (draw args...): the automap panel's draw (its one caller 0x14E3480): the view made (0xD1940), a clip pushed (0x79D920; popped 0x79B6B0 at the end), the active layer's lists drawn (0xD6F10: floor +0x08, walls +0x30, units' icons +0x58, +0x80), then for each room of the player's room's near list (0x2EFDE0: ActiveRoom +0x00, count +0x40; the room itself is in it, as the unit name getter's skip of it shows) each unit of the room (ActiveRoom +0xA8, next 0x34B4A0) to the unit draw below, then the party members the client has no unit of (marker 1, names). Read |
| automap: draw one unit | 0xD76E0 | (unit, view): the marker decision (0xD78F0), the party option (0x2A2CF98: markers 1 and 4 only with it on), the unit's pixels (0x34AF60 / 0x34AFB0: its path's +0x08 / +0x0C, any unit) through the transform 0xD4910 (view, out, x \| y << 32 pixels) -> out (x \| y << 32 on the screen: view +0x28 / +0x2C origin, +0x10 / +0x14 the centre and +0x30 / +0x34 the scale, the pixels / 10 plus (1, -3)), inside the view's rectangle (+0x18 x, +0x1C y, +0x20 width, +0x24 height), the marker 0xD6DB0 (point, marker 0..7, float scale = view +0x38), then with the names option (0x2A2CF9C, "AutoMap Party Names") the name by its kind: 0 / 1 / 2 a player's (0x34B240 through 0xD6AA0, colors 2 / 1), 3 the owner's, 4 the client's unit name (0x9A1B0) in color 4 through the name draw 0xD6B20 (UTF-8, point, scale, color: centred, the font's height above), 5 the string 0xCF3 (the stash). Hooked: named enemies on the map |
| automap: the marker decision | 0xD78F0 | (unit, &marker, &name kind) -> whether drawn: a player: the local one 0, the party 1 (name 0), another 5 (name 1), a dead one in state 7 marker 2; a monster (not dead, not 0x34F8D0): an NPC row (MonStats flags byte +0x3D & 2: interact) marker 6 with its name (kind 4); a pet: 3 yours, 4 the party's, 5 another's with its owner's name; under the reveal-all byte (0x2A2ADD4) every other monster marker 5 (class 0x216 6), no name; objects: a town portal (59) marker 7, a permanent one (60) too but in levels 111, 112, 117, 125..127, the stash (267) only its name (5). The sprite "AUTOMAP/Units" (hd/global/ui/automap/units.sprite: 8 frames of 70 x 36, crosses): 0 blue, 1 green, 2 magenta, 3 slate, 4 dark green, 5 red, 6 white, 7 yellow. D2RCore lists it (and the reveal) among the routines it binds, and calls neither. Read |
| automap: reveal a room | 0xD6550 | (u8 bank, ActiveRoom, force, layer owner): the floor tiles (0x2EFB70 (room, &count): ActiveRoom +0x08, its +0x20 tiles, +0x28 count; 0x48 bytes a tile, +0x18 flags) each not flag 8, and seen (0x20000), the reveal-all byte or force, to the per-tile add onto the owner's +0x08; the walls (0x2EFDF0) onto +0x30; then a tail jump to its unit pass 0xD6680 (bank, owner +0x58): each unit of the room drawn on the screen already (unit flag 0x10000000, 0x349C60; set by the world's unit draw 0x1CC3B0 when it drew the unit) and not on the map yet (0x20000000, 0x349C20; set by 0x34DFD0), or all under the reveal-all byte: a monster by its MonStats2 row's cell (+0x118: in 3.3 only barricadetower's, 1258), an object by its Objects row's (0x38FD00 (bank, class), +0x164: 148 classes in 3.3, shrines 310, wells 309, waypoints 307, quest objects ...) with three rules: the stash (267, cell 319) only where the act of the room's level (`.maho` 0x3E2B5C8) is 2 or 3, the sewer stairs (366) only in mode 2, the valley waypoint object (402) only in level 74; the unit cell add 0xD52B0 (unit, cell, list): the unit's pixels / 10 plus (1, -3) as the key {u16 0, u16 cell, int32 x, int32 y}, the list insert 0xD1460 (list, {node, bool} out, key): a red-black tree that keeps a key once (0xD4B70 finds the place), nodes 0x30 bytes from the game's allocator. Called (the insert): landmarks on the map |
| automap: a tile's cell | 0xD5160 / 0x32BDF0 | the per-tile add (u8 bank, tile, DRLG room, list): a tile on the map already (flag 0x40000) is left, else the flag set, the level type (0x3269B0: the level record's +0x34), the tile's graphics record's type +0x14, style +0x18 and sequence +0x1C (0x1460210 / 0x1460190 / 0x1460150) to the lookup 0x32BDF0, and a cell other than -1 put on the list at the tile's place (room +0x60 / +0x64 plus the tile's +0x08 / +0x0C, tiles to pixels 0x334EF0, / 10, +0x18 to y when the tile's +0x28 is 0x10 or more), key tag 0, through the list insert. The lookup: records {data, count} at 0x2A9A8C8, 0x20 bytes (+0 level type, +4 tile type, +8 style (0xFF any), +9 / +0xA the first and last sequence (0xFF at +9 any), +0xC cells, +0x1C how many), by level type the range [first, last) at 0x2A9A8E0 (int32 pairs); the first that fits; with a count of 1 or more one cell picked by the automap's own stream (0x2372A40, stepped every time), else the first. automap.txt's "Waypoint" rows (Acts 1..4: 19, one cell, 307) are a waypoint's floor tiles; Act 5's waypoints and the Arcane Sanctuary's are objects with the icon 307. Called (the per-tile add, for a tile the table gives the waypoint's cell; the table read, not queried): landmarks on the map |
| a room's preset units | 0x3DE0E0 / 0x3DF600 / 0x3DBB60 | CreateActiveRoom's preset step (u8 bank, DRLG room), for a preset room (+0x74 == 2) whose flag bit 25 is clear: the room's +0x40 -> +0x08 is its preset part; when its DS1 (+0x10) is not loaded (0x3DCF10) the units are made (0x3DF600 (bank, part, the room's seed +0x30)): each of the DS1's (+0x98, next +0x10; type +0x20, class +0x04) copied (0x3DBB60: 0x30 bytes, type +0x20, class +0x04, x +0x08 and y +0x24 the DS1's plus the room's subtiles (+0x18 / +0x1C x 5), a path +0x18) and linked at the part's +0x58 by +0x10; some object classes (0xC4, 0x105 a half, 0x245 a quarter) and the layout's random monster places (0x21..0x23 past the super uniques) kept by the room's seed; a monster's class is a MonStats row below the table's count, then a super unique's row (count + index; the SuperUniques count 0x3971E0: data tables +0x1080). Then bit 25 set. Level 2's preset rooms of types 4..7 get a monster unit of their own (0x360BF0). The outdoor shrines and waypoints are preset rooms (D2RCore's PlaceOutdoorShrines / PlaceOutdoorWaypoint place lvlprest records, 0x32C360); the shrines and wells of objgroup.txt are made by the server when it first fills a room (0x4042A0 -> 0x503790, as a player comes near; the game fills a far room only for a town portal's other end), in no layout: every area whose levels.txt ObjGrp names a group with them (3.3: Act 3's jungle (31 Jungle Wells, 43 jungle shrines), Kurast (43, 76) and Travincal (61, 62), Act 4's (46), Act 5's (99 .. 106, 111 .. 115), every dungeon's); Act 1's and Act 2's outdoor groups have none (rogue corpses, chests, rocks, jugs). **These are the part's units, not yet the room's**: CreateActiveRoom's build step then moves the ones that stand in the room into the room's own list (next row), so after a build the part keeps only other rooms' units. Read: landmarks, named enemies (until 2026-10-02 the part's list was read after the build, and most rooms' units never came) |
| a room's own preset units | 0x328FD0 / 0x3F38D0 / 0x3DE420 / 0x360BF0 | the build step (u8 bank, DRLG room), CreateActiveRoom's for a room whose flag bit 20 is clear: the near links (0x3608A0, when the room has no near rooms yet), the static grids 0x3F38D0 (a room of type 1 0x3F95E0; type 2 a tail jump to 0x3DE420, which after the grids moves every unit of the part's list (+0x58) that stands in the room (0x3605B0, the room's subtiles) into the room's own list (+0x98, by +0x10), x and y less the room's tile x and y (+0x60 / +0x64) times five), the map tiles 0x3F3930, the ActiveRoom (0x326480). The room preset add 0x360BF0 (bank, room, type, class, mode, x, y): a 0x30-byte unit {+0 mode, +4 class, +8 x, +0x10 next, +0x20 type, +0x24 y} put on the same list; the maze steps (0x3DF2E0, 0x3DF540, 0x3E06F1, 0x3F40DE) and the warp tiles (0x3F4670) add theirs so. Read: landmarks, named enemies, area names at exits |
| near links | 0x3608A0 / 0x3614C0 / 0x361750 | (u8 bank, DRLG room), from the build step and 0x328DA0: the room's near rooms of its own area (0x3614C0: the array at +0x10 cleared (+0x18 count), the area's rooms (+0x90 -> +0x10, next +0x48) within 6 tiles of it, sorted), then for each of its flags' bits 4..11 (`test [room+0x50], 0xFF0`; none for level 133) the area that vis slot names (0x360800) - made (GetLevel 0x3267C0) and its rooms made (0x3271C0) when they are not -, and 0x361750 (bank, room, slot, the other area's first room, its slot back, the warp id 0x3DAAD0): for a warp (id not -1) the first room of the other area whose flags have the slot back is pushed among the near rooms and gets a link node (0x30 bytes: +0 that room, +8 the next, +0x10 1, +0x20 the warp's record 0x3DA970 (bank, area, slot, 'b'); the room's +0x78 the list's head); for a walk-through (id -1) every room of the other area with the slot back within 6 tiles is pushed among the near rooms, no node. Last, flag 0x800000 for a room whose area is not one 0x327850 says and that has a near room in one. Read (the near rooms, the links): area names at exits |
| vis and warp slots | 0x360800 / 0x3DAAD0 / 0x326A80 / 0x3F1C60 / 0x3EDE60 | an area's eight vis slots (0x360800 (bank, DRLG, area) -> int[8]) and their warp ids (0x3DAAD0 (bank, area record, slot)): the DRLG's own list first (+0x118: records 0x50 bytes {+0 area, +4 vis[8], +0x24 warp[8], +0x48 next}, made by 0x326A80 as a copy of the area's LevelDefs row, +0x48 vis, +0x68 warp), else the row's. The outdoor link generator 0x3F1C60 (an act's outdoor areas placed by its link table: each area's place and size into its record, +0x24 x, +0x28 y, +0x2C width, +0x30 height, tiles; 0x327650 sets them from LevelDefs for the others) gives two linked areas a vis slot each other's with warp -1 (0x327760), unless the DRLG's +0x870 byte is 4. levels.txt names no vis between outdoor areas (Blood Moor's slots 3..6 are the Den of Evil's four ways in, warps 0..3). An outdoor area's room flags (0x3EDE60, by its 8-tile grid cell): every cell of the area's edge toward a linked area gets that area's slot bit - the whole border, not the gap. Read (through the near links): area names at exits |
| warp records (LvlWarp) | 0x3DA970 | (u8 bank, area record, slot, direction 'l' / 'r' / 'b') -> the record: data tables +0x1890 (count +0x1898), 0x60 bytes: +0x00 name, +0x2C id (the slot's warp id), +0x48 / +0x4C OffsetX / OffsetY, +0x5C direction ('b' both). Matches the loader's lvlwarp.bin. Read: area names at exits |
| a warp tile's preset unit | 0x3F4670 | (bank, DRLG room, tile x, y, tile flags, orientation 0xB right else left): the record for the area, the slot in the tile's flags (>> 0x14 & 0x3F) and the direction; a unit of type 5 put on the room's list by the room preset add, its class the record's id (+0x2C), at the tile less the room's tile x and y, times five, plus the record's offsets. Read: area names at exits (a type 5 preset unit with a link whose record has its id is the warp's tile) |
| collision map | 0x366250 / 0x2EFB30 | (room, x, y, mask) -> the flags at a subtile (0x27 when no room has it): the room that holds the point (0x38DFB0: 0x2F0680 on the ActiveRoom's subtiles +0x80 .. +0x8C, else its near rooms 0x2EFDE0), its collision map (0x2EFB30: ActiveRoom +0x38) {+0 x, +4 y, +8 width, +0x0C height (subtiles), +0x10 / +0x14 tiles, +0x20 u16 flags, row by row}. The bits (D2's): 1 a wall, 2 sight, 4 missiles, 8 a player does not cross, 0x20 no floor, 0x400 an object, 0x800 a door, 0x1000 a unit; the game places a player with the mask 0x1C09 (0x491CA0, 0x2EF8B0). Read: area names at exits (walkable: neither 1 nor 8) |
| the sewer stairs, stairs | 0x5E9DA0 / 0x5A11A0 / 0x590160 | object operate functions 44 (Act 3's sewer stairs, class 366: only in mode 2, open, then 0x5A11A0: the player warped (0x491FF0) through the tile unit (type 5) in the stairs' room) and 47 (the "stair" objects 194 / 195: opened, then through the room's tile unit too). The lever (class 367, OperateFn 45) has an icon of its own (cell 304). Act 3's Sewers Level 1 (92) names no vis slot for Level 2 (93): its four are the Bazaar's and Upper Kurast's (warps 58 / 59); the way down is the stairs over warp 60 ("Act 3 Sewer Down", NoInteract). Read: landmarks (the stairs put on ahead) |
| server: the fill pass | 0x408670 / 0x406550 | the server's frame (0x406550: the frame counter, each act's rooms near its players (0x352410, 0x351DF0), then 0x408670, the events 0x48B5C0, D2RCore's step, ..., every 11 frames each act's level free 0x2EFFF0) runs the fill pass once: for each act of the game (game +0x190, the data tables' +0x108 acts), while the act's +0x84 says rooms were added (cleared, 0x2F0400), every room of its list (head +0x18, 0x192B20; next ActiveRoom +0xB0, 0x2EFC50) down to the head of the walk before: D2RCore's PopulateRoomSpecialMonsters (`.maho` 0x3E2B5C2), then for a room not filled (ActiveRoom +0x54 & 1, 0x2F0600) its presets (0x48F190), random monsters and saved units (0x503790), object groups (0x510F00) and terror step (0x50ACF0), the bit set (0x2F04E0), and +0x54 \| 2 (0x2F0440). A room built goes on its act's list: CreateActiveRoom's ActiveRoom maker 0x326480 calls D2RCore's AllocateActiveRoom (`.maho` 0x3E2B5BC, D2RCore 0x80E000), whose original 0x2EF340 allocates 0xB8 bytes, seeds +0xA0 from the DRLG room's stepped seed, links the room at the act's head (+0xB0 the old head) and sets the act's +0x84. So a room the plugin builds is filled by this pass. Checked (signature, the steps and the bit): rooms filled ahead |
| far room built and filled | 0x328900 / 0x2F0810 / 0x4042A0 | 0x2F0810 (bank, act, x, y): the act's DRLG (+0x70) to 0x328900 (bank, DRLG, x, y): the DRLG room at that place (0x327400) built as CreateActiveRoom builds one; 0x4042A0 (game, ActiveRoom) fills it at once (the pass's steps). The game's own far room: a town portal's other end (0x490070) and the console's warp (0x4100E0). Not called (the pass fills the rooms the plugin builds) |
| object groups | 0x510F00 / 0x512DB0 / 0x38FD90 | (game, ActiveRoom), the fill's third step: the Levels row (0x32C4A0), a record of the level's (0x512DB0 (game, level): +4 the rooms filled so far, counted here, +8 the level's rooms (0x2EFA20), +0x10 a flag); unless Levels +0x178 sends it to 0x511580 first: for each of the eight groups (Levels +0xED, a byte; its chance +0xF5) a step of the room's seed (0x2EFBF0: the ActiveRoom's {lo, hi}), lo % 100, set to 100 (no roll) past three quarters of the level's rooms filled while the record's +0x10 is 0 and the group's row says so (+0x127); below the chance, another step, the group's entry by its PROBs (ObjGroup rows 0x30 bytes: +0 the classes, +0x20 DENSITY, +0x28 PROB; 0x38FD90 (bank, group)), the object's Objects row's PopulateFn (+0x15D, the table 0x239BC10) called with (game, room, the density, the class, 100). The random monsters (0x505D70) step the same seed before it, and the populate functions find free spots among the units already in the room. So what a room rolls goes by what its monsters took and by the order the level's rooms are filled in: nothing can say it before the game fills the room. 3.3: 95 levels can roll an object with an icon (tests/test_mapmarks.cpp; shrines 310, wells 309, Act 5's outdoor waypoints 307 in group 112); Act 1's and Act 2's outdoor groups have none. Read: rooms filled ahead |
| room activation levels | 0x328F60 / 0x328DA0 / 0x328710 / 0x328680 / 0x328780 / 0x328BF0 | a DRLG room's counts at four levels (+0x28 short[4]) and its level (+0x70: 0 a player's own room, 1 built (its ActiveRoom made, filled by the pass), 2 and 3 its data loaded, 4 none); each level's room list in the DRLG (+0x130 + level x 0x1C0, linked by +0x38 / +0x1B0). 0x328F60 (bank, room, level): the room's near rooms one level further (0x328DA0, recursive, levels up to 3), then the room at its level (the table 0x2372700: 0 0x328270, 1 0x3282F0 (builds: 0x328FD0), 2 0x3283D0 (builds only with a level-1 count), 3 0x328560 (loads tiles and presets)). A player's room change 0x328710 (bank, old, new) holds the new one at 0 and lets the old one go; the client's room packets hold and let go at level 1 (0x2EF320 -> 0x328680, 0x2EF700 -> 0x328780). Going down: 0x328620 for levels 0..2 (the level from the counts), 0x328630 for 3, which frees the room (0x3F3AA0: its ActiveRoom through 0x2F0110, which unlinks it and hands its units over; its data) only when its level comes to 4 and the DRLG's +0x110 bit 0 is set: the act maker 0x2EF1C0's fourth argument, 1 for the client (0x86BC0), 0 for the server (0x48AA50). So the server frees no room: a room it built stays built, its units with it, and its level free (0x2EFFF0 / 0x327370: a level no player is in or next to, +0x0C 0, after a countdown +0x1FC; 0x327E70 wants every room at level 4 without the built bit +0x50 & 0x100000, set by 0x3F3930; then 0x327C40) never comes for a level with a built room. Read: rooms filled ahead |
| a room's units | 0x2EFD90 / 0x34B4A0 | an ActiveRoom's first unit (+0xA8) and a unit's next in its room (+0x160): the pick-up's walk over the rooms near the player (0x471997 ..). ActiveRoom +0x54: bit 0 filled (presets and objects), bit 1 its monsters (0x2F05F0 / 0x2F0440). Read: rooms filled ahead |
| a room's monsters made | 0x503790 | (game, ActiveRoom): the server's population of a room, the first time it is activated (from 0x4042A0, which makes a room's units and wakes its sleeping ones, and 0x408670): the random monsters (0x505D70, whose packs make random uniques and champions through 0x49AF30 -> 0x49EDB0) and the layout's preset monsters (0x48DC50 -> 0x50A050: a super unique through its maker 0x499BA0). So nothing knows of a random unique or a Herald before a player has come near its room. Read: named enemies (why they are marked from when they are first seen) |
| SuperUniques rows | 0x499BA0 | data tables +0x1078 (count +0x1080), 0x3C bytes: +0x00 u16 the row, +0x02 u16 the name's string id (the game fills it in at its load; 0 in the loader's compiled rows), +0x08 the MonStats class, +0x0C hcIdx (the maker's switch on special ones). The maker (0x499BA0) sets the monster's kind 2 and 8 and its super unique id (0x38E810: monster data +0x2A; the client's monster from its packet 0x98FA0 too). Read (the name): named enemies |
| a static path's place | 0x34D540 | (unit, room, x, y): +0x00 the room, +0x10 / +0x14 the subtiles, +0x08 / +0x0C the pixels (0x334E00: (x - y) * 16, (x + y) * 8). So a layout object's icon key is the one the reveal gives its unit. Read |
| item requirement test | 0x36BC50 | (item, unit, equipping, int* strength met, int* dexterity met, int* level met, body location) -> nonzero when the unit can use the item. The three flags set to 0, then for an item (type 4) with its Items row (0x314110): strength, the row's +0x11A plus a percent of it (the item's stat 91 item_req_percent and 0x33D4F0 (unit, item, 0, 6), both D2RCore jumps), 10 less for an ethereal item (0x400000), against the unit's stat 0 (nothing at 0 or below), and with `equipping` the item's own strength (0x3768C0, when it has a stat list 0x2F8120) not counted; dexterity the same with +0x11C and stat 2; level, D2RCore's level requirement (0x376DE0, `.maho` ReadWideItemLevelRequirement: the item's, its sockets', a runeword's; -1 none) against stat 12; the flags written. Then, each refusal a jump to the one `xor eax, eax`: the identified flag (item data +0x18 & 0x10), a tome (type exactly 0x12, 0x372C90) with a quantity (stat 70) of 1 or more, the restricted-socket test 0x376110 at the body location asked or at both of the type's (ItemTypes +0x0A / +0x0B, 0x36A4B0; both must refuse), the class: ItemTypes +0x20 below 8 is that class's alone: a player must be of it (0x349860), a monster passes unless a hireling (0x3AF240), which must have the class 0x3473D0 says, any other unit fails. Its entry (`mov [rsp+20h], r9`) is hookable. 18 callers in the exe: the client's (0x159D20 what the mercenary can wear, 0x1C8050, 0x1E71C0, 0x2CACF0 an item given to the mercenary, the inventory's red background 0x2C9940, the tooltips 0x2BD480 / 0x2BF1B0 / 0x1454B80 with the flags for the requirement lines' color), both sides' (0x36AE00 a charm usable, the wear tests 0x36B6A0 / 0x36E360 / 0x388430), the server's (0x42AA10 the corpse's items put back on, 0x471E90 the inventory move, which takes back a move that puts an item on the body that fails, 0x475760 whether a worn item's stats count, 0x4C0E20 / 0x4C3DC0 the mercenary's gear handlers, 0x526800); and D2RCore's CheckInventoryItemRequirementsForDisplay, IsCharmUsable and CheckInventorySlotItemRequirements (its routine table: the pointer at .data 0x6FFA60, the RVA beside it), reached through `.maho` 0x3E2B298 / 0x3E2B61C / 0x3E2B490. Hooked: ignore item requirements |
| restricted sockets | 0x376110 / 0x371FE0 / 0x373300 / 0x375FA0 | (unit, item, body location) -> al: the item's socketed items (its own inventory, 0x371FE0; only for a base that can have sockets) of a restricted type (ItemTypes +0xE6, the Restricted column: 3.3's Colossal Jewel, cjwl, alone) listed; another item the unit wears (body locations 1..12, not the one at the location asked, which is being replaced) with one of the same type (0x373300) -> 1. So two Colossal Jewels are never worn at once. Called: ignore item requirements (the way the test calls it) |
| data tables and their rows | 0x300A90 / 0x314110 / 0x372C90 / 0x36A4B0 | the data tables of a bank: a pointer per bank 16 bytes apart in the array 0x2A9A580 (banks 0 .. 3); Items rows {rows, u64 count} at +0x15A0, 0x1C0 bytes, an item's type the row's +0x12E (int16); ItemTypes at +0x1348, 0xE8 bytes, the game's type ids (row 0 the blank "Any", 18 book; the loader's compiled itemtypes.bin has the same rows): +0x0A / +0x0B BodyLoc1 / BodyLoc2 (bodylocs.txt's rows: 1 head .. 10 gloves), +0x20 the Class (charstats' order less its divider: 0 Amazon .. 7 Warlock; 0xFF none), +0xE6 Restricted. Read: ignore item requirements |
| Hireling rows | 0x396730 / 0x3AF240 / 0x3473D0 | 0x396730 (bank, expansion, monster class) -> the first row of the bank's Hireling rows (data tables +0x520, count +0x528, 0x150 bytes) whose +0x08 is the class and +0x00 the version (100, or 0 in the classic bank). 0x3AF240 (unit) -> a monster's row's +0x0C (the Act: 0 for a monster that is no hireling); 0x3473D0 (unit) -> for a monster with unit flag 0x200 (+0x124) its row's +0x8C (equivalentcharclass, int8: the Rogue's 0 Amazon, the Barbarian's 4, -1 none), else 8. Read: ignore item requirements |
| mercenary flag | 0x34E2F0 / 0x34FBA0 | unit flag 0x200 (+0x124) set (0x34E2F0) on the server by the hireling's level set 0x544AC0, on the client by 0x1A28A0 for every hireling it is told of (the monster packet's handler 0x98FA0, the pet list's 0x13A660); tested by 0x34FBA0. Read |
| client pet list | 0x13ABC0 / 0x2A4DC10 | (player, pet type, any) -> the id of the player's pet of that type, -1 none: nodes from the head 0x2A4DC10 (+0x04 pet type, +0x08 the pet's id, +0x0C its owner's id, +0x20 nonzero for a gone one, which counts only with `any`, +0x30 the next). The client's mercenary check 0x159D20 asks it with type 7 (pettype.txt "hireable") and the local player, then the client's unit of that id (0x9A5D0, a monster) and the requirement test. Read: ignore item requirements |
| server pet of a type | 0x4FF1A0 | (game, player, pet type, any) -> the unit (0x48FE80, a monster) of the first node of the player's list of that type (player data +0x98, 0x20 bytes a type) that is not gone (node flag bit 0) unless `any`. The mercenary's gear handlers 0x4C0E20 / 0x4C3DC0 ask it with 7 and 0. Called: ignore item requirements (the mercenary's items worked out again) |
| items worked out again | 0x470C90 / 0x470CA0 / 0x475760 | 0x470C90 (game, unit, a3, a4): `mov r8d, r9d` and a jump to 0x470CA0 (game, unit, tell the client): for a player or a unit with the mercenary flag, the inventory's charms that D2RCore's IsCharmUsable and 0x475760 pass enabled; every worn item (body locations 1..10) disabled (item flag 0x4000, its stats off, 0x475020), then each that is not broken and passes 0x475760 enabled again (0x475090), over and over until nothing changes (so one item's strength counts for another's requirement); then the item skills. 0x475760 (unit, item) -> the requirement test, then a pairing test (0x371E90). Run by a game's load (0x52E910, the load D2RCore runs, and 0x408EA0, a client's join), an attribute point spent (0x4B3C70), the respec, a purchase, a quest reward, and by the inventory moves (0x471500, 0x471E90, 0x472590) and the identify routine through 0x470CA0. Called (0x470C90): infinite cube ingredients, ignore item requirements |
Client map (UI thread): a dynamic path's room at +0x20 is an ActiveRoom; ActiveRoom +0x18 the DrlgRoom (0x192B20),
DrlgRoom +0x10 near rooms (+0x18 count), +0x48 next in its level, +0x50 flags, +0x58 ActiveRoom, +0x60/+0x64/+0x68/+0x6C
tile x/y/w/h, +0x74 type, +0x90 level; Level +0x10 first room, +0x1C8 the DRLG, +0x1F8 id (0x360FC0); DRLG +0x830
difficulty, +0x838 automap callback, +0x840 map seed. InitLevel 0x3271C0, GetLevel 0x3267C0 (drlg+0x868 list).
Layouts: Unit +0x00 type (0 player), +0x04 class, +0x08 id, +0x0C mode, +0x10 unit data, +0x38 path (players,
monsters, missiles: dynamic, 16.16 x at +0 and y at +4; others static), +0x88 statlist, +0x106 data-table context
byte (via Game+0x106 too), +0x124 flags (bit 2 can be attacked, bit 16 dead), +0x158 next in its hash bucket,
+0x1BD data-table bank (0x34A0E0; indexes the table sets 1..3, taken as the loader's classic/lod/rotw).
Monster data +0x1A u16 kind (2 superunique, 4 champion, 8 unique, 0x10 minion, 0x200 Herald, 0x400 Herald minion;
a champion has 8 as well, 0xD, and a super unique 2 and 8), +0x78 the Herald record (0x38 bytes). Dead: flag bit
16, or monster mode 0/12, player mode 0/17 (0x34C2C0).
MonStats record (0x1FC bytes, table set +0xF58, count +0xF60): +0x36 u16 NameStr string id (the unit-name getter
0x9A1B0), +0x38 DescStr (empty compiles to a default: "an evil force"), +0x3C flags (bit 0 isSpawn, 2 noRatio, 6
boss, 7 primeevil, 8 npc, 10 inTown, 11/12 undead, 13 demon, 15 killable, 30 CannotDesecrate, 31 CannotHerald: the
game's own bit numbers, the classic column order; 3.3's txt has the columns in another), +0x64 u8 Rarity (0: no
area's monsters are picked from the row), +0xC0 the treasure class ids (u16, 8 per difficulty, 0x10 apart: base,
champion, unique, quest, the three terrorized ones, Herald), +0xF0 the quest id. The class id is the compiled row,
which is the txt's hcIdx: the txt's "Expansion" divider row (410) is not compiled (751 rows in 3.3; Baal is 544).
Bosses are still found by flag, which also covers the Uber versions and 3.3's three colossal ancients (745-747).
The flag is on 28 rows: the act bosses (156, 211, 242, 243, 544), the six Uber rows (704..709), Diablo Clone
(333), Baal's clone and stairs rows (570, 559), Radament (229), the Summoner (250), Izual (256), Blood Raven (267),
Griswold (365), Nihlathak (526), the colossal ancients, and the five Putrid Defilers (546..550), which are ordinary
Act 5 monsters: the only ones of the 28 with a Rarity (1) and champion and unique TCs of their own.
Client monster life: stat 6 in 256ths of 128 points (0x8000 = full, 0x97490); exact numbers live only on the server.
D2Damage (0x180, amounts in 256ths, per frame for poison/burn; lengths in frames; the debug damage command 0x4186F0
fills one field per type): +0x00 hit flags (0x1000 skips the can-damage test), +0x04 u16 result flags (0x1 hit,
0x2 killed, 0x400 always kills), +0x18 physical, +0x20 fire, +0x24/+0x28 burn and length, +0x2C lightning, +0x30
magic, +0x34 cold, +0x38/+0x3C poison and length, +0x40 stacked poison sources (12 bytes {source, per frame, length},
u64 count at +0x48), +0x118 chill length, +0x11C freeze length, +0x120 life leech, +0x124 mana drained, +0x128
stamina drained, +0x12C stun length, +0x130 absorb heal, +0x134 life total, +0x138 damage rate. Life/mana stats
are 256ths; the HP commit floors to 0 below 0x100.
Monster AI record (monster data +0x30): +0x60 game, +0x68 owner id, +0x6C owner type (summons and the mercenary name
the player). Unit flag 0x200: the mercenary (0x3473D0 tests it before reading the hireling row).
Items rows (0x1C0): +0x80 code, +0x94 pSpell, +0x98 state, +0x9A/+0x9C cure states, +0x9E stats x3, +0xA4 calcs x3,
+0xB0 length, +0x12E/+0x130 type/type2, +0x13A quest. Item types used: 0x12 book, 0x16 scroll.
Stat ids: 0 strength, 1 energy, 2 dexterity, 3 vitality, 4 statpts, 5 newskills, 6 hp, 7 maxhp, 8 mana, 9 maxmana,
12 level, 13 experience, 29 lastexp, 30 nextexp, 67 velocitypercent, 70 quantity, 74 hpregen, 153 cannot be frozen.

Mana-cost hook: a naked thunk (`d2rcc_mana_cost_thunk`) forwards rsi and rbx (callee-saved, still the
callers' unit pointers) and the return address to the C++ side, which zeroes the cost when either is the
local player. Guarded reads make a non-pointer register harmless.

Loot: the TC drop core hook runs a session for monster drops (no array) and object drops (their array), not
for the drop test commands (a monster with an array). With the loot filter switch the first pass skips NoDrop;
the items it made (our 6-slot array for monsters) are tested with the game's IsShown after the pass (gold find is
applied by then), hidden ones taken off the ground with the expiry sequence (a unique's dropped bit cleared
first) and their slots nulled; then passes with skipNoDrop refill (quest items from a refill are removed, never
duplicated) until done, 200 items in a row without a keeper (a champion, unique, boss or Herald 300, and 900
while its drop has nothing to show yet), 64 passes in a row that make nothing, 300 + 100 per wanted item tried, or
30 ms (the log's own time left out: a line takes the loader some 5 ms). A pass goes through the TC's picks in
their order and stops at its outMax, and a unique's TC is its item and then four potions (Unique: picks -4, Uitem
x1, Cpot x2 with 2 picks each; a champion's Citem and two potions), so under a filter that hides potions a pass's
outMax is how far into a pass a keeper has come yet (1 with none), and every 8th pass takes the whole TC again.
Nothing is filtered while a `LootFilter*` panel is open (PanelManager messages). Superior/ethereal/
socketed: after the quality step, unless restore or quest: request quality 1/2 -> 3 and the returned 1/2 -> 3, request
flags |= 4 (ethereal) and |= 0x10 (socketed; off unless the socket roll's flag test is found); the generator's own
rules decide the rest (the socket roll still checks the base and "no sockets", and its count is untouched: only the
33 % chance is skipped). Perfect rolls: the roll hook always calls the game first
(seed stream unchanged) and answers max(a, b) for items when the return address is in .text and not a choice
site (all eight must resolve and call the roll, else the switch stays off: which skill, class, stat or property group
entry, and a group entry's param, such as Wraithstep's skill tab). An armor's base defense is rolled by
the unit's seed, the one a save keeps, so it is not written: the item is made from a unit seed that rolls the top
(see Item seeds). Items rows come from the loader's DataTableService (row 0x1C0: +0xD4 minac, +0xD8 maxac, +0x121
durability, +0x12E / +0x130 type / type2, +0x13A quest) on the game thread.

Extra drops for elites only (asked 2026-09-28; the panel's switch under the count, `extra_drops_elites_only`): a
session adds the extra drops only for a source that tends to drop better: a monster whose kind (monster data +0x1A)
has champion, unique, super unique or Herald (0x20E) and neither minion bit, or a boss: the MonStats boss flag on a
row with Rarity 0, which leaves the Putrid Defilers out (the row through the loader's getRow on the game thread,
0x1FC bytes checked; findRowById does not take MonStats). A unique's minions (0x10) and a Herald's (0x400) are
ordinary: they drop from the base TC, and 3.3 gives a Herald's tier a TC level boost (0 / 3 / 6 / 9 / 12, RotW Hell
only) and its minions none. Objects (chests, barrels, urns, bodies, racks) get none. Nothing new is called or
hooked: the kind is read from the source the drop core hook is handed. The kind is worked out for every monster's
session (the longer search goes by it too), named narrowest first because a champion has the unique bit as well
(0xD) and a super unique both of its own. The first four sources passed over and the first four elite sessions are
logged with the kind bits (every one with `trace`).

Why a unique dropped nothing (Joshua's run, 2026-09-28: loot filter on, 20 extra drops, elites only, the grail
filter "Cabby Warlock": hide everything; show gold, five bases, and the unique and set items of the codes still
missing): "monster 311 (unique) drop: 5 dropped, 5 hidden by the loot filter, 20 extra asked; 0 added in 40
passes, 25 omitted", while four champions got 22 each. The patience was counted in items, 200 in a row: 40 passes
of a unique's five, four of them potions, so 40 real rolls; a champion's item pick is gold 84 times in 133, which
the filter shows, and a unique's has none. A model of the TC walk, the quality ratios and the once-a-game uniques
over the txt files (scratchpad sim/, rough): one roll of a unique's item in about 100 is a keeper under that
filter (MF 300; 1 in 250 without), so a unique came out empty two times in three from a game's first kill on,
and showed 0.5 items a kill; with the search above 4 to 7 items, empty in 0 to 3 % (8 % without MF), about 1,000
items tried a kill. The uniques run out over a game (each drops once: 188 on that list at level 96), sets do not.

The loot filter asked first (Joshua's idea, 2026-09-28: take what the filter hides out before the roll rather than
roll thousands of times). A drop is refused before it is made, not after: the drop core makes every item through
0x444680 -> CreateItemEx, which is hooked already (the unit seed), and takes "no item" from it as it does when
there is no room. Inside a filtering session (the first pass too) the hook asks loot::refuse: a request of the
session's (its source, mode 3, no restore, version >= 100, a quality 1..7) is refused when the filter hides the
class at every quality the asked one may become: itself, rare for a unique, and magic, superior, normal and low
(the generator's fallbacks and "must be" rules; an item that must be unique, Items +0x139, gold and quest items
are never refused). What the filter says of a class is asked of the game's own IsShown with a stand-in item in the
plugin's memory (game::loot_filter_would_show: unit type 4, class, bank, data -> quality and the ethereal flag),
16 answers a class, kept until the filter may have changed (a LootFilter panel opened or closed, a game left).
No game memory is written, no new routine is called or hooked. The treasure class is still walked and the quality
still rolled by the game, so what drops is the game's own distribution under the filter, as before: only the
making and taking away is saved (and the removal notices, one 0x0A packet an item). Checks: every item a session
does make is put to the game's test, which must say what the stand-in said of its class, quality and
ethereal-or-socketed, and its quality must be one the asked quality may become; one refusable drop in 32 (in
1,024 after 128 of a filter's answers held) is made all the same and must be one the filter hides; nothing is
refused before an item has been seen to come from the bank the answers are asked of (Game +0x106). Three answers
that do not hold turn it off for the session ("asking the loot filter first is off"), and drops are made and
then shown to the filter as before. The model (sim/sessions3.py, no time limit in it): a unique shows 15 items
at a game's start and 5 by its 20th unique (MF 300; the list's uniques run out), 2.4 to 3.7 without MF, empty in
0 to 8 %, in 4,000 to 7,500 rolls a kill (the most 14,000) with some 10 items made besides the checks (about 800
made before). What a roll that ends in a refusal costs in the game is not measured yet: the session lines say.

Drops picked among what the filter shows (asked 2026-09-28: "the rolls are only on possibly dropped items including
the ranges of effects they could have, so that we only roll within allowable items instead of having to ever do
rerolls; we should only refuse drops if that creature no longer has any means of dropping an item within the loot
filter list"). dropodds.cpp works out what a treasure class drops instead of rolling for it:
- The leaves: every item entry the walk can come to, with the mods on the way (the greater at each level) and how
  many of it one walk makes (a pick's share of the chances times the picks; picks below 0 by their order; NoDrop
  skipped, as the switch has the drop core walk it). Treasure classes that drop the same for every source are kept
  flattened; an entry with a condition is taken when the drop core says so.
- The qualities: per leaf the quality roll's chances from its ItemRatio row, the item level, the killer's magic find
  and the mods; 7 or 5 for an entry that names a row; the caller's quality when it gave one.
- The forms: what the generator makes of the quality asked (what the item must be; all superior; a set item only
  with a row at the item's level; a unique by the share of the base's rows, by rarity, that have not come yet, and
  rare or magic for the rest), each with the chances of ethereal (5 in 100, the treasure class's chance, all
  ethereal) and sockets (33 in 100, ..., all socketed) where the base and quality can have them.
- The filter: its 16 answers a class (quality x ethereal-or-socketed, the stand-in of the loot filter asked first).
  A form it shows is kept with weight leaves x quality x form; gold is kept as shown until a pile is hidden.
The game's own walk still runs first, once (it decides how many items drop, makes quest items and gold, and takes
the entries with conditions as it does): the item creation hook judges each request by its forms (dropodds::judge).
All shown: made as it comes. Some: by their chance one of them is made, else none. None: not made (one in 32, later
in 1,024, is made all the same as a check). A form is said through the request: the file index +0x50 (a unique row
still to come, picked by rarity; or one that came already, which has the unique step fail as it does when it comes
to such a one itself: a rare with three times the durability) and the flags +0xC0 (4 / 2 ethereal for sure / never,
0x10 / 8 sockets / none); the request gets its own back after. Every pick that came to nothing, and every extra
drop, is then one pick among the plan's outcomes, made by the drop core from a treasure class of the plugin's own:
a copy of the game's record with one entry (the leaf's own, from the game's memory), one pick (picks -1, so nothing
is rolled for it), no NoDrop, the leaf's mods, no condition; the quality is the drop core's argument (0 for a leaf
shown in every form: the game rolls). So each drop is one weighted pick and one item made; no roll can miss. A
unique made is weighed out of its base at once. What only the game can say is asked of the drop core the same way,
with the hook refusing the item it then asks for (an item asked for is a yes; picks -1 and a quality given, so no
roll): whether there is room beside the source (the request also says the level the source's items get and its set
29 flag), whether a treasure class's condition holds (its record's condition fields, flag 0x40), an item entry's
(flag 0x80 and the entry), a unique row's calc (flags2 4). A session asks once per treasure class or calc.
No new hook and no new routine: the drop core's original and CreateItemEx's hook, as before. New in kind: the drop
core walks records that are the plugin's, and the hook writes two fields of the request (on the game thread,
inside the hook). Checks: every item made is put to the game's filter test (a verdict that differs counts as a
mismatch, three turn the asking off); an item made in a form said must have that quality and those flags, else the
outcome is not picked again in the session and counted ("came out otherwise"); more than one in eight after 32
turns the picking off for the session and the search below takes over. The plan is not used in a classic game
(bank 1), before an item has been seen to come from the game's bank (a run's first drop: its own walk is judged by
what a quality may become, its picks are the plan's), when a table's rows have another size, or for a treasure
class that keeps a NoDrop: the search then runs as before. Cost (tests, tables of the game's size):
2.4 ms the first time in a game (the bases read), 0.05 ms a session after, under a microsecond a pick; the filter's
answers for a treasure class's 400 classes are asked at its first session after the filter changed.
`cabbycodes dropcheck [walks]` (5000): at the next monster's drop the game walks its treasure class that often with
the hook refusing every item and counting the class and quality asked for; the counts are set against
dropodds::expected (totals, by quality, the kind furthest off in roots of its count): "the odds worked out are the
drop core's", or a warning. tests/test_dropodds.cpp: the quality roll and the walk as the game has them against
the chances (400,000 rolls, 300,000 walks a case), and a small game (items, uniques that came, set items, treasure
classes with mods and conditions, three filters, the loot switches) in which rolling everything and throwing away
what the filter hides, the game's walk with the hook's judge, and the picks give the same drops.

A latent sunder charm from a monster that is no Herald (Joshua's run, 2026-09-28, one within minutes) is the
game's own rule, not a pick past a condition. The six are UniqueItems rows 426 and 428..432 ("PreCrafted ..."):
code cm3 (the grand charm), picked among (flags 1), rarity 1 each, level 75, DropConditionCalc
cond('Difficulty', hell); the base's other one is Gheed's Fortune (row 359, rarity 15, level 70), and 2.5's six
(rows 401..406) are not picked among. So the unique step makes a grand charm that comes out unique in Hell at item
level 75 or more one of them 6 times in 21. The treasure class 'Sunder Charms' (cond('MonsterTestElite', herald))
names the same rows and is an entry of the Heralds' treasure classes only ('Act N (H) Herald Item X', 'Herald
Charm 9 / 10'): no other monster's treasure class leads to it (followed over the txt), so a Herald has that
class's odds and every monster the grand charm's. The plain game: a unique monster's item is a unique grand charm
once in 8,400 kills (magic find 300, item level 98: a walk makes 0.0033 grand charms, 3.6 % of them unique), a
latent charm once in 29,000. Under the grail filter with 20 extra drops a pick is one of the few things shown
(0.0083 of a walk), so 1.4 % of the picks are unique grand charms: a latent charm about every tenth unique
monster, until the six have come (each once a game). The first run of the picks (the log of 2026-09-28 20:31):
"25 picked among what the filter shows of treasure class 'Act 5 (H) Unique C Desecrated' (587 items in 177 forms
it shows; a walk makes 5.02 items, 0.0083 of them shown), 0 came out otherwise, 8 questions to the drop core,
2.0 ms; 0 omitted", no warning.

The search (until 2026-09-28 the only way, now what runs when the plan cannot be made): the treasure class is
walked again until something the filter shows comes out, with the patience below; the hook refuses a request whose
class the filter hides at every quality the asked one may become.

Chronicle drops (asked 2026-09-30: "add a new option for the loot window to make possible drops based off the
missing chronicle items. If used with the loot filter option at the same time, then they act as an AND so drops
from either can happen"; the panel's Loot section, `chronicle_only`, the console's `chronicle`). The Chronicle is
3.3's record of the uniques, set items and runewords found (the record: Chronicle ...): the game's lists of the
entries there are, and the player's maps of those found, filled from the shared stash and by identifying a drop.
The switch works as the loot filter switch does, with "a unique or set item of a row the Chronicle misses" in place
of "what the filter shows", and with both on a drop is kept when either holds:
- chronicle.cpp reads, at every drop session (and every 5 s from the tick for the panel's counts, while the switch
  is on), the game's two lists (rebuilt only when their buffers change; their counts must be the totals the game
  divides by) and the local player's two maps (walked bucket by bucket, not hashed). What it misses is the entries
  less the ids found, less the rows kept as drops in this game: a unique drops once a game by the game's own rule,
  and a set item is taken out the same way here, so twenty extra drops are not twenty of one set piece (a new game
  has it again until it is identified). Nothing is written; the offsets and the lists come from the three routines'
  code (sites.cpp).
- dropodds.h: the request has the missing rows (`uniques`, `sets`) and whether the filter's answers count
  (`filter`). A form of a unique still to come is split by the rows: the part whose row is missing is kept whatever
  the filter says of it, named to the unique step from those rows by rarity (pool kUniqueWanted); a set item's path
  likewise, its rows by the set step's own candidates and rarities (0x58ACD0, a named row taken as it is), named
  from the missing ones (kSetWanted). What the filter shows keeps its forms as before; the Chronicle's are added
  only for the ethereal and socketed states the filter hides, so nothing is counted twice. Gold only the filter
  wants (with it off the judge refuses gold, the plan leaves it out); quest items come as always. A form that names
  a row carries it (Force::row), and the item made must be that row.
- loot.cpp: a session asks the filter only with its switch on and its editor closed (with the editor open nothing is
  filtered, the Chronicle's part neither), the Chronicle only in an expansion game when it could be read. Every
  test of a real drop is "shown by the filter or missing from the Chronicle" (quest items kept in the first pass);
  a kept unique or set item is taken out of the missing rows before the plan weighs its class again. The search's
  refusal goes by the filter's answers for what a quality may become and, for the Chronicle, whether the base has a
  missing unique (asked quality 7) or set item (5) at all (dropodds::may_be_wanted). The checks are the filter's
  as before, plus: an audited drop that the Chronicle misses, and a named row that came out otherwise, count as
  mismatches and misses. A drop the judge refuses by the chance of its forms (some are wanted, the roll said no:
  dropodds::Answer::kNotThisTime) is no longer one of the drops audited: made as it comes it may well be wanted, and
  it used to count as a mismatch (with the filter alone only where a unique's rare stand-in is hidden and some of
  its base's uniques came; under the Chronicle at nearly every unique or set item request).
No new hook and no new routine: the drop core's and CreateItemEx's hooks as before; the request's file index is
written for a set item's row as it is for a unique's. Runewords are left out (made, not dropped). Checked without
the game: tests/test_dropodds.cpp's small game with a Chronicle (the rows it misses among uniques that are picked
among, above the item's level, named only, with a condition, that must be unique, that come any number of times,
and a set item beside one a treasure class names), alone and with a filter, all loot switches: rolling everything
and keeping what either wants, the judge on the game's walk and the picks agree within 3.1 sigma; a set item that
came leaves the plan. tests/test_chronicle.cpp builds a map as the game's insert does and reads it back, and refuses
what is no such map. The plugin's sites.cpp over the dump (a scratchpad harness): the three sites once each, the
Chronicle at player data +0x220, the uniques' map +0x20 (sets' +0), buckets +8 and their count +0, an entry's next
+0 and id +8, the lists 0x2A9BDB0 / 0x2A9BDC8 and their counts and totals where the record has them (the dump's
lists are empty: it is taken before the tables are read).

Perfect rolls' class item skills (approved 2026-09-27): the class skill step's bonus argument (the one the request's
flag 0x20 fills with the item level) is raised to the least bonus that clears both its thresholds on a roll of 0,
max(count threshold + 1, 2 x level threshold) = 180, read from its code: three lines, each +3; which skills stays
its pick. (The game's own imbue gives the step a bonus too, the item level: three lines for sure, +3 on a roll of 41
and up.)

A superior item's kind (approved 2026-09-29; asked as "I don't think superior items are always rolling with max
stats added to them"). Perfect rolls put every value of a superior item at its top (the saved ones read 15 / 15 / 3),
but which bonuses it has is a pick: the superior step rolls one of the QualityItems rows, inline on the item's seed,
and takes the first that goes on the item (the record: superior step). So an armor came with durability alone one
time in three, and half the weapons without enhanced damage (the collection's Monarch and Dusk Shroud runewords on
Xeen: kind 4). The step's test of a kind is hooked (its one caller is the step):
- the quality step's hook, which the generator runs before every step of an item's (the fallbacks run it again),
  marks the item on its thread;
- at the step's first question for a fresh item (no restore, quest or display item) under perfect rolls, the hook has
  the test's original say of every kind the step picks among whether it goes on the item (how many it picks among
  as the step counts them: the few for a base that is thrown or has no durability, from the Items and ItemTypes rows
  through the loader), and works out the best of those that do (superior.h: a bonus counts 4 for enhanced damage
  or defense, 2 for attack rating, 1 for anything else, times its top against the most any kind that fits gives of
  it; the bottoms decide between equal sums, and between kinds equal in both the step's roll does);
- from then on it answers no for every other kind: the step marks them tried and rolls again until it comes to the
  best one, which it makes itself (the file index, the mods, the class skills).
So an armor has enhanced defense with durability (kind 7) and a weapon enhanced damage with attack rating (kind 3;
Joshua's choice, no kind has all three), at 15 / 15 and 15 / 3 by the property roll's hook. A weapon too weak for
a percentage to show (a wand, a dagger) gets the game's "+1 to Maximum Damage" for its enhanced damage, as always.
Nothing is written, and nothing of the game's is called but the hooked test's own original. The item is one the
game makes whenever its roll comes to that kind first; the item's seed stream goes on from another place than the
game's would (as with max affixes), which no save keeps. Checks: a row the step asks of that is not one of the
kinds looked at leaves that item to the game (the loader's rows are taken to be the game's own; a row handed over
from elsewhere is known by what it holds); every item whose step was told no must come out superior and of the
kind it was told to take (the item creation hook asks), and the third that does not turns it off for the session
("a superior item's kind is the game's pick for the rest of the session"). sites.cpp finds the step and the test by
signature, reads the step's small count and the two fields from the step's code and the thrown test's, and checks
that the step's call is the test.
Checked without the game (scratchpad harness2/, 2026-09-29): the game's own step, test, item type test and table
getters run from the dump mapped at the game's base, over data tables made from the loader's compiled rows, with
the plugin's loot.cpp, sites.cpp and mem.cpp (stubbed: the log, the switches, the loader's table service, and the
two routines the step calls with its kind, the apply of its mods and the class skills). Over every weapon and armor
base with 48 seeds each (25,104 items): switched off, the kind is the one a second implementation of the step
rolls; switched on, every weapon has kind 3 and every armor kind 7 (the nine quest bases the game's pick), with the
test asked 4.2 times an item instead of 1.7, under a microsecond an item; the same with the loader's rows a copy of
the game's; a restore, a display item and a step without its quality step get the game's pick; three items made to
come out otherwise turn it off.

Max affixes (approved 2026-09-27): the magic, rare (expansion) and crafted steps are hooked. Each runs as the game
made it; then, unless the request is a restore (+0x3C), the item a quest item or a display item, the item is topped
up: magic, the missing side (unless the request's id for it is -1); rare, the step's loop carried on to its table's
top (6; a jewel, the type its `cmp eax, 3Ah` names, to its base + 1 = 4); crafted (expansion items only: the classic
picker leaves groups to its callers), to its top minimum (4). Each added affix: a coin for the side (the plugin's
own, not the item's seed), at most 3 a side, the picker with must and apply (its properties applied at once, through
the property roll, so perfect rolls apply), the request's forced id for that side's next affix, the id in the side's
first empty slot; a side the picker finds nothing for is closed. The top-up comes after the step's own mods and
0x58A050's class skills, so the item's seed stream afterwards is not the game's (the ethereal and socket rolls that
follow in 0x442D60 still roll). sites.cpp derives the picker and the setters from the magic step's calls, checks the
rare and crafted steps call the same picker and walk the same request fields, and reads the counts' tops and the
slot offsets from their code.

Best affixes (approved 2026-09-27, within the item's own level): the picker wrapper 0x58AE00 is hooked, and the rare
step's two direct calls of D2RCore's picker are pointed at it (`PatchCallRel32`; the wrapper hands an expansion item
the same arguments and group 0). The three step hooks mark the item they make (thread-local) when the switch is on
and the item is fresh (not a restore, quest or display item) and not classic (version 0: the classic picker's rules
differ). The picker hook acts only for that item, and never on a forced id: it calls the original with apply 0 (the
pick and the seed stream stay the game's), keeps the best row of the pick's family, and applies the kept row itself
(0x3D3670) when the call asked for it (the magic step, max affixes' additions; the rare and crafted steps apply from
the slots after their loops). A family: the rows of the pick's side (the table's starts), group and classspecific
whose properties give the same stats: per mod, each Properties slot's stat (a stat-less function such as dmg% by its
function), its val, and the param when it names a skill or tab (not for functions 14..17, where it is an amount).
So swing1/2/3 (IAS), move1..3, balance1..3 and cast1..3 are one family each, and dmg-cold goes with cold-min/max/len,
while ac, ac% and ac/lvl (one prefix group) stay apart. Candidates: the rows the picker could have picked for the
item: spawnable (when asked), version, the level window against the item's affix level (the picker's formula from
Items +0x10D and +0x150), rare-only for qualities 6/8/9, frequency, and 0x3D4220. Best: the sum over the family's
stats of the row's top / the family's top for that stat, then the same for the bottoms; a tie keeps the game's pick.
Magnitudes, so "ease" (-requirements) counts as more the lower it goes. By value, not by level: a circlet takes both
the amulet table (the Whale 81-100, level 50) and the ring/helm table (the Mammoth 31-40, level 68). A Python port of
this over the loader's compiled rows (2026-09-27): amulet life at alvl 12 / 30 / 60 -> Fox / Colosuss / Whale, circlet
life -> Whale, large charm life -> Vita 41-45 (the level-110 46-50 row never spawns), MF: amulet, boots, circlet Luck,
ring Fortune, rare boots Fortune (Luck is not rare), small charm Good Luck; IAS: weapon Quickness (Alacrity at alvl
30), gloves Alacrity; FRW boots Acceleration; FCR orb and circlet Magus, amulet Apprentice (its only one); armor
Godly, weapon Cruel. Not covered: forced picks.
The automatic affix (approved 2026-09-30; asked as "the perfect rolls paired with best affixes cheats didn't produce a
sacred targe with +45 all res, instead giving me 30 all res"). A base whose Items row names an AutoMagic group (+0x100)
gets one automatic affix, the generator's last pick (qualities 1, 2, 3, 4, 6, 8, 9), through the picker's twin
0x58AE80: 3.3's groups are 300 and 302 the Amazon bows and spears (+1 .. +3 to a skill tab), 303 the Sorceress orbs
(life of the Jackal .. of the Colossus 41-60, or mana Lizard's .. Great Wyrm's 61-80), 304 the paladin shields (all
resistances Shimmering 5-10 .. Chromatic 35-45 at level 50, or attack rating with enhanced damage Sharp .. Knight's
101-121 / 51-65 %), 305 the Necromancer heads (poison, of Blight .. of Anthrax), 306 the Warlock grimoires (magic
damage of Shadows .. of Nightmares 21-30, or fire damage of Ember .. of Inferno 16-32); 301 no base has. The top rows
of each (Chromatic, Colossus, Great Wyrm's, Anthrax, Nightmares, Inferno, the +3s) are not rare rows. Perfect rolls
only put the value of the row the game picked at its top: Joshua's three superior Sacred Targes of item levels 96 to 99
(Xeen.d2s, 2026-09-30) had Shimmering at 10 twice and Fine (60 / 30 %), and he had seen Scintillating at 30. The twin
is hooked: for the fresh expansion item whose quality step ran last on the thread (the quality step's hook marks it,
the superior kind's mark; the generator runs that step first for every item and hands both the same unit), it calls
the original with apply 0, keeps the best row of the pick's family among the automagic rows (best_affix with the
automagic range: the same group, class, stats and candidate tests, spawnable not asked, as the generator does not
ask it), and answers that id; the generator writes it and applies its row (perfect rolls then put the values at their
tops). The kind stays the game's pick (Joshua's choice for all three bases with two kinds): a paladin shield's all
resistances become Chromatic (+45 with perfect rolls) from affix level 50, which on a Sacred Targe (qlvl 63) is item
level 75, Prismatic 25-35 on a rare one, and attack rating with damage (the game picks it 12 times in 42 from affix
level 50) Knight's. sites.cpp checks that the twin calls the affix picker's two D2RCore pickers. Checked without the
game (scratchpad harness/, 2026-09-30): the plugin's sites.cpp and mem.cpp over the dump, the twin's signature once at
0x58AE80 and the check passing; a port of best_affix over the loader's compiled automagic and Properties rows at affix
levels 37, 49, 93 and 99, rare and not: every pick of a group goes to the top of its own family within the level
window (the list above; Scintillating at 37, Prismatic at 49 or on a rare).
Every kept row must be one vanilla could have picked (Joshua's requirement: every item possible in a vanilla game).
Checked 2026-09-27: D2RCore's picker calls the game's own 0x3D4220 (item types), 0x3D3670 (apply), 0x36CD50 / 0x36CDE0
(slot ids), 0x374130 (item class), 0x314110 (Items row) and 0x300A90 (data tables) through its routine table (D2RCore
.data 0x6FF370.., {pointer set at run time, game RVA, 0} records); the exe's own picker 0x58C940 (vanilla, no callers
now) filters exactly as D2RCore's does (spawnable, version, level window, rare-only for 6/8/9, 0x3D4220, frequency,
class 0xFF / item class 8 / equal, the item's prefix and suffix groups; weight frequency x level on a magic-level base)
and works out the affix level the same way (0x36CCF0: item data +0x38, at least 1; 0x371930: Items +0x10D). The kept
row shares the pick's group and class (so the class and group tests pass as for the pick) and passes every other test
itself, including a weight above 0. No row with a frequency has a level below 1 in 3.3. An exhaustive run over the
loader's compiled rows (vanilla's filter written out separately; 74 base type / magic level pairs, affix levels 1..99,
magic / rare / crafted, both sides, random other groups on the item): 1,357,967 picks, 752,758 kept another version,
none outside vanilla's candidates. Later steps (ethereal, sockets, class skills) do not look at affixes, and the
picks after it see the same groups, so the rest of the item comes out as for that pick. The item's own seed (in
memory only, see Item seeds) would not make the kept row again, as with perfect rolls and max affixes.

Item seeds (read 2026-09-27, settled with Joshua's saves and runs 2026-09-28). An item has two seeds, two outputs in a
row of the game's RNG at its creation: U, then S = low32(U x 0x6AC690C5 + carry) with a carry below the multiplier.
- The unit's (U, unit +0x30) rolls what item init rolls, in this order: for an armor the durability (roll(maxdur >> 1),
  no step for a base with 0 or 1), then the base defense (minac + roll(maxac - minac + 1)); for a weapon a stack's
  quantity, then the durability; for other stackables the quantity; for gold its amount; then the picture variant
  (rings, amulets, charms, jewels). A low quality item rolls its lowered durability on the same stream afterwards and
  keeps 75 % of its base defense.
- The item's own (S, item data +0x10) rolls everything the generator does: affix picks, every value, class skills,
  ethereal, sockets, the automatic affix, the unique or set row.
- A save keeps U only: the writer's save form writes 32 bits of unit +0x30 and the reader puts them back; nothing
  writes S. Seen in Joshua's saves (2026-09-28): the log had both seeds of 12 items; the three he kept (an amulet, a
  small charm and a ring: unit seeds B1179513, AB53AC61, 9202FB6F) have their unit seed in Xeen.d2s (one in the shared
  stash too), and none of the 12 item seeds is in any file, at any bit offset. So nothing is tied to S later: in a
  session it stays in memory (a cube upgrade remakes an item from both seeds), and what it is after a load was not
  read (the exe draws one when the unit is made; D2RCore's side of loading was not read).
- What a save's seed can say of an item, then, is what U rolled: an armor's base defense and the picture (the
  durability and the quantity change in play). The loot switches act in the generator, on S's stream, so nothing
  they do is contradicted by a save; the one thing U rolls that a switch wants is an armor's base defense.
- The unit seed is chosen (itemseed.cpp, the item creation hook), only for this: a request that is not a restore and
  came without the routine's seed argument, under perfect rolls, for a class that is an armor (its Items row's type
  or type2 counts as type 0x32 through the ItemTypes rows' Equiv1 / Equiv2, the game's own test; the rows through
  the loader, on the game thread) with maxac above minac. Random unit seeds are tried (a random start, then an odd
  stride) until one rolls maxac by the two rolls above; S is U x the multiplier plus a random carry below it; both
  go into the request (+0x58 / +0x5C), the original is called with its third argument 1, and the request gets its
  own two back. Item init then rolls the durability, the defense and the picture from U itself: nothing is written
  to the item. Every other request goes to the game as it came.
- The cost: one seed in (maxac - minac + 1) fits. The widest range in 3.3 is 115 (213 of the 217 armor rows have a
  range); a seed takes about 100 tries of two multiplications, 0.1 microseconds, at most some thousand tries.
  No thread, no wait, no file (tests/test_itemseed.cpp: the stream against an independent implementation, a top
  seed for every armor row, and each roll against the seed model).
- Every armor made so is checked: it carries U (its own seed may be another by then: a generator step that fails
  sets the item's seed again, 0x442820), and its base defense (GetUnitBaseStat 31) is maxac; maxac + 1
  with enhanced defense among its properties (0x3D5AC0: a superior armor, most uniques); half as much again when
  ethereal; three quarters of maxac when of low quality. The first four are logged; one that differs is a warning,
  and the third turns the choice off for the session (new armor then keeps the game's own seeds and its rolled
  defense). In the game (2026-09-28, the earlier plugin's check of the same rolls): 74 of 74 armors had the base
  defense their unit seed rolls, 24 of them with enhanced defense.

Seeds in the saves (2026-09-28; tools/stash_seeds.py). Asked: "inspect the offline stashes ... and fix any items that
have invalid item seeds attached to them". The stash files of the save folder (`Saved Games/Diablo II Resurrected/
mods/D2RMM` in the prefix):
- ModernSharedStashSoftCoreV2.d2i, the RotW characters' shared stash: sections of a 64-byte header {0xAA55AA55, 2, the
  format 105, the gold, the section's size, a byte for the kind of tab: 0 items, 1 the materials tab, 2 a record that
  is no list of items}, then "JM", a count and the items. SharedStashSoftCoreV2.d2i is the other characters' (three
  empty tabs).
- SharedStash.d2x, Joshua's collection: a PlugY-format file ("SSS\0" "02", the gold, 256 pages of "ST", flags, a
  name, "JM", a count, items of the 1.10 format with 3.3's bases and stats). The game lists it as it lists every file
  of the folder ("Created save file SharedStash.d2x in container SharedStash", blz-log.txt); no `.d2x` is named in the
  exe, D2RCore or the loader, and the file's date stayed at its copying (2026-09-27 16:29) over the games played
  since. Another tool keeps it.
An item's seed is judged by what item init rolls from it (the order above; arrows and bolts, the item types with a
Quiver, ItemTypes +0x0E, roll their quantity and nothing else): the base defense and the picture must be the save's.
Found in the two stashes (1,586 items with a seed): all 147 pictures (rings, amulets, charms, jewels) as their seeds
roll them, and the durability of 411 of the 452 armor pieces and weapons that are not ethereal (the others worn,
mended or with more of it by a property); of 317 armor pieces 200 have enhanced defense (the top plus one for any
seed), 7 no range, 25 a seed that rolls the top they have, and 85 (10 in the modern stash, 75 in the .d2x) had the top
with a seed that rolls less. No armor has less than its top: the collection grew under olegbl's PerfectDrops (it
writes minac = maxac into armor.txt, so every seed rolled the top then), and the modern stash's ten were in it by the
loader's backups of 2026-09-27 21:27Z (nine) and 2026-09-28 14:19Z, before the unit seed was chosen as it is now
(perfect rolls wrote the defense after the roll, see Dead ends); every armor put in later has a seed that rolls it.
- The 85 got new seeds: the first seed after the old one, the plugin's odd stride apart, that rolls the defense the
  item has, and the durability too where the save still has one item init can have rolled (68 of them), and that no
  item of the stashes or the characters has. Copies of one item (one seed, base and defense: the runeword bases) got
  one seed. 32 bits an item were written, 44 and 319 bytes; both files were read again: the same items, every new
  seed rolling its item, and by the arithmetic of src/itemseed.h too. The originals, the tool's log and the files'
  SHA-256 are in `.save-backup-20260928-stash-seeds/`.
- The characters (asked next: "do the two characters as well"): Xeen.d2s had 4 such armor pieces and Vacate.d2s 6.
  Eight are copies of the collection's (the Shako, the Tiara, the Monarch and Dusk Shroud runewords) and got their
  copies' new seeds (`--copies` with the stash files as they were and as they are); Xeen's rare Dark Tome and
  Vacate's rare Greaves got seeds of their own. Written: the seeds and the header's checksum, 24 and 32 bytes; read
  again as the stashes were. The originals and Xeen.d2rl are in `.save-backup-20260928-character-seeds/`.
- Xeen.d2rl was left as it is, so it names a .d2s that is no more (see the sidecar under Loader facts learned): the
  loader then shows Xeen without the mod and plugin badges and asks nothing at the launch, and the next save writes
  a new sidecar, its history carried on from the old one. Its seal is a signature; it is not to be forged.

The item seed model (tools/seedmodel; research, not in the plugin). For a day (2026-09-27 to 28) the six loot switches
changed nothing in the generator and chose both seeds instead: a model of the generator said what each item seed
makes, searcher threads walked the 2^32 seeds per kind of item, the seeds found were kept in a file, and the first
item of a kind waited for its seeds. Joshua ended it (2026-09-28): only the seed a save keeps has to agree with an
item, and looking seeds up while playing froze the game (an item waited up to the setting's 5 s; a unique with many
values has few seeds or none). What it found stays true and is kept:
- itemgen.cpp follows the generator roll by roll for a request (class, item level, quality, flags, version,
  difficulty, bank, game type, ladder) over copies of the game's tables: the quality that comes out, the affix ids,
  the unique / set / superior row, ethereal, sockets, the skill lines, each value, the RNG's state at the end. Not
  covered: classic items (version < 100), a request without a quality or with quality 1 or 9, quest items, a mod
  that is a property group or function 25, class skills outside bank 3's lists, a unique with a drop condition
  (+0x90), some item types in the normal step.
- Against the game (Joshua's runs, 2026-09-28, Hell, all areas terrorized, item levels 96 to 99; game type 3, ladder
  0; drops ask version 101, flags 0, mode 3): over 1,000 items made from the game's own seeds and over 1,000 made
  from chosen ones, every one as the model said. The plans' prints worked out from the loader's compiled files are
  the game's (60 of 60), though some rows' own prints differ (fields the model does not read).
- Vanilla's own picker (0x58C940, no callers now: D2RCore's took its place) rolls as D2RCore's does: the coin step,
  the candidates in table order with the same tests and weights (511 kept at most), roll(total + 1), the walk with
  the last candidate when the weights run out.
- How many of the 2^32 item seeds give everything (all six switches, Hell, item level 85, every sixth class): asked
  superior, a median of 8 million; magic 720,000; unique 1.4 million; rare 128 (a tenth of the kinds under 20). None
  at all: the rare items and magic orbs that roll class skills (three lines at +3 cost about 1 in 10,000) with all
  six switches as a drop. Tomb Reaver (class 256): about 214,745,792 seeds make it ethereal, one makes every value
  its top (0xAD9D53B3), none both.
- The game makes a rare in a second way, Charsi's imbue (request flags 0x20 and 4 or 2): the class skill bonus is the
  item level and ethereal is for sure or never; a rare bone wand with everything has no seed as a drop and about
  9,600 as an imbue. Nothing in the game makes a magic, set or unique item ethereal for sure (request flag 4 is the
  imbue's and the cube's).
- The tools: `make seedfinder` (tools/seedmodel/seedfinder.cpp, the walk over all seeds per kind, threads),
  `make seedmodel-test` (test_itemgen.cpp: 285 plans, 230 searches). A Python model written from the decompiled code
  and the C++ one agree on 19,376 cases (scratchpad model/).

God mode (2026-09-30; Joshua: "we should hook the methods in the game that decrease player health and negate the
effect. This should not need to be scanning every frame for performance reasons, and we don't need the refill
logic"). Every lowering of a stat goes through D2RCore's four stat writers, and each asks the game's protected-stat
test first (the record); the test is hooked and answers "may not go down" for a player's life (stat 6, unit type 0)
under the switch, else the game's own answer. So a hit's life, the damage an attacker's thorns or Iron Maiden
returns, crushing blow (D2RCore's, from the attacker's item events inside ExecuteEvents, straight to the setter),
poison and burning as the regeneration runs them, Blood Mana's life cost (D2RCore's mana consumption, 0x3DDB50 of
D2RCore.dll, has the game's 0x584E80 inline: state 0x72, then its SetUnitStat) and a skill's life cost all stop there; with life untouched ExecuteEvents marks no kill. The ExecuteEvents hook takes the
rest of a hit on the local player away (strip_hit: poison, burning, chill, freeze, stun, leech, mana and stamina
drain, "always kills") and leaves its damage for the hit's events (thorns, damage taken to mana); FinalizeDamage
still undoes a kill that arrives. Nothing is refilled: life stays where it was and recovers as usual. Nothing runs in
the tick. The test is told only the stat and the unit's type, so every player in a game is kept (offline: the local
one). Which unit a write is about the writer has in rdi: D2RCore's two unit writers keep the unit there (SetWideUnitStat
0x3D9BF0: `mov rdi, rdx` before its test; AddWideUnitStat 0x3D9EE0: `mov rdi, r10`), callee-saved, and a naked thunk
(`mov r8, rdi; jmp impl`) hands it on as a third argument; it is taken for a unit only when it is a plausible pointer
that reads as a player of the local player's id (the server's player and the client's copy both are). The list
writers keep other things there (AddWideListStat the delta, AddWideListStats a list), which do not: for them life is
only kept. A life above its top (Battle Orders ended, an item that gave life taken off) may go down, to the top and no
further (asked 2026-09-30: "the life should be allowed to go down if life is above the current max"): the hook lowers
it to the top itself with the game's setter, on that unit and on the thread writing it (a thread-local guard lets
that write through), and refuses the write that asked, so a hit or a poison tick in the same frame takes the excess
and nothing more; the client's copy does the same as the server's values reach it. The top is the max life stat (7),
which is what the game's max-life routine 0x2F4D20 reads (D2RCore's 0x3D9050 alike). The first four are logged ("god
mode: your life was above its top (1500 > 1200) and goes down to it"). The thunk and a MinHook hook were checked over
the dump (scratchpad harness/thunkcheck.cpp): the impl sees the writer's rdi, rdi survives, and the answers are the
impl's for the player and the game's otherwise. Were D2RCore ever to keep something else in rdi, the unit does not
read as the player and life above its top is only kept (the refusal lines say "no unit in rdi"). Without the
protected-stat hook (its signature, or the check that it
reads the switches ExecuteEvents reads) god mode empties the hits on the player (zero_hit) and says so in the log.
The first four refusals are logged with the D2RCore writer that asked ("god mode: a player's life was not lowered
(asked by the stat writer at D2RCore.dll+0x3D9D1C)"); the console's status counts them.
Why god mode had failed (until 2026-09-30): it held the game's players' no-damage switch from the tick, set once and
remembered, and D2RCore clears that switch at every game's load (the record: no-damage switches), so from a
session's second game on it was the tick's refill of life and FinalizeDamage's undo of a kill.

Damage cheats (all in the ExecuteEvents hook, before the original): the damage multiplier scales a hit whose
attacker is the local player or a monster it owns and whose defender is someone else's monster - the damage types
when computeTotals is 1 (resistances then apply to the scaled amounts), the life total when 0, and the per-frame
poison (its stacked sources each, and the record's own remainder beyond them) and burn either way. A scaled type
is capped at INT32_MAX/16: the game adds the types up in 32 bits after resistances, and ExecuteEvents skips a
total <= 0 (0x44D068), so a wrapped sum would do no damage at all. A protected pet (owned by the local player; the merc by flag
0x200) has everything its hit carries zeroed (types, totals, poison, burn, chill, freeze, stun, leech, drains,
"always kills"); FinalizeDamage undoes a kill that still arrives. Cannot be frozen zeroes the chill and freeze
lengths, cannot be poisoned the poison (and a precomputed total loses its poison frame), on hits on the player.

Poison cure: under cannot be poisoned the tick (not while dead) runs the antidote's cure on state 2: every list the
lookup finds is unlinked (its end callback turns the state off and queues the client update) and freed, at most 64
a tick; the state is toggled off after them if still on. A list still found after its unlink turns the cure off
for the session (it is never freed under the unit).

Consumables: the ShouldRemoveOnUse hook answers false for a TP/ID scroll or a potion under its switch, so the
client's request says "keep" and the server's check agrees; the tome hook skips the quantity update when it is
called from the use effect's site (return address = 0x5817CC + 5, checked to call the routine at install) for the
local player's TP/ID tome. Which item is which comes from the Items rows (loader, game thread, per bank, read by
the tick until the player's bank is classified, every ~5 s; kept for the session): codes tsc/tbk/isc/ibk, potions
by item types hpot/mpot/rpot/spot/apot/wpot (pSpell 3/4/5/6/9 if the types are not found), never quest items.
The classification is cached because the UI thread asks too.

Keys: the UseKey hook answers 1 without the original for the local player under the switch when the inventory holds
a key the original would take (the same walk, read field by field, with the game's inventory getter and item-type test,
both derived from UseKey's own calls; the key type 0x29 is read from its `mov edx, 29h`). Otherwise the original runs
("I need a key" with none). Nothing about the key changes, so the client needs no update.

Item level: the item-name hook adds the game's string "space" (loader localization `d2r:space`) and "(N)" after the
name the game built, N the item's level (item data +0x38, 1 below that), under the switch, for the rows classified on
the game thread per bank: a code, no quest, ShowLevel 0 (a row with it shows its level already), and a weapon type
(through Equiv1/Equiv2 to weap, but not tpot), an armor type (to armo), or the misc types amul ring scha mcha lcha csch
jewl cjwl (the D2RMM option's rule; the same rows: 291 / 217 / 8 in rotw, 291 / 202 / 6 in the base banks, where the
Warlock's grimoires wa1..waf are untyped and never spawn). The client knows the real level only because the writer patch (checked `0F 85`
then `mov edx, 1`, written with PatchJmpRel32 over the 6 bytes) sends it for every item from the load on, the same
seven bits: switching on mid-game shows right numbers at once, items already in the inventory included. The hook goes
in first, the patch only after it, so nothing changes in what the client is sent unless the name can show it.

Durability: the item wear hook and the Impale wear hook return without the original when the owner is the local player
under the switch, so the wear is never rolled (the owner's seed advances a little less than it would). They are the
only routines found that lower durability: stat 72 has no SetUnitStat/AddUnitStat caller, and the two wear routines
write it through 0x2F7940 / 0x43EB30. Nothing is repaired, and nothing changes, so the client needs no update.

Cannot be cursed (approved 2026-09-28): the two curse steps are hooked and return without the original for the local
player under the switch when the state is a curse: a curse skill's step reads it from its context (+0x58), the Cursed
modifier's is its skill's (Skills row 0x42 +0xA2, read through the loader). Nothing is put on, as when the unit is one
the curse does not take. A curse is a States row with curse and curable (curses.cpp reads the rows per bank on the game
thread; three states every game has must have the flags where they are taken to be, else nothing is a curse). The
tick ends every curse that is on the player all the same with the antidote's cure, 13 state tests a tick. The
mercenary and the summons are not covered.

Infinite gold (approved 2026-09-28): the payment hook answers 1 without the original for the local player under the
switch, so nothing is taken, from the inventory or the stash. Every caller tests the gold itself before (inline, stats
14 and 15), so the price must still be held; the client asks nothing and its gold is the server's.

The home town (approved 2026-09-28 as the town a town portal leads to; the wake 2026-09-29, Joshua: "the town
portal cheat is meant to act as the player's hometown or base. This should also be the location they show up when
they die"; the panel's Quality of life, `home_town` 0 .. 5, the console's `home`). The pair maker refuses a pair
across acts and a use moves inside one act, so:
- the cast hook marks the thread (the portals made inside are a scroll's or tome's of that player, not a quest's or
  a traveller's);
- the town end maker's hook, inside a cast of the local player for a portal of class 0x3B whose destination is its
  act's town, hands the original the home town instead (hometown::chosen_act: only a town whose waypoint the
  character has, 0x3D6930; never the fifth in the classic bank), its act made first with 0x48AA50 when the game
  has it not. When the portal made stands in the level asked for, the portal beside the player is told that it
  leads there (D2RCore's setter): the partner getter looks for the other portal in the act of the destination, and
  the client shows the destination's name. A portal the game put elsewhere itself (Tal Rasha's Chamber) is left;
- the use hook acts on a portal of class 0x3B whose destination is a level of another act than the one it stands
  in: what the game would refuse is refused (not open, the user dead or just through a portal; never the
  original, which would move the player as inside one act), the act made if it is not there, then the move to that
  act's town (0x488180, spot 0xB). Towards the town that is the trip: the town end stands at that spot. Back to the
  field the original runs after it, in the right act now: the move to the portal, the quests told that the town
  was left, both portals taken away as for any owner coming back. A move that did not arrive (the level the player
  stands in is of another act) refuses the use;
- the wake hook (the server's handler of a dead player's request to go on) notes whether the local player is in
  its dead mode (0x11, read from the handler's own test), runs the original, which makes the player whole and moves
  it to the town of the act it died in, and then, for a player that was dead, is alive now and stands in its act's
  town, makes the move to the home town when that is another act's (the act made first; 0x488180 with the spot the
  handler's own move has, 0: where a game that starts in that town puts the player). It is what a waypoint taken
  right after the wake would be: a player alive and in a town, moved from inside a packet's handler. A hardcore
  character is left dead by the game, and nothing is done. The body lies where the player died; a portal open to
  the home town stands there and leads to where it was read;
- the client's hook (UI thread, reads only): the client's test whether it goes through a town portal ahead of
  the server answers no for a portal whose other end (the level the server told it, unit +0x1BE) is of another act
  than the level the portal stands in, whatever the switch says (such a portal may stand from before it went off).
  The client then sends the ordinary request and the server's packets move it: the act let go, the other made, the
  rooms, the place. Without it the first trip back crashed the game (Joshua's run, 2026-09-29: home the Rogue
  Encampment, a portal read in the Outer Steppes; the way to town worked, the blue portal's use stopped the game
  2.5 s after the loading screen went): in a town the client makes the room at the portal's other end in the act
  it is in, and Act 1's map has no level 104 (see the record: the level of a DRLG). Outside a town the client makes
  no trip of its own, which is why the way to town worked. The three server hooks go in only after this one.
Not done for the trip to the town: the portal's flags (| 5) and the 3 s of just_portaled, which only the original
sets. The towns and every level's act are read once per bank from the loader's ActInfo and Levels rows (they must
name a town with a waypoint for every act, the waypoints rising). The tick publishes the towns reached once a second
for the panel. The town a game starts in is the game's own (the character list's act). The panel has the picker,
its (?) and nothing under it (Joshua, 2026-09-29: "remove all the text below the town picker to explain it"): what a
choice comes to is in the (?), a town not reached is marked in the picker's list, the counts are in the console's
status.

NPC item services without their quests (approved 2026-09-29; asked as Larzuk's sockets, "allowing for infinite
socketing", then all three at Joshua's choice; the panel's Cheats section, `infinite_imbue`, `infinite_sockets`,
`infinite_personalize`, the console's `imbue`, `addsockets`, `personalize`). Charsi's imbue (quest 3, Tools of the
Trade), Larzuk's sockets (35, Siege on Harrogath) and Anya's personalize (38, Betrayal of Harrogath) are a quest's
reward, once. Both sides of the game ask the quest, each its own way:
- the client puts the service in the NPC's menu as the menu opens, when its copy of the quest record has a reward
  to be had (flag 1) for the quest of the NPC's quest row. The menu's opener is hooked, and for the NPC of a
  service that is switched on, once the tick has its quest out of the record (an entry the server would refuse
  is not offered), the row's own routine is called before the original (npcservice::menu_opens). The original
  then builds the menu with the entry. The game takes the entry out again as the next talk begins (it resets the
  menus then), so a switch that went off shows at the next talk;
- the server's handler tests the quest only for an NPC whose record names one. While a service is switched on the
  tick has the quest byte of its NPC's record at 0 (npcservice::on_tick; put back when the switch is off, and by
  hooks::uninstall, after which no tick writes it again; a byte that is not what the game or the plugin left
  there is somebody else's and is left alone, with a warning), so the handler does the service at once: no quest
  test, no first pass with its save, no "reward used". No quest record is read or written: a reward earned and not used is still there afterwards, a
  quest not done can still be done, and no save holds anything of it.
What is done to the item is the handler's own (the record: NPC item service), and so are the game's tests of the
item on both sides. An imbued item is made by CreateItemEx, so the loot switches act on it as on any new item.
At bind (npcservice::bind, after sites::derive): the NPC table and the client's rows are read and each service's
NPC must be in both the same way (npcservice::find, in the header): one record names the service, its class is
the NPC's (154, 511, 512), a quest asks for it, and one row of that class waits for a reward of that quest; the
row's routine must be code of the shape of the game's menu routines. A service that fails any of it stays the
quest's reward (its switch says why); the others work. As a menu opens, before the routine is called, the menu it
writes must be that NPC's (the menus are on the heap, made as the game starts: they are read on the thread that
makes and changes them, and a menu that is another's is left alone, with a warning). sites.cpp reads
the table, the record's size and the fields from the getter and the handler, checks that a record without a
quest goes straight on to the service (the first pass's three ways out come to one place), tells the services
apart by what the handler does for each (the request's flag 0x20 and quality 6; item flag 0x800; item flag
0x1000000), and takes the rows from the opener's walk, whose quest test must be the routine the handler asks with.
Checked without the game (scratchpad harness/, 2026-09-29: the plugin's sites.cpp, npcservice.cpp, mem.cpp and
cheats.cpp over the dump mapped at the game's base, under wine): every signature once, the facts as above, the
three quest bytes out and back in with their switches and nothing else of the table changed, and the game's own
three menu routines run on a menu array of the test's: Larzuk's menu 39 had 4 entries after it, entry 2 the string
0x58DC and the routine 0x10DD30; Charsi's 3 and Anya's 41 likewise; no menus, too few, or another NPC's menu in
the place, and nothing is called.

Infinite cube ingredients (approved 2026-09-29; asked as "items will no longer be consumed when creating the new
item. Since the items were in the cube at the time of the recipe, place them back into the player's inventory, or
drop onto the ground if no space permits it"; Joshua chose "ingredients + sockets": Clear Sockets gives back what was
in the sockets too; the panel's Cheats section, `infinite_cube_ingredients`, the console's `cube`). A transmute's
second pass (after its first has saved the characters) runs the product routine, which takes every item out of the
cube, makes the products, then frees each ingredient it used up, and for Clear Sockets (3.3's cubemain has
useitem,uns) frees what was in the sockets of the item it empties:
- the hook on the product routine marks the transmute (a thread-local) for the local player under the switch and runs
  the original. Inside it the item free's hook keeps an ingredient instead of freeing it when the call is the product
  routine's (the return address, read from the routine's code), and the socket contents' hook first takes them out
  the way the routine's own other branch ("rem", which 3.3's data never uses) takes them out and keeps them; the
  game's free then finds the sockets empty. Every other free runs as the game has it;
- when the original is through, each kept item goes back, the ingredients in the cube's order and then the socket
  contents: into the inventory the way the quest reward giver hands a player an item (a free spot of page 0, the
  inventory's move, the client told as the cube tells it of its products, flags 0 and command 2, what the player's
  items give worked out again for a charm that counts there), else at the player's feet as the giver drops one,
  else (no spot on the ground either; a warning) freed as the game would have. The client, told a moment before
  that the item was gone (command 4), is told it was added: the pair the product routine sends for an ingredient it
  changes in place.
An item a recipe changes in place (useitem: an upgrade, added sockets, a repair, Clear Sockets' item) is its product
and stays in the cube, changed: nothing is copied. Every item that comes back is the unit the game had (its id, its
seeds, all of it), so nothing a save keeps disagrees with it. The portal recipes (the Cow Level, Pandemonium, the red
portal) keep their ingredients too: the game's routine opens the portal, or answers 0 and then nothing is used up in
the game either. The quest recipes (the Horadric Staff, Khalim's Will) give their parts back beside the quest item,
whose event runs as always. A product that does not fit in the cube is freed as the game frees it (with every
ingredient out first, the room is the game's own). sites.cpp reads every routine it calls from the giver's code and
checks that the product routine tells the client with the giver's notice and frees with the item free it names, that
the giver's free is that one, its inventory getter the key use's and its room getter the one found by name, that the
socket contents' free asks the same inventory getter, and that the routine's own "rem" branch calls the giver's
setters. Checked without the game (scratchpad harness/, 2026-09-29: the plugin's sites.cpp and mem.cpp over the dump
mapped at the game's base): every signature once, each routine and return address where the record has it, the
other facts derived as before. Until the evening of 2026-09-29 the socket contents' free was all the same never
hooked in the game ("hooks: socket contents free not resolved - not hooked", and Clear Sockets' socket contents
destroyed as in the game): its signature is the whole routine, 88 bytes, and the hook installer's buffer for the
expected bytes had 80 (the harness had asked with a buffer of its own). The buffer has 128 now; of the hooked
routines no other has a signature above 80 (78 the longest).

Identify on pickup (approved 2026-10-01; asked as "add a new cheat to automatically identify items on pickup"; Joshua
chose the notice hook, and pick-ups only; the panel's Cheats section under Infinite identify, `auto_identify`, the
console's `autoid`). Every item picked up from the ground goes through one routine (the record: item pick-up): a
click on an item (the request 0x16), the pick-up straight into the Horadric Cube (0x5F), Telekinesis, four console
commands. It takes the item off the ground and puts it in the inventory grid, the belt or the cube, or on the cursor
when the request asks for it, with the inventory's own move, and its last call is the item notice, which sends the
item's packet to the clients (the whole item, through D2RCore's SerializeItem: its properties only once it is
identified). Gold and a stack the item merges into never come to that call.
- the item notice is hooked. For any call but the pick-up's own (its return address, 0x471E6E, read from the
  routine's code) the hook calls the original and nothing else: the notice has 124 callers, D2RCore's among them;
- for the pick-up's call, under the switch, for the local player and an item without the identified flag (the one the
  identify routine tests, read from its code), the identify routine is called with the argument an Identify scroll
  and Cain pass (1): the flag, a charm's stats counted where they count, the clients told, unit event 6, and a drop's
  unique or set item recorded in the Chronicle (every player's of the game: offline the local one's);
- the identify routine's own update reaches the client for an item that is stored, worn or in the belt, and not for
  one on the cursor (the packet sender sends nothing in that mode). So an item the pick-up put on the cursor is
  identified before the pick-up's notice, whose packet then carries it identified; any other after it, the way the
  quest reward giver identifies an item it has just told the client of. Before the pick-up's packet the update would
  be one of an item the client still has on the ground.
What is written is what identifying writes, by the game's own routine. A pick-up never puts an unidentified item on
the body (its test asks the identified flag first), so a magic quiver goes to the inventory as in the game, identified
there. Items already carried or stashed stay as they are (Joshua's choice: no sweep of the inventory). sites.cpp finds
the three routines by signature, checks that the pick-up's last call and the identify routine's notice call the hooked
routine (and that the quest reward giver's notice, read for the cube, is that one too), and reads the flag; when, by
the item's mode, is autoid.h's (tests/test_autoid.cpp). Checked without the game (scratchpad harness/, 2026-10-01:
the plugin's sites.cpp and mem.cpp over the dump mapped at the game's base): 105 of 106 sites usable (the unit lookup
has its two copies), the three once each, the notice's return 0x471E6E, the flag 0x10.

Passive mercenary and minions (approved 2026-10-01; asked as "add a new cheat that turns merc and minions passive so
they don't attack enemies"; Joshua chose the enemy test gate over a stand-still design (the dispatcher handing a
passive pet's tick to the idle tactic: one hook, but the pets would stay where they are), and two switches; the
panel's Cheats section under the Invincible two, `passive_mercenary` / `passive_minions`, the console's `passivemerc`
/ `passiveminions`). How a pet comes to fight (the record: AI dispatcher and the rows after it):
- every monster's AI tick goes through one dispatcher (the AI think event), which finds a target first for the AIs
  whose table entry asks for one (target mode 2: the hydra, the shadows, the raven, the vines) and then runs the AI's
  own routine, which looks for targets itself (the mercenary's, the necromancer's pets', the Druid's, the Assassin's,
  the Warlock demons' new AiPet code);
- every way they find one asks the enemy test (0x492790): the unit searches through their filters (the AI's own two
  enemy checks end in a jump to its first stub), "what my owner fights" (the necromancer's pets and the Druid's search
  around their owner; the raven takes the player's own target), the Warlock demons' and the Shadow Master's own
  filters;
- the one other way is the target kept on the monster (its kind at monster data +0x44), read through one getter. Attract
  and Confuse set it on hostile monsters only (by alignment, or the caster's enemies); on a pet a summoning skill's
  spawn helper and an AI mode's init can set one, and the Warlock demons' AI keeps there the target it took and reads
  it back at its next tick.
So the dispatcher is hooked: for a monster the local player owns whose switch is on (the mercenary by its flag, any
other by the minions' switch: summons, revives, traps, hydras, the demons) the thread is marked for the length of the
tick (a thread-local, with a global count of marks up so that a question asked while none is goes straight to the
game). The enemy test and the kept target's getter are hooked: while the mark is up the test answers "not an enemy"
where the game says enemy, and the getter "none" (each calls its original first, the counts of what was refused
kept for the log and the console). Nothing is written and nothing of the game's is called but the originals. A tick
inside another (none is known) gets its own mark and puts the outer one back. What the pet does without an enemy is
its AI's own: it follows its owner (catching up when left far behind), buffs (the demons), and the vines eat corpses
near their owner (a corpse search of their own, no enemy asked). What is outside an AI tick is untouched:
auras (Holy Freeze, Holy Fire, the spirits'), thorns, the hits the pets take (they do not hit back), the player's own
skills and curses (0x48E460's other callers), an attack whose mode was set before the switch went on. Traps and
hydras (minions by the rule) do nothing; a Death Sentry's Corpse Explosion goes only for a corpse beside a target it
found. A side effect known: a revived Overseer's filter (0x5C2700) counts "no enemy" monsters of its minion type as
its own to whip, so a passive revived Overseer would whip hostile Act 5 minions; no other pet AI reads the test that
way (mode 3's ally filter is only Baal's throne's).
sites.cpp finds the dispatcher, the enemy test, its two entry stubs, the AI's enemy check and the kind's getter by
signature (each once), and checks that both stubs jump to the test and that the AI's enemy check and the routine
right after it each end in one jump to the first stub; without that (or one of the three hooks) both switches are
greyed. Checked without the game (scratchpad harness/, 2026-10-01: the plugin's sites.cpp and mem.cpp over the dump):
110 of 111 sites usable (the unit lookup has its two copies), the five once each, the check passing.

Permanent revives (approved 2026-10-02; asked as "add a cheat so that necromancer revived minions don't time out and
stay until death"; Joshua chose the timer dropped over a timer held while the switch is on, and Revives only over the
same timer's Decoy and Reanimate As as well; the panel's Cheats section under Passive minions, `permanent_revives`, the
console's `revives`). How a revive ends (the record: killself timer and the rows around it): Revive raises the monster
with the killself monster mod and puts an event at the cast plus its calc2 (4500 frames, 180 s); the event runs the
mod's timer, which for a pet whose owner is a player takes it out of the owner's pet list and kills it.
- the timer is hooked. For a live monster the local player owns that is in its pet list of Revive's pet type, under the
  switch, the original is not run. The event is freed all the same (the executor frees it after its handler, as it
  does a frozen monster's), so nothing ends the revive any more: it stays until it dies, the game ends (pets are not
  saved), or the game's limit (petmax, one revive a skill level) makes room for a new one, the oldest first;
- which list: the game's own pet lookup, the first question of the pet removal the original would make next, asked
  with the player (the tick's local server player) and the monster's id. Revive's pet type is its Skills row's +0x112
  (the byte its do-function registers its pets under), read through the loader on the game thread from the tick, per
  bank: 6 in 3.3. A Decoy (8) and a Reanimate As monster (in no list: 0) end as the game has them, and so does every
  monster that is not the local player's;
- switched off: a revive whose timer comes from then on ends as always; the ones it kept have no timer left and stay
  until they die.
Nothing is written, and nothing of the game's is called but the lookup and, for every other monster, the original.
sites.cpp finds the timer and the lookup by signature (each once) and checks that the timer's pet removal (the call
with its kill flag, `mov r9d, 1`) asks the lookup first; without that, or the hook, the switch is greyed. Checked
without the game (scratchpad harness/, 2026-10-02: the plugin's sites.cpp and mem.cpp over the dump): 112 of 113 sites
usable (the unit lookup has its two copies), both once, the removal 0x4FFD30, the hook's expected bytes 95.
tests/test_revive.cpp holds the rule, and every summoning skill's compiled +0x112 to its pettype.txt row (37 skills;
Revive alone "revive", 6).

Ignore item requirements (approved 2026-10-03; asked as "add a new cheat to ignore requirements from items so that a
level 1 with no stats could use anything"; Joshua chose the hook with a refresh at a switch over the hook alone,
class-only items kept with their class, and the mercenary too, under its own switch; the panel's Cheats section after
Infinite cube ingredients, `ignore_requirements` / `ignore_requirements_mercenary`, the console's `requirements` /
`mercrequirements`). What the game does (the record: item requirement test): every test of whether a unit can use an
item, on either side and D2RCore's, asks one routine, which asks level, strength and dexterity first and then the
identified flag, a tome's quantity, the restricted sockets and the class.
- the routine is hooked (any thread). With both switches off it is the game's, flags and all. For the local player
  (game::is_local_player: the server's unit or the client's copy), or its mercenary (the mercenary flag, and the AI
  record's owner on the server or the client's pet list's mercenary of the local player on the client), under that
  unit's switch, the original is called with flags of the hook's own, and then the three flags it hands back say met
  and a no is the routine's later tests asked again the way it asks them (requirements.h: answer, restricted,
  class_ok): the identified flag, the tome's quantity, the restricted-socket test itself, the class against the item
  type's ItemTypes row and, for a hireling, its Hireling row. A no with all three met was a later test's and stays.
  The rows are read where the game's getters read them (the data tables' array), on whatever thread asks, as the
  original has just read them;
- so the client puts the item on, the server's inventory move lets it stay, the server's refresh counts what it gives
  (a charm's too, through D2RCore's IsCharmUsable), a tooltip's requirement lines are not red (the tooltip builders
  color them by the flags), and the inventory's background is not red;
- when a switch changes in a game, the tick (server thread) calls the cube's refresh (0x470C90) on that unit: the
  game takes every worn item's stats off and puts back those the test passes, so what is worn counts, or stops, at
  once (a dead character's once it is alive again; the mercenary only while it is with you, found by 0x4FF1A0 with
  pet type 7, and else as it comes back, when the game works its items out itself). A game's load works them out by
  itself, so the first tick of a game only takes the switches as they are;
- switched off, the game's own rule is back: what is worn stays on, and an item whose requirements are not met
  gives nothing (red). Nothing reaches a save but what the game itself writes.
Class-only items stay with their class: the game's data has hand-to-hand animations (cof files `..ht1` / `..ht2`) for
the Assassin alone, so a claw on another class would have none, and how another class's items look on a character is
something the game never shows (Joshua's choice, asked with the hook). sites.cpp finds the routine, the client's
mercenary check and the server's pet lookup by signature (each once), reads the tail's three places of the routine
(each refusal a jump to the same `xor eax, eax`), the getters' tables and layouts, the client's pet list from the
lookup the mercenary check calls, and checks that the cube's refresh asks the test; without the routine's tail as
read both switches are greyed, without the mercenary's parts its switch alone. Checked without the game (scratchpad
harness2/, 2026-10-03): the game's own routine run from the dump mapped at the game's base, with MinHook on its entry
(as the loader hooks it) and requirements::test as the detour, over data tables made from the loader's compiled rows
(weapons, armor and misc as the Items rows, ItemTypes, Hireling), its four D2RCore entries stubs (a unit's stats, the
item's requirement percent twice, the level requirement), a client pet list of the test's: 692 items x 40 units
(players of each class, yours and another's; mercenaries of each hireling class and a monster that is none, the
server's of yours, the client's copy, another's; a summon of yours) x identified or not x equipping x three body
locations x two tome quantities x each switch: 1,992,960 cases, none off. With the switches off, the game's answer
and flags; for your units under their switch, the game's own answer with level, strength and dexterity far above
any requirement, and the flags met; for every other unit, the game's (124,356 answers turned to yes, 171,804 kept no
by a later test). tests/test_requirements.cpp holds the rule, and the compiled ItemTypes and Hireling rows to their
txt (the class and body locations; the version, monster class, act and equivalentcharclass) and pettype.txt's row 7
to "hireable".

Map reveal: on the UI thread (the UI pump), under the switch, when something says the player may stand in an area
not revealed yet (mapreveal::arm: the loader's LevelChanged with its level, ActChanged, GameJoined, LocalPlayerReady,
PlayerResurrected; a switch or number changed in the panel or the console), from then on every frame until that area
is done, and once a second besides: the client player's level (path room ->
DrlgRoom -> level; its DRLG's +0x838 must be the automap callback, which only the client's DRLG has). An area that
cannot be started (no character in a level yet, the character not yet in the level the loader announced, the automap
on another layer) is looked at from frame to frame for 10 s, then once a second. When the active
automap layer is that level's layer (the update 0xD4310 switches it shortly after an area change; waiting means no layer
is switched), each room of the level's list goes through CreateActiveRoom and the automap callback (force), about 3 ms
a frame, the list walked again from the level each frame (only a count is kept, so a freed level is never read). A
level is done once per client DRLG (a new act makes a new one); the game's automap save keeps the rest. The flood
fill 0x1FB050 and D2RCore's `revealmap` reveal everything connected (an act, about 1 s per MapSense) rather than the
area, and MapSense moved its act-wide reveal off room building for the cost; one area at a time stays small.

Kept between games: on the UI thread (the pump) while a game runs, the automap's UI var and the two Show
Items bytes are read (a Show Items key only while its display mode is Toggle; the modes read with them, 250 ms apart
at the least, while one of its options is on) and what they are is remembered (the TOML's automap_was_open /
show_items_was_on / show_items_unfiltered_was_on, written by the watchdog once a change has held 1.5 s, or at
GameLeft). They are read when something says they may have changed (remember::look: a UI message of the game's that
is an input other than the pointer moving, or PanelManager's; a switch or number changed in the panel or the
console): in the frame after it and once more 150 ms later, and once a second besides. Nothing is recorded
under the Esc menu or once the game is being left (PausePanelMessage ExitGame, FrontEndNavigation ToFrontEnd). At a
game's load (GameJoined / LocalPlayerReady, once per session generation), once the client player stands in the game
(alive, a level known), a key whose state differs from the remembered one is pressed through the key-action table
(ids 7, 37, 67, each entry checked at bind: the game's handler or thunk, or a loader jump out of the image; the Show
Items entries pinned by their release thunks), at most 6 presses 250 ms apart, and kept so until the loading screen is
gone (PanelManager Close/UnloadPanel LoadScreenPanel; 10 s without it) and at least 1 s has passed (every frame,
for those seconds); then tracking
starts. Pressing through the table matters: D2RCore's wrappers update its session memory, so its own restore agrees;
opening the panel directly (0xCD7C0) would leave its memory stale and it would close the map again.

Cinematics close the automap (reported 2026-10-03: "when changing acts it seems the map gets closed even if it's set
to stay open"; the game log of that day has Andariel killed at 14:21:27 and Lut Gholein at 14:22:04, so act02start
played). Every panel the game opens goes through the panel gate (the record: panel gate), and the automap's row of its
rules closes the automap only for panel 17, the cinematics', which the video helper opens as a progression video plays
(the first arrival in Acts 2, 3 and 4 after the act's boss, Diablo's end with the expansion's intro, Baal's end) and
closes at MovieComplete; nothing in the game opens the map again. A waypoint between acts plays none, and the map stays
open (the trace of 2026-09-29: no PanelManager message of the AutoMap's from the LoadScreenPanel's opening to past its
unloading). The other ways the game closes the map itself are the close-all routine's automap flag (leaving a game,
the Esc key with nothing else open, and packet 0x62 with type 6, which only a debug command and a classic game's quest
timer send). So: sites.cpp derives the gate's rules from the Automap key's toggle (its gate call, the gate's walk over
the UI vars and its rules, 32 panels) and game.cpp reads the automap's row at bind ("game: key actions: ... the game
closes the automap as panel 17 opens"). While one of those panels is open (game::automap_held_closed), the tracker
records nothing ("remember: a cinematic of the game's closed the automap - it is opened again once the cinematic is
over", when the map was remembered open); once it is closed (a look at "Cinematics" messages, else the look once a
second), the three are put back as at a load (the same presses and checks), after a loading screen still up (an act
change's; LoadScreenPanel OpenPanel to Close/UnloadPanel, 10 s at the most) and kept 1 s ("remember: the automap is
open again after the game's cinematic (1 press)"); a dead or absent character is waited for without the load's 60 s
limit. Without the gate's rules everything is recorded as before. No new hook and no new routine called: the rules are
static data (base-relocated), and the presses are the load's.

All areas terrorized (approved 2026-09-27; it replaced the zone lock, a DesecrateGetCurrentScheduledZone hook that
answered with one chosen zone): the tick, under the switch, looks from a game's first second and then every 5 s. If
the game has terror zones (0x488C70), the zones are each manual group's zone of the config for now, then each
rotation zone with a level none of those covers (the Moo Moo Farm); a zone with a level that has a runtime (the
game skips a level without one) but no manual entry, listed or queued, is applied (game, zone, 2) and the clients
told (0x4F22C0), as a shard's use does, one update per zone as one per shard; a zone applied 4 times in a game and
still short is left alone (a warning). The rotation carries on underneath: its levels wait in the queue behind the
manual ones (the Moo Moo Farm's too, applied as manual from the rotation's own zone record). Switched off in a game it
terrorized: 0x488DF0 (game, 2), then the resolver's zone applied again (game, zone, 1) and the clients told - what a
rotation does with its own kind, which also rebuilds D2RCore's Herald state (only its apply does). sites.cpp names
the four routines from the shard's use and checks them against the job's calls, and checks the layouts terror.cpp
reads (the config's groups in the pick; the game's lists and level runtimes in the removal and 0x489C10).

Heralds are D2RCore's, not the game's: the exe parses only the old token fields (chance_to_gain_herald_token,
max_herald_tokens), which 3.3's desecratedzones.json does not set; D2RCore reads its zone_chance_* tier curves and each
level's zone_data_id group and zone_completion_weight ("3.2 config"). Only RotW Hell has tiers (5). On every monster
death (D2RCore 0x2B41E0) an eligible kill (the Desecrated state 0xBD, counted for kill progress, not a critter, Herald
or minion, not NoXp/NoTreasureClass or MonStats CannotHerald, a level with desecrated runtime (+0x2E8) and in D2RCore's
level map) takes its group's completion (0x2BE940: weighted mean over the group's levels of rooms activated / rooms
(D2RCore's per-level room count, a levels.txt override or the level's own) x killed / populated (runtime +0x2D0 /
+0x2CC)), minus the group's completion at its last Herald, and rolls tier curve + base (0 in 3.3): shift + asymptote /
(1 + e^(-slope (progress - midpoint))), in percent per kill. Positive from about 52 / 43 / 43 / 30 / 6 points of
progress for tiers 1..5, up to about 3.9 / 2.9 / 2.9 / 2.5 / 2.35 % at 100. Accepted: a spawner at once when the killer
is in that level (else queued until a room change), the group's mark set to the current completion, and the game's
tier (+0x1A5C4, 1 from the game's creation, 0x4034C0) + 1 up to the max; nothing lowers it but the debug commands.
D2RCore's apply wrapper rebuilds its level map and zeroes its counters but keeps the group records (their marks) and
the room counts, and the kill counters are the level runtimes', so re-terrorizing (a rotation, a shard, this switch)
resets nothing: a group's Heralds are limited by its own progress, one or two at low tiers, more at tier 5. Levels
without a zone_data_id (20, 45, 50, 73, 132) never roll above 0. D2RCore's debug mode (loader console `debug_mode`)
has a Desecrated Zones panel with all of this per group.

Character: the Esc menu pauses a single-player game, and the tick (every server frame) does not run while it is
up; the UI callback does. So a save is taken by the UI pump from the client's copy of the character (base stats and
the skill list: own skills of the class, base level > 0), the tick only as a fallback (both read one flag while
nothing waits); a respec or a load waits for
the tick, and the panel says it happens when the menu closes (with a Cancel). Load = the reset, then attributes
(str, dex, vit, ene up to the preset's base values) and skills through the 0x3A/0x3B handlers with synthetic
packets, in passes until nothing more can be spent (prerequisites unlock across passes).

## Tools (scratchpad `re/` holds the dump, the fixed PE and a Ghidra project `D2Rq`)

- DEV builds dump the decrypted image to `d2rloader/logs/D2R-<build>.dumped.exe` (or `cabbycodes dump`).
  `tools/fix_dump.py` rewrites section headers so the dump loads in Ghidra (`*.fixed.exe`).
- `tools/disasm.py dump rva [len]`, `tools/make_sig.py dump rva [len]` (wildcard the rel32/RIP operands by
  hand where it does not), `tools/pdata.py dump rva...` (function bounds, chained unwind),
  `tools/xrefs.py dump rva...` (callers), `tools/find_stat_sites.py dump --stat N` (stat helper call sites).
- `analyzeHeadless <proj> D2Rq -process D2R-93847.fixed.exe -noanalysis -scriptPath tools/ghidra
  -postScript DecompileAddrs.java <rva...>` decompiles a reading list.
- Verify every new pattern is unique in the dump before adding it to `sites.cpp`.
- A harness (a mingw exe run under wine, kept in the scratchpad) maps the dump at the game's base and runs the
  plugin's sites.cpp over it, or the game's own routines: link it at another base (`-Wl,--image-base=0x300000000
  -Wl,--disable-dynamicbase`, else the dump cannot be mapped at 0x140000000) and map the dump executable. The data
  tables are not in the dump (the array 0x2A9A580 is empty there): they are made from the loader's compiled rows
  (the record: superior kind test), the units from the layouts below, and what goes too deep is given a stub at
  its entry (`mov rax, imm64; jmp rax`).
- The loader ships the game's excel tables as text: `<game>/d2rloader/data/compiler/3.3.0/data/global/excel/*.txt`
  (monstats, monstats2, levels, skills, ...). A MonStats class id is the row among the compiled rows (skip the
  "Expansion" divider); MonStats2 rows join by MonStatsEx.
- `tools/stash_seeds.py [--also file]... file...` reads stash files (.d2i, PlugY-format .d2x) and characters (.d2s)
  with the game's tables (the loader's txt copies under `GAME_DIR`, or `--excel`) and says which items have a seed
  that does not roll their base defense or picture; `--fix --backup <dir>` gives those a seed that does, the game
  closed (a character's checksum is written with them, its sidecar never; `--copies <before> <after>` hands the
  seeds of an earlier fix to the copies of its items; see Seeds in the saves). `--all` lists every item with a seed.
- `tools/casc_extract.py <CascLib> <game dir> <out dir> <data path>...` reads files from the game's CASC storage
  with the CascLib D2RMM ships (`<D2RMM>/tools/CascLib.so`): string tables (`local/lng/strings/*.json`),
  `hd/items/items.json`, sprites, excel txt (in 3.3 identical to the loader's copies). What D2RMM mods read.

Terror zones: the game's strings name the module (`D2Common\src\Levels\DesecratedZones.cpp`,
`data/hd/global/excel/desecratedzones.json`); `tools/data_xrefs.py` finds the code from the strings. The
loader's own sections (`.maho` jump table, `.seija`, `.koishi`) sit at 0x3E29000+; a call into `.maho` is
a D2RCore reimplementation of that routine: the slot's `jmp [rip+x]` pointer (in the dump) gives its D2RCore RVA
(base 0xC0DE5000000). `<game>/D2RCore.dll` is an ordinary PE without symbols (its export-like name table has no
game names); Ghidra's headless import with analysis takes about 5 minutes, and its .data table of the game routines
it calls ({pointer set at run time, game RVA, 0}, 0x18 apart) says which vanilla routines it still uses; the game's
data it touches has records {pointer, game RVA} 0x10 apart (the no-damage switches at .data 0x704C20 / 0x704C30), so
a RIP-relative load of the record's pointer finds the code that reads or writes a game global. Its export table
(pefile's DIRECTORY_ENTRY_EXPORT) names 763 of its own routines, the `.maho` targets among them
(ApplyWideTimedStatEffect, SelectExpansionMagicAffix, CastAmplifyDamage, AddWideUnitStat, ...), and two by ordinal
only: 100 (0x1CE910, its init) and 108 (0x1CEF40, a game's load). Its console commands are a table of {name, game
RVA of the handler, arguments, description} (`killme` 0x411E70, `god` 0x410A90).

Stamina is stat 10 (max 11), refilled on the tick on both the server unit and the client's mirror (the client
decides when it stops running).

Movement speed: the bonus goes in the player's base velocitypercent (stat 67), so a skill's or a state's own (in
their stat lists) and gear's faster run/walk stack on top; the client's mirror gets the same base from the tick. The
game sets that base to 100 for each game's character, on a new unit (the record: a character's stats for a game), and
offline the player's id is 1 in every game. So the tick keeps what it wrote and the game's own base under it
(movespeed.h): a base that is not what it wrote, or another unit, is the game's own, and the bonus goes on top again;
a lowered bonus comes off the game's own base. Until 2026-10-02 the id said whether the bonus was in (Joshua: "after
exiting the game it went back to normal speed, then if I reduced the value back to normal speed it made me even
slower"): the next game's character never got it back, and 0 then wrote 100 - 100 + 0, a base of 0.

Health bars: once a frame on the UI thread, walk the client's monster table, drop the dead, the ones that cannot be
attacked (unit flag 0x4: hydras and the like, which the other tests let through), MonStats NPCs and unkillables and
whatever CLIENT_IsHostile rejects, read stat 6, and ask UnitAboveHeadToScreen for the anchor; the
present thread scales render pixels to the window and draws (hidden under the open side's half, and while the pause
menu is up). The one piece that works at every frame (the bars follow what moves), and only with something to
follow: with both switches off, outside a game, in a town or under the Esc menu the walk is not made and the last
bars are taken away once. The MonStats flags are the UI thread's own copy (taken when the game thread has read the
table again), so the walk takes no lock. Prime evils within 40 subtiles (kept until 60) go on the boss bar with their MonStats name (loader
localization by string id). RuffnecKk's Floating Damage calls ProjectUnitToScreen from an UpdateCamera (0xB9B90)
hook instead; the UI callback needs no hook. MonsterHealthHUD (D2RMM) only restyles the hover bar's layout. A bar's frame goes by the monster's kind, narrowest first
(fixed 2026-09-28: champions had the uniques' gold frame): a boss by its MonStats flags; a minion (a unique's 0x10,
a Herald's 0x400) plain; a super unique gold; a champion blue, asked before unique because the client gives a
champion the unique bit as well (its handler of a monster's mods, 0x12CE00: 4 when the message says champion, then
8 for every monster that has mods); a unique or a Herald gold.

What runs when (asked 2026-09-28: what runs at every frame while playing, and whether an event can stand in for
it). Three paces: the game's frames (the UI callback, the UI message listener and the present hook, all on the UI
thread under Proton), the server's frames (the tick, 25 a second) and the plugin's own threads.
- What it cost, from the run of 2026-09-28 (the DEV build, which `make install` installs) and from the building
  blocks timed under the game's Proton (GE-Proton11-1, a throwaway prefix): a read of a flag 1.5 ns, a guarded read
  2 to 6, InterlockedExchange 6, a critical section 11, GetTickCount64 1.5, QueryPerformanceCounter 53, snprintf 54,
  IsWindowVisible 96, GetClientRect 119, GetCursorPos 220, SetEvent 186, GetForegroundWindow 4,200 (a call to the
  wineserver); an ImGui frame without a renderer 1 us, 2.5 with 50 bars. So every look that was made at every frame
  (the map reveal's walk from the player to its level, the automap's and Show Items' state, the preset lock, the
  swap chain's description and the window's visibility at a present) came to under half a microsecond a frame
  together, 0.007 % of a core at 144 frames a second: not what was felt. What was: log lines, 5 to 9 ms each, on the
  UI thread. The DEV log of UI messages wrote 2,000 of them in the run's first 150 s, up to 108 in one second (more
  than half of that second), the health bars' counts one every three seconds of a fight, every level change its
  event line, every area its map reveal line.
- The log (log.cpp): a thread of the game hands its line to the writer thread (a formatted copy into a queue, some
  0.4 us) and goes on; the writer writes them in their order, and a line written 250 ms or more after it was made
  says by how much ("(made N ms earlier)"). Written at once, by the thread that made them, after whatever waits:
  the plugin's own threads' lines (the worker, the watchdog, the DEV poller), warnings and errors, and every line
  with `trace = true`, which is what to set when a crash is looked for (the last lines before it are then in the
  file, and the frames wait for them as before). 8,192 lines may wait; more are counted and left out.
- The log's switch and its level (asked 2026-09-28: "an option in imgui to enable or disable logging", then "a
  dropdown to select the level of severity to print (pick the least critical type to allow and include more
  critical than that)"; the panel's Diagnostics section: the checkbox Write the log and the dropdown beside it,
  Information / Warnings / Errors; `logging` and `log_level` ("info", "warning", "error") in the TOML; `cabbycodes
  log on|off` and `cabbycodes loglevel info|warning|error`; off and "info" unless the file says otherwise). Off as
  installed (asked 2026-09-29: "disable the logging by default on a new install"): the shipped TOML, which the
  loader writes at the plugin's first load, and the default for a file without the key both say false
  (tests/test_config.cpp holds the shipped file to the defaults); an install that has the key keeps its own. The
  level is the least severe kind of line that is written, the more severe are written too; switched off, none is.
  A line that is not written returns before it is put together (2 ns). Written whatever is chosen: what is asked
  for by name, made inside a LogAlways (the console's perf outcome and dropcheck, a DEV command's lines), and what
  the log says of itself ("log: switched off ...", "log: switched on - ...", "log: from here on warnings and errors
  are written", and at a load one line when it is off or takes less than everything, "CabbyCodes vX: the log is
  off (logging = false) ..." or "... the log takes errors only (log_level = "error") ..."), so a log that ends,
  never begins or has gaps says why. The settings are read before the first line for that. A line that is
  written only the first few times (log_first, log_once, with the line's kind: the drop sessions, the affixes,
  the unit seeds, a tome's charge, a key, the wear, the item level; of the warnings the tables of another size,
  the forms that came out otherwise, the treasure classes not worked out) does not use its count up while it
  would not be written, so those lines come once the log takes them; what the plugin found at the game's start
  (the sites, the hooks) was never made and needs another start. plugin.cpp tells the log when either changes
  (the switch is a cheats::Kind like the others, kLogging; the level a number of cheats', like the exit method).
- DEV logs that came by the frame are bounded whatever the log costs: a kind of UI message (target, command, text)
  is logged the first two times it is seen, the two that come every frame never (with trace every message as it
  comes, repeats collapsed, 2,000 at most); the health bars' counts six times a game and the side panels four
  (with trace as long as they change); the mana cost four times.
- The UI callback (the pump, once a frame): the health bars (above); a preset to save (a flag); the map reveal and
  the automap and Show Items, each behind gate.h: a flag set by what happened, then the work - the map reveal from
  frame to frame until its area is done (gate::Work), the automap and Show Items in the frame after and once more
  150 ms later (gate::Looks) - and a look once a second for what nothing announced. The pump still asks the loader
  for the next frame's callback at every frame (the health bars need it; `perf` times what that costs).
- The UI message listener (three or four messages a frame): the two that come every frame are told apart by their
  first letter and a comparison, the rest by target.
- The present hook: the game's swap chain and its window are found once (GetDesc and IsWindowVisible were asked at
  every present); a frame with the health bars alone sets the window's size and a time step itself and leaves out
  the Win32 backend's input (the foreground window, the cursor, the key states: the panel's needs); the back buffer
  index comes from the IDXGISwapChain3 asked for once. Nothing is drawn, and no ImGui frame made, without bars or
  the panel. The presents a second are no longer counted (nothing read them).
- The area names with their levels (the level-name hook, at every frame the automap is open): put together once per
  name, difficulty and table read, then compared.
- Named enemies on the map (the hook on the automap's draw of one unit): the game draws its automap at every frame it
  is open (the minimap too), and calls the routine for each unit of the rooms near the player; under the switch a
  monster costs a few reads (its kind and row; a named one its name, the client's hostility test and three drawing
  calls, and its mark kept up to date), and the local player's own call draws the marks of the layer whose monster is
  not drawn itself now (a few: spawn spots, and named monsters left behind) and the exits' names of the area the
  player is in (a few). Nothing when the automap is not drawn or both switches are off. The landmarks, the spawn spots
  and the exits are read in the map reveal's area pass, once per area and act visit (an exit area's border rooms: a
  few hundred reads of their collision maps).
- Rooms filled ahead (the landmarks' rolled shrines): the tick looks at the server player's area when an area or game
  event says so and once a second; while an area is being filled, every server frame: up to 8 rooms built, 2 ms at
  the most, then the game's fill pass fills them in its own frame step; once all are filled, one walk of the area's
  rooms' units. Each area once a game.
- The tick was left as it is: 25 a second, a dozen stat reads, the refills and the mirrors that are what the
  switches do, a flag each for the tables, the presets and the terror look (every 5 s: nothing but a shard or the
  switch changes manual terror, and the look is the check that it holds).
- The watchdog sleeps until it is woken (watchdog::wake: a setting changed, a preset saved or deleted, the game
  left, leaving the game asked for) and looks once a second besides; 40 times a second, as before, only while it
  waits for something by the clock (the loader to take UI work, a remembered change to have held 1.5 s, the exit
  ladder, a sample).
- Looked at and left: the window subclass (every message under the ImGui lock, some 50 ns) and the input the
  overlay clears at every frame the panel is hidden (a button's release must reach ImGui after the panel is gone);
  the DEV command file's poll (four times a second, its own thread).
- `cabbycodes perf [seconds]` (10; 1 to 120) times the pieces in the game and writes the outcome to the log when
  the time is up (the watchdog writes it): runs, the mean and the longest of each, the monsters a walk met and the
  bars it made, the time spent writing log lines kept apart, and the sum for the frames. Outside a sample a timer
  is one read of a flag.
- Not done, as they need something new in the game or a trial in it: the health bars drawn through the loader's
  OverlayService (its frame callback and canvas: lines, rectangles, text; no gradient) instead of a second ImGui
  pass with its own D3D12 command list at every frame with bars, which is now the most the plugin does in a frame
  beside the walk itself; the monsters kept from the client's unit and life messages (hooks) instead of walked; a
  distance cut before the three game calls a monster costs (hostile, life, the point above its head), should `perf`
  show the walk to be worth it (the client knows monsters some two screens around, and all of them are projected).

## D2RMM (mod/)

The mod folder D2RMM lists carries the plugin (`d2rloader/`, which D2RMM for D2RLoader installs) and nothing else:
`mod.json` has no `config`, so D2RMM shows no settings page for it, and `mod.js` writes no game data (it logs one line
at Install Mods). Joshua, 2026-10-01: "we shouldn't have this setting anymore and no settings page".

Until then the mod had one option of its own, the rune numbers (asked for 2026-09-27, after olegbl's Numbered Rune
Names, which does the same and which Joshua runs in D2RMM): `local/lng/strings/item-runes.json` is an array of {id,
Key, 13 languages}; the item names are Key r01..r33 (r01L.. are the short names). Some languages start with a grammar
tag ("[fs]El-Rune"), so the number went at the end; a name already ending with it was left alone. Rune names are game
data read at start-up, out of the plugin's reach: string lookup is D2RCore's (`.maho`), the data-table views are
read-only by the SDK contract, and the Resource service registers files only under the plugin's own namespace
(`.../d2rloader/cabbycodes/...`), so no game file can be replaced from the plugin and a panel switch for them is not
possible.

The item level was the second option here (the ShowLevel column of weapons.txt / armor.txt / misc.txt, in
`global/excel/` and `global/excel/base/`; 3.3's data sets it nowhere) until Joshua asked for it as a panel switch the
same day; the plugin shows it at run time now (see Item level above). Its row rule is the plugin's: rows with a code
and no quest id, weapons but type tpot, all armor, misc types amul ring scha mcha lcha csch jewl cjwl (3.3 adds cjw
Colossal Jewel, untyped and not spawnable in base, and cs2 Crafted Sunder Charm, rotw only): 291 / 217 / 8 rows
(base 291 / 217 / 6) in the txt.

D2RMM 1.9.2 (from its app.asar): mod.js runs in QuickJS as `(function(){ const config =
JSON.parse(D2RMM.getConfigJSON()); <mod.js> })()`. readTsv splits on "\n" only, so the CRLF tables keep "\r" in the
last column (writeTsv round-trips it); readTxt stops at the first NUL (no binary reads). A mod's settings are
`mods/<id>/config.json` over the mod.json defaults. Mods run in list order over the same in-memory files: a later
one sees, and can overwrite, an earlier one's changes. Checked with a node harness (D2RMM's parseTsv/encodeTsv)
over the CASC files: only the intended cells and entries change, 429 rune strings (33 x 13), and a second numbering
pass changes nothing.

## Dead ends

- Rolling a drop again until the loot filter shows one, and the longer search for elites (2026-09-28, a day): under
  a strict filter one roll in 100 is a keeper, so a kill cost thousands of rolls and still came out short or empty
  (the patience, the 30 ms). Joshua: roll only among what the filter allows. It is what runs when the plan cannot
  be made.
- Making new items from chosen item seeds (2026-09-27 to 28; see The item seed model): the six loot switches left
  the generator alone and chose both of an item's seeds, found by a search over the 2^32 seeds per kind of item
  (searcher threads, a seeds file, the first item of a kind waiting for its seeds). No save keeps an item's own seed,
  so the search bought nothing a save could show; some kinds have no seed for everything (so they needed other ways:
  a rare made as an imbue, ethereal asked for by flag); and the waits froze the game (5 s a unique with the wait at
  its top). Joshua: only the saved seed matters, and nothing is looked up while playing.
- The seeds that come closest, for an item without seeds that give everything (in for an hour, 2026-09-27): Joshua
  wants every item to have everything its switches ask for.
- Writing an armor's base defense after the game's roll (perfect rolls until 2026-09-27: stat 31 set to maxac in a
  hook on the defense roll): the unit seed a save keeps would not roll the defense the save has. The unit seed is
  chosen to roll the top instead, and the game's own item init rolls it.
- Curse resistance (stat 109) at 100 for curse immunity: the game's own rule, and one stat write, but the rule goes by
  the curse flag, which the shrines' bonuses have too: no shrine would work.
- Hooking the routine that puts a curse on (0x433D20), or waiting for one through D2RCore: its entry is D2RCore's
  jump, and what D2RCore's version calls back in the exe (the curse mask test, the state test for Attract, the stat
  list getters) either has no unit or cannot refuse. The callers are hooked.
- A price of 0 for infinite gold (the transaction cost 0x36F0B0 is asked by client and server alike): the buy handler
  refuses a price of 0.
- Buying without holding the price: the test is inline in each of the five handlers, on stats read through D2RCore's
  getters. It would take a hook on every handler that lifts the gold for the length of the call. Not asked for.
- A town portal across acts through the pair maker (0x432CE0): it answers 0 when the destination's act is not the
  room's. The town end maker is handed the other town instead.
- A portal into another act with hooks on the server alone (2026-09-28 to 29): the client goes through a town
  portal ahead of the server when it stands in a town, in the act it is in. What the server does was never
  reached: the game stopped at the click. Whatever is made to lead across acts needs the client's side looked at
  too: what the client does before it asks.
- Waking in the home town by handing the wake's own move another town (a hook on the move to a level that looks at
  its caller), or from the tick once the player is alive again (no hook at all): the first changes acts while the
  player is still in its dead mode, the second from inside the units' update, and the game does neither anywhere.
  The wake's handler is hooked and the move made after it, as a waypoint taken at once would make it.
- A superior item's kind without a new hook (2026-09-29, read, not built): by the item's seed (its RNG set in the
  quality step's hook so that the step's first roll comes to the kind), but a failed magic step seeds the item again
  before the superior step, and choosing an item's own seed is what was given up (see The item seed model); by a
  top-up after the step (the missing property applied, the file index changed), but a weapon that came with attack
  rating and durability cannot be given enhanced damage in place of one of them. The step's test is hooked, and the
  step picks the kind itself.
- The quest's flag lifted for the length of the service handler's call (a hook on 0x4FC230 that sets "a reward
  to be had" and puts the quest's two bytes back after): the handler's first pass saves every character with the
  reward marked used, so the save would say so of a reward the character never had, until the next save. The
  quest is taken out of the NPC's record instead: no first pass, no save, no quest record touched.
- A hook on the quest record's getter (0x325C50) that answers for the handler's and the menu opener's call: 1,546
  call sites on both sides of the game go through it, and the handler's first pass sets the flag again after its
  save, which would then be in the record for good.
- A hook on the NPC table's getter (0x3971A0) that hands the handler a record of the plugin's: its first ten bytes
  hold a RIP-relative `lea`, and nothing is gained over the byte.
- The loader's `PatchWrite` for the NPC record's byte: a patch is for the session, and a switch is to work, and to
  stop working, while playing.
- Looking the local player up in the global server unit table (never there).
- The player's id as what tells one game's character from the next one's (movement speed until 2026-10-02): offline
  it is 1 in every game. What the tick wrote, read back from the unit, says whether its bonus is still there.
- Taking the looks made at every frame for what slowed the game (2026-09-28): together they were under half a
  microsecond a frame. The log lines were the cost, 5 to 9 ms each on the thread that draws the frames, the DEV
  build's most of all (see What runs when). Time a thing before it is rebuilt: `cabbycodes perf`.
- A log of UI messages that collapses repeats of one message: the ones that come every frame alternate (the
  pointer, the hover bar), so none repeats and all are written.
- The item level from the name hook alone: for an item without ShowLevel the server sends the client a placeholder
  level 1, so every name would read (1). The writer patch is what makes it work.
- Colored uber keys and better rune visibility as display options (added and removed 2026-09-27): they need the
  sprites of edipo2s's ColorUberKeys and of Sandman's / park0n9's Better Rune Visibility, which may not be shared.
  Do not bundle another mod's assets.
- `runOnUiThread` from `LoadPlugin` (refused); `PatchCallRel32` into plugin code (contract forbids it).
- 0x550C20 / 0x5343EE / 0x545530 / 0x588B00 as the experience award (level-up utilities, hireling paths,
  kill handler); the award is 0x44F2E0.
- A tick-only mana refill: the client predicts the cost and the server never sends a correction, so the
  orb dipped. Zeroing the cost on both sides fixed it.
- D2RMM 1.9.2 (native Linux) Run Game with `waitforexitandrun` while Battle.net is open: hangs in
  `wineserver -w`. The stock D2RMM cannot install plugins; D2RMM for D2RLoader can.
- The item flag 0x80000000 (Roll returns max for it) as a perfect-roll switch: it marks the game's display items
  (tooltips, socketing rules, ethereal without durability). The debug quality switches 0x2AA6A6C..72 force every
  quality (uniques included), so all superior uses the quality step instead.
- A per-item hook on 0x444680 for the drops: the drop core's own output array gives the items.
- The ExecuteEvents hook up to 0.1.0 declared four arguments, so its "damage record" was the computeTotals int in
  r9 (0 or 1): its body never acted, and the original only got the real record because the compiler tail-jumped.
  Fixed; god mode ran on the native switch and exit before death on FinalizeDamage, so nothing visible changed.
- Setting the players' no-damage switch once and remembering that it is set (god mode until 2026-09-30): D2RCore
  clears it at every game's load. Then, for a day, reading it and setting it again at every tick and hit, with the
  life refilled and watched at every tick: it held, but it looked every frame, and Joshua asked for the routines
  that lower life to be hooked instead. The protected-stat test is.
- Toggling the monsters' no-damage switch (0x2AA6201) around a pet's hit: SetUnitStat (0x2F7DF3) and two other
  stat routines read it on every thread, so the client could drop a monster's life update in that window.
- A separate server path for casting a tome's skill from the skill bar: the book skills' function (entry 113 of
  the skill function table 0x238EA00, dispatched by 0x435520 through Skills+0xA0 -> +0x36) is 0x584B60, which only
  tells the client (0x47F520) to use the item; the client then sends the ordinary use request, like the TP hotkey
  (0x119B60) and the inventory. No skill module debits a book.

## Test checklist (Joshua runs it)

1. Log: sites resolved (all exactly once), hooks installed (regen, ExecuteEvents, FinalizeDamage, death
   penalties, GetManaCost, PLAYER_AddExperience, hireling experience, ITEMS_ShouldRemoveOnUse, item quantity update, inventory item
   name, item creation, class item skill bonuses, superior kind test, magic / rare / crafted affixes, affix picker,
   automatic affix picker, curse skill step,
   Cursed monster modifier step, vendor payment, client's portal trip, portal use, portal town end, town portal
   cast, wake in town, client's NPC menu, item free, socket contents free, cube products, item notice, enemy test,
   kept target, AI dispatcher, killself timer, automap's draw of one unit, item requirement test), "the item writer
   sends every item's real level to the client", "thread service: UI work accepted", "character: N presets in ...".
   No line begins with "seeds:" any more.
2. F7 panel; Esc menu shows it automatically; the Character section shows the class and points once in a game.
3. God mode in a pack; infinite mana with a costly skill (orb must not move); speed 0/100/300;
   exp: kill at 1x then 10x and at 1000x and compare the Status note ("Experience: N x 1000.00 = M"); with a
   mercenary along its gains are scaled too ("Experience: your mercenary's N x ...") and its level chases yours (a
   big kill can put it a few levels ahead, where it stops gaining until you pass it: the game's own gate); the
   console status reads "exp=x1000.0 (N gains scaled, M the mercenary's)";
   exit before death: potion, "armed", lethal hit ->
   character screen, no death counted; settings persist across restarts. Movement speed across games, without
   closing the game in between: 100, Save and Exit, load a character: as fast at once ("movement speed: base
   velocitypercent 100 -> 200 (bonus 100% on the game's own 100; a game's character, which starts without it)");
   lower it to 0: normal speed, not slower ("... 200 -> 100 (bonus 0% on the game's own 100)").
4. Health bars: a bar over each enemy, none over town folk, minions or the mercenary, or hydras (the Council
   Members' in Travincal and Baal's waves, a Sorceress's; DEV log "health bars: monster class 351 cannot be attacked -
   no bar", 351..353 are the hydras); bars follow while running,
   hide under the inventory's half, hide with Esc; boss bar near an act boss (log: "health bars: rotw MonStats ...
   prime evils: Andariel (156) ...", "projected points are WxH render pixels").
5. Loot (log: hooks TreasureClassDropCore / item generation quality step / property roll / item creation,
   "the property roll's 8 choice sites call it", "loot filter manager 0x1BF9D0", "unique dropped bits +0x3638",
   "ground item removal bound, loot filter bound"): with the loot filter switch every normal monster drops and only
   what the profile shows ("loot: monster ... hidden by the loot filter ... added"); opening the filter editor logs
   "loot filter editor open"; extra drops 3 adds three per kill and per chest/barrel; perfect rolls: identified
   drops at the top of their ranges ("loot: perfect roll ..."), armor at max base defense (18), Hellfire Torch classes
   still vary, and so does Wraithstep's "+1 to <tab> Skills" (Demon, Eldritch or Chaos; it was Chaos every time
   before 2026-09-29), a runeword made by socketing is maxed; all superior: white weapons/armor drops and vendor normals are
   Superior; all ethereal: non-set weapon/armor drops are ethereal, vendor stock is not; all socketed (log: "sites:
   socket roll: request flags ... hits=1", "settings: ... socketed=1"): every white and superior weapon, body armor,
   helm and shield that drops is socketed, with varying counts (1 up to the base's maximum for its level; never above
   3 in Normal, 4 in Nightmare), vendor normals too; throwing weapons, gloves, boots, belts, jewelry, magic and better
   items are unchanged, and a new character starts with its usual unsocketed gear.
6. Combat (log: "game: respec bound, point spenders bound, antidote cure bound", "settings: damage=x..."):
   damage x10 - monsters die ten times faster, from spells, missiles, melee, poison and your summons; the console
   `cabbycodes` status shows "N hits scaled". Invincible mercenary / minions: let a pack hit them, life stays full,
   no poison, no deaths ("pet hits taken away"). Cannot be frozen: a cold monster's hits do damage but never slow
   or freeze you; cannot be poisoned: no green life orb from poison; get poisoned with it off, then switch it on
   with F7 while playing: the orb turns red at once ("cannot be poisoned: the poison on you ended (1 stat list
   taken off)", status "1 poisons ended"); several poisoners at once end together.
7. Consumables (log: "hooks: ITEMS_ShouldRemoveOnUse hooked", "the item-use effect takes a tome's charge at 0x5817CC
   as expected", "consumables: bank N Items: ... potions (by their item types)"): a TP scroll read from the
   inventory, the belt and the TP hotkey stays; a TP tome's count stays after right click, hotkey and skill cast
   ("consumables: a town portal tome keeps its charge"); identify scroll and tome likewise; potions from the belt
   (1-4), the inventory and shift+click to the mercenary stay; merging two tomes still moves the charges; a
   Potion of Life or Book of Skill is used up.
8. Character: Save a preset from the Esc menu - it is in the list at once ("character: preset 'x' saved").
   Reset skills and attributes (confirm) from the Esc menu - the panel says it waits for the menu to close; close
   it: the respec sound, all points to spend ("character: respec - N attribute points and M skill points"); with
   F7 in the running game it happens at once. Reset, then Load the preset - the same skills and attributes back
   ("character: preset 'x' loaded - 0 attribute and 0 skill points short"); cabbycodes.presets.txt appears beside
   the settings; a preset of another class is not listed in a game.
9. Keys (log: "hooks: key use hooked", "game: key search bound (key item type 0x29)"): with one key in the
   inventory, open several locked chests (a locked door too, if one turns up) - the key count stays ("consumables:
   a key opened a lock and stays", status "N keys kept"); with the key in the stash or cube, or none, the chest says
   it needs a key; with the switch off the key is used up as usual.
10. Map reveal (log: "sites: ... DRLGROOM_CreateActiveRoom / automap: reveal a room / automap serializer: checked cell
   count" once each, "game: ... map reveal bound, automap save has the checked cell count", "map reveal: ready"): switch
   it on in a game - the automap shows the whole current area within a moment ("map reveal: level N (layer L): R rooms
   revealed, 0 not built, in F frames"); walk or waypoint into other areas (outdoors, a cave, another act) - each fills
   in; go back and forth between two areas and a cave - the reveals stay and no hitch builds up; Save and Exit, rejoin:
   it works again in the new game. Watch for a long hitch on a big area (Arcane Sanctuary, a large outdoor area) and
   for "waits for the automap" lines.
11. Kept between games (log: "sites: ... Show Items key: press / Show Items key: release / Automap key / key binding ->
   key-action table" once each, "game: key actions: Automap bound (D2RCore's wrapper), Show Items bound (D2RCore's
   wrapper), Show Items (Unfiltered) bound (the game's); display modes bound", "remember: 3 of 3 keys can be kept"): set
   the game's Item Name Display and Unfiltered Item Name Display to Toggle; switch the three options on, open the map
   and turn both Show Items on ("remember: the automap now open", "remember: Show Items now on"), Save and Exit, close
   the game, start it again and load a character: the map is open and both Show Items are on when the loading screen
   goes ("remember: pressed the key for ...", "remember: the automap is open as it was (1 press)"). Then close the map
   and turn Show Items off, Save and Exit, load another character: closed and off ("... as it was (0 presses)"). Take
   a waypoint to another act with the map open: it stays open. With Item Name Display on Hold the panel says to set
   Toggle, and Show Items is not kept. With the options off, D2RCore's own behaviour is back (the map closes on the
   first game after a launch).
   Cinematics (log: "sites: panel gate's rules 0x229E930", "game: key actions: ... the game closes the automap as panel
   17 opens"): with the map open, kill Andariel and go east with Warriv (or Duriel and Meshif, Mephisto and the red
   portal, Diablo, Baal): the cinematic closes the map ("remember: a cinematic of the game's closed the automap - it is
   opened again once the cinematic is over"); watch it or skip it: once it and the loading screen are gone the map is
   open ("remember: the automap is open again after the game's cinematic (1 press)"), and no "remember: the automap
   now closed" line. Save and Exit, load: still open. With the map closed before, it stays closed and nothing is
   pressed. With the automap option off the cinematic closes the map as in the game.
12. No durability loss (log: "sites: ... item durability loss (a hit's wear) / Impale: weapon durability loss" once
   each, "hooks: item durability loss hooked at 0x441B10", "hooks: Impale's weapon wear hooked at 0x5590C0",
   "settings: ... durability=1"): note your weapon's and armor's durability, switch it on, then melee a pack for a
   while, hitting and getting hit: nothing goes down ("no durability loss: a hit's wear on your weapon or armor
   skipped"; the console `cabbycodes` status shows "N wear rolls skipped"). An ethereal item keeps its durability too.
   With an Amazon, Impale leaves the weapon alone ("no durability loss: Impale's wear on your weapon skipped").
   Switch it off and the numbers go down again as usual.
13. The D2RMM mod (not the plugin): `make install-d2rmm`, then restart D2RMM (it reads a mod's mod.json only when it
   lists the mods). D2RCabbyCodes has no settings page, and Install Mods logs "D2RCabbyCodes: no game data changed;
   the plugin is installed by D2RMM for D2RLoader (Plugins tab)." The runes read as the game, or another mod (Numbered
   Rune Names), has them.
14. Item level (log: "sites: ... item writer: ShowLevel test / inventory item name (item level)" once each, "hooks:
   inventory item name hooked at 0x1E1D80", "hooks: the item writer sends every item's real level to the client
   (0x37D726: jne -> jmp 0x37D69E)", in a game "item level: bank 3 Items: N rows; 291 weapons, 217 armor pieces and 8
   rings, amulets, charms and jewels get their level (0 already shown by the game's data)", banks 1 and 2 291 / 202 /
   6): with Show Item Level off
   in D2RMM, hover a weapon, an armor piece, a ring, a charm and a jewel - no level; switch Item level on (F7, while
   playing): the same tooltips read "Name (N)" at once, with the item's real level (a new character's starting gear
   is 1; compare a drop from a known area level); a rune, a gem, a potion and a quest item show none. Save and Exit,
   reload: still right. Switch it off: the names are plain again. ("item level: shown after an item's name (level N)"
   the first time.)
15. All areas terrorized (log: "sites: Worldstone Shard: use (terrorize an act) 0x580320 hits=1", "sites: terror
   zones on (game) 0x488C70 / terror zone apply 0x3E2B430 / terror client update 0x4F22C0 / terror removal (kind)
   0x488DF0", "terror: all areas terrorized bound"): a character with Baal dead on its difficulty (Hell to compare
   with a real shard), offline terror zones on; switch it on and load a game: within a couple of seconds
   "all areas terrorized: Act1-Manual .. Act5-Manual applied as manual terror (37 / 34 / 27 / 5 / 22 areas)",
   "Act1-MooMooFarm applied as manual terror (1 area)" and "126 areas terrorized in this game now"; the panel reads
   "Terrorized now: 126 areas". Blood Moor, the Arcane Sanctuary, the Chaos Sanctuary, the Worldstone Keep and the Cow
   Level each say "Terrorized" on entering, with boosted monsters. Play past a rotation (or change the zone with the
   game's `desecrated` console command): all stay terrorized, and no "applied again" line. Switch it off: "all areas
   terrorized: off - the manual terror is taken off this game, the rotation's zone applied again", the panel's count
   drops to the rotation zone's areas, and an area not visited yet in this game comes normal; on again: applied again.
   Save and Exit, load again: applied again. A difficulty without Baal dead: "this game has no terror zones - nothing
   applied" and the panel says so. A real Worldstone Shard with the switch on is refused (its act has no zone left).
16. Max affixes and class item skills (log: "sites: item generation: magic affixes / rare affixes / crafted affixes /
   class item skill bonuses" once each, "sites: affix picker 0x58AE00", "affix slots (prefix/suffix) -24 / -12",
   "most affixes rare 6, jewel 4 (item type 58), crafted 4", "class skill top bonus 180", "game: ... affix picker and
   slots bound", "hooks: max affixes covers magic yes, rare yes, crafted yes", "hooks: class item skill bonuses
   hooked at 0x58AF50", "settings: ... affixes=1"): switch max affixes on, kill monsters and open chests, identify the
   drops: every magic item (charms and jewels too) has a prefix and a suffix ("loot: max affixes: magic item (class
   N, level L) had 1 affix, 1 added (at most 2)"), rares from a high enough area have 6 affixes, three of each ("rare
   item ... had 4 affixes, 2 added (at most 6)"), rare jewels 4, never more; vendor magic stock and gambled rares
   likewise; a crafted item (a Blood or Caster recipe, even at a low character level) has 4 random affixes besides the
   recipe's; uniques, set items and runewords are unchanged. Switch it off: the counts vary again. With perfect rolls
   on, a new class item (an orb, a shrunken head, a pelt, a Barbarian helm, a Paladin shield, a claw, an Amazon bow or
   javelin) has three "+3 to <skill> (<Class> Only)" lines ("loot: perfect roll: class item (class N, level L) rolls
   its skill lines with bonus 180 (the game's was 0)"); the console `cabbycodes` status shows "N class items' skills
   maxed, M affixes added".
17. Best affixes (log: "sites: item generation: affix picker / affix fits the item (item types) / affix table (bank)"
   hits=1 each, "sites: rare step: suffix pick 0x58BD3B", "rare step: prefix pick 0x58BDBC", "affix apply 0x3D3670",
   "game: ... affix table, fit test and apply bound", "hooks: affix picker hooked at 0x58AE00", "hooks: the rare affix
   step's picks go through the affix picker: suffix yes, prefix yes", "hooks: best affixes covers magic yes, rare yes,
   crafted yes", "settings: ... best=1"): with max affixes off, switch best affixes on and play in Hell (or a high
   area), identify drops: life suffixes on amulets, belts, body armor and circlets are "of the Whale" (81-100), on
   rings and helms "of the Mammoth", magic find on boots and amulets "of Luck", on rings "of Fortune", weapon attack
   speed "of Quickness", armor enhanced defense "Godly", weapon enhanced damage "Cruel" ("loot: best affixes: suffix
   N "of the Jackal" (level 1) -> M "of the Whale" (level 50) on item class C, affix level A"; the first four logged).
   Rares too (six affixes with max affixes on: all top versions, "of Luck" never on a rare). In Normal Act 1 the
   versions stay low (an item's level caps them: no level-37 Whale on an Act 1 amulet). Vendor magic stock and
   gambled rares likewise; uniques, sets and runewords unchanged. The console status shows "N affixes upgraded".
   Switch it off: low versions come again. If the loader refuses the rare step patch ("the loader refused to point
   the rare affix step's ... pick"), rare items keep the game's picks.
   The automatic affix (log: "sites: item generation: automatic affix picker 0x58AE80 hits=1", "sites: automatic
   affix picker calls the affix picker's two pickers, with its group", "hooks: automatic affix picker hooked at
   0x58AE80", "hooks: best affixes covers magic yes, rare yes, crafted yes, automatic yes"): perfect rolls and best
   affixes on, all superior too if you like, play in Hell and pick up paladin shields (Sacred Targe, Sacred Rondache,
   Kurast, Zakarum, Vortex Shield): every one reads "All Resistances +45" (Chromatic) or "+121 to Attack Rating"
   with "+65% Enhanced Damage" (Knight's), about 7 in 10 the first; none has less ("loot: best affixes: automatic
   affix N "Shimmering" (level 1) -> M "Chromatic" (level 50) on item class C, affix level A"). A rare paladin shield
   (an imbue at Charsi) reads +35 at most: Chromatic never goes on a rare. Sorceress orbs read "+60 to Life" or "+80
   to Mana", Necromancer heads "of Anthrax" (poison), Amazon bows and spears "+3 to Bow and Crossbow Skills" or "+3 to
   Javelin and Spear Skills", grimoires 21-30 magic or 16-32 fire damage. Save and Exit, load: as they were. Items
   made before this are not changed.
18. The saved seed and no lookups (log: "sites: item creation (CreateItemEx) 0x43CD50 hits=1", "hooks: item creation
   hooked at 0x43CD50"): all six switches on, play where a lot drops (Hell, terrorized areas) for a few minutes: no
   hitch when items drop, uniques and rares included, and every item has everything its switches ask for (six best
   affixes at their top values on a rare, three +3 skill lines on a class item, ethereal, sockets on white items).
   The first four armors are logged: "unit seed: armor class N (quality Q, ethereal) made from unit seed XXXXXXXX,
   found in T tries (U us): base defense D, the top of A..B as that seed rolls it" (every one with `trace = true`);
   the console `cabbycodes` status "unit seed (the one a save keeps): N armor pieces ...". A white armor shows the top
   of its base's defense; one with enhanced defense the top plus one, times its bonus; an ethereal one half as much
   again. A warning "unit seed: armor class ... has base defense D, not the top" or "did not take the unit seed" is a
   roll read wrong: send the log (after three the choice is off for the session, and the panel says so). Save and
   Exit, load: the armor is as it was. Nothing reads or writes cabbycodes.seeds.txt any more (its date stays; it can
   be deleted), and the panel has no wait setting.
19. Extra drops for elites only (log: "settings: ... extra=3 elites=1"): set Extra drops to 3 and tick "Only from
   champions, uniques, bosses and Heralds" under it (the loot filter switch off makes it easiest to count). Ordinary
   monsters and a unique's minions drop as in the plain game, chests and barrels too ("loot: extra drops: monster N
   (kind 0x10) is no champion, unique, boss or Herald - none added", "... object N (kind 0x0) is no ...", the first
   four). A champion pack: every champion drops three more ("loot: monster N (champion, kind 0xD) drop: D dropped,
   0 hidden by the loot filter, 3 extra asked; 3 added in P passes (I items tried, T ms), 0 omitted"); a random
   unique "(unique, kind 0x9)", its minions nothing more; a super unique (Bishibosh, Rakanishu, Pindleskin) "(super
   unique, ...)"; an act boss, Blood Raven, Radament, the Summoner, Izual or Nihlathak "(boss, ...)"; in a
   terrorized area in Hell a Herald "(Herald, ...)" and its minions nothing more. Putrid Defilers (Nihlathak's
   temple, the Worldstone Keep) are ordinary. With the loot filter switch on as well, every kill still drops only
   what the filter shows, and only the elite ones get the three more. Untick it: every kill and chest gets three
   more again. Console: `cabbycodes elites on|off`, the status line reads "extra drops=3 (elites only=1)". A
   warning "loot: no MonStats row of 508 bytes ..." means bosses are not recognised: send the log.
20. Drops picked among what the filter shows (log: no warning that begins "loot: what treasure class", "loot: item
   class ... came out", "loot: asking the loot filter first" or "drop odds:"): the loot filter switch on, 20 extra
   drops, elites only, the grail filter, all the loot switches as you play. The lines read "loot: monster N (unique,
   kind 0x9) drop: 5 dropped, 5 hidden by the loot filter, 20 extra asked; 25 picked among what the filter shows of
   treasure class 'Act 5 (H) Unique C' (390 items in 210 forms it shows; a walk makes 5.00 items, 0.0400 of them
   shown), 0 came out otherwise, 4 questions to the drop core, 1.8 ms; 0 omitted". Every champion, unique, boss and
   Herald drops its own picks and the 20 extra, every one an item the filter shows; ordinary monsters one drop
   each, mostly gold. No hitch at a kill: the times stay at a few ms. Nothing lies on the ground that the filter
   hides (Alt, with the filter's own Show Items). The console `cabbycodes` status: "N drops the filter hides never
   made, M picked among what it shows".
21. What drops (the same run): uniques and set items of the filter's lists; each unique once a game ("... N omitted
   - the treasure class has nothing (left) that the loot filter shows" late in a long game, when a monster's
   treasure class has no unique of the list left and no set item; a new game starts the list again). The five bases
   of the filter come ethereal or socketed as the filter asks, from the monsters that drop plain items (not from a
   champion's or unique's item pick, which is always magic or better). Open the loot filter editor, enable "Show
   Runes", close it: runes drop from the next kill on. A warning "loot: item class C asked for with quality Q was
   to come out with quality R ... and came out with ..." is a form worked out wrong: send the log (the item is kept
   when the filter shows it; too many turn the picking off for the session, and the panel says so). A warning
   "loot: what treasure class 'X' drops cannot be worked out (why)" means that one is rolled for as before.
22. The odds against the game (`cabbycodes dropcheck` in the loader's console, then kill a monster; a hitch of a
   fraction of a second): "dropcheck: monster class N, treasure class 'X' (item level L, ...): 5000 walks in T ms",
   "dropcheck: a walk asks for 5.0000 items as worked out, 5.0000 as walked; K kinds of item and quality worked
   out, J walked", a line a quality ("dropcheck:   unique  W worked out, V walked"), "dropcheck: of the K kinds
   worked out to come 5 times or more, the one furthest off is ... (S times what chance allows) ...", and
   "dropcheck: the odds worked out are the drop core's". Once on an ordinary monster, once on a champion or unique,
   once on a boss, with your magic find on; `cabbycodes dropcheck 20000` for more walks. A warning "dropcheck: the
   odds worked out are not the drop core's" is a routine read wrong: send the log. Nothing drops from the walks.
23. What runs when (log: "ui thread: the loader delivered a UI callback", "thread service: UI work accepted", as
   before). Play the first minutes after a launch, where the DEV build used to stutter: a run's log has some
   hundred "ui message:" lines instead of 2,000, each kind twice ("... (not logged again)" on the second), none with
   command 'MouseMove' or target 'MonsterHealth'; "health bars: N monsters known ..." at most six times a game (the
   sixth says "not logged again in this game"). A line written late says so ("(made 312 ms earlier)"): some at a
   game's load are to be expected, none while playing. Then the features as before, each of which now waits for
   something to happen: walk or waypoint into a new area - its map fills in with its first frames (item 10); press
   the Automap key and the Show Items key - "remember: the automap now closed" comes at once (item 11); switch
   Reveal the map on with F7 in an area not revealed - it fills in at once; save a preset from the Esc menu - in
   the list at once (item 8); change a switch in the panel and close the game within a second - the setting is in
   cabbycodes.toml; exit before death leaves the game as promptly as before (item 3). In town, and with both health
   bar switches off, no bars; leave town - bars at once; Esc - they hide, and are back as the menu closes.
24. The cost, timed (`cabbycodes perf` in the loader's console, then keep playing; `cabbycodes perf 30` for longer):
   ten seconds later the log has the lines that begin "perf:": one per piece with its runs, "us each", "the longest"
   and "ms in all", "monsters met" and "bars made" a walk, and the sum "perf: in the game's frames X ms of 10000 ms
   (P %), U us a frame over N frames; on the server thread's ticks T ms; writing log lines in either L ms". Once in
   a town and once in a fight with a screen of monsters. To expect: "map reveal" and "automap and Show Items" run at
   every frame but "of them looking" only a few times (once a second, and at keys and areas); "UI message (each)"
   well under a microsecond; the health bars and "frame drawn over it" the two that count in a fight, nothing
   drawn in town; L at 0. Send the lines: they say whether the walk of the monsters or the frame drawn over the
   game's is worth more work. With `trace = true` in cabbycodes.toml every line is written by the thread that made
   it, as before (for a crash hunt): the same sample then shows the log lines' milliseconds.
25. The log's switch and level (log: "settings: ... log=1/info trace=0"). F7, section Diagnostics. Untick "Write the
   log": the log's last lines are "The log OFF (panel)" and "log: switched off - from here on nothing is written
   ...", the dropdown beside it is greyed and the line under it reads "off: nothing is written"; playing on adds
   nothing (kill, change area, open the map: no "loot:", "event", "map reveal:" or "remember:" line).
   `cabbycodes perf 5` in the console still writes its "perf:" lines, `cabbycodes dropcheck` its "dropcheck:"
   lines. Tick it again: "log: switched on - information, warnings and errors are written", and the lines come
   as before, the first of each limited kind included ("loot: monster ... drop:", "unit seed: armor class ...").
   The dropdown, "The least severe lines to write": pick Warnings - "Log level Warnings (panel)" and "log: from
   here on warnings and errors are written" are the last information in the log, the line under the dropdown
   reads "written: warnings and errors", and playing on adds no [INFO] line (a [WARN] or [ERROR] one would still
   come; none is to be expected in a game that goes well). Pick Errors: "log: from here on errors only are
   written". Pick Information: "log: from here on information, warnings and errors are written", and the lines
   come again. Leave it at Warnings, Save and Exit, close the game: cabbycodes.toml has log_level = "warning".
   Start the game: cabbycodes.log begins "CabbyCodes v...: the log takes warnings and errors (log_level =
   "warning") ..." and has no [INFO] line after it; everything works as with the whole log (the panel's switches,
   the hooks: the cheats act). Untick the log, close the game, start it: one line, "CabbyCodes v...: the log is
   off (logging = false) - nothing is written ...". Tick it in the panel: "log: switched on - ...", and from there
   on the lines of the game being played (not the sites and hooks of the start: start again for those).
   `cabbycodes log on|off` and `cabbycodes loglevel info|warning|error` do the same from the console; the status
   says "log: written, level warning (warnings and errors)" or "log: off, level ...".
   A new install: with the game closed, move d2rloader/config/cabbycodes.toml aside and start the game. The new
   cabbycodes.toml is the shipped one (its comments and all) with logging = false, cabbycodes.log has the one line
   "CabbyCodes v...: the log is off (logging = false) ...", and "Write the log" is unticked. Close the game and put
   your own file back.
26. Cannot be cursed (log: "sites: curse skill: step for one unit 0x55A7E0 / Cursed monster modifier: Amplify Damage
   step 0x4A06B0" hits=1 each, "sites: curse apply 0x433D20", "sites: curse steps state at context +0x58; the
   Cursed modifier casts skill 66, a skill's state at row +0xA2", "hooks: curse skill step hooked at 0x55A7E0",
   "hooks: Cursed monster modifier step hooked at 0x4A06B0", "settings: ... uncursable=1", in a game "curses: bank 3
   States: 232 rows; 13 curses that can be cured (states 9 19 23 27 55 56 57 58 59 60 61 113 114); the Cursed
   monster modifier puts on state 9"). With the switch off, get cursed (the Oblivion Knights of the Chaos Sanctuary
   or the River of Flame; the succubi of Act 5; a unique monster that is Cursed), then switch it on with F7 while
   the curse is on you: the curse's mark over your head goes at once ("cannot be cursed: the curse on you ended
   (state 9: 1 stat list taken off)"). Stay among them: no curse lands any more ("cannot be cursed: a curse (state
   N) was not put on you", the first four; the console `cabbycodes` status counts "N curses kept off and M ended").
   Take a shrine (armor, combat, experience, a resist shrine): its bonus comes and lasts as always. Your own curses
   on monsters still land. Your mercenary can still be cursed. Switch it off: curses land again.
27. Infinite gold (log: "sites: vendor payment (gold taken) 0x5416D0 hits=1", "hooks: vendor payment hooked at
   0x5416D0", "settings: ... gold=1"). Note your gold and your stash's. Buy a potion, then something dear: the gold
   stays ("infinite gold: a price of N gold was not taken", the first four). Shift-buy a stack of potions, buy scrolls
   with a tome in the inventory (they go into it); gamble; repair one item and Repair All; have Cain identify (before
   he is rescued on that difficulty he charges); hire a mercenary, and bring a dead one back: nothing is taken, the
   stash's gold neither. Something dearer than all the gold you hold is refused as in the plain game ("not enough
   gold"): that is the game's own test. Sell an item: the gold comes. Switch it off: the next purchase costs.
28. Home town (log: "sites: town portal: cast / portal: the one made at the destination / portal: use / move a
   player to a level / act made for the game / waypoint: the character has it / wake in town (after a death) /
   client: portal trip ahead of the server" hits=1 each, "sites: portal pair maker 0x432CE0", "object destination
   setter 0x3E2B020", "sites: town portal object class 59, town spot 11, state after a use 102, class at context
   +0x20, acts at game +0x190, the other end's level at a client's portal +0x1BE", "sites: a death's wake a player
   in mode 17, moved to its act's town at spot 0", "game: town travel bound ...", "home town: bound (portals of
   object class 59, a town's portal spot 11; a death wakes a player in mode 17 at a town's spot 0)", "hooks:
   client's portal trip hooked at 0xFE1F0", "portal use hooked at 0x58F680", "portal town end hooked at 0x435DD0",
   "town portal cast hooked at 0x46FA70", "wake in town hooked at 0x4B6160", "settings: ... home=5", in a game
   "home town: bank 3: 5 acts, their
   towns levels 1 (waypoint 0), 40 (waypoint 9), 75 (waypoint 18), 103 (waypoint 27), 109 (waypoint 30); 138
   levels"). With a character that has all five towns: F7, Quality of life, "Home town": Act V - Harrogath.
   Portals: in Act 1 (the Cold Plains, say) read a town portal: a short hitch the first time in a game ("home town:
   act 5 made for this game"), the portal's label names Harrogath ("home town: the portal leads to Harrogath (level
   109, not the game's own 1); the portal there leads back"). Step in: the loading screen of an act change, you
   stand in Harrogath beside a blue portal ("home town: a portal from act 1 into act 5 used: the character taken to
   Harrogath, where the portal leads"). The mercenary came along. Step into the blue portal (where the game
   stopped on 2026-09-29): "home town: the portal in level 109 leads into act 1 (level 3): the client makes no trip
   of its own and asks the server", then "home town: a portal from act 5 into act 1 used: the character taken to
   the Rogue Encampment, the game's own use takes it on to the portal": an act change's loading screen, you stand
   where you read the scroll, and both portals are gone, as always. Should the game stop there again, d2rloader.log's
   last line says why (a line "Outdoor map generation stopped ..." is the client's own trip again). Read one in
   Harrogath's own act (the
   Bloody Foothills): the game's own, no act change, no log line. Choose Act IV and read one in Act 5; choose "The
   town of the act you are in": the game's own everywhere. Cast twice in a row in the field: the first pair goes as
   always (the portal in the home town too).
   A death: home Harrogath again, die in Act 1 (no portal open) and press Esc: the loading screen of an act change,
   you wake in Harrogath where a game that starts in Act 5 puts you, whole ("home town: a death in act 1: the
   character woke in Harrogath (level 109), taken on from the Rogue Encampment, the game's own"; the console's
   status reads "1 wakes at home"); the mercenary is with you; your body lies where you died
   (the waypoint and the walk back, and it gives you your gear as always). With a portal read before you died: it
   stands in Harrogath when you wake, and takes you to where you read it. Die in Act 5: you wake in Harrogath as
   always, no log line. With exit before death on you never die: switch it off for this.
   Not reached: with a character that has not reached Act 5 (or a classic one) the choice reads "(not reached)" in
   the picker's list, a portal leads to its act's town ("home
   town: the character has not the waypoint of Harrogath - the portal leads to ...") and a death wakes it in the
   town of the act it died in ("... - it woke in ..., the game's own").
   Kill Duriel and read a town portal in his chamber: Lut Gholein ("home town: the game put the portal in level 40
   itself ..."). The console: `cabbycodes home 4`, `cabbycodes home 0`, and the status line "home town: home=5
   (Harrogath); the character has the town waypoints of acts 1 2 3 4 5 and stands in act 1; N portals made to the
   home town, M uses across acts (J the client left to the server), K wakes at home". A warning that begins "home town:" (an act that could not be
   made, a move that did not arrive, a portal that could not be made) is a routine read wrong: send the log. If the
   log says "hooks: the home town leaves a death's wake to the game", portals work and a death wakes you as the game
   has it.
29. Imbues, sockets and personalizing without their quests (log: "sites: NPC record (by class) 0x3971A0 / server: an
   NPC's item service 0x4FC230 / client: an NPC's menu opened 0x1141D0" hits=1 each, "sites: NPC item services 43
   records of 28 bytes at 0x237EA60, the service at +0xB (imbue 1, sockets 2, personalize 3), its quest at +0x10
   (at most 42, asked for flag 1); the client's menu rows of 24 bytes at 0x22A70E0", "npc services: imbue: Charsi
   (class 154, record 2 at 0x237EA98) does service 1 for quest 3; the client's row 3 puts it in menu 3 with
   0x109100 (the entry runs 0x10DD30)", "npc services: sockets: Larzuk (class 511, record 38 at 0x237EE88) does
   service 2 for quest 35; the client's row 5 puts it in menu 39 with 0x109300 ...", "npc services: personalize:
   Anya (class 512, record 37 at 0x237EE6C) does service 3 for quest 38; the client's row 6 puts it in menu 41
   with 0x109400 ...", "npc services: 3 of 3 bound", "hooks: client's NPC menu hooked at 0x1141D0", "settings: ...
   imbue=0 addsockets=0 personalize=0").
   Larzuk first, with a character in Act 5 that has not done Siege on Harrogath or has used its reward: talk to
   him with the switch off: no Add Sockets. F7, Cheats, tick "Infinite sockets (Larzuk)", close the panel and the
   Esc menu ("npc services: sockets: no quest asks for it now (quest 35 taken out of Larzuk's record)"). Talk to
   him: Add Sockets is in his menu ("npc services: Larzuk offers sockets (the entry put in the menu; the quest is
   not asked)", the first four). Pick it, click a white weapon or armor piece whose base can have sockets,
   confirm: it has the most sockets its base can have at its level. Talk to him again: Add Sockets is there
   again; a magic item gets one or two, a rare, set or unique one; an item that has sockets is refused, as in the
   game. The quest log is as it was. With a character that has the reward unused: socket three items with the
   switch on, switch it off, talk to him: Add Sockets is there once more (the quest's own), and gone after it is
   used. Switch off ("npc services: sockets: quest 35 asks for it again (put back in Larzuk's record)") and talk
   to him without a reward: no Add Sockets. Switched off while his menu is open the entry stays until the next
   talk, and picking it then does nothing to the item.
   Charsi (the Rogue Encampment; "Infinite imbues (Charsi)"): Imbue in her menu without Tools of the Trade; a white
   item without sockets comes back rare, again and again (with perfect rolls, max and best affixes on: six top
   affixes at their top values). Anya (in Harrogath once she is freed, Prison of Ice; "Infinite personalizing
   (Anya)"): Personalize in her menu without Betrayal of Harrogath (Nihlathak alive, or the reward used); the item
   has the character's name; an item that has a name is refused.
   Save and Exit with the switches on, load: all three work in the new game. The console: `cabbycodes addsockets
   on|off`, `imbue on|off`, `personalize on|off`, and the status line "npc services: imbue (Charsi)=0, the quest
   asks, offered 0 times; sockets (Larzuk)=1, no quest asks, offered 3 times; personalize (Anya)=0, ...". A switch
   that is greyed with a reason beside it, or a warning that begins "npc services:" or "sites: ... an NPC's item
   service ...", is a table or a routine read wrong: send the log.
30. Infinite cube ingredients (log: "sites: cube: a recipe's products (ingredients used up) 0x5269C0 / item free (on no
   inventory, not on the ground) 0x43EC10 / item: what is in its sockets freed 0x48FA40 / quest reward: an item handed
   to a player 0x517530" hits=1 each, "sites: cube ingredients the ingredient free returns to 0x527D0F, Clear Sockets'
   to 0x5270FF; an item handed over: grid 0x34A410, spot 0x3865B0, move 0x471500, notice 0x535F60, charm 0x3E2B61C,
   refresh 0x470C90; at the feet: place 0x34A040, spot 0x43DE10, put 0x43DD00; socket contents kept: first 0x388C10,
   taken out 0x389820", "game: item handover bound (...), socket contents take-out bound", "cube: bound (an
   ingredient's free returns to 0x527D0F; Clear Sockets' socket contents kept too)", "hooks: item free hooked at
   0x43EC10", "hooks: socket contents free hooked at 0x48FA40", "hooks: cube products hooked at 0x5269C0", "settings:
   ... cube=0"). F7, Cheats, tick "Infinite cube ingredients"; open the cube and the inventory.
   Three runes of one kind (three El Runes): Transmute: the next rune in the cube, the three El Runes back in the
   inventory ("cube: a transmute kept 3 ingredients and 0 socket contents: 3 into the inventory, 0 at your feet, 0
   with no place anywhere (freed)", the first four). Three perfect gems and a magic small charm (the reroll): a new
   magic charm in the cube, the gems and the old charm back, and the old charm's bonus is on the character screen
   at once (life, a resistance). Clear Sockets (Hel, a Scroll of Town Portal, an item with gems, runes or jewels in
   its sockets): the item in the cube with its sockets empty, the Hel, the scroll and everything that was in the
   sockets in the inventory ("... kept 2 ingredients and N socket contents ..."). An upgrade or the socketing
   recipe (Tal, Thul, a Perfect Topaz and a normal body armor): the armor changed in the cube, the runes and the gem
   back, no second armor. Fill the inventory and transmute: what does not fit lies at your feet ("... N at your
   feet"), and picks up as always. With the Uber keys: the red portal opens and the three keys come back. Save and
   Exit, load: the items that came back are there. Switch it off: a recipe uses up its ingredients again. The
   console: `cabbycodes cube on|off`, and the status line "cube: ingredients kept=1 (Clear Sockets' socket
   contents kept too); N transmutes kept M ingredients and K socket contents: ...". A switch greyed with a reason
   beside it, or a warning that begins "cube:" or "sites: the quest reward giver ..." / "sites: the cube's product
   routine ..." / "sites: Clear Sockets' ...", is a routine read wrong: send the log.
31. A superior item's kind (log: "sites: item generation: superior kind step 0x58B9E0 / item generation: superior kind
   fits the item 0x3D40D0" hits=1 each, "sites: superior kinds picked among the first 4 for a base that is thrown
   (ItemTypes +0x10) or has no durability (Items +0x122), else all of them", "hooks: superior kind test hooked at
   0x3D40D0"). Perfect rolls and All superior on; the loot filter switch off, or a filter that shows plain items,
   makes them easy to find. Every white weapon that drops reads "+15% Enhanced Damage" and "+3 to Attack Rating" (a
   bow, a crossbow, a throwing weapon and a Phase Blade too), every body armor, helm, shield, belt and pair of boots
   or gloves "+15% Enhanced Defense" and "Increase Maximum Durability 15%"; none has durability or attack rating
   alone. A wand, a dagger or another weapon with a top damage of 6 or less reads "+1 to Maximum Damage" in place of
   the percentage: the game's own. The first four are logged ("loot: perfect roll: superior item (class N, level L)
   takes kind 7 of the kinds 2 4 7 that go on it; the game's roll came to kind 4 first"; every one with `trace =
   true`); the kinds are 0 attack rating, 1 enhanced damage, 2 enhanced defense, 3 attack rating with enhanced
   damage, 4 durability, 5 attack rating with durability, 6 enhanced damage with durability, 7 enhanced defense with
   durability. The console's status reads "superior items (perfect rolls): N given the best kind ...". Vendors' superior
   stock likewise, after the shop's next restock. With All superior off the game's own superior drops (a few in a
   hundred) are the same. Perfect rolls off: the kinds vary again (an armor with durability alone, a weapon without
   enhanced damage). Save and Exit, load: the items are as they were. Items made before this are not changed. A warning
   "loot: item class N was to be superior of kind K ... and came out with ..." or "loot: the superior step asks of a
   kind ..." is a routine read wrong: send the log (after three the kind is the game's pick for the session, and the
   panel says so).
   The cube's Clear Sockets with this build (item 30): the log must now have "hooks: socket contents free hooked at
   0x48FA40" and no "hooks: socket contents free not resolved"; Hel, a Scroll of Town Portal and an item with gems,
   runes or jewels in its sockets give everything back, what was in the sockets too.
32. God mode (log: "sites: stat: may not go down (the no-damage switches) 0x2F2830 hits=1", "sites: the protected-stat
   test reads the players' no-damage switch 0x2AA6200 and the monsters' 0x2AA6201", "hooks: protected-stat test
   hooked at 0x2F2830", "hooks: god mode refuses every lowering of a player's life (the protected-stat test), and
   and takes the rest of a hit on you away"; with the log on). Should the game stop at once after the load with the log's
   last line the hook's, it is the hook: send d2rloader.log. Switch god mode on and stand in a pack somewhere hard
   (Hell, a terror zone): the life orb does not move at all, no freeze, chill, poison or stun lands, and the first
   four "god mode: a player's life was not lowered (asked by the stat writer at D2RCore.dll+0x..., your unit)" come
   ("no unit in rdi" from the writers at D2RCore.dll+0x3D9D1C or +0x3D9F53 means D2RCore keeps the unit elsewhere
   now: send the log). The
   character still flinches from hits (the game's own). Save and Exit, load again: the same in the second game.
   What used to kill: damage x100 with a melee character against Oblivion Knights (Chaos Sanctuary, River of Flame)
   until one casts Iron Maiden on you, or a Herald's thorns in a terrorized area in Hell: the orb stays where it
   is. Get poisoned with god mode off, switch it on: the green orb stays until the poison ends, and life does not
   go down. Blood Mana (succubi) with god mode on: casting costs no life. God mode does not refill: switched on at
   half life, life stays at half and recovers as usual (potions, regeneration). Battle Orders ending while god mode
   is on (or an item that gives life taken off): life comes down to the new top at once, "god mode: your life was
   above its top (N > M) and goes down to it", and no further, a hit in the same moment included. The console `cabbycodes` status reads "god=1 (N hits on
   you, M life losses refused)". Switch it off: hits hurt again at once. `cabbycodes perf` with god mode on in a
   fight: "server tick" costs what it did before (god mode does nothing there now).
33. Chronicle drops (log: "sites: Chronicle: a player's 0x347440 / Chronicle: an entry by kind and id 0x3D87D0 /
   Chronicle: the share of a kind found 0x3D8870" hits=1 each, "sites: Chronicle a player's at its data +0x220; set
   items' map +0x0, uniques' +0x20 (buckets +0x8, their count +0x0; an entry's next +0x0, its id +0x8); the uniques'
   list 0x2A9BDB0 ...", "settings: ... chronicle=0"). F7, Loot, tick "Only drop what the Chronicle still misses"
   (the loot filter switch off), then kill something: "chronicle: 317 of its 403 uniques and 61 of its 135 set items
   found (...); 86 and 74 missing" (Xeen's stash of 2026-09-30; the counts must be the game's Chronicle panel's), and
   under the switch "still to find: 86 of 403 uniques, 74 of 135 set items". Every monster and
   chest drops only unique and set items the Chronicle misses, or nothing ("loot: monster N drop: 3 dropped, 3 not
   missing from the Chronicle, 0 extra asked; 3 picked among what the Chronicle misses of treasure class '...' (...
   items in ... forms wanted; ...), 0 came out otherwise ..."): identify one, open the Chronicle panel: it is there,
   and "chronicle: now 85 uniques and 74 set items missing" comes with the next drop. A set piece that dropped does
   not drop again in the same game (Save and Exit, load: it can again until it is identified); a unique drops once a
   game as always. Twenty extra drops from a boss: twenty different missing items at most, then "... omitted - the
   treasure class has nothing (left) that the Chronicle misses". No gold, no potions, no bases; quest items still
   drop. Then tick the loot filter switch too (the grail filter, or one that shows gold and runes): both kinds come,
   "... N neither shown by the loot filter nor missing from the Chronicle ... picked among what the filter shows or
   the Chronicle misses ...". Open the loot filter editor: nothing is filtered while it is open. The console:
   `cabbycodes chronicle on|off`, and the status line "chronicle: only what it misses=1; it misses 86 of 403 uniques
   and 74 of 135 set items; N of them dropped this game". A warning that begins "chronicle:" (a list or a map that
   does not read as expected, counts the game does not keep) or "loot: item class ... (row R) and came out with ...
   (row S)" is a layout or a step read wrong: send the log.
34. Identify on pickup (log: "sites: item notice (the clients told where an item went) 0x535F60 / item picked up from
   the ground 0x471950 / item identified 0x46E8C0" hits=1 each, "sites: identify on pickup the pick-up's notice of
   the item's new place returns to 0x471E6E; the identify routine tests and sets item flag 0x10 ...", "game: item
   handover bound (...), socket contents take-out bound, identify bound", "identify on pickup: bound (the pick-up's
   notice returns to 0x471E6E; identified is item flag 0x10)", "hooks: item notice hooked at 0x535F60", "settings:
   ... autoid=0"). F7, Cheats, tick "Identify items on pickup" (under Infinite identify). Kill until magic, rare, set
   and unique items drop (on the ground they show their bases' names, as in the game). Pick one up: in the inventory
   it is identified at once, its tooltip has its properties, no scroll used ("identify on pickup: item class N
   (quality Q) identified as it was picked up into the inventory", the first four). A unique or set item the
   Chronicle misses: it is in the Chronicle panel at once. A magic small charm: its bonus is on the character screen
   at once. Fill the inventory, then click a magic item on the ground: it comes on the cursor already identified
   ("... onto the cursor"); put it in the stash: its properties are there. A Sorceress's Telekinesis on an item:
   identified as it comes to the inventory. Rings, amulets, jewels, a rare from a chest: all the same; gold, potions,
   runes, white items come as always. Items already in the inventory or the stash stay unidentified (a scroll or Cain
   as always). Save and Exit, load: the items picked up are identified. Switch it off: items come unidentified again.
   The console: `cabbycodes autoid on|off`, and the status line "identify on pickup: autoid=1; N items identified as
   they were picked up (M of them onto the cursor)". The switch greyed with a reason beside it, a warning "identify on
   pickup: the game's identify left item class ... unidentified", or one that begins "sites: ... items are not
   identified as they are picked up", is a routine read wrong: send the log. Should the game stop at a pick-up, send
   d2rloader.log.
35. Passive mercenary and minions (log: "sites: AI: a monster's AI tick (dispatcher) 0x4A2A00 / AI: is the other unit an
   enemy 0x492790 / AI: the enemy test's two entry stubs 0x48E460 / AI: enemy check of the unit searches 0x597710 / AI:
   the kind of the target kept on a monster 0x544A00" hits=1 each, "sites: enemy test its two entry stubs jump to it,
   and the AI's own two enemy checks end in a jump to the first", "passive pets: bound (the AI dispatcher 0x4A2A00, the
   enemy test 0x492790, the kept target's getter 0x544A00)", "hooks: enemy test hooked at 0x492790", "hooks: kept target
   hooked at 0x544A00", "hooks: AI dispatcher hooked at 0x4A2A00", "settings: ... passivemerc=0 passiveminions=0").
   Should the game stop at once after the load with the log's last line one of these, send d2rloader.log. F7, Cheats:
   tick "Passive mercenary" (and "Invincible mercenary", or it dies doing nothing) and walk into a pack: the mercenary
   follows you and attacks nothing, not even what hits it or you ("passive mercenary: your mercenary (monster class N)
   had an enemy to take on and left it (K enemy tests answered no in its AI tick)", the first four). An Act 2
   mercenary's aura still works (Holy Freeze still slows the pack, Might still shows on you). Untick it: it fights again
   within a moment. "Passive minions" with a summoner: a Necromancer's skeletons, mages, golems and revives, a Druid's
   ravens, wolves, bear and vines, an Amazon's Valkyrie, an Assassin's shadows, a Warlock's demons all follow you and
   fight nothing; a Fire Golem's Holy Fire still burns what comes near; a Carrion Vine still eats corpses and heals you;
   a Sorceress's hydra and an Assassin's traps do nothing while it is on. With a Warlock, tick it while the demons are
   fighting: each one stops ("passive minions: your minion (monster class N) left aside the target kept on it (kind 2)"
   once a demon). Hostile monsters still attack you and your pets. Your own skills and curses work as always. The two
   switches are separate: the mercenary passive and the minions fighting, and the other way round. Save and Exit, load:
   both as they were. The console: `cabbycodes passivemerc on|off`, `passiveminions on|off`, and the status line
   "passive pets: passivemerc=1 passiveminions=0; N AI ticks of your pets kept passive, M of them with an enemy to take
   on (K enemy tests answered no, J kept targets left aside)". A switch greyed with a reason beside it, or a warning
   that begins "sites: ... your mercenary and minions cannot be made passive", is a routine read wrong: send the log.
36. Permanent revives (log: "sites: monster mod: killself's timer (a revive's end) 0x4A1D20 / pets: which of its owner's
   pet lists a unit is in 0x4FF3B0" hits=1 each, "sites: permanent revives the killself timer kills a player's pet with
   the pet removal 0x4FFD30, which asks the pet lookup first", "game: ... identify bound, pet lookup bound", "permanent
   revives: bound (the killself timer 0x4A1D20, its pet removal 0x4FFD30, the pet lookup 0x4FF3B0)", "hooks: killself
   timer hooked at 0x4A1D20", "settings: ... revives=0", in a game "permanent revives: bank 3: Revive (skill 95) keeps
   its revives as pet type 6"). Should the game stop when a revive's time is up, send d2rloader.log. With a
   Necromancer that has Revive: F7, Cheats, tick "Permanent revives" (under Passive minions; "Invincible minions"
   keeps them alive through a long test). Revive a few monsters and play on past 3 minutes: they stay ("permanent
   revives: your revive (monster class N) came to the end of its time and stays until it dies", the first four). Untick
   it and revive one more: that one falls after its 3 minutes, the ones kept before stay. Take a waypoint into another
   act: they come along and stay. Switch Invincible minions off and let one be killed: it dies as always. At the limit
   (one revive a skill level) a new revive makes the oldest fall, as in the game. An Amazon's Decoy and a monster
   Tomb Reaver or Faith reanimates end as always. Save and Exit, load: the switch is as it was (pets are never kept
   between games). The console: `cabbycodes revives on|off`, and the status line "permanent revives: revives=1; N
   revives of yours stayed past their time". A switch greyed with a reason beside it, or a warning that begins
   "sites: ... revives time out as the game has them" or "permanent revives:", is a routine or a table read wrong:
   send the log.
37. Landmarks on the map (log: "sites: automap: draw one unit (marker and name) 0xD76E0 hits=1", "sites: map: landmarks,
   named enemies waypoint tiles: the per-tile add 0xD5160 onto a layer's +0x8, the tile lookup's records 0x2A9A8C8 (...);
   object icons: the list insert 0xD1460 onto a layer's +0x58, an Objects row's cell +0x164, the stash (class 267) in
   acts 3..4, the sewer stairs (class 366) in mode 2, the Arcane waypoint (class 402) in level 74; a room's preset
   units; the unit draw's transform 0xD4910, marker 0xD6DB0, name 0xD6B20 (an NPC's color 4), unit name 0x9A1B0", "game:
   on the map: a tile's add bound, a cell's insert bound, the unit draw's transform, marker and name bound", "map:
   landmarks ready (waypoint tiles yes, object icons yes), named enemies ready", in a game "map: bank 3: 148 object
   classes with an icon (the waypoints' 307), 66 super uniques (the first "Bishibosh"), 23 bosses (Andariel, ...)",
   "settings: ... landmarks=0"). Reveal the map off, Landmarks on the map on (F7, Display), a new game in Act 1: as you
   enter the Blood Moor and then the Cold Plains, the automap shows the area's waypoint and its shrines and wells at
   once, far from you, on an otherwise black map ("landmarks: level 3 (layer 0, act 1): N object icons on the map (N
   new), M waypoint tiles put on"). Walk to one: when it comes on screen nothing changes (no second icon beside it).
   In the Stony Field the Cairn Stones show, in the Dark Wood the Inifuss tree, in the Forgotten Tower its tome, in the
   Arcane Sanctuary the portals and the waypoint, Act 5's waypoints too (objects there: "... (no waypoint of this
   kind of area is a floor tile)"). A dungeon (the Den of Evil, a cave, the Catacombs): its waypoint at once; its
   random shrines and wells still appear as you come near (the game makes them then), and so do most shrines and wells
   of Act 3's jungle, Kurast and Travincal and of Acts 4 and 5 (2026-10-02: "I just saw a shrine pop up after getting
   close that wasn't already shown", then "the one that didn't show was indoor in durance of hate, and it was a health
   shrine": the Durance's levels 100 .. 102 name group 61, five shrines at 20 each (MephistoShrine1 .. 3 magic, 4 mana,
   5 (206) health: Objects Parm0 3 / 2 / 1) at ObjPrb 25 a room, and of its 58 room files (lvlprest's "Act 3 -
   Mephisto ...", read from CASC) only MephNSE2.ds1 places a shrine itself, a MephistoShrine5: that one is the
   layout's and shows at once). On Durance Level 3 the Hellgate's icon (342, cell 339) is on the map at once. With Reveal the map on as well:
   the whole area plus the shrines and the rest at once. Save and Exit, load: the icons are still there (saved with
   the map). The console: `cabbycodes landmarks on|off` and the status line "map: landmarks=1, this game N areas, N
   object icons and M waypoint tiles put on; ...". A switch greyed with a reason, or a warning that begins "sites: the
   automap's reveal of a room does not put ...", "sites: CreateActiveRoom does not keep a room's preset units ..." or
   "map:", is a routine or a table read wrong: send the log. Should the game stop as you enter an area with the switch
   on, send d2rloader.log. With this build (the room's own preset list, log "sites: map: area names at exits a built
   room's own preset units at +0x98; ..."): in the Flayer Dungeon Level 3 Khalim's Brain chest (the sparkle chest
   icon) is on the map as you enter; in Act 3's Sewers Level 1 the lever's icon and the stairs down (closed until the
   lever is pulled) at once; Khalim's Heart chest in Level 2, Khalim's Eye chest in the Spider Cavern, the Horadric
   Cube's and Scroll's chests, the Staff of Kings' chest likewise. The "landmarks: level N ..." lines count more icons
   in dungeons than before.
38. Named enemies on the map (log: as item 37, "hooks: automap's draw of one unit hooked at 0xD76E0", "settings: ...
   enemies=0"). F7, Display, tick "Named enemies on the map", open the automap (Tab) or have the minimap on. In the Cold
   Plains a red cross with "Bishibosh" marks his camp as soon as you enter ("named enemies: level 3 (layer 0): 1 spawn
   spot - Bishibosh"), far away. Walk up: as he comes near the cross follows him ("named enemies: Bishibosh (monster
   class 58, kind 0xA) drawn on the map"), one cross, not two. Walk away without killing him: his cross stays where he
   was last seen, however far you go (zoom the map out, or go to the next area of the same map). Kill him: his cross
   goes and does not come back in this game ("named enemies: Bishibosh (level 3) seen dead - its mark is let go for
   this game"); a new game marks it again. A random unique (a gold-named pack leader) gets a red cross with its whole
   name once you have come near enough for the game to make its pack, and keeps it after you leave, until it dies;
   its minions get nothing. A champion pack: a red cross on each, the monster's name once, in blue. Andariel's, Duriel's, the Summoner's, Izual's, the Countess's, Pindleskin's,
   Nihlathak's and the Ancients' spawn spots are marked when you enter their areas; in a terror zone a Herald gets a
   cross. Town folk keep the game's own white crosses, your mercenary and summons theirs (no red ones). The names are
   drawn even with the game's own automap names option off. Switch it off: the crosses go at once. The console:
   `cabbycodes enemies on|off` and the status line "map: ...; enemies=1, N spawn spots read, M named enemies drawn (J
   of them on the map now), K marks let go as their monster died". `cabbycodes perf` with the automap open in a fight: "automap unit (each)"
   well under a microsecond. Should the game stop when the automap opens, send d2rloader.log.
39. Area names at exits (log: "sites: collision: a subtile's flags in its room's map 0x366250 / DRLG: a warp tile's preset
   unit 0x3F4670" hits=1 each, "sites: map: area names at exits a built room's own preset units at +0x98; its near
   rooms at +0x10, its warp links at +0x78 (a warp's id at +0x2C, its tile's preset unit of type 5), an ActiveRoom's
   collision map at +0x38", "map: landmarks ready (...), named enemies ready, area names at exits ready", in a game
   "map: bank 3: ... 138 levels (137 named: level 2 "Blood Moor")", "settings: ... exits=0"). F7, Display, tick "Area
   names at exits", open the automap (Tab) or have the minimap on. Rogue Encampment: "Blood Moor" at the camp's gate.
   Blood Moor: "Rogue Encampment" by the gate, "Cold Plains" at the opening into it, "Den of Evil" on the cave's
   entrance ("exits: level 2 (layer 0): 3 exits - Den of Evil (a warp), Rogue Encampment (an opening of N subtiles),
   Cold Plains (an opening of M subtiles)"). Walk into the Cold Plains: Blood Moor's names go, the Cold Plains' come:
   "Blood Moor" on this side of the same opening, "Stony Field", "Burial Grounds", "Cave Level 1". Inside the Den of
   Evil: "Blood Moor" at the way out. A dungeon (the Catacombs, the Act 2 sewers, the Durance of Hate): the stairs
   down and up named. Act 2: Lut Gholein's trap door "Sewers Level 1" and the palace's "Harem Level 1"; the Canyon of
   the Magi's seven "Tal Rasha's Tomb". Act 3's jungle borders, Act 4's, Act 5's outdoor areas the same way as Act 1's.
   Without Reveal the map the names stand on the black map where the exits are. Act 3's Sewers Level 1 names no way
   down (the stairs the lever opens: Landmarks on the map shows them). The console: `cabbycodes exits on|off`, and in
   the status line "exits=1, N areas, W warps and B openings named (E on the map now)". To send: a name in the wrong
   place, an opening named where nothing can be crossed, or one missing; a warning "exits: level N: R built rooms have
   no collision map where their place says" or "sites: a built room is not linked to the areas next to it ...". Should
   the game stop as you enter an area with the switch on, send d2rloader.log.
40. Rolled shrines on the map (log: "sites: server: fill the rooms built since (each frame) 0x408670 hits=1", "sites: rooms
   filled ahead the fill pass fills each room of an act's list whose state +0x54 lacks 0x1; a room's units from +0xA8,
   the next +0x160", "landmarks: the shrines the game rolls are put on the map ahead (...)", in a game "map: bank 3: ...
   (137 named: level 2 "Blood Moor"; 95 roll objects with an icon as their rooms are filled)"). Landmarks on the map
   on (Reveal the map off makes it easiest to see). Walk into the Durance of Hate Level 1: within a second or two its
   shrines are on the map, far from you ("landmarks: level 100: N rooms built ahead for the game to fill (M were built
   already, 0 could not be), filled in T ms; K objects in the area, J with an icon handed to the map", then "landmarks:
   level 100: J objects the game rolled as it filled the area's rooms on the map (J new)"). Walk to one: it is there,
   and taking it works as always (a health shrine heals). The same in Level 2, Act 3's jungle and Kurast, Travincal,
   Act 4's areas, Act 5's (its outdoor waypoints too), the dungeons of every act. Act 1's and Act 2's outdoor areas and
   the towns: no such line (nothing they roll has an icon). The monsters are where the game put them; the fight goes as
   always. No hitch on entering an area (`cabbycodes perf` while entering: "rooms built ahead" a few ms at the most in
   a frame). The console's status line "map: landmarks=1, ..., N areas filled ahead (R rooms built for the game to
   fill) and K of the objects it rolled put on". A warning "sites: the game's room fill pass is not made the way
   expected" means the rolled shrines appear as the game shows them: send the log. Should the game stop as you enter
   an area, send d2rloader.log.
41. Ignore item requirements (log: "sites: item: can a unit use it (its requirements) 0x36BC50 / client: can the mercenary
   wear an item 0x159D20 / pets: a player's pet of a type (its mercenary) 0x4FF1A0" hits=1 each, "sites: ignore item
   requirements past level, strength and dexterity: item flag 0x10, a tome (type 18) its stat 70, the restricted
   sockets 0x376110, ...; the items worked out again 0x470C90", "game: item requirements: the restricted-socket test
   bound, a player's mercenary bound, the items worked out again bound", "ignore item requirements: bound (... your
   gear worked out again at a switch: yes)", "ignore item requirements: your mercenary too (...)", "hooks: item
   requirement test hooked at 0x36BC50", "settings: ... requirements=0/0"). Should the game stop at once after the
   load with the log's last line the hook's, send d2rloader.log. A new character (level 1, no points spent) or a low
   one carrying a high item of its class's (a rare weapon with Required Level 40 and Strength 100, an elite helm, a
   runeword armor): its tooltip's requirement lines are red, its inventory cell red. F7, Cheats, tick "Ignore item
   requirements": at once the lines are white and the cell is not red. Put it on: it goes on (no "I can't use this
   yet"), and the character screen shows what it gives (damage, defense, its bonuses; an amulet's +skills on the skill
   tree) ("ignore item requirements: item class N (item level L) is usable by your character, though its level,
   strength requirements are not met", the first four). A charm whose level you do not have (a grand charm,
   Annihilus, a Torch): its bonuses count. Another class's item (an orb for a Barbarian, a claw for a Sorceress) and an
   unidentified item: refused, as in the game. Untick it with the item on: it stays on, its cell turns red and its
   bonuses leave the character screen at once ("ignore item requirements: off for your character - what it wears and
   carries worked out again"); tick it: back at once ("... on ..."). Save and Exit, load with it on: what is worn
   counts from the start; load with it off: worn, red and giving nothing (the game's own). The mercenary: tick
   "Ignore item requirements (mercenary)" with a low-level one and give it a weapon or armor it cannot use (level,
   strength, dexterity): it takes it, and the mercenary's screen counts it. An Act 1 Rogue takes an Amazon bow, an Act
   5 Barbarian a Barbarian helm, as in the game; an Act 2 mercenary still refuses a class item. Untick: its gear stays,
   what it cannot use gives nothing ("ignore item requirements: on/off for your mercenary (monster class N) - what it
   wears worked out again"; without one: "... none is with you now ..."). The console: `cabbycodes requirements
   on|off`, `mercrequirements on|off`, and the status line "item requirements: requirements=1 mercrequirements=0; a
   no turned into a yes N times for you and M for your mercenary, the gear worked out again K times". A switch greyed
   with a reason, or a warning that begins "sites: the requirement test is not made the way expected" or "sites:
   ... your mercenary's item requirements cannot be ignored", is a routine read wrong: send the log.

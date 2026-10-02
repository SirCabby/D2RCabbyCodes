#pragma once

#include <D2RLPlugin/api.h>

struct ImFont;

// Monster health bars: a short life bar above every enemy on screen, and an act
// boss's name and life at the top centre of the screen while it is near.
//
// Three threads meet here, each on its own side of the game:
//   game thread     the tick copies the MonStats flags and the prime evils'
//                   names once per game (the loader's tables are game-thread-only)
//   UI thread       every frame, from the loader's UI callback: walks the
//                   client's monsters, asks the game where it draws above each
//                   enemy (the routine its own overhead text uses) and publishes
//                   a snapshot. The one piece that has to work every frame (the
//                   bars follow what moves); with nothing to follow - the
//                   switches off, no game, a town - it reads flags and returns
//   present thread  draws the latest snapshot with ImGui
namespace d2rcc::healthbars {

void set_services(const D2RL::PluginContext* ctx, const D2RL::DataTableService* tables,
                  const D2RL::LocalizationService* strings);
// After game::bind(): what the game offers, and whether UI-thread work can run.
// Sets the two switches' why_not.
void bind(bool ui_thread);

void collect_tables();  // game thread (the tick), once per game
void collect();         // UI thread, every frame
void game_left();

// Present thread. take_snapshot() picks up the latest snapshot for this frame
// and says whether it has anything to draw; draw() then draws it (inside an
// ImGui frame). The preview shows the boss bar where the panel's slider puts it.
bool take_snapshot();
void draw(ImFont* font);
void draw_boss_preview(ImFont* font, int position_pct);
bool legacy_graphics();  // the last collection found legacy graphics, where there are no bars

}  // namespace d2rcc::healthbars

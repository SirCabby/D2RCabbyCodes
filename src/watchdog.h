#pragma once

// The plugin's own thread (plugin.cpp): it writes the settings and the presets and drives leaving the game.
// It sleeps until it is woken or something it waits for by the clock comes due; whoever has work for it
// wakes it (any thread).
namespace d2rcc::watchdog {

void wake();

}  // namespace d2rcc::watchdog

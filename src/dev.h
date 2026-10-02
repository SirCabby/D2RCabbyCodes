#pragma once

// Developer aids, compiled in DEV builds only (make DEV=1, the default while
// developing): a command file the plugin polls, so a test can flip switches and
// drive the game's window without a keyboard - handy from a shell that cannot
// reach the game's input (a Wayland desktop, a remote session).
//
// Write lines to <d2rloader/logs>/cabbycodes.cmd; the file is consumed:
//   god on|off  mana on|off  exit on|off  exp <x>  speed <pct>  panel on|off
//   key <vk-name|0xNN>       a key press (RETURN, ESCAPE, F7, SPACE, 0x41 ...)
//   click <x> <y> [right]    a click at client coordinates of the game window
//   move <x> <y>             move the pointer there
//   text <string>            type the string (ASCII)
//   status                   log the cheat status
//   save                     write the settings file
namespace d2rcc::dev {
void start();
void stop();
}  // namespace d2rcc::dev

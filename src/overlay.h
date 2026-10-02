#pragma once

#include <windows.h>

#include "config.h"

// The Dear ImGui panel and the machinery that gets it on screen. D2R renders
// through Direct3D 12 and presents through a DXGI swap chain (dxgi.dll's on
// Windows, DXVK's under Proton, one class either way). overlay_d3d12.cpp hooks
// the swap chain class's Present and ResizeBuffers functions (MinHook, the way
// the other D2RLoader overlay plugins do, so the hooks chain with theirs),
// captures the game's command queue as its swap chain is made, and draws the
// panel into the back buffer about to be shown, on the thread that presents.
namespace d2rcc::overlay {

bool install();    // worker thread, once: the D3D12 hooks (before the game makes its swap chain)
void uninstall();  // plugin unload

bool ensure_context(HWND hwnd);  // ImGui context + Win32 backend + window subclass, once
bool wants_draw();               // the visibility rule for this frame (lock held)
bool hud_wanted();               // the health bars have something to show this frame (lock held)
void draw_hud();                 // between ImGui::NewFrame() and ImGui::Render(), before the panel (lock held)
void draw_panel();               // between ImGui::NewFrame() and ImGui::Render() (lock held)
void shutdown_imgui();

// One ImGui context; the window's thread feeds it messages and the present
// thread draws from it, so every touch of the context is taken under this lock.
void lock_imgui();
void unlock_imgui();
struct ImGuiLock {
  ImGuiLock() { lock_imgui(); }
  ~ImGuiLock() { unlock_imgui(); }
  ImGuiLock(const ImGuiLock&) = delete;
  ImGuiLock& operator=(const ImGuiLock&) = delete;
};

bool visible();  // the panel was drawn in the last frame
void set_forced(bool on);  // show the panel whatever the pause menu does (the toggle key, the console)
void set_pause_open(bool on);  // plugin.cpp: the game's pause menu opened or closed (from its UI messages)
bool pause_open();             // ... and whether it is up now (any thread)

// Provided by plugin.cpp: the live settings, where the settings file lives
// (UTF-8, trailing separator), and what to do when the panel changed a setting.
const config::Settings& settings();
const char* config_dir_utf8();
void on_setting_changed();

namespace d3d {  // overlay_d3d12.cpp
bool install();
void uninstall();
bool presenting();  // the game's swap chain has presented through the hook
}  // namespace d3d

}  // namespace d2rcc::overlay

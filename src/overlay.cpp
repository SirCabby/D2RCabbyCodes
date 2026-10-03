#include "overlay.h"

#include <commctrl.h>

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

#include "backends/imgui_impl_win32.h"
#include "character.h"
#include "cheats.h"
#include "chronicle.h"
#include "config.h"
#include "game.h"
#include "healthbars.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "itemseed.h"
#include "log.h"
#include "loot.h"
#include "remember.h"
#include "sites.h"
#include "terror.h"
#include "hometown.h"
#include "version.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace d2rcc::overlay {
namespace {

HWND g_hwnd = nullptr;
bool g_subclassed = false;
constexpr UINT_PTR kSubclassId = 0xCABB1;
bool g_context_ready = false;
volatile LONG g_visible = 0;
volatile LONG g_forced = 0;      // the toggle key (or the console) put the panel up outside the pause menu
volatile LONG g_user_hidden = 0; // the toggle key hid it while the pause menu is up
volatile LONG g_pause_open = 0;  // the game's pause menu (Esc) is up
bool g_was_paused = false;
char g_ini_path[MAX_PATH] = {};
float g_scale = 1.0f;  // the style scale (window height / 900), for fixed-size controls
ImFont* g_hud_font = nullptr;  // the health bars' text: a bold system font, or ImGui's own

// The panel's sections and whether each is open, kept in the ImGui ini beside
// the window's size and position through a settings handler of our own.
enum Section : int { kSecCheats = 0, kSecDisplay, kSecLoot, kSecCharacter, kSecQol, kSecDiagnostics, kSecCount };
const char* const kSectionKeys[kSecCount] = {"Cheats", "Display", "Loot", "Character", "QualityOfLife",
                                             "Diagnostics"};
bool g_section_open[kSecCount] = {true, true, true, true, true, true};

// The Character section's own state: the name being typed and the preset chosen in the list.
char g_preset_name[presets::kMaxName + 1] = {};
std::string g_preset_selected;
int g_preset_selected_class = -1;

void* ini_read_open(ImGuiContext*, ImGuiSettingsHandler*, const char* name) {
  return std::strcmp(name, "Sections") == 0 ? reinterpret_cast<void*>(1) : nullptr;
}
void ini_read_line(ImGuiContext*, ImGuiSettingsHandler*, void*, const char* line) {
  char key[32] = {};
  int value = 0;
  if (std::sscanf(line, "%31[^=]=%d", key, &value) != 2) return;
  for (int i = 0; i < kSecCount; ++i)
    if (std::strcmp(key, kSectionKeys[i]) == 0) g_section_open[i] = value != 0;
}
void ini_write_all(ImGuiContext*, ImGuiSettingsHandler* handler, ImGuiTextBuffer* out) {
  out->appendf("[%s][Sections]\n", handler->TypeName);
  for (int i = 0; i < kSecCount; ++i) out->appendf("%s=%d\n", kSectionKeys[i], g_section_open[i] ? 1 : 0);
  out->append("\n");
}
void register_ini_handler() {
  ImGuiSettingsHandler handler;
  handler.TypeName = "CabbyCodes";
  handler.TypeHash = ImHashStr("CabbyCodes");
  handler.ReadOpenFn = ini_read_open;
  handler.ReadLineFn = ini_read_line;
  handler.WriteAllFn = ini_write_all;
  ImGui::AddSettingsHandler(&handler);
}

// The health bars' names and numbers want a font that reads at any size; ImGui
// rasterises a TrueType font on demand at the size asked for. Windows and Wine
// prefixes both have these in the Windows fonts folder. The panel keeps ImGui's
// own font, which is added first so it stays the default.
ImFont* load_hud_font(ImGuiIO& io) {
  wchar_t dir[MAX_PATH] = {};
  const UINT n = GetWindowsDirectoryW(dir, MAX_PATH);
  if (!n || n >= MAX_PATH) return nullptr;
  static const wchar_t* const kFiles[] = {L"arialbd.ttf", L"tahomabd.ttf", L"segoeuib.ttf", L"arial.ttf", L"tahoma.ttf"};
  for (const wchar_t* file : kFiles) {
    wchar_t path[MAX_PATH];
    std::swprintf(path, MAX_PATH, L"%ls\\Fonts\\%ls", dir, file);
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) continue;
    char utf8[MAX_PATH * 3];
    if (!WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8, sizeof(utf8), nullptr, nullptr)) continue;
    if (ImFont* font = io.Fonts->AddFontFromFileTTF(utf8, 18.0f)) {
      logf("overlay: health bar font %s", utf8);
      return font;
    }
  }
  log_warn("overlay: no system font for the health bars - ImGui's own font is used");
  return nullptr;
}

// A collapsing header whose state is remembered across sessions.
bool section(Section idx, const char* label) {
  ImGui::SetNextItemOpen(g_section_open[idx], ImGuiCond_Once);
  const bool open = ImGui::CollapsingHeader(label);
  if (open != g_section_open[idx]) {
    g_section_open[idx] = open;
    ImGui::MarkIniSettingsDirty();
  }
  return open;
}

CRITICAL_SECTION g_imgui_cs;
bool g_imgui_cs_ready = false;

void ensure_lock() {
  if (!g_imgui_cs_ready) {
    InitializeCriticalSection(&g_imgui_cs);
    g_imgui_cs_ready = true;
  }
}

int toggle_key() { return settings().toggle_key; }

// The pause menu: not known yet (milestone 4 finds it); until then the panel
// is the toggle key's.
bool pause_menu_open() { return g_pause_open != 0; }

void toggle(const char* how) {
  if (pause_menu_open() && settings().show_on_pause) {
    InterlockedExchange(&g_user_hidden, g_user_hidden ? 0 : 1);
    logf("panel %s by the toggle key%s", g_user_hidden ? "hidden" : "shown", how);
  } else {
    InterlockedExchange(&g_forced, g_forced ? 0 : 1);
    logf("panel %s by the toggle key%s", g_forced ? "opened" : "closed", how);
  }
}

LRESULT CALLBACK subclass_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
  // F-keys above F9 arrive as WM_SYSKEYDOWN; accept both, ignore auto-repeat.
  if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && !(lp & (1 << 30)) && static_cast<int>(wp) == toggle_key()) {
    toggle("");
    return 0;
  }
  if (g_context_ready) {
    bool swallow = false;
    {
      ImGuiLock guard;
      // ImGui sees every message, visible or not: a button release that arrives
      // after the panel is hidden would otherwise never be delivered.
      if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) return 1;
      if (g_visible) {
        const ImGuiIO& io = ImGui::GetIO();
        if (io.WantCaptureMouse && msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) swallow = true;
        if (io.WantTextInput && msg >= WM_KEYFIRST && msg <= WM_KEYLAST && wp != VK_ESCAPE) swallow = true;
        // Raw Input: a packet for the device the panel is using goes no further.
        if (msg == WM_INPUT) {
          RAWINPUTHEADER h{};
          UINT size = sizeof(h);
          if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lp), RID_HEADER, &h, &size, sizeof(RAWINPUTHEADER)) == sizeof(h)) {
            if ((h.dwType == RIM_TYPEMOUSE && io.WantCaptureMouse) || (h.dwType == RIM_TYPEKEYBOARD && io.WantTextInput))
              swallow = true;
          }
        }
      }
    }
    if (g_visible && msg == WM_SETCURSOR && LOWORD(lp) == HTCLIENT) {
      ImGuiLock guard;
      if (ImGui::GetIO().WantCaptureMouse) {
        SetCursor(LoadCursorA(nullptr, reinterpret_cast<LPCSTR>(IDC_ARROW)));
        return TRUE;
      }
    }
    if (swallow) return msg == WM_INPUT ? DefWindowProcW(hwnd, msg, wp, lp) : 0;
  }
  return DefSubclassProc(hwnd, msg, wp, lp);
}

// --- panel helpers -------------------------------------------------------------------------
void help_marker(const char* text) {
  ImGui::SameLine();
  ImGui::TextDisabled("(?)");
  if (ImGui::IsItemHovered() && ImGui::BeginTooltip()) {
    ImGui::PushTextWrapPos(420.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
  }
}

void changed(const char* what, const char* state) {
  logf("%s %s (panel)", what, state);
  on_setting_changed();
}

void cheat_row(cheats::Kind k, const char* label, const char* help) {
  const char* why = cheats::why_not(k);
  const bool available = why == nullptr;
  bool v = cheats::enabled(k);
  if (!available) ImGui::BeginDisabled();
  if (ImGui::Checkbox(label, &v)) {
    cheats::set_enabled(k, v);
    changed(cheats::name(k), v ? "ON" : "OFF");
  }
  if (!available) ImGui::EndDisabled();
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && ImGui::BeginTooltip()) {
    ImGui::PushTextWrapPos(400.0f);
    ImGui::TextUnformatted(help);
    if (!available) {
      ImGui::Separator();
      ImGui::TextUnformatted(why);
    }
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
  }
  if (!available) {
    ImGui::SameLine();
    ImGui::TextDisabled("(%s)", why);
  }
}

// A button that asks first: a modal with the question and a confirming button.
bool confirm(const char* id, const char* question, const char* yes) {
  bool confirmed = false;
  if (ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
    ImGui::TextUnformatted(question);
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    if (ImGui::Button(yes)) {
      confirmed = true;
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
  return confirmed;
}

// Under a "keep between games" row: what is remembered, or why a Show Items key is not kept.
void keep_note(remember::Which w, cheats::Kind k, const char* option) {
  if (!cheats::enabled(k) || cheats::why_not(k)) return;
  const int mode = remember::display_mode(w);
  if (w != remember::kAutomap && mode != game::kDisplayUnknown && mode != game::kDisplayToggle) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.82f, 0.35f, 1.0f));
    ImGui::TextWrapped("   The game's %s option is %s: set it to Toggle in the game's options for this to work.",
                       option, mode == game::kDisplayHold ? "Hold" : "Timed");
    ImGui::PopStyleColor();
    return;
  }
  const bool on = remember::remembered(w);
  ImGui::TextDisabled("   remembered: %s", w == remember::kAutomap ? (on ? "open" : "closed") : (on ? "on" : "off"));
}

// The character's points: the respec, and presets of what they were spent on.
void draw_character(const cheats::Status& st) {
  const bool in_game = st.player_found && st.char_class >= 0;
  const character::Pending pending = character::pending();
  const bool resetting = pending.respec || !pending.load.empty();
  if (in_game)
    ImGui::Text("%s, level %d: %d attribute and %d skill points to spend", presets::class_name(st.char_class),
                st.level, st.stat_points, st.skill_points);
  else
    ImGui::TextDisabled("Enter a game to reset the character or to save and load presets.");

  const char* respec_why = cheats::why_not(cheats::kRespec);
  ImGui::BeginDisabled(!in_game || respec_why || resetting);
  if (ImGui::Button("Reset skills and attributes")) ImGui::OpenPopup("Reset skills and attributes?");
  ImGui::EndDisabled();
  if (respec_why) {
    ImGui::SameLine();
    ImGui::TextDisabled("(%s)", respec_why);
  } else {
    help_marker("Every skill point and attribute point the character spent comes back to spend again, as with a "
                "Token of Absolution (the game's own reset). Quest rewards already received stay received.");
  }
  if (confirm("Reset skills and attributes?",
              "Every skill point and attribute point spent comes back to spend again, as with a Token of "
              "Absolution.",
              "Reset")) {
    character::request_respec();
    logf("respec requested (panel)");
  }
  // A reset changes the character on the game's own tick, which a paused game does not run.
  if (resetting) {
    char waiting[160];
    if (pending.respec)
      std::snprintf(waiting, sizeof(waiting), "The reset");
    else
      std::snprintf(waiting, sizeof(waiting), "Loading \"%s\"", pending.load.c_str());
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.82f, 0.35f, 1.0f));
    ImGui::TextWrapped("%s %s", waiting,
                       pause_menu_open() ? "happens as soon as the game runs again: close this menu (the Esc menu "
                                           "pauses the game)."
                                         : "happens at the game's next frame.");
    ImGui::PopStyleColor();
    if (ImGui::SmallButton("Cancel")) character::cancel_reset();
  }

  ImGui::Spacing();
  ImGui::SeparatorText("Presets");
  const char* presets_why = cheats::why_not(cheats::kPresets);
  const std::vector<presets::Preset> all = character::list();
  const presets::Preset* chosen = nullptr;
  for (const presets::Preset& p : all)
    if (p.char_class == g_preset_selected_class && p.name == g_preset_selected) chosen = &p;
  char label[128];
  auto describe = [&](const presets::Preset& p) {
    if (in_game)
      std::snprintf(label, sizeof(label), "%s  (level %d, %d skill points)", p.name.c_str(), p.level,
                    presets::skill_points(p));
    else
      std::snprintf(label, sizeof(label), "%s  (%s, level %d)", p.name.c_str(), presets::class_name(p.char_class),
                    p.level);
    return label;
  };
  bool any = false;
  for (const presets::Preset& p : all) any = any || !in_game || p.char_class == st.char_class;
  if (ImGui::BeginCombo("Preset", chosen ? describe(*chosen) : any ? "choose a preset" : "none saved yet")) {
    for (const presets::Preset& p : all) {
      if (in_game && p.char_class != st.char_class) continue;  // another class's points do not fit
      // The list is a fresh copy each frame: the IDs come from what a preset is, not where it sits.
      ImGui::PushID(p.char_class);
      ImGui::PushID(p.name.c_str());
      if (ImGui::Selectable(describe(p), &p == chosen)) {
        g_preset_selected = p.name;
        g_preset_selected_class = p.char_class;
      }
      if (ImGui::IsItemHovered() && ImGui::BeginTooltip()) {
        ImGui::Text("Strength %d, dexterity %d, vitality %d, energy %d", p.strength, p.dexterity, p.vitality,
                    p.energy);
        ImGui::Text("%d skill points in %zu skills", presets::skill_points(p), p.skills.size());
        ImGui::EndTooltip();
      }
      ImGui::PopID();
      ImGui::PopID();
    }
    ImGui::EndCombo();
  }
  ImGui::BeginDisabled(!chosen || !in_game || presets_why || resetting ||
                       (chosen && chosen->char_class != st.char_class));
  if (ImGui::Button("Load")) ImGui::OpenPopup("Load preset?");
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(!chosen);
  if (ImGui::Button("Delete")) ImGui::OpenPopup("Delete preset?");
  ImGui::EndDisabled();
  if (presets_why) {
    ImGui::SameLine();
    ImGui::TextDisabled("(%s)", presets_why);
  }
  char question[256];
  std::snprintf(question, sizeof(question),
                "Reset every skill and attribute point, then spend them the way \"%s\" does? Points that cannot "
                "be spent yet (a lower level, fewer quest rewards) are left to spend.",
                g_preset_selected.c_str());
  if (confirm("Load preset?", question, "Load")) {
    if (!character::request_load(g_preset_selected.c_str(), g_preset_selected_class))
      logf("preset '%s' not found (panel)", g_preset_selected.c_str());
  }
  std::snprintf(question, sizeof(question), "Delete the preset \"%s\"?", g_preset_selected.c_str());
  if (confirm("Delete preset?", question, "Delete") &&
      character::remove(g_preset_selected.c_str(), g_preset_selected_class)) {
    logf("preset '%s' deleted (panel)", g_preset_selected.c_str());
    g_preset_selected.clear();
  }

  ImGui::InputTextWithHint("##preset name", "name for a new preset", g_preset_name, sizeof(g_preset_name));
  ImGui::SameLine();
  const std::string name = presets::clean_name(g_preset_name);
  ImGui::BeginDisabled(!in_game || pending.save || name.empty() || !game::has_base_stat());
  if (ImGui::Button("Save")) {
    character::request_save(name.c_str());
    g_preset_selected = name;
    g_preset_selected_class = st.char_class;
    logf("preset '%s' save requested (panel)", name.c_str());
  }
  ImGui::EndDisabled();
  help_marker("Keeps what this character spent its points on - attributes and skills, not what items add - "
              "under this name; a preset of the same name is replaced. Loading one resets the character first and "
              "spends the points again, prerequisites first.");
  const std::string result = character::last_result();
  if (!result.empty()) ImGui::TextDisabled("%s", result.c_str());
}

}  // namespace

// --- context -------------------------------------------------------------------------------
bool ensure_context(HWND hwnd) {
  if (g_context_ready) return true;
  if (!hwnd) return false;
  ensure_lock();
  g_hwnd = hwnd;
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  // The layout file lives beside the settings, in the loader's config folder.
  std::snprintf(g_ini_path, sizeof(g_ini_path), "%s", config_dir_utf8());
  std::strncat(g_ini_path, "cabbycodes.imgui.ini", sizeof(g_ini_path) - std::strlen(g_ini_path) - 1);
  io.IniFilename = g_ini_path[0] ? g_ini_path : nullptr;
  io.IniSavingRate = 1.0f;  // a moved, resized or collapsed panel is on disk a second later
  register_ini_handler();
  io.ConfigWindowsMoveFromTitleBarOnly = true;
  io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
  io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
  io.Fonts->AddFontDefault();
  g_hud_font = load_hud_font(io);
  ImGui::StyleColorsDark();
  ImGuiStyle& style = ImGui::GetStyle();
  style.WindowRounding = 4.0f;
  RECT rc{};
  GetClientRect(hwnd, &rc);
  const float scale = std::clamp(static_cast<float>(rc.bottom - rc.top) / 900.0f, 1.0f, 2.5f);
  style.ScaleAllSizes(scale);
  style.FontScaleMain = scale;
  g_scale = scale;
  if (!ImGui_ImplWin32_Init(hwnd)) {
    log_error("overlay: ImGui Win32 backend init failed");
    ImGui::DestroyContext();
    return false;
  }
  g_subclassed = SetWindowSubclass(hwnd, subclass_proc, kSubclassId, 0) != FALSE;
  if (!g_subclassed) log_warn("overlay: the game window could not be subclassed - no keyboard or mouse for the panel");
  g_context_ready = true;
  char cls[64] = "?";
  GetClassNameA(hwnd, cls, sizeof(cls));
  logf("overlay ready (hwnd=%p class '%s' %ldx%ld, scale %.2f, present thread %lu, window thread %lu)",
       static_cast<void*>(hwnd), cls, rc.right - rc.left, rc.bottom - rc.top, scale, GetCurrentThreadId(),
       GetWindowThreadProcessId(hwnd, nullptr));
  return true;
}

void lock_imgui() {
  ensure_lock();
  EnterCriticalSection(&g_imgui_cs);
}
void unlock_imgui() {
  if (g_imgui_cs_ready) LeaveCriticalSection(&g_imgui_cs);
}

bool hud_wanted() { return g_context_ready && !pause_menu_open() && healthbars::take_snapshot(); }
void draw_hud() { healthbars::draw(g_hud_font); }

bool visible() { return g_visible != 0; }
void set_forced(bool on) { InterlockedExchange(&g_forced, on ? 1 : 0); }
void set_pause_open(bool on) { InterlockedExchange(&g_pause_open, on ? 1 : 0); }
bool pause_open() { return g_pause_open != 0; }
bool wants_draw() {
  const bool paused = pause_menu_open() && settings().show_on_pause;
  if (paused != g_was_paused) {
    g_was_paused = paused;
    InterlockedExchange(&g_user_hidden, 0);  // the next time the pause menu opens the panel is back
    InterlockedExchange(&g_forced, 0);
  }
  const bool vis = g_context_ready && (paused ? !g_user_hidden : g_forced != 0);
  InterlockedExchange(&g_visible, vis ? 1 : 0);
  ImGuiIO& io = ImGui::GetIO();
  if (vis) {
    // The physical buttons are the truth: a press or release the window never
    // got as a message (Raw Input can take them) is still the panel's.
    const bool focused = GetForegroundWindow() == g_hwnd;
    static const int kVk[3] = {VK_LBUTTON, VK_RBUTTON, VK_MBUTTON};
    for (int b = 0; b < 3; ++b) {
      const bool down = focused && (GetAsyncKeyState(kVk[b]) & 0x8000) != 0;
      if (io.MouseDown[b] != down) io.AddMouseButtonEvent(b, down);
    }
  } else {
    // Input made while the panel is away is not the panel's.
    io.ClearEventsQueue();
    io.ClearInputMouse();
    io.ClearInputKeys();
  }
  return vis;
}

void draw_panel() {
  const cheats::Status st = cheats::status();
  // A free-size window: drag any edge or corner; the size and position are
  // remembered in the ini next to the settings. Controls stretch with it.
  ImGui::SetNextWindowPos(ImVec2(40.0f, 120.0f), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(560.0f, 640.0f), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSizeConstraints(ImVec2(360.0f, 200.0f), ImVec2(FLT_MAX, FLT_MAX));
  bool open = true;
  if (!ImGui::Begin("Diablo II: Resurrected - Cabby Codes  v" D2RCC_VERSION, &open)) {
    ImGui::End();
    return;
  }
  // Sliders and combos take the width that is left after their labels.
  ImGui::PushItemWidth(std::max(160.0f, ImGui::GetContentRegionAvail().x - 200.0f));
  if (!open) {
    set_forced(false);
    InterlockedExchange(&g_user_hidden, 1);
  }

  // A number box is as wide as its largest value plus the two step buttons.
  const ImGuiStyle& style = ImGui::GetStyle();
  const float number_box = ImGui::CalcTextSize("00000").x + style.FramePadding.x * 2.0f +
                           (ImGui::GetFrameHeight() + style.ItemInnerSpacing.x) * 2.0f;

  if (section(kSecCheats, "Cheats")) {
    cheat_row(cheats::kGodMode, "God mode",
              "Nothing takes life from you and nothing kills you: every way the game lowers a player's life is "
              "refused (hits, damage returned to you, crushing blow, poison, burning, Blood Mana). A hit does "
              "nothing else to you either: no freeze, chill, stun or drain. Your life is not refilled; it recovers "
              "as usual, and a life above its maximum (Battle Orders ended) still comes down to it.");
    cheat_row(cheats::kInfiniteMana, "Infinite mana", "Your skills cost no mana, and the orb is kept full.");
    cheat_row(cheats::kInfiniteStamina, "Infinite stamina", "Stamina is kept full, so you can run without end.");
    {
      int pct = cheats::move_speed_bonus();
      ImGui::SetNextItemWidth(number_box);
      if (ImGui::InputInt("Movement speed bonus (%)", &pct, 5, 25)) cheats::set_move_speed_bonus(std::clamp(pct, 0, 300));
      if (ImGui::IsItemDeactivatedAfterEdit()) changed("Movement speed", "changed");
      if (const char* why = cheats::why_not(cheats::kMoveSpeed)) {
        ImGui::SameLine();
        ImGui::TextDisabled("(%s)", why);
      } else {
        help_marker("Extra run and walk speed, 0 to 300 percent, on top of everything your gear and skills give.");
      }
    }
    {
      float m = cheats::exp_multiplier();
      ImGui::SetNextItemWidth(number_box);
      if (ImGui::InputFloat("Experience multiplier (x)", &m, 0.5f, 5.0f, "%.1f"))
        cheats::set_exp_multiplier(std::clamp(m, 1.0f, 50.0f));
      if (ImGui::IsItemDeactivatedAfterEdit()) changed("Experience multiplier", "changed");
      if (const char* why = cheats::why_not(cheats::kExpMultiplier)) {
        ImGui::SameLine();
        ImGui::TextDisabled("(%s)", why);
      } else {
        help_marker("Every experience gain is multiplied by this, 1 to 50: kills and quest rewards alike.");
      }
    }
    {
      float m = cheats::damage_multiplier();
      ImGui::SetNextItemWidth(number_box);
      if (ImGui::InputFloat("Damage multiplier (x)", &m, 0.5f, 5.0f, "%.1f"))
        cheats::set_damage_multiplier(std::clamp(m, 1.0f, 100.0f));
      if (ImGui::IsItemDeactivatedAfterEdit()) changed("Damage multiplier", "changed");
      if (const char* why = cheats::why_not(cheats::kDamageMultiplier)) {
        ImGui::SameLine();
        ImGui::TextDisabled("(%s)", why);
      } else {
        help_marker("The damage your hits do is multiplied by this, 1 to 100: your attacks and spells, your "
                    "summons' and your mercenary's, poison and burning included. Resistances and immunities still "
                    "apply.");
      }
    }

    ImGui::Spacing();
    cheat_row(cheats::kInvincibleMerc, "Invincible mercenary",
              "Your mercenary takes nothing from any hit: no damage, no poison, burning, chill, freeze or stun, "
              "and no mana or stamina drained.");
    cheat_row(cheats::kInvinciblePets, "Invincible minions",
              "The same for everything you summon: skeletons and mages, golems, revives, valkyries, wolves and "
              "bears, spirits, vines, shadows and the rest.");
    cheat_row(cheats::kPassiveMerc, "Passive mercenary",
              "Your mercenary fights nothing: it follows you as always, but never picks a fight, and does not hit "
              "back when hit (Invincible mercenary keeps it alive). Its aura still works - Holy Freeze still chills, "
              "Might still helps you - and an attack it had begun when you switch this on still lands.");
    cheat_row(cheats::kPassivePets, "Passive minions",
              "The same for everything you summon: skeletons and mages, golems, revives, valkyries, wolves and "
              "bears, ravens, shadows, the Warlock's demons and the rest. They follow you, and what they do without "
              "an enemy stays (buffs, a Fire Golem's Holy Fire, vines eating corpses). Traps and hydras do nothing "
              "while this is on.");
    cheat_row(cheats::kPermanentRevives, "Permanent revives",
              "The monsters your Necromancer revives stay until they die: their 3 minutes never run out. The limit "
              "of revives (one a skill level) is still the game's: past it, a new revive takes the oldest one's "
              "place. Switched off, new revives time out again; the ones already kept stay until they die.");
    cheat_row(cheats::kCannotBeFrozen, "Cannot be frozen",
              "Hits never freeze or chill you (cold damage itself still hurts).");
    cheat_row(cheats::kCannotBePoisoned, "Cannot be poisoned",
              "Hits never poison you, and a poison already on you ends at once, as an antidote potion ends it.");
    cheat_row(cheats::kCannotBeCursed, "Cannot be cursed",
              "Curse immunity: a monster's curse never lands on you - Amplify Damage, Decrepify, Weaken, Lower "
              "Resist, Defense Curse, Blood Mana and the rest, from the monsters that cast them and from Cursed "
              "unique monsters - and a curse already on you ends at once. Shrines work as always: the game counts "
              "their bonuses among the curses (one takes the other's place), and those are left alone.");

    ImGui::Spacing();
    cheat_row(cheats::kInfiniteTownPortal, "Infinite town portal",
              "A Scroll of Town Portal stays after you read it, and a Tome of Town Portal keeps its charges.");
    cheat_row(cheats::kInfiniteIdentify, "Infinite identify",
              "A Scroll of Identify stays after you use it, and a Tome of Identify keeps its charges.");
    cheat_row(cheats::kAutoIdentify, "Identify items on pickup",
              "An item you pick up is identified at once, as if you had read a Scroll of Identify on it: from the "
              "ground into your inventory, belt or Horadric Cube, onto the cursor when there is no room, or with "
              "Telekinesis. A unique or set item goes into your Chronicle then, as identifying it puts it there. "
              "Items you already carry or keep in the stash stay as they are.");
    cheat_row(cheats::kInfinitePotions, "Infinite potions",
              "A potion you drink stays where it was, in the belt or the inventory, and so does one you give your "
              "mercenary: healing, mana, rejuvenation, stamina, antidote and thawing potions. Quest items are used "
              "up as usual.");
    cheat_row(cheats::kInfiniteKeys, "Infinite keys",
              "A key opens every locked chest and door without being used up: carry at least one in your "
              "inventory (not the stash or the cube) and the count never goes down.");
    cheat_row(cheats::kNoDurabilityLoss, "No durability loss",
              "Your weapon and armor never lose durability: not from your hits, the hits you take or Impale, "
              "ethereal items included. An item already broken stays broken until it is repaired. (Your "
              "mercenary's gear never wears in the game anyway.)");
    cheat_row(cheats::kInfiniteGold, "Infinite gold",
              "Nothing you pay for takes your gold: buying, gambling, repairs, Deckard Cain's identifying, hiring a "
              "mercenary and bringing one back to life. You still need to hold the price, in your inventory or "
              "your stash - the game checks that before it sells - but it is not taken.");

    ImGui::Spacing();
    cheat_row(cheats::kInfiniteImbue, "Infinite imbues (Charsi)",
              "Charsi offers Imbue whenever you talk to her, whether or not you have done her quest (Tools of the "
              "Trade), and as often as you like: the quest's own reward is neither needed nor used up. What she "
              "makes is the game's: a plain item (low quality, normal or superior) without sockets becomes a rare "
              "one of your character's level plus four. The loot switches act on it as on any new item.");
    cheat_row(cheats::kInfiniteSockets, "Infinite sockets (Larzuk)",
              "Larzuk offers Add Sockets whenever you talk to him, whether or not you have done his quest (Siege "
              "on Harrogath), and as often as you like: the quest's own reward is neither needed nor used up. "
              "What an item gets is the game's: a normal or superior item the most sockets its base can have at "
              "its level, a magic item one or two, a rare, set, unique or crafted item one. An item that has "
              "sockets gets no more.");
    cheat_row(cheats::kInfinitePersonalize, "Infinite personalizing (Anya)",
              "Anya offers Personalize whenever you talk to her in Harrogath, whether or not you have done the "
              "quest for it (Betrayal of Harrogath), and as often as you like: the quest's own reward is neither "
              "needed nor used up. The item gets your character's name, as in the game. (She is in Harrogath once "
              "you have freed her: Prison of Ice.)");

    ImGui::Spacing();
    cheat_row(cheats::kInfiniteCubeIngredients, "Infinite cube ingredients",
              "A Horadric Cube recipe uses nothing up: what you put in comes back to your inventory beside what the "
              "recipe made (dropped at your feet when there is no room). Clear Sockets gives back what was in the "
              "sockets too, and the portal recipes keep their keys, organs and Wirt's Leg. An item a recipe changes "
              "in place - an upgrade, added sockets, a repair, Clear Sockets' item - is what it makes: it is changed, "
              "not copied.");

    ImGui::Spacing();
    {
      cheat_row(cheats::kTerrorAll, "All areas terrorized",
                "Every act is terrorized in every game while this is on, as if its Worldstone Shard had been used, "
                "and so is the Moo Moo Farm, which no shard covers: every area a terror zone can take. It lasts until "
                "you leave the game (the half-hour rotation does not end it) and is back in the next one. Switched "
                "off, it ends in the game you are in, a shard you used yourself included, and the rotation's zone "
                "stays. Areas you enter from then on generate terrorized; areas already visited keep their monsters "
                "as they were. It needs a game with terror zones: Baal killed on that difficulty, and the game's "
                "Terror Zones option on.");
      const terror::Stats ts = terror::stats();
      if (!ts.in_game)
        ImGui::TextDisabled("   Terrorized now: not in a game");
      else if (cheats::enabled(cheats::kTerrorAll) && ts.checked && !ts.has_zones)
        ImGui::TextDisabled("   This game has no terror zones");
      else
        ImGui::TextDisabled("   Terrorized now: %d area%s", ts.areas, ts.areas == 1 ? "" : "s");
    }

    ImGui::Spacing();
    {
      cheat_row(cheats::kExitBeforeDeath, "Exit before death",
                "A hit that would kill you is stopped at 1 life and the game is left at once, so the death is never "
                "recorded: no experience loss, no gold drop, no corpse, no hardcore death.");
      if (cheats::enabled(cheats::kExitBeforeDeath) && st.player_found)
        ImGui::TextDisabled(st.exit_armed ? "   armed" : "   arms once life is above the trigger (a hit that would kill still exits)");
      // Fixed widths for these two: the slider fits its longest label, the box its longest choice.
      const float threshold_width = ImGui::CalcTextSize("lethal hits only").x + style.FramePadding.x * 6.0f;
      const float method_width = ImGui::CalcTextSize("Save and exit").x + style.FramePadding.x * 4.0f + ImGui::GetFrameHeight();
      int pct = cheats::exit_below_percent();
      ImGui::SetNextItemWidth(threshold_width);
      if (ImGui::SliderInt("Also leave below", &pct, 0, 99, pct ? "%d %% life" : "lethal hits only"))
        cheats::set_exit_below_percent(pct);
      if (ImGui::IsItemDeactivatedAfterEdit()) changed("Exit threshold", "changed");
      int method = static_cast<int>(cheats::exit_method());
      ImGui::SetNextItemWidth(method_width);
      if (ImGui::Combo("How to leave", &method, "Save and exit\0Force close\0")) {
        cheats::set_exit_method(static_cast<config::ExitMethod>(method));
        changed("Exit method", method ? "force close" : "save and exit");
      }
      help_marker("Save and exit uses the game's own Save and Exit. Force close ends the game at once; anything "
                  "since the last save is lost, but so is the death.");
    }
  }

  if (section(kSecCharacter, "Character")) draw_character(st);

  if (section(kSecLoot, "Loot")) {
    cheat_row(cheats::kLootFilterOnly, "Only drop what the loot filter shows",
              "Monsters and chests drop only what your loot filter shows (the game's own filter, the character's "
              "active profile), and every pick drops: there is no NoDrop. Nothing is rolled again and again: what a "
              "treasure class can drop is worked out - every item, quality, and whether ethereal or socketed - what "
              "the filter hides is struck from it, and a pick the filter would hide is made up for by one among "
              "the rest, each with the odds it has in the game. A pick stays empty only when the monster or chest "
              "can drop nothing (more) that the filter shows. Drops are not filtered while the loot filter editor "
              "is open.");
    cheat_row(cheats::kChronicleOnly, "Only drop what the Chronicle still misses",
              "Monsters and chests drop only the unique and set items your Chronicle does not have yet (the game's "
              "record of what you have found, as its Chronicle panel shows it), and every pick drops. As with the "
              "loot filter switch nothing is rolled again and again: what a treasure class can drop is worked out, "
              "and a pick is made up for by one of those items, with the odds it has in the game, named to the game "
              "as it makes it. A unique drops once a game in the game itself, and a set item the Chronicle misses "
              "drops once a game too, so extra drops do not bring the same piece again and again. An item goes "
              "into the Chronicle when it is identified. With the loot filter switch on as well, a drop may be "
              "either: what the filter shows, or an item the Chronicle misses. Runewords are made, not dropped: "
              "the Chronicle's runewords are left to you.");
    if (const chronicle::Stats cs = chronicle::stats(); cheats::enabled(cheats::kChronicleOnly) && cs.read)
      ImGui::TextDisabled("   still to find: %d of %d uniques, %d of %d set items", cs.uniques_missing, cs.uniques,
                          cs.sets_missing, cs.sets);
    {
      int n = cheats::extra_drops();
      ImGui::SetNextItemWidth(number_box);
      if (ImGui::InputInt("Extra drops per kill or chest", &n, 1, 5)) cheats::set_extra_drops(std::clamp(n, 0, 20));
      if (ImGui::IsItemDeactivatedAfterEdit()) changed("Extra drops", "changed");
      if (const char* why = cheats::why_not(cheats::kExtraDrops)) {
        ImGui::SameLine();
        ImGui::TextDisabled("(%s)", why);
      } else {
        help_marker("0 to 20 more items from the same treasure class for every monster killed and every chest, "
                    "barrel, urn, body or rack opened. With the loot filter or Chronicle switch on they follow it "
                    "too.");
      }
      ImGui::Indent();
      cheat_row(cheats::kExtraDropsElite, "Only from champions, uniques, bosses and Heralds",
                "The extra drops come only with the kills that tend to drop better: champions, unique and super "
                "unique monsters, a terror zone's Heralds, and bosses (the act bosses and their Uber versions, Diablo "
                "Clone, Blood Raven, Radament, the Summoner, Izual, Griswold, Nihlathak, the colossal Ancients). "
                "Ordinary monsters get none, nor do a unique's or a Herald's minions, nor chests, barrels, urns, "
                "bodies and racks.");
      ImGui::Unindent();
    }
    ImGui::TextDisabled("New items:");
    help_marker("The six switches below act while the game makes an item, in its own generator, and leave every "
                "item one the game itself can make. A save keeps one seed with an item, the one that rolled its "
                "durability, an armor's base defense and its picture: under perfect rolls an armor is made from a "
                "seed that rolls its top defense, so the seed in the save agrees with the item. Nothing is looked "
                "up while you play.");
    cheat_row(cheats::kPerfectRolls, "Perfect rolls",
              "Every new item's stats are at the top of their ranges - drops, vendor stock, gambling, crafting, "
              "runewords, cube results - and armor gets its highest base defense, never above what the game itself "
              "can make. Class items (the ones only one class can use) get three \"+skill\" lines, each at +3. A "
              "superior item gets the best of the bonuses the game has for it: armor enhanced defense with durability, "
              "a weapon enhanced damage with attack rating. Which skill, class or stat a random property picks stays "
              "random, and so do socket counts.");
    cheat_row(cheats::kAllSuperior, "All new items superior",
              "Every freshly made normal or low quality item that can be superior is: weapons and armor from drops, "
              "vendor stock and cube results. Magic and better items, and items that are never superior (potions, "
              "gems, runes, jewelry), are unchanged.");
    cheat_row(cheats::kAllEthereal, "All new items ethereal",
              "Every freshly made item the game lets be ethereal is: weapons and armor with durability, not set or "
              "inferior items. Vendor stock, gambling and quest items are never ethereal in the game and stay so. "
              "Ethereal items cannot be repaired.");
    cheat_row(cheats::kAllSocketed, "All new items socketed",
              "Every freshly made normal or superior item whose base can have sockets has them: weapons, body armor, "
              "helms and shields, from drops, vendor stock and cube results. How many is still random, from 1 up to "
              "what the base allows at the item's level (the game caps it at 3 in Normal and 4 in Nightmare). Magic "
              "and better items, throwing weapons, gloves, boots, belts and jewelry are unchanged.");
    cheat_row(cheats::kMaxAffixes, "Max affixes on new items",
              "Every freshly made magic, rare and crafted item gets as many affixes as the game lets its quality "
              "have: a magic item a prefix and a suffix, a rare item six (three of each; a rare jewel four), a crafted "
              "item four random ones besides its recipe's. Which affixes is still the game's pick, by its own rules "
              "(the item's type and level, one affix per group), so an item that has fewer to choose from at its "
              "level gets fewer, as in the game. Drops, vendor stock, gambling and cube results; not uniques, set "
              "items or runewords, which have fixed properties.");
    cheat_row(cheats::kBestAffixes, "Best affixes on new items",
              "Every affix a freshly made magic, rare or crafted item gets is the best version of that affix the item "
              "could have at its level: the same stats, the biggest numbers. Life becomes \"of the Whale\" rather "
              "than \"of the Jackal\" on an amulet whose level allows the Whale, magic find \"of Luck\" on boots, "
              "attack speed \"of Quickness\" on a weapon. So does the automatic affix some bases come with: a "
              "paladin shield's all resistances (up to 45) or its attack rating and damage, an orb's life or mana, a "
              "Necromancer head's poison, an Amazon bow's or spear's skills, a grimoire's damage. Which affixes, and "
              "how many, is still the game's roll (max affixes adds more), and every item stays one the game could "
              "drop there: an item from a low area gets the best its level allows. Drops, vendor stock, gambling and "
              "cube results.");
    const loot::Stats ls = loot::stats();
    if (ls.kinds_off)
      ImGui::TextDisabled("A superior item's kind is the game's own pick for this session (see the log)");
    if (ls.ask_first_off)
      ImGui::TextDisabled("The loot filter is asked after a drop is made for this session (see the log)");
    else if (ls.odds_off)
      ImGui::TextDisabled("Drops are rolled for until the loot filter shows one for this session (see the log)");
    if (itemseed::stats().off)
      ImGui::TextDisabled("New armor keeps the game's own seeds for this session (see the log)");
  }

  if (section(kSecDisplay, "Display")) {
    cheat_row(cheats::kRevealMap, "Reveal the map",
              "The whole map of the area you are in is revealed on the automap, and so is each area you enter "
              "after it. A large area fills in over a few frames.");
    cheat_row(cheats::kMapLandmarks, "Landmarks on the map",
              "What the automap shows of an area only once you have come near it - waypoints, shrines, wells, quest "
              "objects (the Cairn Stones, the Inifuss tree, the Horadric and Khalim chests, the seals), the Arcane "
              "Sanctuary's portals, trap doors and stairs - is on it as soon as you enter the area, wherever the "
              "area's layout places them. The shrines and wells the game places at random in a dungeon as you come "
              "near are in no layout: they appear as the game has them. What is put on is saved with the map, as "
              "what you see yourself is.");
    cheat_row(cheats::kMapEnemies, "Named enemies on the map",
              "Unique and super unique monsters, bosses, champions and a terror zone's Heralds get a red cross on the "
              "automap with their name above it, the way the game marks town folk (a champion pack's name once, in "
              "the game's champion blue), however far away they are. Super uniques and bosses are marked from the "
              "moment you enter their area (Bishibosh's camp, the Countess's floor, Andariel's lair...). Other "
              "uniques, champions and Heralds do not exist until the game makes them - it makes a room's monsters as "
              "you first come near the room - and are marked from then on, where they are or were last seen, "
              "wherever you go, until they die. Minions and your own pets get none.");
    cheat_row(cheats::kMapExits, "Area names at exits",
              "The name of the area each exit leads to is written on the automap, for the area you are in and each "
              "area you enter after it: at a cave's entrance, a dungeon's stairs or a trap door, and at each opening "
              "where one area meets the next (the zone line between two outdoor areas, a town's gate). Where two areas "
              "meet, each side names the other, a little inside its own area.");

    ImGui::Spacing();
    cheat_row(cheats::kAreaLevelAutomap, "Area level in the automap's area name",
              "The area's monster level for this difficulty, after its name, in the automap's area text.");
    cheat_row(cheats::kAreaLevelWaypoints, "Area level in the waypoint list",
              "The same, after each destination in the waypoint panel (open it again after switching this on).");

    ImGui::Spacing();
    cheat_row(cheats::kItemLevel, "Item level after an item's name",
              "An item's level after its name in its tooltip, as in \"Grand Charm (91)\": weapons, armor, rings, "
              "amulets, charms and jewels (not quest items or throwing potions), items you already have included.");

    ImGui::Spacing();
    cheat_row(cheats::kMonsterBars, "Health bars above monsters",
              "A short life bar floats above every enemy on screen: a blue frame for champions, gold for uniques and "
              "bosses. Town folk, your minions and your mercenary get none. HD graphics only; bars under an open "
              "inventory or character panel are hidden.");
    if (cheats::enabled(cheats::kMonsterBars) && healthbars::legacy_graphics())
      ImGui::TextDisabled("   no bars in legacy graphics");
    cheat_row(cheats::kBossBar, "Boss health bar",
              "While an act boss is near - Andariel, Duriel, Mephisto, Diablo, Baal, their Uber versions and Diablo "
              "Clone - its name and life are shown at the top centre of the screen.");
    {
      const bool on = cheats::enabled(cheats::kBossBar) && !cheats::why_not(cheats::kBossBar);
      const float width = ImGui::CalcTextSize("50 % from the top").x + style.FramePadding.x * 6.0f;
      int pos = cheats::boss_bar_position();
      if (!on) ImGui::BeginDisabled();
      ImGui::SetNextItemWidth(width);
      if (ImGui::SliderInt("Boss bar position", &pos, 0, 50, "%d %% from the top")) cheats::set_boss_bar_position(pos);
      if (ImGui::IsItemActive()) healthbars::draw_boss_preview(g_hud_font, cheats::boss_bar_position());
      if (ImGui::IsItemDeactivatedAfterEdit()) changed("Boss bar position", "changed");
      if (!on) ImGui::EndDisabled();
      help_marker("How far down the screen the boss bar sits (0: at the very top, where the game's own bar for the "
                  "monster under the cursor also appears). Drag to see where.");
    }
  }

  if (section(kSecQol, "Quality of life")) {
    cheat_row(cheats::kRememberAutomap, "Keep the automap open or closed between games",
              "When you load a character, the automap is open or closed the way it was when you last left a game "
              "(with any character). D2RCore already keeps it within one run of the game, and on the first game of "
              "a run it closes the map; this keeps it after the game is closed too, and overrides the game's own "
              "'open on load' option.");
    keep_note(remember::kAutomap, cheats::kRememberAutomap, "");
    cheat_row(cheats::kRememberShowItems, "Keep Show Items on between games",
              "When you load a character, Show Items (the names of the items on the ground, as your loot filter "
              "shows them) is on again if it was on when you last left a game, as if you had pressed its key. Only "
              "with the game's Item Name Display option set to Toggle.");
    keep_note(remember::kShowItems, cheats::kRememberShowItems, "Item Name Display");
    cheat_row(cheats::kRememberShowItemsUnfiltered, "Keep Show Items (Unfiltered) on between games",
              "The same for Show Items (Unfiltered), the key that names every item on the ground whatever the loot "
              "filter says. Only with the game's Unfiltered Item Name Display option set to Toggle.");
    keep_note(remember::kShowItemsUnfiltered, cheats::kRememberShowItemsUnfiltered, "Unfiltered Item Name Display");

    ImGui::Spacing();
    {
      // The home town: the game's own (the town of the act you are in), or one act's.
      static const char* const kTowns[hometown::kActs + 1] = {
          "The town of the act you are in", "Act I - the Rogue Encampment", "Act II - Lut Gholein",
          "Act III - the Kurast Docks",     "Act IV - the Pandemonium Fortress", "Act V - Harrogath"};
      const char* why = cheats::why_not(cheats::kHomeTown);
      const hometown::Stats ts = hometown::stats();
      const bool known = ts.in_game && ts.known;
      const bool wakes = hometown::wakes_at_home();
      const auto reached = [&ts](int act) { return act < ts.acts && ((ts.reach >> act) & 1u) != 0; };
      const float width = ImGui::CalcTextSize("Act IV - the Pandemonium Fortress (not reached)").x +
                          style.FramePadding.x * 4.0f + ImGui::GetFrameHeight();
      const int choice = cheats::home_town();
      if (why) ImGui::BeginDisabled();
      ImGui::SetNextItemWidth(width);
      if (ImGui::BeginCombo("Home town", kTowns[choice])) {
        for (int i = 0; i <= hometown::kActs; ++i) {
          char label[96];
          std::snprintf(label, sizeof(label), "%s%s", kTowns[i],
                        i && known && !reached(i - 1) ? " (not reached)" : "");
          if (ImGui::Selectable(label, i == choice) && i != choice) {
            cheats::set_home_town(i);
            changed("Home town", kTowns[i]);
          }
        }
        ImGui::EndCombo();
      }
      if (why) ImGui::EndDisabled();
      if (why) {
        ImGui::SameLine();
        ImGui::TextDisabled("(%s)", why);
      } else if (wakes) {
        help_marker("Your base. The game opens a town portal to the town of the act you read it in, and wakes you "
                    "after a death in the town of the act you died in. Choose a town here and both lead there "
                    "instead, whatever act you are in: your town portals (the portal that stands in that town "
                    "takes you back to where you read it), and your wake after a death. Only a town your character "
                    "has the waypoint of: for another, both are as the game has them.");
      } else {
        help_marker("Your base. The game opens a town portal to the town of the act you read it in. Choose a town "
                    "here and your portals lead there instead, whatever act you read them in; the portal that "
                    "stands in that town takes you back to where you read it. Only a town your character has the "
                    "waypoint of: for another the portal leads where the game has it lead. After a death you "
                    "still wake in the town of the act you died in: the game's wake routine was not found in this "
                    "version of the game.");
      }
    }
  }

  if (section(kSecDiagnostics, "Diagnostics")) {
    cheat_row(cheats::kLogging, "Write the log",
              "The plugin's log, d2rloader/logs/cabbycodes.log in the game's folder: what it found and hooked as "
              "the game started, and what it does as you play. It is what to send along when something does not "
              "work. Switched off, nothing is written, from now on and in the games to come, but what you ask for "
              "by name in the loader's console (cabbycodes perf, cabbycodes dropcheck). What happened while it was "
              "off is not in the log when you switch it on, what the plugin found at the game's start included: "
              "start the game again for that.");
    {
      const bool on = cheats::enabled(cheats::kLogging);
      static const char* const kLevels[3] = {"Information", "Warnings", "Errors"};
      const ImGuiStyle& style = ImGui::GetStyle();
      const float width = ImGui::CalcTextSize("Information").x + style.FramePadding.x * 4.0f + ImGui::GetFrameHeight();
      int level = static_cast<int>(cheats::log_level());
      if (!on) ImGui::BeginDisabled();
      ImGui::SetNextItemWidth(width);
      if (ImGui::Combo("The least severe lines to write", &level, kLevels, 3)) {
        cheats::set_log_level(static_cast<LogLevel>(level));
        changed("Log level", kLevels[static_cast<int>(cheats::log_level())]);
      }
      if (!on) ImGui::EndDisabled();
      help_marker("What goes into the log, by how severe it is: pick the least severe kind of line to write, and "
                  "the more severe kinds are written too. Information is everything the plugin says: what it does, "
                  "and every warning and error. Warnings leaves the information out: only what went other than "
                  "it should is written, and the errors. Errors writes only what failed.");
      const LogLevel least = cheats::log_level();
      if (!on)
        ImGui::TextDisabled("   off: nothing is written");
      else
        ImGui::TextDisabled("   written: %s", least == LogLevel::kError     ? "errors only"
                                              : least == LogLevel::kWarning ? "warnings and errors"
                                                                            : "information, warnings and errors");
      if (on && settings().trace)
        ImGui::TextDisabled("   trace = true (cabbycodes.toml): every line is written the instant it is made");
    }
  }

  ImGui::Separator();
  ImGui::TextDisabled("%s hides this panel", config::key_name(toggle_key()));
  ImGui::PopItemWidth();
  ImGui::End();
}

void shutdown_imgui() {
  if (!g_context_ready) return;
  ImGui_ImplWin32_Shutdown();
  if (ImGui::GetCurrentContext()) ImGui::DestroyContext();
  g_context_ready = false;
  InterlockedExchange(&g_visible, 0);
}

bool install() { return d3d::install(); }

void uninstall() {
  if (g_subclassed && g_hwnd && IsWindow(g_hwnd)) {
    RemoveWindowSubclass(g_hwnd, subclass_proc, kSubclassId);
    g_subclassed = false;
  }
  d3d::uninstall();
  ImGuiLock guard;
  shutdown_imgui();
  logf("overlay hooks removed");
}

}  // namespace d2rcc::overlay

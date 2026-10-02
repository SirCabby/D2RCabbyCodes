#include "dev.h"

#ifdef D2RCC_DEV

#include <D2RLPlugin/api.h>

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "cheats.h"
#include "config.h"
#include "context.h"
#include "log.h"
#include "overlay.h"

namespace d2rcc::dev {
namespace {

HANDLE g_thread = nullptr;
volatile LONG g_stop = 0;
HWND g_game_hwnd = nullptr;

std::wstring cmd_path() {
  const D2RL::PluginContext* ctx = context();
  std::wstring dir = ctx && ctx->pluginLogPath ? ctx->pluginLogPath : L"";
  const size_t cut = dir.find_last_of(L"\\/");
  return cut == std::wstring::npos ? L"" : dir.substr(0, cut + 1) + L"cabbycodes.cmd";
}

BOOL CALLBACK find_game_window(HWND hwnd, LPARAM lp) {
  DWORD pid = 0;
  GetWindowThreadProcessId(hwnd, &pid);
  if (pid != GetCurrentProcessId() || !IsWindowVisible(hwnd)) return TRUE;
  RECT rc{};
  GetClientRect(hwnd, &rc);
  if (rc.right - rc.left < 640 || rc.bottom - rc.top < 480) return TRUE;
  *reinterpret_cast<HWND*>(lp) = hwnd;
  return FALSE;
}

HWND game_window() {
  if (g_game_hwnd && IsWindow(g_game_hwnd)) return g_game_hwnd;
  g_game_hwnd = nullptr;
  EnumWindows(find_game_window, reinterpret_cast<LPARAM>(&g_game_hwnd));
  return g_game_hwnd;
}

void send_key(int vk) {
  INPUT in[2] = {};
  in[0].type = INPUT_KEYBOARD;
  in[0].ki.wVk = static_cast<WORD>(vk);
  in[0].ki.wScan = static_cast<WORD>(MapVirtualKeyA(static_cast<UINT>(vk), MAPVK_VK_TO_VSC));
  in[1] = in[0];
  in[1].ki.dwFlags = KEYEVENTF_KEYUP;
  SendInput(1, &in[0], sizeof(INPUT));
  Sleep(60);
  SendInput(1, &in[1], sizeof(INPUT));
}

bool to_screen(int x, int y, POINT* out) {
  HWND hwnd = game_window();
  if (!hwnd) return false;
  POINT p{x, y};
  if (!ClientToScreen(hwnd, &p)) return false;
  *out = p;
  return true;
}

void send_move(int x, int y) {
  POINT p{};
  if (!to_screen(x, y, &p)) return;
  SetForegroundWindow(game_window());
  SetCursorPos(p.x, p.y);
  INPUT in{};
  in.type = INPUT_MOUSE;
  in.mi.dx = p.x * 65535 / GetSystemMetrics(SM_CXSCREEN);
  in.mi.dy = p.y * 65535 / GetSystemMetrics(SM_CYSCREEN);
  in.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
  SendInput(1, &in, sizeof(INPUT));
}

void send_click(int x, int y, bool right) {
  send_move(x, y);
  Sleep(80);
  INPUT in[2] = {};
  in[0].type = in[1].type = INPUT_MOUSE;
  in[0].mi.dwFlags = right ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_LEFTDOWN;
  in[1].mi.dwFlags = right ? MOUSEEVENTF_RIGHTUP : MOUSEEVENTF_LEFTUP;
  SendInput(1, &in[0], sizeof(INPUT));
  Sleep(80);
  SendInput(1, &in[1], sizeof(INPUT));
}

const char* vk_names[][2] = {{"RETURN", "13"}, {"ENTER", "13"}, {"ESCAPE", "27"}, {"ESC", "27"},   {"SPACE", "32"},
                             {"TAB", "9"},     {"UP", "38"},    {"DOWN", "40"},   {"LEFT", "37"}, {"RIGHT", "39"},
                             {"BACK", "8"},    {"DELETE", "46"}, {"INSERT", "45"}, {"HOME", "36"}, {"END", "35"}};

int vk_of(const char* name) {
  for (const auto& kv : vk_names)
    if (_stricmp(name, kv[0]) == 0) return std::atoi(kv[1]);
  if (const int vk = config::key_from_name(name)) return vk;
  if (std::strlen(name) == 1) return std::toupper(static_cast<unsigned char>(name[0]));
  return 0;
}

bool on_off(const char* s, bool* out) {
  if (!_strnicmp(s, "on", 2)) { *out = true; return true; }
  if (!_strnicmp(s, "off", 3)) { *out = false; return true; }
  return false;
}

void run(const std::string& raw) {
  const LogAlways always;  // a command of the developer's: what it says is written with the log switched off too
  std::string line = raw;
  while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
  if (line.empty() || line[0] == '#') return;
  logf("dev: %s", line.c_str());
  char word[64] = {}, a1[128] = {}, a2[64] = {}, a3[64] = {};
  std::sscanf(line.c_str(), "%63s %127s %63s %63s", word, a1, a2, a3);
  bool on = false;
  if (!_stricmp(word, "god") && on_off(a1, &on)) cheats::set_enabled(cheats::kGodMode, on);
  else if (!_stricmp(word, "mana") && on_off(a1, &on)) cheats::set_enabled(cheats::kInfiniteMana, on);
  else if (!_stricmp(word, "exit") && on_off(a1, &on)) cheats::set_enabled(cheats::kExitBeforeDeath, on);
  else if (!_stricmp(word, "exp")) cheats::set_exp_multiplier(static_cast<float>(std::atof(a1)));
  else if (!_stricmp(word, "speed")) cheats::set_move_speed_bonus(std::atoi(a1));
  else if (!_stricmp(word, "panel") && on_off(a1, &on)) overlay::set_forced(on);
  else if (!_stricmp(word, "key")) send_key(vk_of(a1));
  else if (!_stricmp(word, "move")) send_move(std::atoi(a1), std::atoi(a2));
  else if (!_stricmp(word, "click")) send_click(std::atoi(a1), std::atoi(a2), _stricmp(a3, "right") == 0);
  else if (!_stricmp(word, "text")) {
    for (const char* c = line.c_str() + 5; *c; ++c) {
      const SHORT vk = VkKeyScanA(*c);
      if (vk == -1) continue;
      const bool shift = (vk & 0x100) != 0;
      if (shift) send_key(VK_SHIFT);
      send_key(vk & 0xFF);
    }
  } else if (!_stricmp(word, "status")) {
    const cheats::Status st = cheats::status();
    logf("dev: status player=%d #%u life %d/%d mana %d/%d lvl %d exp %lld vel %d ticks %u absorbed %u lethal %u | god=%d mana=%d exit=%d exp=x%.2f speed=+%d",
         st.player_found, st.player_id, st.hp, st.max_hp, st.mana, st.max_mana, st.level,
         static_cast<long long>(st.experience), st.velocity, st.ticks, st.hits_absorbed, st.lethal_hits,
         cheats::enabled(cheats::kGodMode), cheats::enabled(cheats::kInfiniteMana),
         cheats::enabled(cheats::kExitBeforeDeath), static_cast<double>(cheats::exp_multiplier()),
         cheats::move_speed_bonus());
  } else if (!_stricmp(word, "save")) {
    overlay::on_setting_changed();
  } else {
    log_warn("dev: unknown command '%s'", line.c_str());
  }
}

DWORD WINAPI poll(void*) {
  log_own_thread();
  const std::wstring path = cmd_path();
  if (path.empty()) return 0;
  logf("dev: watching %ls", path.c_str());
  while (!g_stop) {
    Sleep(250);
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) continue;
    std::vector<std::string> lines;
    char buf[512];
    while (std::fgets(buf, sizeof(buf), f)) lines.emplace_back(buf);
    std::fclose(f);
    DeleteFileW(path.c_str());
    for (std::string& l : lines) {
      while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
      run(l);
      Sleep(50);
    }
  }
  return 0;
}

}  // namespace

void start() {
  if (g_thread) return;
  InterlockedExchange(&g_stop, 0);
  g_thread = CreateThread(nullptr, 0, poll, nullptr, 0, nullptr);
}

void stop() {
  InterlockedExchange(&g_stop, 1);
  if (g_thread) {
    WaitForSingleObject(g_thread, 2000);
    CloseHandle(g_thread);
    g_thread = nullptr;
  }
}

}  // namespace d2rcc::dev

#else
namespace d2rcc::dev {
void start() {}
void stop() {}
}  // namespace d2rcc::dev
#endif

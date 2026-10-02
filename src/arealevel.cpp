#include "arealevel.h"

#include <windows.h>

#include <cstdio>
#include <cstring>

#include "game.h"
#include "log.h"
#include "mem.h"

namespace d2rcc::arealevel {
namespace {

constexpr int kMaxLevels = 192;

struct Level {
  int id;
  char key[64];
  int mon_lvl[3];     // MonLvl, (N), (H)
  int mon_lvl_ex[3];  // MonLvlEx, (N), (H) - the expansion's, what the game uses
};

// "Name [level]" per level id, handed to the game (UI thread only). The automap asks for its area's name at
// every frame, so the text is put together once: again only when the name, the difficulty or the table read
// is another.
struct Named {
  char text[128];   // empty: the level has no level to show, the name goes back as it came
  char source[96];  // the name it was made from
  int difficulty;
  LONG revision;    // of the table read (0: nothing made yet)
};
constexpr size_t kMaxSource = sizeof(Named::source) - 1;

const D2RL::PluginContext* g_ctx = nullptr;
const D2RL::DataTableService* g_tables = nullptr;
Named g_named[kMaxLevels];
volatile LONG g_revision = 1;  // moves on when the levels are read, or forgotten

CRITICAL_SECTION g_cs;
bool g_cs_ready = false;
Level g_levels[kMaxLevels];
int g_count = 0;
volatile LONG g_ready = 0;
size_t g_lvl_offset = 0;  // where the six MonLvl values sit in a record (0 = unknown)
int g_lvl_width = 2;      // bytes per value: 1, 2 or 4

void ensure() {
  if (g_cs_ready) return;
  InitializeCriticalSection(&g_cs);
  g_cs_ready = true;
}

// Find the six MonLvl values of a known row to learn where the columns are and
// how wide they are (the loader's compiled table keeps them as int16).
bool calibrate(const uint8_t* row, size_t size, const int (&expect)[6]) {
  const int widths[3] = {2, 4, 1};
  for (int w : widths) {
    for (size_t off = 0; off + 6 * static_cast<size_t>(w) <= size; ++off) {
      bool ok = true;
      for (int i = 0; i < 6 && ok; ++i) {
        int v = 0;
        const uint8_t* at = row + off + static_cast<size_t>(i * w);
        if (w == 4) {
          int32_t v32;
          std::memcpy(&v32, at, 4);
          v = v32;
        } else if (w == 2) {
          int16_t v16;
          std::memcpy(&v16, at, 2);
          v = v16;
        } else {
          v = *at;
        }
        ok = v == expect[i];
      }
      if (ok) {
        g_lvl_offset = off;
        g_lvl_width = w;
        return true;
      }
    }
  }
  return false;
}

int read_value(const uint8_t* row, size_t off) {
  if (g_lvl_width == 4) {
    int32_t v;
    std::memcpy(&v, row + off, 4);
    return v;
  }
  if (g_lvl_width == 2) {
    int16_t v;
    std::memcpy(&v, row + off, 2);
    return v;
  }
  return row[off];
}

int level_value(const Level& l) {
  int d = game::difficulty();
  if (d < 0 || d > 2) d = 0;
  return l.mon_lvl_ex[d] ? l.mon_lvl_ex[d] : l.mon_lvl[d];
}

}  // namespace

void set_services(const D2RL::PluginContext* ctx, const D2RL::DataTableService* tables) {
  ensure();
  g_ctx = ctx;
  g_tables = tables;
}

bool ready() { return g_ready != 0; }
void reset() {
  InterlockedExchange(&g_ready, 0);
  InterlockedIncrement(&g_revision);
}

bool collect() {
  ensure();
  if (!g_ctx || !g_tables || !g_tables->getTable) return false;
  D2RL::DataTables::TableView view{};
  view.structSize = D2RL::DataTables::TableViewSize;
  const D2RL::DataTables::Bank banks[3] = {D2RL::DataTables::Bank::Rotw, D2RL::DataTables::Bank::Lod,
                                           D2RL::DataTables::Bank::Classic};
  D2RL::DataTables::Result r = D2RL::DataTables::Result::Unavailable;
  const char* bank_name = "";
  for (int b = 0; b < 3; ++b) {
    r = g_tables->getTable(g_ctx, banks[b], D2RL::DataTables::TableId::Levels, &view);
    if (r == D2RL::DataTables::Result::Success && view.rows && view.rowCount) {
      bank_name = b == 0 ? "rotw" : b == 1 ? "lod" : "classic";
      break;
    }
  }
  if (r != D2RL::DataTables::Result::Success || !view.rows || !view.rowCount) {
    log_warn("area levels: the Levels table is not available (%u)", static_cast<unsigned>(r));
    return false;
  }
  if (view.rowSize < 64 || view.rowSize > 0x2000) {
    log_warn("area levels: unexpected Levels row size %u", view.rowSize);
    return false;
  }
  const uintptr_t rows = reinterpret_cast<uintptr_t>(view.rows);
  const uint32_t count = view.rowCount < static_cast<uint32_t>(kMaxLevels) ? view.rowCount : kMaxLevels;
  static uint8_t row[0x2000];
  Level fresh[kMaxLevels];
  int n = 0;
  g_lvl_offset = 0;
  logf("area levels: %s Levels table, %u rows of %u bytes at %p", bank_name, view.rowCount, view.rowSize, view.rows);
  // Learn the layout from The Secret Cow Level: where its name key sits in the
  // row, and where its six MonLvl values (28/53/78, 28/64/81) are.
  size_t key_offset = 0;
  bool have_key = false;
  for (uint32_t i = 0; i < count && !have_key; ++i) {
    if (!mem::copy_from(row, rows + i * view.rowSize, view.rowSize)) break;
    for (size_t off = 0; off + 13 <= view.rowSize; ++off) {
      if (std::memcmp(row + off, "Moo Moo Farm", 13) == 0 && (off == 0 || row[off - 1] == 0)) {
        key_offset = off;
        have_key = true;
        const int cow[6] = {28, 53, 78, 28, 64, 81};
        if (!calibrate(row, view.rowSize, cow)) {
          log_warn("area levels: the cow level's row (%u) has no MonLvl block; its bytes follow", i);
          for (size_t at = 0; at < view.rowSize; at += 32) {
            char hex[3 * 32 + 1] = {}, txt[33] = {};
            for (size_t k = 0; k < 32 && at + k < view.rowSize; ++k) {
              std::snprintf(hex + k * 3, 4, "%02X ", row[at + k]);
              txt[k] = row[at + k] >= 32 && row[at + k] < 127 ? static_cast<char>(row[at + k]) : '.';
            }
            logf("  +%04zX %s %s", at, hex, txt);
          }
        }
        break;
      }
    }
  }
  if (!have_key || !g_lvl_offset) {
    log_warn("area levels: the Levels table has no recognisable cow level (%s) - no area levels",
             have_key ? "values not found" : "name not found");
    return false;
  }
  logf("area levels: name key at +0x%zX, MonLvl at +0x%zX as %d-byte values", key_offset, g_lvl_offset, g_lvl_width);
  for (uint32_t i = 0; i < count; ++i) {
    if (!mem::copy_from(row, rows + i * view.rowSize, view.rowSize)) break;
    row[view.rowSize - 1] = 0;
    const char* key = reinterpret_cast<const char*>(row + key_offset);
    if (!key[0] || std::strlen(key) > 60) continue;
    Level& l = fresh[n];
    std::memset(&l, 0, sizeof(l));
    l.id = static_cast<int>(i);
    std::snprintf(l.key, sizeof(l.key), "%s", key);
    for (int k = 0; k < 6; ++k) {
      const int v = read_value(row, g_lvl_offset + static_cast<size_t>(k * g_lvl_width));
      if (k < 3) l.mon_lvl[k] = v;
      else l.mon_lvl_ex[k - 3] = v;
    }
    ++n;
  }
  EnterCriticalSection(&g_cs);
  std::memcpy(g_levels, fresh, sizeof(Level) * static_cast<size_t>(n));
  g_count = n;
  LeaveCriticalSection(&g_cs);
  InterlockedExchange(&g_ready, 1);
  InterlockedIncrement(&g_revision);
  logf("area levels: %d levels read from the %s Levels table", n, bank_name);
  return true;
}

int level_of(int level_id) {
  if (!g_ready || level_id < 0) return -1;
  ensure();
  int v = -1;
  EnterCriticalSection(&g_cs);
  for (int i = 0; i < g_count; ++i) {
    if (g_levels[i].id == level_id) {
      v = level_value(g_levels[i]);
      break;
    }
  }
  LeaveCriticalSection(&g_cs);
  return v;
}

const char* with_level(int level_id, const char* name) {
  if (!name || !*name || level_id < 0 || level_id >= kMaxLevels) return name;
  Named& n = g_named[level_id];
  const LONG revision = g_revision;
  const int difficulty = game::difficulty();
  if (n.revision == revision && n.difficulty == difficulty && std::strcmp(n.source, name) == 0)
    return n.text[0] ? n.text : name;
  const int v = level_of(level_id);
  if (v < 0) n.text[0] = 0;
  else std::snprintf(n.text, sizeof(n.text), "%s [%d]", name, v);
  // A name too long to keep is put together every time.
  const bool kept = std::strlen(name) <= kMaxSource;
  if (kept) std::snprintf(n.source, sizeof(n.source), "%s", name);
  n.difficulty = difficulty;
  n.revision = kept ? revision : 0;
  return n.text[0] ? n.text : name;
}

}  // namespace d2rcc::arealevel

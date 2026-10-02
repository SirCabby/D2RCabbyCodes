// seedfinder: which item seeds make an item with everything asked of it. Research, not part of the plugin (no save
// keeps an item's own seed, so the plugin does not choose it): a search over all 4,294,967,296 seeds with the seed
// model (itemgen.cpp), over the loader's compiled tables (the same rows the game has in memory).
//
//   seedfinder --excel <the loader's compiled excel folder> --out <a seeds file> [options]
//     --wants pmbes[,pe,...]   what is wanted, a list: p perfect rolls, m max affixes, b best affixes, e ethereal,
//                              s sockets (all superior is the quality 3 of --qualities)
//     --levels 96,98-99        item levels
//     --qualities 3,4,5,6,7,8  2 normal .. 8 crafted
//     --flags 0[,2]            the request's flags (0 a drop; 2 never ethereal, 8 no sockets: vendors, gambling)
//     --imbue 1                with the flags 0 and the quality 6: also the rare items as an imbue makes them (the
//                              class skill bonus, ethereal for sure or never); 0: not
//     --ethereal 1             with e among the wants: also the items asked for as ethereal for sure (the
//                              request's flag 4); 0: not
//     --asked 1                the items as the game asks for them; 0: only the two other ways
//     --difficulty 2  --type 3  --ladder 0  --version 101   the game (Game +0x104, +0x101, +0x108) and the item's
//                              version (the request's +0x3A)
//     --classes hax,rin        only these item codes
//     --keep 8                 seeds looked for per kind
//     --part 100               how much of the 4,294,967,296 seeds a kind's walk may get to in this run, in
//                              percent (a first run with 30 has the kinds with seeds soon; a second one ends the
//                              walks of the kinds without any)
//     --from <file>            another seeds file, read too (a kind keeps the walk that got further)
//     --threads 14
// An existing --out file is read first (the same model's): its kinds are kept, and a walk goes on where it was.
// A line per kind on the standard output: what was found.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include "itemgen.h"
#include "tables.h"

using namespace d2rcc::itemgen;

namespace {

constexpr uint32_t kStride = 0x9E3779B1u;  // a walk: base + i * stride (odd: every seed once)
constexpr uint64_t kAllSeeds = 1ull << 32;
constexpr size_t kMostSeeds = 64;
constexpr char kFileHead[] = "cabbycodes-seeds 1 model ";

struct Key {
  uint64_t print;
  uint32_t wants;
  uint64_t dropped;
  bool operator<(const Key& o) const {
    return std::tie(print, wants, dropped) < std::tie(o.print, o.wants, o.dropped);
  }
};
struct Record {
  uint32_t base = 0;
  uint64_t done = 0;
  std::vector<uint32_t> seeds;
};

std::vector<std::string> list_of(const std::string& text) {
  std::vector<std::string> out;
  size_t at = 0;
  while (at <= text.size()) {
    const size_t end = text.find(',', at);
    const std::string part = text.substr(at, end == std::string::npos ? std::string::npos : end - at);
    if (!part.empty()) out.push_back(part);
    if (end == std::string::npos) break;
    at = end + 1;
  }
  return out;
}

std::vector<int> numbers_of(const std::string& text) {
  std::vector<int> out;
  for (const std::string& part : list_of(text)) {
    const size_t dash = part.find('-', 1);
    const int from = static_cast<int>(std::strtol(part.c_str(), nullptr, 0));
    const int to = dash == std::string::npos ? from : static_cast<int>(std::strtol(part.c_str() + dash + 1, nullptr, 0));
    for (int v = from; v <= to; ++v) out.push_back(v);
  }
  return out;
}

Wants wants_of(const std::string& s) {
  Wants w;
  for (const char c : s) {
    if (c == 'p') w.perfect = true;
    if (c == 'm') w.most = true;
    if (c == 'b') w.best = true;
    if (c == 'e') w.ethereal = true;
    if (c == 's') w.sockets = true;
  }
  return w;
}

std::string wants_text(uint32_t bits) {
  std::string s;
  for (int i = 0; i < 5; ++i)
    if (bits & (1u << i)) s += "pmbes"[i];
  return s.empty() ? "-" : s;
}

bool read_records(const std::string& path, std::map<Key, Record>* records) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  std::string line;
  bool head = false;
  while (std::getline(f, line)) {
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    if (!head) {
      head = true;
      const size_t n = sizeof(kFileHead) - 1;
      if (line.compare(0, n, kFileHead) != 0 || std::strtoul(line.c_str() + n, nullptr, 10) != kModelVersion) {
        std::fprintf(stderr, "%s is not this seed model's: it is written anew\n", path.c_str());
        return false;
      }
      continue;
    }
    const char* p = line.c_str();
    char* next = nullptr;
    Key key{};
    Record r;
    key.print = std::strtoull(p, &next, 16);
    if (next == p) continue;
    key.wants = static_cast<uint32_t>(std::strtoul(p = next, &next, 16));
    if (next == p) continue;
    key.dropped = std::strtoull(p = next, &next, 16);
    if (next == p) continue;
    r.base = static_cast<uint32_t>(std::strtoul(p = next, &next, 16));
    if (next == p) continue;
    r.done = std::strtoull(p = next, &next, 16);
    if (next == p || r.done > kAllSeeds) continue;
    for (;;) {
      const unsigned long s = std::strtoul(p = next, &next, 16);
      if (next == p) break;
      if (r.seeds.size() < kMostSeeds) r.seeds.push_back(static_cast<uint32_t>(s));
    }
    // Of two walks of a kind, the one that got further, with the other's seeds.
    const auto known = records->find(key);
    if (known == records->end()) {
      (*records)[key] = std::move(r);
      continue;
    }
    Record& k = known->second;
    const std::vector<uint32_t> others = r.done > k.done ? k.seeds : r.seeds;
    if (r.done > k.done) k = std::move(r);
    for (const uint32_t s : others)
      if (k.seeds.size() < kMostSeeds && std::find(k.seeds.begin(), k.seeds.end(), s) == k.seeds.end())
        k.seeds.push_back(s);
  }
  return true;
}

bool write_records(const std::string& path, const std::map<Key, Record>& records) {
  const std::string temp = path + ".new";
  {
    std::ofstream f(temp, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << "# CabbyCodes: the seeds found for new items, a line per kind of item. They are found again when\r\n"
         "# this file is gone.\r\n";
    f << kFileHead << kModelVersion << "\r\n";
    char line[128];
    for (const auto& [key, r] : records) {
      std::snprintf(line, sizeof(line), "%016llX %02X %016llX %08X %09llX", static_cast<unsigned long long>(key.print),
                    key.wants, static_cast<unsigned long long>(key.dropped), r.base,
                    static_cast<unsigned long long>(r.done));
      f << line;
      for (const uint32_t s : r.seeds) {
        std::snprintf(line, sizeof(line), " %08X", s);
        f << line;
      }
      f << "\r\n";
    }
    if (!f) return false;
  }
  return std::rename(temp.c_str(), path.c_str()) == 0;
}

// The walk from `done` on, in rounds of a part per thread, until `keep` seeds are known or the walk is at `until`
// (every seed was looked at: 2^32). Every part of a round is looked at to its end, so `done` is exact.
void walk(const Plan& plan, const Search& search, int threads, size_t keep, uint64_t until, Record* r) {
  uint64_t part = 1ull << 14;
  while (r->done < until && r->seeds.size() < keep) {
    const uint64_t left = until - r->done;
    const uint64_t each = std::min(part, (left + threads - 1) / threads);
    std::vector<std::vector<uint32_t>> found(threads);
    std::vector<std::thread> pool;
    for (int t = 0; t < threads; ++t) {
      const uint64_t from = r->done + each * t;
      if (from >= until) break;
      const uint64_t count = std::min(each, until - from);
      pool.emplace_back([&, t, from, count] {
        uint64_t looked = 0;
        while (looked < count) {
          const uint32_t start = r->base + static_cast<uint32_t>(from + looked) * kStride;
          const Found f = d2rcc::itemgen::search(plan, search, start, kStride, count - looked);
          looked += f.tried;
          if (!f.all) break;
          found[t].push_back(f.seed);
        }
      });
    }
    for (std::thread& t : pool) t.join();
    r->done = std::min(until, r->done + each * threads);
    for (const auto& seeds : found)
      for (const uint32_t s : seeds)
        if (r->seeds.size() < kMostSeeds && std::find(r->seeds.begin(), r->seeds.end(), s) == r->seeds.end())
          r->seeds.push_back(s);
    if (part < (1ull << 24)) part <<= 1;
  }
}

}  // namespace

int main(int argc, char** argv) {
  std::string excel, out, from, wants = "pmbes", levels = "99", qualities = "3,4,5,6,7,8", flags = "0", classes;
  int difficulty = 2, type = 3, ladder = 0, version = 101, threads = 0, part = 100, imbue = 1, ethereal = 1;
  int as_asked = 1;
  size_t keep = 8;
  for (int i = 1; i + 1 < argc; i += 2) {
    const std::string name = argv[i], value = argv[i + 1];
    if (name == "--excel") excel = value;
    else if (name == "--out") out = value;
    else if (name == "--wants") wants = value;
    else if (name == "--levels") levels = value;
    else if (name == "--qualities") qualities = value;
    else if (name == "--flags") flags = value;
    else if (name == "--classes") classes = value;
    else if (name == "--from") from = value;
    else if (name == "--part") part = std::atoi(value.c_str());
    else if (name == "--imbue") imbue = std::atoi(value.c_str());
    else if (name == "--ethereal") ethereal = std::atoi(value.c_str());
    else if (name == "--asked") as_asked = std::atoi(value.c_str());
    else if (name == "--difficulty") difficulty = std::atoi(value.c_str());
    else if (name == "--type") type = std::atoi(value.c_str());
    else if (name == "--ladder") ladder = std::atoi(value.c_str());
    else if (name == "--version") version = std::atoi(value.c_str());
    else if (name == "--threads") threads = std::atoi(value.c_str());
    else if (name == "--keep") keep = static_cast<size_t>(std::atoi(value.c_str()));
    else {
      std::fprintf(stderr, "unknown option %s\n", name.c_str());
      return 2;
    }
  }
  if (excel.empty() || out.empty()) {
    std::fprintf(stderr, "usage: seedfinder --excel <folder> --out <file> [options] (see tools/seedfinder.cpp)\n");
    return 2;
  }
  if (threads < 1) threads = std::max(1, static_cast<int>(std::thread::hardware_concurrency()) - 2);
  keep = std::clamp<size_t>(keep, 1, kMostSeeds);
  Tables t;
  if (!tables::load(excel, &t)) {
    std::fprintf(stderr, "the tables in %s could not be read\n", excel.c_str());
    return 1;
  }
  const uint64_t until = part >= 100 ? kAllSeeds : kAllSeeds / 100 * static_cast<uint64_t>(std::max(part, 1));
  std::map<Key, Record> records;
  read_records(out, &records);
  const size_t kept = records.size();
  if (!from.empty() && !read_records(from, &records)) std::fprintf(stderr, "%s was not read\n", from.c_str());
  const std::vector<std::string> codes = list_of(classes);
  const std::vector<int> level_list = numbers_of(levels), quality_list = numbers_of(qualities),
                         flag_list = numbers_of(flags);
  size_t looked_for = 0, with_seeds = 0, none = 0, skipped = 0, unsaved = 0;
  const auto begin = std::chrono::steady_clock::now();
  // A kind's walk, and a line of what was found.
  const auto look = [&](const char* code, const Request& r, const Search& search) {
    const auto plan = make_plan(t, r);
    if (!plan) return;
    const Key key{fingerprint(*plan), search.wants.bits(), 0};
    auto found = records.find(key);
    if (found != records.end() && (found->second.seeds.size() >= keep || found->second.done >= until)) {
      ++skipped;
      return;
    }
    // A kind every seed gives everything has nothing to choose; one the model follows no seed of nothing to find.
    int followed = 0, all = 0;
    uint32_t x = static_cast<uint32_t>(key.print) ^ 0x2545F491u;
    for (int i = 0; i < 64; ++i) {
      x = x * 1664525u + 1013904223u;
      const Outcome o = simulate(*plan, search, x);
      if (o.followed) ++followed;
      if (o.followed && o.all) ++all;
    }
    if (!followed || all == 64) return;
    if (found == records.end()) {
      Record fresh;
      fresh.base = static_cast<uint32_t>(key.print ^ (key.print >> 32));
      found = records.emplace(key, fresh).first;
    }
    Record& record = found->second;
    // The seeds read are looked at again: the file may be from other tables.
    record.seeds.erase(std::remove_if(record.seeds.begin(), record.seeds.end(),
                                      [&](uint32_t seed) {
                                        const Outcome o = simulate(*plan, search, seed);
                                        return !(o.followed && o.all);
                                      }),
                       record.seeds.end());
    const auto start = std::chrono::steady_clock::now();
    walk(*plan, search, threads, keep, until, &record);
    const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    ++looked_for;
    if (!record.seeds.empty()) ++with_seeds;
    else if (record.done >= kAllSeeds) ++none;
    std::printf("%-4s class %3u quality %d level %2d flags 0x%02X wants %-5s: %s (%zu seeds, %.0f%% of the seeds "
                "looked at, %.1f s)\n",
                code, r.item_class, r.quality, r.ilvl, r.flags, wants_text(key.wants).c_str(),
                !record.seeds.empty() ? "seeds" : record.done >= kAllSeeds ? "NONE" : "none yet", record.seeds.size(),
                100.0 * static_cast<double>(record.done) / static_cast<double>(kAllSeeds), sec);
    std::fflush(stdout);
    if (++unsaved >= 50 || sec > 5) {
      unsaved = 0;
      if (!write_records(out, records)) std::fprintf(stderr, "%s could not be written\n", out.c_str());
    }
  };
  for (const std::string& want : list_of(wants)) {
    Search search;
    search.wants = wants_of(want);
    for (const int flag : flag_list)
      for (const int level : level_list)
        for (const int quality : quality_list)
          for (uint32_t c = 0; c < t.items_count; ++c) {
            char code[5] = {};
            std::memcpy(code, t.items.data() + static_cast<size_t>(c) * kItemsRow + 0x80, 4);
            for (int k = 3; k >= 0 && (code[k] == ' ' || code[k] == 0); --k) code[k] = 0;
            if (!code[0]) continue;
            if (!codes.empty() && std::find(codes.begin(), codes.end(), code) == codes.end()) continue;
            Request r;
            r.item_class = c;
            r.ilvl = level;
            r.quality = quality;
            r.flags = static_cast<uint32_t>(flag);
            r.version = static_cast<uint16_t>(version);
            r.difficulty = static_cast<uint8_t>(difficulty);
            r.game_bank = 3;
            r.game_type = static_cast<uint8_t>(type);
            r.game_ladder = ladder;
            const auto asked = make_plan(t, r);
            if (!asked) continue;
            // The ways the game makes the item: as asked; a rare that drops as an imbue makes it; with ethereal
            // wanted, an item that can be ethereal asked for as ethereal for sure.
            if (as_asked) look(code, r, search);
            const bool can_be_ethereal = ethereal_base(*asked);
            bool imbue_ethereal = false;
            if (imbue && r.flags == 0 && quality == kImbueQuality && level >= kImbueLeastLevel &&
                can_be_imbued(t, c)) {
              Request way = r;
              way.flags = imbue_flags(search.wants.ethereal && can_be_ethereal);
              if (make_plan(t, way)) {
                imbue_ethereal = (way.flags & kFlagEthereal) != 0;
                look(code, way, search);
              }
            }
            if (ethereal && search.wants.ethereal && can_be_ethereal && !imbue_ethereal && quality != 5 &&
                quality != 1 && !(r.flags & (kFlagNeverEthereal | kFlagEthereal))) {
              Request way = r;
              way.flags |= kFlagEthereal;
              look(code, way, search);
            }
          }
  }
  if (!write_records(out, records)) {
    std::fprintf(stderr, "%s could not be written\n", out.c_str());
    return 1;
  }
  const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
  std::printf("%zu kinds looked for in %.0f s: %zu with seeds, %zu that no seed gives everything; %zu known already; "
              "%zu kinds in %s (%zu before)\n",
              looked_for, sec, with_seeds, none, skipped, records.size(), out.c_str(), kept);
  return 0;
}

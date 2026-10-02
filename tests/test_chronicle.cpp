// The Chronicle's maps and lists as the game keeps them (src/chronicle.h): a map built here the way the game's insert
// (0x3D79D0) builds one - the id hashed with MurmurHash3's finalizer into a power of two of buckets, each a chain of
// entries {next, id, ...} - is read back entry for entry, and so are the game's lists of the entries there are (a
// pointer to a row each, the set items' tagged). Whatever does not read as such a map is refused.
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <vector>

#include "chronicle.h"

using namespace d2rcc;

static int g_failures = 0;
#define CHECK(cond)                                                     \
  do {                                                                  \
    if (!(cond)) {                                                      \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
      ++g_failures;                                                     \
    }                                                                   \
  } while (0)

// The game's memory, as far as these checks go: plain reads, and a set of addresses that fail as a guarded read does.
static std::set<uintptr_t> g_unreadable;
static bool read(uintptr_t at, uint64_t* out) {
  if (g_unreadable.count(at)) return false;
  std::memcpy(out, reinterpret_cast<const void*>(at), sizeof(*out));
  return true;
}

// An entry of a map, as the insert makes one: 0x18 bytes, the next first, the id at +8 (and again at +0xC), the
// kind, a value and the time after it.
struct Entry {
  Entry* next = nullptr;
  uint32_t id = 0, id_again = 0;
  uint8_t kind = 0;
  uint8_t pad = 0;
  uint16_t value = 0;
  uint32_t minutes = 0;
};
static_assert(sizeof(Entry) == 0x18, "an entry");

struct Map {
  uint64_t buckets = 0;
  Entry** array = nullptr;
  uint64_t size = 0;
  float load = 1.0f;
};

static uint64_t bucket_of(uint32_t id, uint64_t buckets) {
  uint64_t h = static_cast<uint64_t>(id) * 0xFF51AFD7ED558CCDull;
  h = (h >> 33 ^ h) * 0xC4CEB9FE1A85EC53ull;
  return (h ^ h >> 33) & (buckets - 1);
}

// A map of these ids, in `buckets` buckets.
struct Built {
  Map map;
  std::vector<Entry*> array;
  std::vector<Entry> entries;
  Built(const std::vector<uint32_t>& ids, uint64_t buckets) : array(buckets, nullptr), entries(ids.size()) {
    map.buckets = buckets;
    map.array = array.data();
    for (size_t i = 0; i < ids.size(); ++i) {
      Entry& e = entries[i];
      e.id = e.id_again = ids[i];
      e.kind = 1;
      e.value = 0xBF4;
      Entry*& head = array[bucket_of(ids[i], buckets)];
      e.next = head;
      head = &e;
      ++map.size;
    }
  }
  uintptr_t at() const { return reinterpret_cast<uintptr_t>(&map); }
};

int main() {
  const chronicle::MapLayout layout;  // the game's: the count first, the buckets at +8; an entry's next, its id at +8
  {
    // Ids as the game has them (a unique's row), some sharing a bucket.
    std::vector<uint32_t> ids;
    for (uint32_t id = 0; id < 403; id += 3) ids.push_back(id);
    ids.push_back(421);
    Built built(ids, 256);
    int longest = 0;
    for (Entry* head : built.array) {
      int n = 0;
      for (Entry* e = head; e; e = e->next) ++n;
      if (n > longest) longest = n;
    }
    dropodds::Rows got;
    const int n = chronicle::ids_of(built.at(), layout, read, &got);
    std::printf("a map of %zu ids in 256 buckets (the longest chain %d): %d read back\n", ids.size(), longest, n);
    CHECK(n == static_cast<int>(ids.size()));
    CHECK(got.count() == static_cast<int>(ids.size()));
    bool all = true;
    for (const uint32_t id : ids) all = all && got.has(id);
    CHECK(all);
    CHECK(!got.has(1) && !got.has(420));
    CHECK(longest > 1);
  }
  {
    // A map nothing was put in: no buckets yet.
    Map empty;
    dropodds::Rows got;
    CHECK(chronicle::ids_of(reinterpret_cast<uintptr_t>(&empty), layout, read, &got) == 0 && got.count() == 0);
  }
  {
    // Not a map: a bucket count that is no power of two, or one that is too large.
    Built odd({1, 2, 3}, 8);
    odd.map.buckets = 6;
    dropodds::Rows got;
    CHECK(chronicle::ids_of(odd.at(), layout, read, &got) == -1);
    odd.map.buckets = chronicle::kMostBuckets * 2;
    CHECK(chronicle::ids_of(odd.at(), layout, read, &got) == -1);
  }
  {
    // A chain that does not end, an id past the rows, memory that cannot be read.
    Built loop({5, 13, 21, 29, 37, 45, 53, 61, 69}, 4);  // more ids than buckets: a chain of two at least
    Entry* head = nullptr;
    for (Entry* h : loop.array)
      if (h && h->next) head = h;
    if (head) {
      Entry* last = head;
      while (last->next) last = last->next;
      last->next = head;
      dropodds::Rows got;
      CHECK(chronicle::ids_of(loop.at(), layout, read, &got) == -1);
      last->next = nullptr;
    }
    CHECK(head != nullptr);
    Built past({7, dropodds::Rows::kMost + 3}, 8);
    dropodds::Rows got;
    CHECK(chronicle::ids_of(past.at(), layout, read, &got) == -1);
    Built fine({7, 8, 9}, 8);
    g_unreadable.insert(reinterpret_cast<uintptr_t>(&fine.entries[1].id));
    CHECK(chronicle::ids_of(fine.at(), layout, read, &got) == -1);
    g_unreadable.clear();
    dropodds::Rows again;
    CHECK(chronicle::ids_of(fine.at(), layout, read, &again) == 3 && again.has(7) && again.has(8) && again.has(9));
  }
  {
    // The game's lists: the uniques' a pointer to a row each (its id the row's first 16 bits), the set items' 16
    // bytes an entry with a tag (0 a set item, 1 a set, whose own rows are another table's).
    struct Row {
      int16_t id;
      char rest[0x15A];
    };
    std::vector<Row> rows(40);
    for (size_t i = 0; i < rows.size(); ++i) rows[i].id = static_cast<int16_t>(i);
    std::vector<const Row*> uniques;
    for (size_t i = 0; i < rows.size(); i += 2) uniques.push_back(&rows[i]);
    dropodds::Rows got;
    CHECK(chronicle::list_ids(reinterpret_cast<uintptr_t>(uniques.data()), uniques.size(), 8, -1, read, &got) == 20);
    CHECK(got.count() == 20 && got.has(0) && got.has(38) && !got.has(1));
    struct Tagged {
      const void* row;
      uint64_t tag;
    };
    struct SetRow {
      int16_t id;
      char rest[0x15E];
    } set_rows[3] = {};
    set_rows[0].id = 900;  // a set's own row: not a set item's id at all
    std::vector<Tagged> sets = {{&set_rows[0], 1}, {&rows[3], 0}, {&rows[4], 0}, {&rows[5], 0}};
    dropodds::Rows set_items;
    CHECK(chronicle::list_ids(reinterpret_cast<uintptr_t>(sets.data()), sets.size(), 16, 8, read, &set_items) == 3);
    CHECK(set_items.count() == 3 && set_items.has(3) && set_items.has(5) && !set_items.has(900));
    dropodds::Rows none;
    CHECK(chronicle::list_ids(0, 0, 8, -1, read, &none) == 0);
    CHECK(chronicle::list_ids(reinterpret_cast<uintptr_t>(uniques.data()), uniques.size(), 4, -1, read, &none) == -1);
  }
  {
    // The rows' own arithmetic: what the Chronicle misses is its entries less what it has.
    dropodds::Rows entries, found;
    for (uint32_t r = 0; r < 100; ++r) entries.set(r);
    for (uint32_t r = 0; r < 100; r += 4) found.set(r);
    found.set(dropodds::Rows::kMost - 1);
    dropodds::Rows missing = entries;
    missing.without(found);
    CHECK(missing.count() == 75 && !missing.has(0) && missing.has(1) && !missing.has(dropodds::Rows::kMost - 1));
    CHECK(!missing.set(dropodds::Rows::kMost) && !missing.has(dropodds::Rows::kMost));
    CHECK(!(missing == entries));
    missing.clear();
    CHECK(missing.count() == 0);
  }
  if (g_failures) {
    std::printf("%d failure(s)\n", g_failures);
    return 1;
  }
  std::printf("test_chronicle: ok\n");
  return 0;
}

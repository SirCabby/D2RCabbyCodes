#pragma once

#include <cstddef>
#include <cstdint>

namespace d2rcc::game {
struct Unit;
}

// Infinite cube ingredients: a Horadric Cube recipe uses nothing up. What the game does with a transmute (the
// client's request 0x20: a first pass saves the characters, a second makes the products):
//   the product routine (hooked) takes every item out of the cube (the inventory's own move; the client is not told
//   yet), makes the products - a new item, or the ingredient itself changed in place for an upgrade, added sockets,
//   a repair, Clear Sockets - and for Clear Sockets frees what was in the sockets of the item it changes (a routine
//   of its own, hooked); then it tells the client that every item that was in the cube is gone, frees each
//   ingredient it used up (the free of an item that lies on no inventory and not on the ground, hooked), and puts
//   the products in the cube. A portal recipe (the Cow Level, Pandemonium, the red portal) makes its portal instead.
// Under the switch, for the local player, the frees are not made inside the product routine: the items are kept,
// socket contents taken out the way the routine's own "keep them" variant takes them out (3.3's Clear Sockets uses
// the other). When the routine is through, each goes back the way the game's quest reward giver hands a player an
// item: a free spot in the inventory grid, the inventory's move, the client told, a charm's stats counted; with no
// room, at the player's feet as the giver drops one; with no spot on the ground either, it is freed, as the game
// would have freed it. An item a recipe changes in place is its product and stays in the cube, changed: it is not
// copied. No game data is written.
namespace d2rcc::cube {

using game::Unit;

// The cube holds 12 items at most, and one of them at most 6 in its sockets.
constexpr int kMaxKept = 32;

enum Kind : uint8_t { kIngredient = 0, kSocketed, kKinds };

// What one transmute keeps, in the order the game gave each up. Given back ingredients first (the cube's order),
// then what was in the sockets.
class Kept {
 public:
  // False when the list is full or has the item already: the game then frees it as it would have.
  bool add(Unit* item, Kind kind) {
    if (!item || kind >= kKinds || count_ >= kMaxKept) return false;
    for (int i = 0; i < count_; ++i)
      if (items_[i] == item) return false;
    items_[count_] = item;
    kinds_[count_] = kind;
    ++count_;
    return true;
  }
  int size() const { return count_; }
  bool full() const { return count_ >= kMaxKept; }
  int count(Kind kind) const {
    int n = 0;
    for (int i = 0; i < count_; ++i) n += kinds_[i] == kind ? 1 : 0;
    return n;
  }
  // The order they are given back in: every ingredient, then every item that was in a socket, each kind in the
  // order it was kept. Returns how many were written.
  int order(Unit** out, Kind* kinds, int cap) const {
    int n = 0;
    for (int k = 0; k < kKinds; ++k)
      for (int i = 0; i < count_ && n < cap; ++i)
        if (kinds_[i] == k) {
          out[n] = items_[i];
          if (kinds) kinds[n] = kinds_[i];
          ++n;
        }
    return n;
  }
  void clear() { count_ = 0; }

 private:
  Unit* items_[kMaxKept] = {};
  Kind kinds_[kMaxKept] = {};
  int count_ = 0;
};

// The routines hooked: the product routine (game, player, the recipe, its ingredients as matched), and the two
// frees (game, item).
using ProductsFn = void(__fastcall*)(void* game, Unit* player, void* recipe, void* inputs) noexcept;
using FreeFn = void(__fastcall*)(void* game, Unit* item) noexcept;

// After sites::derive() and game::bind(): null when the switch can work, else why not.
const char* bind();
// hooks_game.cpp says which frees are hooked; without the socket contents' hook Clear Sockets destroys them.
void hooked(bool sockets);
bool sockets_kept();

// Server thread, the hook on the product routine: the original, then what it gave up handed back. `free_item` is
// the original item free, for an item there is no place for anywhere.
void products(ProductsFn original, FreeFn free_item, void* game, Unit* player, void* recipe, void* inputs);
// The hook on the item free: true when the item was kept (the free is not made). `from` is the call's return
// address: only the product routine's own call is taken, inside a transmute the switch was on for.
bool keep_ingredient(Unit* item, uintptr_t from);
// The hook on the socket contents' free, before the original: inside such a transmute, for the product routine's
// own call, what is in the item's sockets is taken out and kept; the original then frees whatever is left in them
// (nothing, unless the list ran out of room). One taken out that cannot be kept is freed with `free_item`.
void keep_socketed(void* game, Unit* item, uintptr_t from, FreeFn free_item);

// For the console (any thread).
struct Stats {
  unsigned transmutes = 0;  // this session: transmutes under the switch that kept anything
  unsigned kept[kKinds] = {};
  unsigned inventory = 0;   // ... given back into the inventory
  unsigned ground = 0;      // ... dropped at the player's feet
  unsigned lost = 0;        // ... with no place anywhere, freed as the game would have
};
Stats stats();

}  // namespace d2rcc::cube

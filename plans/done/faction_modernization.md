## STATUS (completed 2026-07-04)
**100% DONE.** All 4 items addressed: (1) `MAX_FAC_NAME_SIZE` deleted, (2) `epilogue_data` migrated to `std::set<faction_epilogue>` with named struct, (3) all `faction_template` members encapsulated with `_` suffix and getters/setters (~50 sites across 18 files), (4) `enum class` skipped per plan recommendation. Build verified green on Windows MSVC.

# Faction Cleanup — Plan

> Scope rewritten 2026-06-23 after review. The prior version described a
> `struct faction { std::string name; int likes_u; ... }` that does not exist
> (those fields live in the **`faction_template` base class**, `faction.h:77-91`;
> `faction : public faction_template`, `:94`), fabricated the relationship enum
> (`relationship_fear/mate/kill`), proposed `operator<=>=default` that cannot
> compile (a member is `snippet_id`, a `string_id`, which has `operator<`/`==`
> but **no `operator<=>`** — `string_id.h`), and understated the blast radius.
> Corrected below. This is pure code hygiene — **no perf benefit** (`epilogue()`
> runs once at game-over, `faction_display`/`faction_info_text` are UI-only).

## Ordered by value (do the cheap/safe ones first; encapsulation is the churn)

### 1. Delete dead `MAX_FAC_NAME_SIZE` (trivial, safe)

`faction.h:20` `static constexpr int MAX_FAC_NAME_SIZE = 40;` has **0 callers**
(verified `grep -rn MAX_FAC_NAME_SIZE src/` → only its own definition). `name` is
already a `std::string`. Delete the line and its `// TODO: Redefine?` comment.
There is no char-array to migrate — the prior plan invented that.

Verify: `rg MAX_FAC_NAME_SIZE src/` → 0.

### 2. Replace the epilogue tuple with a named struct (contained, safe)

`faction.h:91`: `std::set<std::tuple<int, int, snippet_id>> epilogue_data;`
The fields are `power_min`, `power_max`, `snippet_id` (from the loader at
`faction.cpp:126`). All readers are **contained in `faction.cpp`** (`:80`, `:126`,
`:141-143` use `std::get<0/1/2>`, `:443` copies the whole set) — so this change
touches one file.

```cpp
struct faction_epilogue {
    int power_min;
    int power_max;
    snippet_id id;
    // snippet_id (string_id) has operator< but NO operator<=> — hand-write,
    // do NOT use = default, and keep the same ordering std::set relied on.
    bool operator<( const faction_epilogue &rhs ) const {
        return std::tie( power_min, power_max, id ) < std::tie( rhs.power_min, rhs.power_max, rhs.id );
    }
};
```

Update `faction.cpp:80,126,141-143,443` to named fields. `std::set` keeps working
via the hand-written `operator<`.

Verify: `rg "std::get<.*epilogue" src/` → 0. Build green.

### 3. Encapsulate `faction_template` data members (the churn — scope honestly)

The `// TODO: make private`-worthy fields are on **`faction_template`**
(`faction.h:77-91`), not `faction`. Encapsulating means:
- Add private `name_`/`likes_u_`/… + public getters/setters **on
  `faction_template`** (the base), and update its member-initializer-list
  constructor at `faction.cpp:105-130`.
- Update all external access. Reality of the blast radius (do not under-promise):
  ~50 sites across `crafting.cpp, npctalk.cpp, npctalk_funcs.cpp, avatar.cpp,
  npc.cpp, activity_actor.cpp, consumption.cpp, pickup.cpp,
  activity_item_handling.cpp, inventory.cpp`, **including 12 compound-assignment
  sites** (`my_fac->likes_u += …` `npctalk_funcs.cpp:123`; `fac->likes_u -= 1`
  `avatar.cpp:1557`; `my_fac->likes_u = std::max(0, my_fac->likes_u/2+10)`
  `npc.cpp:1515`) that each become `set_likes_u( likes_u() + v )`, plus
  serialize/deserialize in `savegame_json.cpp:3601-3608` and the direct writes
  `faction.cpp:419 fac.name = name_new`, `:440 elem.second.name = …`.

Constraints to respect:
- `faction` is a Lua usertype (`catalua_bindings_type_defs.cpp:25`, `reg_id<faction>`/
  `SET_MEMB`). It currently does **not** expose these data members to Lua, so
  encapsulation won't break bindings — but **confirm** no `SET_MEMB(faction, name)`
  etc. exists before/after (keep it a verification item).
- The codebase authors already flagged this inheritance as painful
  (`catalua_bindings_type_defs.cpp:29-30`). Encapsulating the *base* is the right
  layer; do not try to flatten the inheritance in this pass.

Verify: build green; load an existing save and confirm faction reputation values
match (savegame round-trip). The prior plan's
`rg "faction\.(name|likes_u|…)"` check is **useless** — access is via `fac->`,
`my_fac->`, `get_owner()->`, so that regex returns 0 today regardless of work
done. Use the savegame round-trip + compile errors as the real gate.

### 4. `enum relationship` → `enum class` — SKIP unless you want the cast churn

`npc_factions::relationship` (`faction.h:37-47`) is a 7-entry enum used as a
**bitset index**: `std::bitset<npc_factions::rel_types> relations` (`:89`) and
`.test(npc_factions::kill_on_sight)` (`npc.cpp:2269/2271`, `npcmove.cpp:528`).
Converting to `enum class` forces `static_cast<size_t>()` at every bitset
index/size site and at `relation_strs` (`:49`, string→enum) and the savegame
serialize (`savegame_json.cpp:3608`). The type-safety gain is marginal and it
fights the bitset usage. **Recommend skipping** — low value, pure cast noise. If
done anyway, add a `static_cast` helper and convert every `.test()`/`std::bitset<…>`
site; "rg `enum relationship` → 0" alone is not sufficient verification.

## Files

| File | Items |
|---|---|
| `src/faction.h` | 1 (delete const), 2 (struct), 3 (base getters/setters) |
| `src/faction.cpp` | 2 (readers), 3 (ctor init-list + direct writes) |
| ~10 caller files + `src/savegame_json.cpp` | 3 (access migration) |

## Effort
- Items 1+2: ~0.5 day, safe, do first.
- Item 3: 1–2 days (mechanical churn, ~50 sites, savegame round-trip risk).
- Item 4: skip.

# Vehicle continuous-movement program (stages A–F) — execution plan

## Context

`plans/vehicle-system-rework-assessment.md` concluded: the vehicle subsystem is **not** a rewrite
candidate, but grid-decoupled continuous vehicle movement and rotation **is** an adopted product
goal, reachable only as an ordered six-stage program (A registry/identity → B occupant absorption →
C per-turn cache membership → D single position authority → E continuous rendering → F converge).
This plan executes that program. Per-tile consequence dispatch (bash, creature collision, traps,
ramps, traction) is deliberately retained in every stage — deriving it from Box2D contact impulses
was already tried and rejected in-tree (`src/map_vehicle.cpp:733-749`) because the only validation
asset is ~345 frozen drag constants plus the per-prototype efficiency table.

End state, restated so "the vehicle becomes one entity" is not over-read: after F a driving vehicle
is one registry-owned object whose identity cannot dangle, owns its occupants' positions (committed
once per turn), publishes cache membership and invalidation once per turn over its swept footprint,
is driven by exactly one position authority, and is drawn at a continuous heading — while still
*querying* the tiles it sweeps to decide what it hit.

Baseline: HEAD `030cfc6576`, branch `feature/merge-dev-into-improvements`. `VEHICLE_CACHE_SIGSEGV_PLAN.md`
and `plans/vehicle-system-rework-assessment.md` are untracked working-tree files at that HEAD.

First action before any code edit: copy this plan to `plans/vehicle-continuous-program.md` (repo
convention — `plans/` is the permanent record that survives session resets) and delete the now-subsumed
`VEHICLE_CACHE_SIGSEGV_PLAN.md` from the repo root, since stage A0 *is* its step 1 and Gate A item 1
*is* its steps 2-3.

## Working agreements (apply to every stage)

- **Build** (never synchronously, never with a short timeout — a killed ninja corrupts `.ninja_deps`):
  `cmake --build --preset osx-arm-slim --target cataclysm-bn-tiles cata_test-tiles` started as a
  background job with `timeout: 1800`, polled to completion.
- **Test binary is `./cata_test-tiles` at the repo root**, not `out/build/osx-arm-slim/tests/...`
  (that path is a stale months-old leftover on this preset). `stat` its mtime against the build
  before trusting any result.
- **Do not run `cmake --build --target format`.** This tree's astyle/clang-format versions
  mass-reformat unrelated files. Match the enclosing file's existing style by hand: `src/map_vehicle.cpp`,
  `src/cata_tiles*.cpp` use `vehicle* veh` / `if (x) {` (clang-format style); `src/map.cpp`,
  `src/vehicle*.cpp`, `src/mapbuffer.cpp` use `vehicle *veh` / `if( x ) {` (astyle style).
- **Accepted test baseline — 5 failures** (`plans/merge-main-into-improvements.md:24`):
  `flung creatures stop at the reality bubble edge`, `vision_wall_obstructs_light`,
  `vision_single_tile_skylight`, `vision_see_out_of_vehicle`, `vision_see_into_vehicle`.
  Any other failure is a regression. Runs must be serial (shared `./test_user_dir/`) and seeded:
  `--order decl --rng-seed 1`.
- **Never re-baseline `tests/vehicle_drag_test.cpp` or `tests/vehicle_efficiency_test.cpp` constants.**
  The drag test prints replacement rows on mismatch (`tests/vehicle_drag_test.cpp:131-134`); doing
  that once already concealed a real regression (`plans/box2d-vehicle-physics-implementation.md:235-237`).
  A changed constant means the stage broke physics, not that the table is stale.
- **One commit per numbered step** where the step compiles on its own; Conventional Commits, no body
  unless critical.
- Each stage ends at its **Gate**. Do not start the next stage before its gate passes.

## Program order and dependencies

`A → B → C → D` is a hard chain; `E` runs in parallel with all of them and blocks only `F`:

- **B needs A**: the creature→vehicle back-reference must be a generation-checked handle, not a
  fourteenth raw `vehicle *`.
- **C needs B**: the churn measurement is only meaningful once the occupant work has left the
  per-tile path, and C's own gate is expressed in `on_vehicle_moved` calls per turn.
- **D needs B and C**: D converts the rails/ramp assertions onto committed-anchor semantics, which is
  what B (occupant commit) and C (one invalidation per turn) establish.
- **E is independent of A–D** (renderer only) but F needs it landed or explicitly deferred.
- **A0 subsumes `VEHICLE_CACHE_SIGSEGV_PLAN.md`**: that document's step 1 is A0 here, and the rest of
  it (its own steps 2-3 and contingency 3a) is Gate A item 1. Do not execute it as a separate plan.

Every stage leaves the tree buildable and the existing suite green at its gate. Within a stage the
numbered steps are ordered; where a step changes a signature the same commit updates every caller.

## Stage A — vehicle identity that cannot dangle

### A0. Stop-gap: purge per-z vehicle lists in `map::load()`

`map::load()` (`src/map.cpp:1544-1567`) re-anchors the entire bubble but never clears
`level_cache::vehicle_list`/`zone_vehicles`; `loadn()` only ever inserts into them
(`src/map.cpp:2208-2218`), and the trailing `reset_vehicle_cache()` (`:1564`) then re-seeds
`veh_cached_parts` from orphaned pointers. `map::shift()` already does it correctly
(`src/map.cpp:1884-1895`: `clear_vehicle_cache()`, then `clear_vehicle_list(gridz)` for every z
inside the `std::views::iota( -OVERMAP_DEPTH, OVERMAP_HEIGHT + 1 )` loop).

Insert into `map::load()`, between `clear_submap_cache();` (`:1554`) and `funnel_locations_.clear();`
(`:1555`):

```cpp
    // Every z-level's vehicle_list/zone_vehicles is cleared here (mirroring map::shift()'s
    // pre-shift clear) so the loadn() calls below repopulate it from only the submaps that
    // remain resident in the new bubble, before reset_vehicle_cache() rebuilds
    // veh_cached_parts/cached_veh_rope from it. Without this, entries for submaps no longer
    // in the reloaded bubble survive the rebuild and dangle once MAPBUFFER evicts them.
    for( const auto z : std::views::iota( -OVERMAP_DEPTH, OVERMAP_HEIGHT + 1 ) ) {
        clear_vehicle_list( z );
    }
```

No new include (`map.cpp:1894` already uses that `iota` form) and no `src/map.h` change. Reuse the
existing `map::clear_vehicle_list(int)` (`src/map_vehicle.cpp:287-294`); write no new clearing logic.
This is a standalone commit and its own gate (A0 gate below), landed before A1 begins.

### A1. `vehicle_handle` + identity registry (new files)

Create `src/vehicle_handle.h` and `src/vehicle_handle.cpp`. The header must not include
`vehicle.h` (forward-declare `class vehicle;`) so every index holder can include it cheaply.

```cpp
/// Generation-checked reference to a vehicle. Cheap to copy and safe to store:
/// resolving a handle whose vehicle has been freed yields nullptr instead of a
/// dangling pointer. generation == 0 is the never-valid sentinel.
struct vehicle_handle {
    std::uint32_t slot = 0;
    std::uint32_t generation = 0;

    auto operator<=>( const vehicle_handle & ) const = default; // *NOPAD*
    auto is_set() const -> bool { return generation != 0; }
};

/// Process-wide slot table mapping handles to live vehicles. A vehicle enters in
/// its constructor and leaves in its destructor, so every free path — including
/// MAPBUFFER submap eviction — invalidates every handle to it.
class vehicle_registry
{
    public:
        static auto get() -> vehicle_registry &; // *NOPAD*

        auto attach( vehicle &veh ) -> vehicle_handle;
        auto detach( vehicle_handle handle ) -> void;
        auto resolve( vehicle_handle handle ) const -> vehicle *; // *NOPAD*
        auto live_count() const -> std::size_t;

    private:
        struct slot_entry {
            vehicle *veh = nullptr;
            std::uint32_t generation = 0;
        };
        std::vector<slot_entry> slots_;
        std::vector<std::uint32_t> free_slots_;
};

/// Convenience wrapper used by the index resolvers.
inline auto resolve_vehicle( vehicle_handle handle ) -> vehicle * // *NOPAD*
{
    return vehicle_registry::get().resolve( handle );
}

template<>
struct std::hash<vehicle_handle> {
    auto operator()( const vehicle_handle &h ) const noexcept -> std::size_t {
        return ( static_cast<std::size_t>( h.generation ) << 32 ) ^ h.slot;
    }
};
```

Semantics, fixed here so they are not re-decided: generations start at 1 and increment on `detach`
(so a reused slot never matches an old handle, and `generation == 0` means "never set"); `attach`
pops `free_slots_` or appends; `resolve` returns `nullptr` when `handle.slot >= slots_.size()`,
when the slot's generation differs, or when `handle.generation == 0`; `detach` on an already-stale
handle is a silent no-op (double-free of a slot must not corrupt a live vehicle's slot);
`live_count()` exists for the stage-F metric and the A5 test. Resolution is an index plus an integer
compare — no allocation, no hashing — because it lands on `map::veh_at_internal`, which the source
marks "called A LOT" (`src/map_vehicle.cpp:1490`).

Do **not** reuse `src/safe_reference.h` for this: its invalidation is driven by `cata_arena<T>`
hooks and it carries id/JSON-persistence machinery, while vehicles are plain `unique_ptr`s in
`submap::vehicles`. Recorded so it is not re-litigated. `safe_reference` is instantiated for `item`
only today (`src/safe_reference.h:70-71`, driven by `src/cata_arena.h:28`).

Why a *generation* and not just "check it is still in a live set": every current index is keyed by
address, so after a free the allocator can hand the same address to the next vehicle and every stale
key silently starts pointing at a different, live vehicle (ABA). `mapbuffer::indexed_vehicle_part_at_unlocked`
(`src/mapbuffer.cpp:1327-1366`) defends against this today only by re-deriving the answer
(`loaded_vehicles_.contains`, part-index range, `abs_part_location(part) == p`) on every read — a
hand-maintained check the generation compare replaces with one integer comparison.
Add both files to the build the same way sibling `src/*.cpp` files are picked up — `src/CMakeLists.txt`
globs sources with `CONFIGURE_DEPENDS`, so no CMake edit is needed; confirm by seeing the new TU
compile.

### A2. RAII registration on `vehicle`

`src/vehicle.h`: include `vehicle_handle.h`, add a private member and a public accessor next to the
constructors (`src/vehicle.h:473-476`):

```cpp
        /// Stable identity for every index that used to store a raw vehicle*.
        auto handle() const -> vehicle_handle { return self_handle_; }
```
with `vehicle_handle self_handle_;` in the private data section.

`src/vehicle.cpp`: in the `vproto_id` constructor (`src/vehicle.cpp:426-465`) add
`self_handle_ = vehicle_registry::get().attach( *this );` as the **last** statement of the body, and
replace `vehicle::~vehicle() = default;` (`:473`) with a body calling
`vehicle_registry::get().detach( self_handle_ );`. The default constructor delegates
(`vehicle::vehicle() : vehicle( vproto_id() )`, `:467`), so one attach site covers both. `vehicle` is
non-copyable and non-movable (`src/vehicle.h:480-483`), so no handle-fixup on copy/move is possible
or needed.

### A3. `map` becomes the notification choke point

The assessment's stage A asked the *registry* to fan notifications out. It must not: the registry
sits below `map` in the include graph (`vehicle.cpp` uses it), and `map` already reaches every
dependent index (`get_mapbuffer()`, `get_physics_world()`, `get_overmapbuffer()`). So the registry
owns identity only, and the choke point is three new **public** `map` methods, declared beside the
existing cache helpers in `src/map.h:1149-1155`:

```cpp
        /// The only sanctioned way to make a vehicle visible to the world's indices.
        void register_vehicle( vehicle &veh );
        /// The full removal sequence, authored from the union of map::loadn's purge and
        /// map::detach_vehicle — neither is complete today (see below).
        void unregister_vehicle( vehicle &veh );
        /// Parts added, removed, merged, split or re-anchored: footprint-derived indices only.
        void vehicle_footprint_changed( vehicle &veh );
```

Implement them in `src/map_vehicle.cpp`:

- `register_vehicle`: insert the handle into `get_cache(z).vehicle_list` (and `zone_vehicles` when
  `!veh.loot_zones.empty()`), run the existing per-part cache seeding (the current body of
  `add_vehicle_to_cache`, `src/map_vehicle.cpp:202-237`), call `get_mapbuffer().register_vehicle( &veh )`,
  and `if( auto *pw = get_physics_world(); pw ) { pw->on_vehicle_added( &veh ); }`.
- `unregister_vehicle`: the union of the two existing removal paths, **both of which are
  incomplete** (confirmed by reading them this session): `map::loadn`'s no-part purge
  (`src/map.cpp:2196-2204`) calls `reset_vehicle_cache()` → `get_mapbuffer().unregister_vehicle` →
  overmap `remove_vehicle` → `dirty_vehicle_list.erase` → `veh_vec.erase( iter )`, but never erases
  `ch.vehicle_list`/`zone_vehicles`, and it runs `reset_vehicle_cache()` *before* the `unique_ptr`
  dies at `:2203`, so the cache is re-seeded from a pointer freed one line later;
  `map::detach_vehicle` (`src/map_vehicle.cpp:376-391`) does erase those two sets but only for
  `z = veh->abs_sm_pos.z()` (the E15 gap) and has two early-return branches that clean up almost
  nothing (`:368-375`, `:393-395`). `unregister_vehicle` must therefore do, in this order: physics
  `on_vehicle_removed`, per-part cache clear for **every** z the vehicle occupies,
  `vehicle_list`/`zone_vehicles` erase on every z, `cached_veh_rope` column purge,
  `get_mapbuffer().unregister_vehicle`, overmap `remove_vehicle` when `tracking_on`,
  `dirty_vehicle_list.erase`, `unboard` every occupant (as `detach_vehicle` does at
  `src/map_vehicle.cpp:355-358`), and the footprint-dirty marking
  (`on_vehicle_moved` over the union of occupied submaps per z, as at `:347-352`) — **all of it
  before the caller destroys the object**, and with no early-return branch that skips a subset.
- `vehicle_footprint_changed`: per-part cache re-seed + `mapbuffer` footprint reindex
  (`refresh_vehicle_footprint`) + `pw->on_vehicle_parts_changed( &veh )`.

Then convert every mutator that currently picks a subset. Each of these call sites is replaced by
one of the three methods; the enumeration is the cutover list, produced this session by
`grep -n 'add_vehicle_to_cache|update_vehicle_list|clear_vehicle_list|clear_vehicle_cache|clear_vehicle_point_from_cache|reset_vehicle_cache|dirty_vehicle_list' src tests`:

|Site|Path|Becomes|
|---|---|---|
|`map::detach_vehicle` per-z-only erase|`src/map_vehicle.cpp:376-391`|`unregister_vehicle`|
|`map::detach_vehicle` submap-not-found early return|`src/map_vehicle.cpp:368-375`|`unregister_vehicle` (same sequence, no partial path)|
|`map::loadn` no-part purge|`src/map.cpp:2196-2204`|`unregister_vehicle`|
|`map::loadn` registration|`src/map.cpp:2208-2218`|`register_vehicle`|
|`map::clear_vehicle_list` external callers|`src/map.cpp:1895`, `:2861`, `src/game_setup.cpp:1691-1693`, `src/editmap.cpp:1527`, `:1699`, `src/overmap.cpp:6217`, `:6244`, `:6261`, `:6374`|keep `clear_vehicle_list` (bulk bubble-teardown, not a per-vehicle mutation) but make it also clear `veh_cached_parts`/`veh_exists_at`/`cached_veh_rope` for that z so the four indices can no longer disagree|
|`vehicle::part_removal_cleanup`|`src/vehicle_parts.cpp:893`, cache calls at `src/vehicle_parts.cpp:754-758`, `:903-921`|`vehicle_footprint_changed`|
|`vehicle::merge_rackable_vehicle`|`src/vehicle_parts.cpp:605`|`vehicle_footprint_changed`|
|`vehicle::split_vehicles`|`src/vehicle_parts.cpp:1166-1345` (insert at `:1305-1308`)|`register_vehicle` for the new vehicle + `vehicle_footprint_changed` for the old|
|`map::add_vehicle_to_map` wreck fusion|`src/mapgen.cpp:6058-6062`, recursive returns at `:6138`, `:6145`|`register_vehicle`; the function returns a *different* `vehicle *` than handed in, so return `vehicle_handle` from `map::add_vehicle` instead and have callers resolve it|
|`vehicle_damage.cpp` full cache reset|`src/vehicle_damage.cpp:411-414`|`vehicle_footprint_changed`|
|`vehicle_move.cpp` cache add after collision|`src/vehicle_move.cpp:482`|`vehicle_footprint_changed`|
|`vehicle_part_handler.h` spawn/removal hooks|`src/vehicle_part_handler.h:71`, `:113-117`|`vehicle_footprint_changed`|
|`iuse_misc.cpp` unfolded vehicle|`src/iuse_misc.cpp:952`|`register_vehicle`|
|`construction.cpp`|`src/construction.cpp:2038`|`register_vehicle`|
|`iexamine_elevator.cpp`, `iuse` vehicle placement|`src/iexamine_elevator.cpp:215-219`|`register_vehicle`|
|`veh_interact_complete.cpp`|`src/veh_interact_complete.cpp:219-221`, `:351-362`|`register_vehicle` / `vehicle_footprint_changed`|
|`editmap::cleartmpmap` hand-clearing all four indices|`src/editmap.cpp:1525-1540`|keep, now a single `clear_vehicle_list(z)` per z (A3 made it clear all four)|
|tests calling `add_vehicle_to_cache` directly|`tests/map_test.cpp:573`, `tests/monster_test.cpp:572`, `:628`, `:645`, `:749`, `tests/npc_test.cpp:396`, `tests/rot_test.cpp:59`, `tests/vehicle_collision_test.cpp:138`, `tests/vehicle_ladder_test.cpp:36`, `tests/vehicle_test.cpp:288`|`here.register_vehicle( *veh )`|
|`map::on_submap_unloaded` ad-hoc vehicle purge (its own comment describes the hazard: "a stale entry that survives silently until something reads it … segfaults on a freed vehicle far from the actual leak")|`src/map.cpp:717-749`|`unregister_vehicle` per evicted vehicle|
|`map::add_vehicle` (definition lives in mapgen, not map_vehicle)|`src/mapgen.cpp:5997`|`register_vehicle`|
|`vehicle::install_part( dp, vehicle_part && )` — the one overload that notifies physics today (`src/vehicle_parts.cpp:566-568`); the other two delegate to it (`:477`, `:487`)|`src/vehicle_parts.cpp:498`|`vehicle_footprint_changed` (drop the direct `on_vehicle_parts_changed` call)|
|`tests/map_helpers.cpp::clear_vehicles`|`tests/map_helpers.cpp:30-37`|unchanged (`destroy_vehicle` per vehicle) — but it becomes the A5 tests' teardown proof|

`map::add_vehicle_to_cache` and `map::clear_vehicle_point_from_cache` become **private** members of
`map` after this conversion (their only remaining callers are the three choke-point methods and
`reset_vehicle_cache`), so a future mutator cannot re-open a partial path. `map::reset_vehicle_cache`
stays, but rebuilds from handles (A4).

### A4. Handle-valued indices

Replace the stored `vehicle *` with `vehicle_handle` in, and resolve at read:

|Index|Declaration|New type|
|---|---|---|
|`level_cache::veh_cached_parts`|`src/map.h:492`|`std::map<tripoint_bub_ms, std::pair<vehicle_handle, int>>`|
|`level_cache::vehicle_list`|`src/map.h:493`|`std::set<vehicle_handle>`|
|`level_cache::zone_vehicles`|`src/map.h:494`|`std::set<vehicle_handle>`|
|`map::dirty_vehicle_list`|`src/map.h:1994`|`std::set<vehicle_handle>`|
|`map::cached_veh_rope`|`src/map.h:2453`|`std::map<tripoint_bub_ms, std::pair<vehicle_handle, int>>`|
|`mapbuffer::loaded_vehicles_`|`src/mapbuffer.h:916`|`std::set<vehicle_handle>`|
|`mapbuffer::vehicle_footprint_locations_`|`src/mapbuffer.h:919-920`|`std::unordered_map<vehicle_handle, std::vector<tripoint_abs_ms>>` (add a `std::hash<vehicle_handle>` specialization in `vehicle_handle.h`)|
|`mapbuffer::vehicle_footprint_by_location_` entries|`src/mapbuffer.h:917-918`, POD at `:762-765`|handle-valued vehicle field|
|`coop_server::vehicle_id_map_` / `_rev_`|`src/coop_server.h:272-273`|drop both; `vid` becomes `vehicle_handle::slot` and lookup becomes `resolve_vehicle( { .slot = vid, .generation = gen } )` with the generation carried in the packet alongside `vid`; `register_vehicle_for_test` (`src/coop_server.h:127-132`) returns `veh->handle().slot`|
|`coop_client::coop_vehicle_map_` / `_inv_`|`src/coop_client.h:114-115`|same handle-keyed treatment; the map is read-only today (`src/coop_client.cpp:283-284`, no writer in `src/`) so this is a type change with no behaviour change|
|`map::last_full_vehicle_list` (`VehicleList` of `wrapped_vehicle`)|`src/map.h:2451`|unchanged — it is rebuilt per read from `vehicle_list` and never outlives a turn; leaving `vehicle *` here avoids touching its ~30 consumers|

**Not a pointer holder, leave alone:** overmap vehicle tracking stores `om_vehicle` **by value**
keyed on `vehicle::om_id` (`src/overmap.h:85-88`, `:436`); `overmapbuffer::remove_vehicle`
(`src/overmapbuffer.cpp:790`) only reads that id. `PhysicsWorld::vehicle_bodies_`
(`src/physics/physics_world.h:188`) and `authority_revoked_by_unload_` (`:196`) stay pointer-keyed:
their entries are created and destroyed by `on_vehicle_added` (`src/physics/physics_world.cpp:110`)
/ `on_vehicle_removed` (`:225`), which after A3 are reached only from the choke point, so they can
no longer outlive the object. Convert them only if Gate A's falsifier fires.

`map::veh_at_internal` (`src/map_vehicle.cpp:1489-1506`) becomes: bitset fast path unchanged; on a
`veh_cached_parts` hit, `resolve_vehicle( it->second.first )` — and when that returns `nullptr`,
**erase the entry, clear the `veh_exists_at` bit, set the z-level's vehicle cache dirty, and return
`nullptr`** instead of the current unchecked dereference. Self-healing, matching the pattern
`mapbuffer::indexed_vehicle_part_at_unlocked` already uses (`src/mapbuffer.cpp:1327-1366`). No
`debugmsg` on that path: a stale handle is now an expected, handled state.

`map::reset_vehicle_cache` (`src/map_vehicle.cpp:186-200`) resolves each handle in `vehicle_list` and
**drops** unresolvable ones instead of calling `elem->adjust_zlevel(...)` on them — that dereference
is the crash chain in `plans/vehicle-cache-sigsegv-handoff.md`.

`map::update_vehicle_list` (`src/map_vehicle.cpp:296-307`) inserts `elem->handle()`.

### A5. Regression tests (none exist today)

No test asserts consistency of `veh_cached_parts` / `vehicle_list` / `zone_vehicles` / the mapbuffer
footprint index. Add to `tests/vehicle_test.cpp`, following that file's existing vehicle-construction
idiom (`here.add_vehicle(...)` + `install_part` + `here.register_vehicle( *veh )` as at
`tests/vehicle_test.cpp:287-289`):

- `TEST_CASE( "vehicle_handle_invalidated_by_destroy", "[vehicle][cache]" )` — spawn a vehicle,
  record `veh->handle()`, `REQUIRE( here.veh_at( tile ) )` on one of its tiles, call
  `here.destroy_vehicle( veh )`, then `CHECK( !here.veh_at( tile ) )` and
  `CHECK( resolve_vehicle( recorded ) == nullptr )`.
- `TEST_CASE( "vehicle_handle_invalidated_by_bubble_reanchor", "[vehicle][cache]" )` — spawn a
  vehicle, record every tile it occupies and its handle, re-anchor with
  `here.load( point_abs_sm( … far from the vehicle … ), true )` (the path `clear_map()` uses), then
  `CHECK( !here.veh_at( t ) )` for every recorded tile and `CHECK( resolve_vehicle( recorded ) == nullptr )`
  — resolving, not dereferencing, so the test is meaningful even while the bug is live.

Both must fail on the pre-A2 tree (the first by returning a live-looking part, the second by
crashing or returning a stale part) and pass after A4.

### Gate A

1. `A0 gate` (after A0 only): rebuild, then
   `rm -rf /tmp/vehA0 && mkdir -p /tmp/vehA0 && timeout 120 ./cata_test-tiles "~[.]" --order decl --rng-seed 1 --shard-count 4 --shard-index 2 --user-dir=/tmp/vehA0`
   exits on its own (code 0/1, not 139/135 and not killed by `timeout`).
2. `A5` cases pass.
3. Unsharded `timeout 3600 ./cata_test-tiles "~[coop]" --order decl --rng-seed 1 --user-dir=/tmp/vehA_coop`
   completes without SIGSEGV (the historical crash was at accumulated case ~483).
4. All four `~[.]` shards: failures are a subset of the 5 accepted baseline failures.
5. `grep -rn 'vehicle \*' src/map.h src/mapbuffer.h` shows no vehicle-pointer index members left
   except `last_full_vehicle_list`'s `wrapped_vehicle`.

**Falsifier (pre-decided):** if a dangling-`vehicle*` crash still occurs after A from a holder the
handle cannot cover, the problem is ownership-shaped: implement O3 from the assessment — move the
`unique_ptr` out of `submap::vehicles` into a world-level store keyed by handle, keeping
`abs_sm_pos`/`sm_ms_pos` serialization where it is so the save format is unchanged (submap JSON emits
the vehicles whose anchor is in it) — **before** starting B.

## Stage B — the vehicle owns its occupants' position

Today every crossed tile teleports every occupant: `map::displace_vehicle` runs a retry loop over
`veh.get_riders()` (`src/map_vehicle.cpp:1637-1694` — `for (size_t i = 0; !complete && i < riders.size(); i++)`,
O(riders²) worst case) with a `g->critter_at( psgp )` probe per rider per pass (`:1680`), a
`psg->setpos( psgp )` per rider (`:1691`), and — for the avatar — `g->update_map( g->u )` (`:1796`)
and `g->vertical_shift( z_to )` (`:1801`) fired from *inside* the tile walk. Stage B makes the
vehicle the sole writer and commits once per turn.

### B1. Make pets data-identified

`vehicle::get_passenger` already resolves characters by id (`g->critter_by_id<player>( part.passenger_id )`,
`src/vehicle_query.cpp:1060-1072`). `vehicle::get_pet` resolves by asking what monster stands on the
part's tile (`g->critter_at<monster>( bub_part_location( p ), true )`, `:1074-1081`) — which breaks
the instant commits are deferred.

Add to `vehicle_part` beside `passenger_id` (`src/vehicle_part.h:272`):

```cpp
        /** Boarded animal, when animal_flag is set. Not serialized; rebuilt at load (B7). */
        weak_ptr_fast<monster> animal_ref;
```

and rewrite `get_pet` to `return animal_ref.lock().get();` — exactly the holder pattern
`creature_tracker` already uses for monsters (`weak_ptr_fast<monster>` with `lock().get()`,
`src/creature_tracker.h:26-33`). Monsters have no persistent id, only `temporary_id`, which is why
this must be a weak pointer and not an id. Set `animal_ref` wherever `animal_flag` is set today
(`grep -rn 'animal_flag' src` — the boarding path in `vehicle_part_handler.h` / `map_vehicle.cpp`
and the item/monster-capture paths) and clear it with the flag.

### B2. Creature → vehicle back-reference

In `src/creature.h`, public data beside the other position state:

```cpp
        /// Vehicle whose seat currently owns this creature's position, and the part
        /// index of that seat. boarded_part < 0 means "not owned by a vehicle".
        /// Never a raw vehicle* — that would be a fourteenth dangling holder.
        vehicle_handle boarded_vehicle;
        int boarded_part = -1;
```

Written in `map::board_vehicle` (`src/map_vehicle.cpp:1512-1543`, beside the existing
`passenger_flag` / `passenger_id` writes at `:1536-1537`) and on the animal-boarding path; cleared in
both `map::unboard_vehicle` overloads (`:1545-1558`, `:1560-1579`). Not serialized — rebuilt at load
by B7.

### B3. Single-writer enforcement

Keep `Creature::position` as an O(1) cache — `bub_pos()` has ~2919 callsites across 191 files, so a
computed accessor would put a handle resolve on the hottest read in the tree. Enforce ownership
instead:

- Add `bool committing_occupants = false;` to `vehicle` (set only inside `commit_occupants`).
- Add to `Creature` a protected helper, defined in `src/creature.cpp`:

```cpp
        /// While boarded, the owning vehicle is the only legitimate writer of this
        /// creature's position. Logs and drops ownership on any other write.
        auto check_position_write_owner() -> void;
```
  Body: return immediately when `boarded_part < 0`; resolve `boarded_vehicle` and return when the
  vehicle is null (it died — drop ownership silently) or `veh->committing_occupants` is true;
  otherwise `debugmsg( "position of %s written outside its vehicle's commit path", get_name() )`,
  then clear `boarded_part`/`boarded_vehicle`, set `in_vehicle = false`, and let the write proceed.
  Loud, not fatal — this is the failure class already pinned by
  `TEST_CASE("vehicle_collision_hits_occupant_with_stale_in_vehicle_flag")` in
  `tests/vehicle_collision_test.cpp`.
- Call it as the first statement of the two leaf writers every other overload funnels into:
  `Character::setpos( const tripoint_abs_ms & )` (`src/character.cpp:840`) and
  `monster::setpos( const tripoint_abs_ms & )` (`src/monster.cpp:466`). `Character::setpos(bub)`
  (`:838`), `monster::setpos(bub)` (`:464`), `player::setpos` (`src/player.cpp:87-100`) and
  `Creature::setpos(abs)` (`src/creature.cpp:2824-2827`) all delegate, so two call sites cover
  everything.

### B4. `vehicle::commit_occupants()`

```cpp
        /// Publish every occupant's tile from its seat. Called once per vehicle per
        /// turn from map::vehmove(); the only sanctioned writer of occupant position.
        auto commit_occupants() -> void;
```

Body (in `src/vehicle_move.cpp` beside the other occupant code):

1. Early-return when no part carries `passenger_flag` or `animal_flag`.
2. Set `committing_occupants = true` (reset on all exits).
3. For each occupied boardable part, derive the target tile exactly as `bub_part_location` does —
   `bub_ms_location() + tripoint_rel_ms( pt.precalc[0], pt.mount.z() + pt.z_terrain[0] )`
   (`src/vehicle_query.cpp:1098-1102`) — and, if it differs from the occupant's current
   `bub_pos()`, write it through the creature's normal `setpos`, so the `creature_tracker` index,
   the Box2D creature body and the co-op mutation log all still fire, once per turn instead of once
   per crossed tile.
4. Blocking resolution, in this order (replacing the deleted retry loop): if the target tile holds a
   creature that is not an occupant of this vehicle, displace it with the ray helper already used
   for this in `part_collision` (`calc_ray_end`, `src/vehicle_move.cpp:681-691`); if that fails,
   place the occupant on the nearest free tile of this vehicle's own footprint; if the footprint has
   none, unboard them to the nearest passable tile outside it. Every fallback logs via the existing
   `add_msg( m_debug, "Part/passenger position mismatch: …" )` text (`src/map_vehicle.cpp:1664-1669`)
   rather than silently no-opping.
5. Exactly once, after all occupants are placed: if the avatar's submap changed, `g->update_map( g->u )`;
   if its z changed, `g->vertical_shift_notify( old_z, new_z )` — the two effects
   `update_map_after_player_setpos` (`src/player.cpp:16-37`) triggers per write today.

Call site: `src/map_vehicle.cpp` line **834** — after the `if (phys_world)` readback block closes at
`:833` and before the `ZoneScopedN("veh_cleanup")` block at `:836`. Iterate a freshly fetched
`get_vehicles()` (the local list built at `:610` can contain entries wrecked earlier in the walk) and
call `commit_occupants()` on every live vehicle, not just authority-owned ones, so rail vehicles and
the `phys_world == nullptr` path are covered too. This ordering is correct for the turn:
`game::do_turn` runs `m.vehmove()` (`src/game.cpp:1109`) before `m.build_map_cache( get_levz(), true )`
(`:1152`) and before `monmove()` (`:1168`), so
committed occupant tiles are already in place when vision rebuilds and when monster AI picks targets
in the same turn.

### B5. Delete the per-tile rider loop

Remove `src/map_vehicle.cpp:1637-1694` entirely (the `get_riders()` retry loop, the per-rider
`g->critter_at( psgp )` probe and the `psg->setpos( psgp )`), plus the now-dead `need_update` /
`z_change` / `z_to` locals (`:1640-1642`) and their consumers: the `mark_vehicle_moved()` pre-call
guarded on `need_update` (`:1794`), `g->update_map( g->u )` (`:1796`) and `g->vertical_shift( z_to )`
(`:1801`). Keep the unconditional `mark_vehicle_moved()` (`:1825`), both `add_vehicle_to_cache`
calls (`:1797`, `:1803` — membership stays per tile) and the z-list maintenance (`:1805-1817`).

`z_change` also gated `g->vertical_shift`, which is *not* the same call as
`vertical_shift_notify`: `commit_occupants` uses `vertical_shift_notify` for the avatar, and the
vehicle's own z-change handling stays where it is in `shift_zlevel` / `adjust_zlevel`.

### B6. Convert the pet collision exclusion to identity

`part_collision` spares a pet only because it is found standing on a boardable part of this vehicle:
`if( ovp && ( &ovp->vehicle() == this ) && get_pet( ovp->part_index() ) )`
(`src/vehicle_move.cpp:672-676`). With commits deferred, a pet at a not-yet-updated tile fails that
test and gets run over by its own vehicle. Replace it with the identity form the player/NPC branch
already uses (`src/vehicle_move.cpp:610-621`): scan this vehicle's boardable parts for the critter —
now cheap, because after B1 `get_pet` resolves `animal_ref` with no map query. Keep the surrounding
`SWIMMABLE` fish-displacement branch (`:677-693`) unchanged.

### B7. Immediate-write paths and load-time rebuild

Occupants must be positioned *now*, not at commit, wherever they stop riding or are thrown. Add an
immediate `setpos` (inside a `committing_occupants` scope so B3's guard permits it) and clear
`boarded_part`/`boarded_vehicle` where the occupant leaves, at:
`map::shake_vehicle`'s throw-from-seat (`src/vehicle_move.cpp:1974+`), occupant death inside
`part_collision`, both `map::unboard_vehicle` overloads (`src/map_vehicle.cpp:1545-1579`),
`map::detach_vehicle`'s unboard loop (`:355-358`), `vehicle::split_vehicles`
(`src/vehicle_parts.cpp:1166-1345`, which already reseats passengers through
`vehicle::relocate_passengers` — `src/vehicle.h:656`, defined `src/vehicle_parts.cpp:1154-1160`,
called at `:1327`; extend that existing primitive to also move `boarded_vehicle`/`boarded_part`
rather than writing a second reseating path), and the ramp z-change path (`shift_zlevel`/`adjust_zlevel`,
`src/vehicle_move.cpp:1734-1860`). (`Character::forced_dismount`, `src/character_mount.cpp:489`, is
about riding a monster mount, not vehicle boarding — do not touch it.)

**No save-format change.** `passenger_id` is already serialized. Rebuild `animal_ref`,
`boarded_vehicle` and `boarded_part` in one post-load pass over parts carrying
`passenger_flag`/`animal_flag`, resolving characters by id and monsters by the part's tile — the
position-based lookup being removed from the hot path is still correct exactly once, at load. Hook it
where the map finishes attaching vehicles: the `veh->attach()` branch in `map::loadn`
(`src/map.cpp:2190-2195`).

### Gate B — four new cases, each failing before the stage and passing after

Add to `tests/vehicle_test.cpp` (or `tests/vehicle_collision_test.cpp` for (b)):

- (a) `"[vehicle][occupant]"`: drive a vehicle carrying the avatar, an NPC and a pet at cruise for 10
  turns; after each turn every occupant's `bub_pos()` equals `bub_part_location` of its part,
  `g->critter_at` finds each of them there, and a counter probe reports **exactly one** `setpos` per
  occupant per turn (before the change: one per crossed tile). Implement the probe as a
  `static` counter in the test incremented from a `commit_occupants` hook — or, simpler and with no
  production hook, by comparing `g->m.take_vehicle_move_notifications()`-style counting added for
  this purpose on `Creature`: `unsigned position_writes = 0;` incremented in the two leaf `setpos`
  writers. Choose the `Creature` counter; it is three lines and stage F reads it too.
- (b) `"[vehicle][occupant][collision]"`: drive over a tile holding an unrelated monster with a pet
  aboard — the unrelated monster takes the `part_collision` hit, the pet takes none.
- (c) `"[vehicle][occupant][ramp]"`: drive up a ramp with passengers; each occupant's z follows the
  vehicle. (This suite currently opts out of Box2D authority at `tests/vehicle_ramp_test.cpp:106`,
  `:147`; leave that opt-out alone until stage D.)
- (d) `"[vehicle][occupant][throw]"`: a collision hard enough to throw an occupant leaves that
  occupant outside the vehicle with `in_vehicle == false`, `boarded_part == -1` and a real position.

Plus: `tests/vehicle_collision_test.cpp`'s stale-`in_vehicle` case still passes, and
`tests/vehicle_drag_test.cpp` passes with constants unchanged.

**Falsifier (pre-decided):** if B3's single-writer invariant forces changes to more than a handful of
`bub_pos()`/`setpos` callsites, drop B3 and B2's ownership semantics and keep only the timing change
(commit once per turn, occupant position still writable by anyone — assessment option O8a). The
measured saving is identical; carry the invariant into stage F instead.

## Stage C — per-turn cache invalidation instead of per-tile churn

Scope decision, fixed here: **only the invalidation fan-out is batched, not index membership.**
`veh_cached_parts` / `veh_exists_at` must stay correct after every single tile crossing, because
`part_collision`, the occupant-exclusion checks and `vehicle_vehicle_collision` all query `veh_at`
*during* the walk. What is safe to batch is `map::on_vehicle_moved` (`src/map_vehicle.cpp:400-470`),
which only sets dirty flags consumed later in the turn (`m.vehmove()` at `src/game.cpp:1109`,
`m.build_map_cache(...)` at `:1152`).

### C1. Measure before changing anything

Add to `map` a counter incremented at the top of `map::on_vehicle_moved` and a read-and-reset
accessor (declare beside the vehicle helpers in `src/map.h:1149-1155`):

```cpp
        /// Diagnostic for the per-turn cache-churn gate (stage C).
        auto take_vehicle_move_notifications() -> unsigned;
```

Add `TEST_CASE( "vehicle_move_notifications_per_turn", "[vehicle][perf]" )` to
`tests/vehicle_test.cpp`: build a long straight road, cruise one vehicle at ~10 m/s for 10 turns,
and after each `g->m.vehmove()` record `take_vehicle_move_notifications()`. Assert nothing yet —
`REQUIRE`-log the per-turn vector via `CAPTURE` and a `CHECK( count >= 1 )`. Run it and record the
numbers in the commit message. Expected before C2: roughly one per tile crossed (≈5-6 per turn at
cruise; the walk is bounded at 64, `src/map_vehicle.cpp:700`).

**If the measured before-number is already ~1 per vehicle per turn, stop: drop C2 and C3, keep C1's
test with the assertion tightened to `== 1`, and go straight to stage D.** Do not do C for tidiness.

### C2. Batch scope around the readback walk

In `src/map.h`, private section beside `cached_veh_rope` (`:2453`):

```cpp
        bool batching_vehicle_moves_ = false;
        std::map<int, std::pair<point_bub_sm, point_bub_sm>> pending_vehicle_move_bounds_;
```

In `src/map_vehicle.cpp`:
- `map::on_vehicle_moved`: when `batching_vehicle_moves_`, union `sm_min.xy()`/`sm_max.xy()` into
  `pending_vehicle_move_bounds_[smz]` (component-wise min/max) and return; otherwise behave exactly
  as today.
- Add `void map::begin_vehicle_move_batch()` (sets the flag, clears the map) and
  `void map::flush_vehicle_move_batch()` (clears the flag, then calls the real `on_vehicle_moved`
  once per recorded z with the unioned bounds, then clears the map). Both private; `vehmove` is the
  only caller. `flush` must clear the flag *first* so the replayed calls take the normal path.
- Call `begin_vehicle_move_batch()` as the first statement of `map::vehmove()`
  (`src/map_vehicle.cpp:472-474`) and `flush_vehicle_move_batch()` at line 834 — after the
  `if (phys_world)` readback block closes at `:833` and before the `ZoneScopedN("veh_cleanup")`
  block at `:836-848`. Stage B's occupant commit runs **after** the flush (see B4), because
  committing the avatar can trigger `g->update_map()`, which rebuilds caches and must see the
  flushed dirty flags.

### C3. Tighten the gate test

Change C1's `CHECK( count >= 1 )` to `CHECK( count == 1 )` for every turn in which the vehicle
moved, and `CHECK( count == 0 )` for a parked vehicle.

### Gate C

- `./cata_test-tiles "[vehicle]" --order decl --rng-seed 1` passes, including the C3 assertions.
- `tests/vehicle_drag_test.cpp` and `tests/vehicle_efficiency_test.cpp` pass with **unchanged**
  constants.
- `./cata_test-tiles "~[.]" --order decl --rng-seed 1` per shard: failures ⊆ the 5 accepted baseline.

**Falsifier:** if batching makes any vision/pathfinding test fail because something inside
`vehmove` reads a cache that is now dirtied later, narrow the batch to the invalidation calls that
are provably turn-end-consumed (`invalidate_lightmap_caches`, `m_solar.last_built_hour`,
`set_seen_cache_dirty`, `visibility_cache_dirty`, the GPU transparency invalidate) and keep
`transparency_cache_dirty` / `floor_cache_dirty` / `pf_dirty` per tile.

## Stage D — single position authority

Authority is granted today to everything except rail-capable vehicles
(`v.box2d_position_authority = !v.can_use_rails();`, `src/physics/physics_world.cpp:131`), and even
for authority vehicles the legacy mover still owns every z-change and fall
(`if( box2d_position_authority && !should_fall && requested_z_change == 0 ) { return this; }`,
`src/vehicle_move.cpp:1682`; the readback walk also skips them outright at
`src/map_vehicle.cpp:659`). D removes both exceptions, then retires the legacy motion fields.

### D1. Rails under authority

The exclusion's reason is documented in place (`src/physics/physics_world.cpp:118-131`): rail motion
is a kinematic constraint applied by `vehicle_movement::process_movement_on_rails`
(`src/vehicle_move.cpp:2221-2301`), which `act_on_map` only reaches *after* the authority
early-return, so an authority-owned train never gets its heading corrected to the track, derails and
skids. Fix the ordering rather than the exclusion:

1. Hoist the rail block — `rpres = vehicle_movement::process_movement_on_rails( here, *this )` and
   the `if( rpres.do_turn ) { turn_dir = rpres.turn_dir; }` application (`src/vehicle_move.cpp:1686-1692`)
   — to **above** the early-return at `:1682`.
2. After it, when `vehicle_movement::is_on_rails( here, *this )` (`:2303-2322`), make the track
   constraint authoritative over the physics angle: `face.init( turn_dir )`, set
   `physics_angle` to `turn_dir` in radians, zero `angular_velocity_rads`, and push that angle into
   the body. `PhysicsWorld` has no angle-only setter today, so add one beside `clamp_body_to_tile`
   (`src/physics/physics_world.h:70-73`, defined at `src/physics/physics_world.cpp:208`):
   `void set_body_angle( vehicle &v, float radians );` — `b2Body_SetTransform` with the existing
   position plus `b2MakeRot( radians )`, and `b2Body_SetAngularVelocity( bid, 0.0f )`.
   Rails keep 45°-quantized headings; only the tile commit becomes physics-driven.
3. `rpres.do_shift` (the lateral track shift, `make_shift`, `:2213-2219`) is a discrete displacement
   a rigid body cannot express: add `rpres.shift_amount` to `physics_pos` and re-seat the body with
   `clamp_body_to_tile`, then let the readback walk commit the tiles. Remove the legacy
   `do_shift` handling so there is one path.
4. Flip `src/physics/physics_world.cpp:131` to `v.box2d_position_authority = true;` and replace the
   comment with the new contract (rails constrain heading before the step; Box2D owns position).
5. With no vehicle able to opt out, `authority_revoked_by_unload_`
   (`src/physics/physics_world.h:196`, used at `physics_world.cpp:485-487`, `:502-506`) exists only
   to distinguish self-opt-outs from unload revocations: delete the member and its uses, and delete
   the now-unrepresentable `SECTION( "a vehicle that opted out itself is never re-granted" )` in
   `tests/vehicle_test.cpp:685-690`. The rest of that TEST_CASE (`:654-684`) stays.

### D2. Z-changes and falls under authority

1. Remove `!should_fall && requested_z_change == 0` from the early-return condition
   (`src/vehicle_move.cpp:1682`), so authority vehicles always exit before the legacy mover. The
   pre-return logic that must keep running already does: sinking, vertical-velocity integration,
   traction, skidding (the comment at `:1674-1681` lists them with their line numbers).
2. Stop skipping them in the readback walk: replace
   `if (v.is_falling || (v.is_aircraft() && v.get_z_change() != 0)) { continue; }`
   (`src/map_vehicle.cpp:659`) with inclusion, and after the horizontal walk for that vehicle issue
   the vertical step through the same call the legacy mover uses —
   `move_vehicle( veh, tripoint_rel_ms( 0, 0, dz ), veh.face )` — once per z level of change, so
   `adjust_zlevel` → `displace_vehicle` → `shift_zlevel` (`src/vehicle_move.cpp:1734-1860`) and
   every ramp/fall consequence still fire exactly as today. `dz` comes from `veh.get_z_change()`
   for requested changes and from the fall logic's own decision for `is_falling`.
3. Remove the test-side opt-outs `veh_ptr->box2d_position_authority = false;`
   (`tests/vehicle_ramp_test.cpp:106`) and `veh.box2d_position_authority = false;` (`:147`). Convert
   the suite's assertions from the legacy mover's step to the tile the vehicle **commits** to
   (`lround(physics_pos)` / `bub_ms_location()` after `vehmove()`), in this order: rail exact
   pivot-tile equality and the 45°-step contract (`tests/vehicle_rails_test.cpp`, 11 cases), then
   ramp per-mount z-transition and anchor deltas (`tests/vehicle_ramp_test.cpp`, 11 cases), then the
   integer-tile-anchor distance metric in `tests/vehicle_efficiency_test.cpp`. The in-tree note at
   `tests/vehicle_test.cpp:912-918` records that the ramp suite's 83 assertions were only ever
   exercising the legacy path — converting them is the point of this step, not collateral.

### D3. Retire the legacy motion fields

Execute Phase 10 Step 6 from `plans/box2d-vehicle-physics-implementation.md:560-589`, with two
corrections that plan states about itself:

- **Its line numbers are stale** (it says so at `:475`). Re-derive every callsite with
  `grep -rn 'of_turn\|of_turn_carry\|angular_velocity_rads' src tests` before editing. The fields are
  declared at `src/vehicle.h:1785`, `:1787` and `:1790` in the current tree, not at the `:1755-1757`
  the audit lists.
- **Do not execute Phase 12-A** (deleting `src/physics/veh_box2d_solve.*`): Correction 1 at
  `plans/box2d-vehicle-physics-implementation.md:18-30` disproves its premise.

Order: (1) delete `of_turn` / `of_turn_carry` and replace `map::vehmove`'s `of_turn` priority-queue
scheduling (`src/map_vehicle.cpp:472-620`) with straight iteration over the live vehicle list — the
Box2D step already integrates a whole game second (`phys_world->step_turn(1.0f)`, `:637`), so
ordering by remaining move budget no longer means anything; (2) delete `angular_velocity_rads`
(`src/vehicle.h:1790`) once D1 no longer writes it; (3) add save-load guards so old saves carrying
the retired keys still load (`vehicle::deserialize`, `src/savegame_json.cpp` — locate with
`grep -n 'of_turn_carry' src/savegame_json.cpp`); (4) leave `velocity` and `turn_dir` in place —
that plan's own note (`:580`) measures ~200 callsites each and defers them, and this program does
not need them gone (the end-state target is three position representations, not zero legacy scalars).

### D4. Efficiency constants: what to do when they move

`plans/box2d-vehicle-physics-implementation.md:118-122` records that the efficiency distances encode
the legacy mover's `of_turn` truncation loss and that the Box2D path measured **+0.3% to +4.3%** over
the upper bound for most prototypes, with `fire_truck_test` on dirt **~40% under** the lower bound.
So D3 will move them. The policy, fixed here so nobody regenerates the table:

- The drag table (`tests/vehicle_drag_test.cpp`) must **not** move at all: it measures `c_air`,
  `c_rolling`, `c_water`, `safe_velocity`, `max_velocity`, none of which depend on the mover. A
  changed drag constant means D broke physics — stop and fix the code.
- For `tests/vehicle_efficiency_test.cpp`, a bound may be widened only with an analytic
  justification recorded in the commit message: expected tiles-per-fuel computed from the
  (unchanged) drag constants and engine power at the test's cruise speed. A bound may never be
  adjusted by pasting a generated row.
- The `fire_truck` dirt shortfall is a traction bug, not a calibration drift (a 40% one-prototype,
  one-terrain outlier cannot come from removing a uniform sub-tile truncation). Root-cause it in
  `src/vehicle_move.cpp:1907-1975` (wheel traction) before touching its numbers.

### Gate D

- `grep -rn 'box2d_position_authority' src tests` shows only the declaration
  (`src/vehicle.h:1805`), `on_vehicle_added`/`on_vehicle_removed` in
  `src/physics/physics_world.cpp`, and read-only assertions in tests — no writer that clears it.
- `grep -rn 'of_turn' src tests` returns nothing.
- `./cata_test-tiles "[vehicle]" "[vehicle][ramp]" "[vehicle][railroad]" --order decl --rng-seed 1`
  passes; `tests/vehicle_efficiency_test.cpp` and `tests/vehicle_collision_test.cpp` pass.
- `tests/vehicle_drag_test.cpp` passes with **unchanged** constants.
- `tests/vehicle_test.cpp`'s `[!shouldfail]` collider-accumulation spec (`:1000-1013`) either passes
  with the tag dropped, or keeps the tag with its blocking reason restated from a measurement (the
  original reason was that the test harness cannot build colliders at all — verify that claim before
  restating it).
- All four `~[.]` shards plus unsharded `~[coop]`: failures ⊆ the 5 accepted baseline failures.

**Falsifier (pre-decided):** if rails or ramps cannot be restored under single authority without
inventing balance constants no test can validate, stop and make the dual model explicit and
permanent instead of finishing it: document the contract, keep exactly one reconciliation point
(the readback walk), and record in `plans/vehicle-system-rework-assessment.md` that D is closed as
"deliberately dual". A–C and E still deliver the program's other four outcomes.

## Stage E — continuous rendering (parallel with A–D, blocks only F)

Today a vehicle cannot be drawn at an arbitrary angle: `draw_from_id_string` collapses a vehicle
part's rotation to 4-way (`true_rota = 3 - face.dir4();`, `src/cata_tiles.cpp:2013`), fed from a
whole-degree rounding of `part_display_direction` (`src/cata_tiles_draw_layers.cpp:643-644`,
`:715-716`, `:761-762`). The only continuous channel is the whole-vehicle pixel offset
`render_offset_x/y` (`:660-662` for parts, `:884-885` and `:1232-1233` for riders).

Two decisions, fixed here:

1. **Composite-then-rotate.** A vehicle is one rigid body: draw its parts once into an off-screen
   target in vehicle-local space, then draw that target as a single sprite rotated by
   `physics_angle`. Per-part rotation cannot work — `sprite_instance::rotation` rotates the quad
   "around its centre" (`src/lighting/sprite_batcher.h:47-49`), i.e. each part would spin in place
   rather than orbit the vehicle pivot.
2. **Vision and lighting stay tile-quantized.** Vehicle geometry continues to be published to
   `vehicle_floor_cache` / `vehicle_obscured_cache` / `vehicle_obstructed_cache` (`src/map.h:454-469`)
   and their GPU mirrors at the *committed* tile anchor, so the five compute shaders that index
   those buffers by tile need no change. Up to half a tile of visual/logical divergence is accepted.

### E1. Per-vehicle composite target

Reuse the portrait pattern verbatim: `ui_composite_target` (allocated for the avatar at
`src/lighting/render_state.cpp:164-168`, drawn into at `src/sdl_render_frame.cpp:928-933` via
`begin_pass( cmd, at->texture(), at->width(), at->height(), clear_transparent, … )` →
`flush_avatar_sprites` → `end_pass`).

In `src/lighting/render_state.{h,cpp}` add a small pool:

```cpp
    /// Off-screen target per on-screen vehicle, keyed by vehicle_handle::slot.
    auto vehicle_target( std::uint32_t slot, std::uint32_t px_w, std::uint32_t px_h )
    -> ui_composite_target *; // *NOPAD*
    auto vehicle_sprites_empty( std::uint32_t slot ) const noexcept -> bool;
    auto flush_vehicle_sprites( std::uint32_t slot, sprite_batcher &dst, SDL_GPUSampler *sampler )
    -> void;
    auto set_vehicle_route( std::uint32_t slot ) noexcept -> void;   // 0 = off
    auto retire_unused_vehicle_targets() -> void;   // called once per frame from begin_frame
```

Routing mirrors `avatar_route_` (`src/lighting/render_state.h:197-208`): while a vehicle route is
set, queued tile sprites go to that vehicle's queue instead of the world queue. Targets are
allocated lazily at footprint size rounded up to whole tiles, re-allocated when the footprint or
zoom changes, and retired when a vehicle is not drawn for a frame.

### E2. Route part sprites into the target

In `cata_tiles::draw_vpart` (`src/cata_tiles_draw_layers.cpp:608+`): when the vehicle has a nonzero
`physics_angle`, call `set_vehicle_route( veh.handle().slot )`, emit every part sprite with
destination coordinates **relative to the vehicle's local origin** (mount extents × tile size)
instead of screen coordinates, and clear the route afterwards. Drop `active_anim_xform_`'s
`render_offset_*` application inside the route (the offset is applied once to the composite quad in
E3). Vehicles with `physics_angle == 0` keep today's direct per-tile path — no regression risk for
every stationary/axis-aligned vehicle and no composite cost for them.

### E3. One rotated draw per vehicle

Add a pass in `src/sdl_render_frame.cpp` between Pass W-a and W-b (the terrain/entity split at
`:1022-1063`): for each routed vehicle, `begin_pass` on its target with `clear_transparent`, flush
its queue, `end_pass`; then emit into the world queue one `sprite_instance` whose `dst_*` is the
vehicle footprint in screen space (plus `render_offset_x/y × tile size`), `src_*` the whole target,
and `rotation = veh.physics_angle` (radians, already the unit `sprite_instance::rotation` expects).

### E4. Rider sprites

Riders are drawn separately and already inherit the vehicle offset (`:883-885`, `:1231-1233`). Keep
them in the world pass and add the same rotation about the vehicle centre to their screen position
(rotate the rider's offset vector from the vehicle pivot by `physics_angle`), leaving the rider
sprite itself unrotated — a passenger does not visually bank with the chassis.

### Gate E

- In the installed macOS build, with a vehicle driving in a circle: two frames whose
  `physics_angle` differs by ≈20° and which is **not** a multiple of 90° produce a non-zero pixel
  diff over the vehicle's bounding box, measured with the tooling in `tools/visual_verify/`
  (numbers only — do not read frames into context).
- `[render][perf]` (`src/sdl_render_frame.cpp:1579-1591`) `render_body avg` over a 120-frame window
  with one vehicle on screen is within noise (≤5%) of the same scene before E.

**Falsifier:** if the per-vehicle composite cannot hold frame budget at the vehicle counts the game
draws (e.g. a parking lot), gate the composite path on `physics_angle` exceeding half a 4-way step
(22.5°) and snap the rest — F then ships continuous collision and motion with rendering snapped to
the committed anchor, and E continues separately. E does not block A–D.

## Stage F — converge and verify against recorded numbers

Re-derive the assessment's own metrics and check them against these expected values:

|Metric|Command|Expected after A–D|
|---|---|---|
|Position representations|read `src/vehicle.h:1754-1822`|three (physics transform, save anchor, render residual)|
|Raw-`vehicle *` index holders|`grep -rn 'vehicle \*' src/map.h src/mapbuffer.h src/coop_server.h src/physics/physics_world.h`|one registry + handle-valued indices|
|Authority opt-out sites|`grep -rn 'box2d_position_authority' src tests`|zero opt-outs (declaration/accessor only)|
|Occupant position writes per vehicle per turn|stage B gate (a) counter|1|
|`on_vehicle_moved` per vehicle per turn|stage C gate counter|1|
|Vehicle test population|`grep -c 'TEST_CASE(' tests/vehicle_*.cpp tests/coop_vehicle_test.cpp tests/npc_vehicle_muscle_test.cpp tests/ranged_vehicle_recoil_test.cpp`|≥ 104 + the 7 cases added by A5/B/C|

Then run the full acceptance suite: all four `~[.]` shards plus unsharded `~[coop]`, failures ⊆ the
5 accepted baseline failures, with `tests/vehicle_drag_test.cpp` constants unchanged. With those
numbers in hand, decide whether continuous terrain collision (removing the tile walk entirely) is
worth opening — that is a separate balance project with its own oracle problem and is explicitly not
part of this program.

## Critical files and anchors

|File|Region|Why|
|---|---|---|
|`src/map_vehicle.cpp`|`reset_vehicle_cache` :186, `add_vehicle_to_cache` :202, `clear_vehicle_point_from_cache` :239, `clear_vehicle_list` :287, `update_vehicle_list` :296, `detach_vehicle` :309, `on_vehicle_moved` :400, `vehmove` :472-871 (readback walk :667-833, insertion point :834, cleanup :836-848), `veh_at_internal` :1489, `board_vehicle` :1512, `unboard_vehicle` :1545/:1560, `displace_vehicle` :1581-1828 (rider loop :1637-1694)|Every stage touches this file; it holds the fragile index, the walk, and the commit point|
|`src/map.h`|`level_cache` vehicle members :490-494, vehicle-cache API :1149-1180, `dirty_vehicle_list` :1994, `last_full_vehicle_list` :2451, `cached_veh_rope` :2453|The index declarations A4 retypes; editing it triggers a full rebuild, so batch every `map.h` change of a stage into one edit|
|`src/vehicle.h` / `src/vehicle.cpp`|ctors :473-475, dtor :476 / :426, :467, :473; position fields :1754-1822; `box2d_position_authority` :1805; `relocate_passengers` :656|A2's RAII registration, D's authority and field retirement|
|`src/vehicle_move.cpp`|occupant exclusions :610-621 / :672-676, `act_on_map` early-return :1682, rail block :1686-1692, `shift_zlevel`/`adjust_zlevel` :1734-1860, traction :1907-1975, `shake_vehicle` :1974+, `process_movement_on_rails` :2221-2301, `is_on_rails` :2303-2322|B6, D1 and D2 all live here|
|`plans/box2d-vehicle-physics-implementation.md`|Correction 1 :18-30, efficiency deviation :118-122, Phase 12 :501-559, Phase 10 Step 6 :560-589, status table :663-679|D3's checklist — read the corrections before the checklist, its line numbers are stale by its own admission (:475)|

## Verification

Prerequisites for every run: repo root as working directory, a completed background build, and
`ls -l ./cata_test-tiles` newer than that build's start. Scratch user dirs per run; runs are serial.

```sh
# build (background job, timeout >= 1800s, never killed)
cmake --build --preset osx-arm-slim --target cataclysm-bn-tiles cata_test-tiles

# fast crash repro / A0 gate (historically SIGSEGVs at case #22)
rm -rf /tmp/veh_s2 && mkdir -p /tmp/veh_s2
timeout 120 ./cata_test-tiles "~[.]" --order decl --rng-seed 1 \
    --shard-count 4 --shard-index 2 --user-dir=/tmp/veh_s2

# per-stage vehicle suite
./cata_test-tiles "[vehicle]" --order decl --rng-seed 1 --user-dir=/tmp/veh_stage

# full acceptance (per shard 0..3, plus unsharded coop)
timeout 1800 ./cata_test-tiles "~[.]" --order decl --rng-seed 1 \
    --shard-count 4 --shard-index N --user-dir=/tmp/veh_shN
timeout 3600 ./cata_test-tiles "~[coop]" --order decl --rng-seed 1 --user-dir=/tmp/veh_coop
```

New-behaviour proofs (each is a concrete input → observable output, not just a green build):

|Stage|Input|Expected observable|
|---|---|---|
|A0|shard-2 run above|process exits 0/1 on its own; no 139/135, not killed by `timeout`|
|A5|spawn vehicle, record handle, `destroy_vehicle`, then re-anchor the bubble|`veh_at(tile)` is `nullopt` and `resolve_vehicle(recorded) == nullptr` in both cases (both assertions fail before A2)|
|B (a)|avatar + NPC + pet aboard, cruise 10 turns|each occupant's `bub_pos() == bub_part_location(part)` every turn, `critter_at` finds them, and `Creature::position_writes` increments by exactly 1 per occupant per turn (≈5-6 before B)|
|B (b)|drive over an unrelated monster with a pet aboard|monster takes the `part_collision` hit; pet takes none|
|B (d)|collision hard enough to throw an occupant|that occupant ends outside the vehicle with `in_vehicle == false`, `boarded_part == -1`, a real position|
|C|cruise one vehicle 10 turns|`take_vehicle_move_notifications()` == 1 per moved vehicle per turn (≈5-6 before C); == 0 parked|
|D|`"[vehicle][ramp]"` + `"[vehicle][railroad]"` with no authority opt-out anywhere|both suites pass against committed-anchor assertions; `grep -rn 'of_turn' src tests` empty|
|E|vehicle driving in a circle, two frames ≈20° apart and off the 4-way set|non-zero pixel diff over the vehicle bounding box via `tools/visual_verify/`; `[render][perf] render_body avg` within 5% of the pre-E scene|
|F|metric table in stage F|every row matches its expected value|

## Assumptions and contingencies

- **The program is executed in full, in order.** If only part of it is wanted, A alone is the
  self-contained fix (it closes the open SIGSEGV); B alone is the self-contained perf/structure win.
  C, D and F are not independently valuable.
- **Save compatibility is preserved throughout.** No stage changes the save format: handles,
  `animal_ref`, `boarded_vehicle`/`boarded_part` are all runtime-only and rebuilt at load; D3 only
  adds read-guards for retired keys. If a stage appears to need a save-format change, stop — that is
  the signal to reconsider O3 (world-level vehicle store) as a separate, explicitly-approved piece of
  work rather than smuggling a migration into this program.
- **Rendering may be deferred.** If stage E's composite cannot hold frame budget, F ships with
  rendering snapped to the committed anchor (assessment's recorded fallback) and the art track
  continues separately. A–D are unaffected.
- **`[render][perf]`/`[sim][perf]` numbers are captured from the installed macOS build**; if that
  build cannot be driven at the time, E's frame-budget check may be deferred to F, but E's pixel-diff
  check may not — a rotation that cannot be observed has not been delivered.
</content>

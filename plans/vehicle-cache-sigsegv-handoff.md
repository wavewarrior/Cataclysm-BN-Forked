# Handoff: vehicle-cache SIGSEGV in `~[coop]` single-process runs

## Status: OPEN — root cause not found, not fixed

## Repro

```sh
cd /Users/nigel.fierens/dev-projects/Cataclysm-BN-Forked
rm -rf test_user_dir
./cata_test-tiles "~[coop]" --order decl --rng-seed 1 -r xml -o /tmp/repro.xml
```

Crashes deterministically (confirmed via lldb, not flaky/ASLR-dependent) after **exactly 484**
`--order decl --rng-seed 1` test cases execute (483 pass, the 484th's own *setup* — not its body —
segfaults). Bisection is unusually flat: prefixes of 424, 454, 469, 480, and 483 cases all complete
cleanly; only the full 484-case prefix crashes. The specific test occupying slot 484 is
interchangeable — excluding `tree_terrain_supports_climbing_destination_above` (the first test
found to trigger it) simply moves the crash to whichever test now lands in that slot
(`bash_through_roof_can_destroy_multiple_times` reproduced identically). This is the signature of
**resource accumulation**, not a pairwise state leak between two specific tests.

Sharding avoids it entirely and should NOT be read as evidence of a fix:

```sh
mkdir -p /tmp/s6shard{0,1,2,3}
./cata_test-tiles "~[coop]" --order decl --rng-seed 1 --shard-count 4 --shard-index 0 --user-dir=/tmp/s6shard0 -r xml -o /tmp/s6_shard0.xml &
./cata_test-tiles "~[coop]" --order decl --rng-seed 1 --shard-count 4 --shard-index 1 --user-dir=/tmp/s6shard1 -r xml -o /tmp/s6_shard1.xml &
./cata_test-tiles "~[coop]" --order decl --rng-seed 1 --shard-count 4 --shard-index 2 --user-dir=/tmp/s6shard2 -r xml -o /tmp/s6_shard2.xml &
./cata_test-tiles "~[coop]" --order decl --rng-seed 1 --shard-count 4 --shard-index 3 --user-dir=/tmp/s6shard3 -r xml -o /tmp/s6_shard3.xml &
wait
```

Each shard runs ~270 cases — below the ~483 accumulation threshold — so all four complete with
zero SIGSEGV. `--user-dir=<dir>` (equals-sign form; the space-separated form is rejected by this
binary's arg parser) is required to avoid the four processes colliding on `./test_user_dir`.

Note also: `[coop]` (159 cases, the tagged-in subset) has never reproduced this, in any run.

## Crash site (confirmed via lldb, not guessed)

```
vehicle::part_with_feature(...) const
map::build_absorption_cache(int)          [src/sounds.cpp:~1690]
map::build_map_cache(int, bool)
wipe_map_terrain()                        [tests/map_helpers.cpp:39]
clear_map()                               [tests/map_helpers.cpp:117]
clear_states(enum_bitset<test_state> const&)
CATCH2_INTERNAL_TEST_39()
```

The crash is inside **test setup/teardown for the case AFTER the 483rd**, not inside any test's own
assertions. `map::build_absorption_cache` → `submap::rebuild_absorption_cache` (sounds.cpp
~1488-1491) does `if( const auto &vp = m.veh_at( btri ) ) { vp.part_with_feature(...) ... }`. The
straightforward read is that `veh_at()` — which resolves through
`level_cache::veh_cached_parts` — returns a **dangling `vehicle*`**: some earlier vehicle was freed
without every z-level's `veh_cached_parts`/`vehicle_list`/`cached_veh_rope` entry pointing at it
being purged first.

Reproduce under a debugger directly (locals are optimized out in this release build, so `evaluate`
on `this`/pointers will not work — the backtrace itself is the only evidence obtainable this way):

```
debug launch: program=./cata_test-tiles, args=["~[coop]","--order","decl","--rng-seed","1"], adapter=lldb-dap, cwd=<repo>
debug continue   (blocks until either it stops or exits — takes several minutes; poll with `sessions`)
debug stack_trace levels=30   (once status=stopped, reason=exception)
```

## What has been ruled out (ablation-tested, not inferred)

Both were **fully reverted to their pre-S6 (commit `6d7620d9`) content**, rebuilt, and the exact
484-case repro re-run. The crash reproduced **identically** (`482/3`, same crash site) in both
cases, ruling them out as the cause:

1. **`src/map_vehicle.cpp`** — this stage's rope-ladder-column caching in `add_vehicle_to_cache`/
   `clear_vehicle_point_from_cache`, and the `detach_vehicle` detached-vehicle-footprint dirty
   marking. Thematically the most obvious suspect (it touches exactly the caches implicated), but
   provably not the cause.
2. **`item::process`'s new recursive nested-container content walk** (`src/item_process.cpp`) —
   this stage's largest, most novel piece of ported logic. Also ruled out.

## What has been fixed (real bug, but did NOT resolve this crash)

`map::on_submap_unloaded()` (`src/map.cpp`) sweeps physics bodies (`phys_world->on_submap_unloaded`),
active-item indices, and funnel locations for a submap leaving the tracked grid — but **never**
purged that submap's vehicles from the owning z-level's `veh_cached_parts`/`vehicle_list`/
`cached_veh_rope`. If MAPBUFFER later evicts (and frees) that submap, every level-cache entry
pointing at one of its vehicles is left dangling. This is a real, independently-justified gap
(pre-existing since before S6 — confirmed via `git log -L` that this function's vehicle-blind spot
predates this merge stage entirely) and has been fixed:

```cpp
// src/map.cpp, inside on_submap_unloaded(), before cache_submap_at_grid(..., nullptr):
if( submap* sm = MAPBUFFER_REGISTRY.get( dim_id ).lookup_submap_in_memory( pos );
    sm != nullptr && !sm->vehicles.empty() ) {
    level_cache& ch = get_cache( pos.z() );
    bool removed_any = false;
    for( const auto& veh : sm->vehicles ) {
        if( ch.vehicle_list.erase( veh.get() ) > 0 ) { removed_any = true; }
        ch.zone_vehicles.erase( veh.get() );
    }
    if( removed_any ) {
        last_full_vehicle_list_dirty = true;
        reset_vehicle_cache();
    }
}
```

**Verified via direct rerun that this does NOT fix the reported crash** — a full rebuild + 484-case
repro after applying it hit the identical SIGSEGV at the identical accumulation point. Keep this
fix regardless (it closes a real hole), but do not mistake it for the fix to this ticket.

## Leads not yet run to ground

- **`map::load()`'s own comment is a smoking gun and was not chased to completion.** Around
  `src/map.cpp:1526-1533`, the comment states this re-anchor path "drops all submaps without
  routing through `on_submap_unloaded`" and that "the vehicle cache is cleared just below and
  rebuilt by `reset_vehicle_cache()`" — but the only call actually present is `clear_submap_cache()`
  (the submap **grid-pointer** cache), not any vehicle-cache clear. `tests/map_helpers.cpp`'s
  `clear_map()` calls exactly this re-anchor path (`g->m.load(canonical_xy, true)`) on every single
  test teardown. If `map::load` genuinely drops submaps' vehicles without ever routing through
  `on_submap_unloaded` (which is where the fix above lives), the fix above would never fire on this
  path at all — which would fully explain why it didn't help. **Next step: add the equivalent
  purge (`clear_vehicle_cache()` + `clear_vehicle_list(z)` for every z, or the safer
  `sm->vehicles`-based purge from the fix above, generalized across every z touched by the
  re-anchor) directly inside `map::load()`, before its `loadn` loop, and re-run the 484-case repro.**
  This was identified but not implemented or tested this session — highest-confidence next step.
- **Multi-z vehicle registration.** `map::update_vehicle_list()` inserts a vehicle into whichever
  z-level's `vehicle_list` its *submap* belongs to, but `map::detach_vehicle()` only erases from
  `get_cache( veh->abs_sm_pos.z() ).vehicle_list` — a single z. A vehicle that (legitimately)
  touches more than one z-level (a multi-level structure, or specifically the newly-imported
  `tests/vehicle_ladder_test.cpp` scenarios this stage added) could leave a freed pointer in a
  *different* level's `vehicle_list`, later re-added by `reset_vehicle_cache()`'s per-z loop. Not
  tested; plausible and testable in isolation by instrumenting `detach_vehicle` to erase from every
  z's `vehicle_list`, not just its own.
- **Instrumentation not yet added**: a one-shot `debugmsg` (or assertion) inside
  `map::reset_vehicle_cache()` right before `add_vehicle_to_cache(elem)`, cross-checking `elem`
  against vehicles actually owned by currently-resident submaps, would name the leaking pointer's
  provenance directly in one repro run instead of further ablation. This is the fastest path to a
  definitive root cause and was recommended but not executed this session.

## Vehicle-physics architecture context (informs, but is not the fix)

A parallel architecture review (recorded verbatim, not re-derived, since it was produced by a
separate analysis pass this session) concluded:

- The system already runs Box2D as a continuous positional authority *and* a legacy tile-stepper as
  the sole gameplay-consequence authority (bash damage, creature collision, ramp z-transitions) —
  reconciled every turn by walking `move_vehicle()` once per tile crossed (up to 64/turn).
- Each such per-tile call does a full `veh_cached_parts` cache eviction/reinsertion — i.e. the
  number of cache-mutation opportunities scales with **tiles crossed**, not turns elapsed. This is
  flagged as the most likely *systemic* source of cache-invalidation fragility (of which this
  SIGSEGV may be one instance), independent of the specific bug above.
- **Rejected as a fix direction**: making vehicles grid-independent while driving and snapping to
  grid only when parked. Every gameplay-consequence check (bash, creature collision, ramp entry) is
  computed by the tile-quantized walk by deliberate prior design (raw Box2D contact impulses were
  tried and explicitly rejected as "inventing a balance model with nothing to validate it against").
  Deferring consequences to a snap-to-grid moment breaks core gameplay; keeping the tile walk while
  also being "grid-independent" adds a sixth position representation on top of the five that already
  exist, worsening the reconciliation problem this bug is a symptom of.
- **Recommended direction instead** (not implemented, flagged for a future session): decouple
  cache-membership updates from per-tile consequence checks — compute the turn's full swept
  footprint once, evict/insert `veh_cached_parts` once per vehicle per turn instead of once per tile
  crossed, while leaving bash/collision detection tile-granular exactly as it is now. This does not
  by itself fix the reported SIGSEGV, but would shrink the space of places a future audit needs to
  check, and independently addresses the `move_vehicle`-driven fragility class this bug's shape
  matches.

## Suggested order of attack for the next session

1. Implement and test the `map::load()` purge (highest-confidence untested lead, above).
2. If (1) doesn't resolve it, add the `reset_vehicle_cache()` cross-check instrumentation and run
   the 484-case repro once to get the leaking pointer's exact provenance.
3. Only after root-causing: decide whether the per-tile → per-turn cache-membership batching
   (architecture section above) is worth doing as a follow-on hardening pass, separate from this
   fix.
4. Re-run the unsharded single-process `~[coop]` repro (not sharded — sharding hides this bug) as
   the acceptance test. It must complete to zero SIGSEGV before this handoff doc can be closed.

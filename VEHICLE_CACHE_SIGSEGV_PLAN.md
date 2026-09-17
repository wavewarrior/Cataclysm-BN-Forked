# Fix vehicle-cache SIGSEGV in `map::load()` re-anchor path

## Context

`plans/vehicle-cache-sigsegv-handoff.md` documents an open SIGSEGV: `map::build_absorption_cache()`
→ `map::veh_at()` → `vehicle::part_with_feature()` dereferences a dangling `vehicle*`, reproducible
in a bounded 90s run via `timeout 90 ./cata_test-tiles "~[.]" --order decl --rng-seed 1
--shard-count 4 --shard-index 2 --user-dir=<dir>` (crashes at case #22) and, as the final acceptance
bar, in the unsharded single-process `~[coop]` suite (crashes after case #483). The doc's own
"Suggested order of attack" (its final section) names the untested, highest-confidence lead:
`map::load()`'s re-anchor path never purges `level_cache::vehicle_list`/`zone_vehicles` for
submaps that leave the bubble, so `reset_vehicle_cache()` (called at the end of `map::load()`)
rebuilds `veh_cached_parts`/`cached_veh_rope` from a `vehicle_list` that still contains pointers to
vehicles whose owning submap may since have been evicted (and freed) by `MAPBUFFER`. This plan
implements exactly that fix, verifies it against both repro tiers, and closes out the handoff docs.

This session re-derived and confirmed the mechanism (not merely re-asserting the handoff doc): the
already-working `map::shift()` (`src/map.cpp:1885-1896`) clears `clear_vehicle_cache()` **and**
loops every z-level calling `clear_vehicle_list(gridz)` *before* letting shift-driven submap loads
repopulate it, then calls `reset_vehicle_cache()` afterward. `map::load()` (`src/map.cpp:1544-1567`)
only does the second half: it calls `reset_vehicle_cache()` at the end but never clears
`vehicle_list`/`zone_vehicles` first. Its own `loadn()` callee (`src/map.cpp:2077-2221`) only ever
**inserts** into `ch.vehicle_list` (`if (!map_cache.vehicle_list.contains(veh.get())) { ... }`,
`src/map.cpp:2212-2216`) — it never removes stale entries. Since `map::load()`'s `bubble_submaps()`
loop (`src/map.h:2204-2206`) covers the *entire* local grid across every z on every call, this is a
full re-anchor: any vehicle recorded in `vehicle_list` before the call whose owning submap is not
part of the newly-loaded bubble is now orphaned there, permanently, until something else clears it.

## Approach

1. **Apply the fix in `src/map.cpp`, function `map::load()` (currently lines 1544-1567).** Add a
   per-z `clear_vehicle_list(z)` loop mirroring `map::shift()`'s existing pattern, placed right
   after `clear_submap_cache();` and before `funnel_locations_.clear();`. Reuse the existing
   `clear_vehicle_list(int zlev)` member (`src/map_vehicle.cpp:287-291`, already used by `shift()`)
   — do not write new clearing logic. Full replacement text for the function:

   ```cpp
   void map::load( const point_abs_sm &w, const bool update_vehicle, const bool pump_events )
   {
       // Re-anchoring the whole bubble invalidates every body in it.  The cache clear
       // below drops all submaps without routing through on_submap_unloaded, and
       // loadn() then builds fresh bodies, so without this the old ones both accumulate
       // and keep bubble-relative positions that no longer mean anything — a distant
       // load carries no shift delta, so on_map_shifted never corrects them either.
       // Vehicle bodies go too: every z-level's vehicle_list/zone_vehicles is cleared
       // below (mirroring map::shift()'s pre-shift clear) so the loadn() calls below
       // repopulate it from only the submaps that remain resident in the new bubble,
       // before reset_vehicle_cache() rebuilds veh_cached_parts/cached_veh_rope from
       // it. Without this, stale entries for submaps no longer in the reloaded bubble
       // survive the rebuild; if MAPBUFFER later evicts (and frees) one of those
       // submaps, reset_vehicle_cache() dereferences a dangling vehicle* while
       // rebuilding the cache, corrupting it with a pointer that segfaults whenever
       // something later reads it (e.g. map::veh_at() from map::build_absorption_cache()).
       if( phys_world ) { phys_world->clear_world_bodies(); }
       clear_submap_cache();
       for( const auto z : std::views::iota( -OVERMAP_DEPTH, OVERMAP_HEIGHT + 1 ) ) {
           clear_vehicle_list( z );
       }
       funnel_locations_.clear();
       set_abs_sub( w );
       for( const auto p : bubble_submaps() ) {
           loadn( p, update_vehicle );
           MAPBUFFER_REGISTRY.get( bound_dimension_ ).actualize_submap( map_local_to_abs( *this, p ) );
           if( pump_events ) { inp_mngr.pump_events(); }
       }
       refresh_active_submap_view();
       validate_active_submap_view_complete( "map::load" );
       reset_vehicle_cache( );

       charge_removal_blacklist::split_deferred();
   }
   ```

   `std::views::iota` needs no new include: `map::shift()` already uses the identical
   `std::views::iota( -OVERMAP_DEPTH, OVERMAP_HEIGHT + 1 )` form in this same translation unit
   (`src/map.cpp:1894`). This is safe when `update_vehicle` is `false` (the only such caller,
   `src/game_setup.cpp:997-1000`, constructs a fresh stack-local `tinymap` per loop iteration and
   never reads `vehicle_list` back out of it — clearing an already-effectively-unused list to
   empty changes nothing observable there). No header (`src/map.h`) changes are needed — avoid
   touching `map.h`, since header edits trigger the project's expensive full-rebuild cascade.

2. **Rebuild** `cataclysm-bn-tiles` and `cata_test-tiles` from the repo root using the
   `osx-arm-slim` preset, as an unattended **background** job (never a synchronous/short-timeout
   build — a killed build corrupts `.ninja_deps`/`.ninja_log` per this repo's hard build rule):

   ```sh
   cmake --build --preset osx-arm-slim --target cataclysm-bn-tiles cata_test-tiles
   ```

   Poll to completion; do not interrupt it. After it finishes, confirm freshness before trusting
   any test run: `ls -l ./cata_test-tiles ./cataclysm-bn-tiles` (repo root — **not**
   `out/build/osx-arm-slim/...`, which is a stale leftover on this preset per this repo's
   `AGENTS.md`) and compare both mtimes against `git log -1 --format=%cI` — both binaries must be
   newer than the build's start time.

3. **Run the fast regression gate** (the doc's cheap repro, expected to now pass):

   ```sh
   rm -rf /tmp/vehfix_shard2 && mkdir -p /tmp/vehfix_shard2
   timeout 90 ./cata_test-tiles "~[.]" --order decl --rng-seed 1 --shard-count 4 --shard-index 2 \
       --user-dir=/tmp/vehfix_shard2
   ```

   Expected: the process exits on its own well within 90s (Catch2 exit code 0 or 1), not killed by
   `timeout` and not a SIGSEGV (exit 139/135). Any failing test names must be a subset of the five
   already-accepted baseline failures (`plans/merge-main-into-improvements.md:24`):
   `flung creatures stop at the reality bubble edge`, `vision_wall_obstructs_light`,
   `vision_single_tile_skylight`, `vision_see_out_of_vehicle`, `vision_see_into_vehicle`. Any other
   failing test, or a crash/timeout, means the fix did not (fully) resolve the issue — go to the
   contingency in step 3a before proceeding.

   **3a. Contingency — if step 3 still SIGSEGVs at the same crash site** (confirm via
   `lldb`/backtrace per the handoff doc's debug recipe if the site is unclear): the fix above closes
   the specific leak this session root-caused, but a second, independent leak path may exist. Add a
   temporary diagnostic to `map::reset_vehicle_cache()` (`src/map_vehicle.cpp:186-200`) that names
   the leaking pointer's provenance instead of guessing further. Replace the function body with:

   ```cpp
   void map::reset_vehicle_cache() {
       last_full_vehicle_list_dirty = true;
       clear_vehicle_cache();

       const int zmin = -OVERMAP_DEPTH;
       const int zmax = OVERMAP_HEIGHT;
       for (int zlev = zmin; zlev <= zmax; zlev++) {
           level_cache& ch = get_cache(zlev);
           for (const auto elem : ch.vehicle_list) {
               // DIAGNOSTIC (temporary, remove once provenance is found): confirm elem
               // is still owned by its recorded home submap before dereferencing it below.
               const submap *owner = MAPBUFFER_REGISTRY.get( bound_dimension_ )
                                     .lookup_submap_in_memory( elem->abs_sm_pos );
               const bool owned = owner != nullptr && std::ranges::any_of( owner->vehicles,
               [elem]( const auto & up ) { return up.get() == elem; } );
               if( !owned ) {
                   dbg( DL::Error ) << "[VEHCACHE-LEAK] zlev=" << zlev << " ptr=" << elem
                                    << " abs_sm_pos=" << elem->abs_sm_pos.x() << ","
                                    << elem->abs_sm_pos.y() << "," << elem->abs_sm_pos.z()
                                    << " owner=" << ( owner == nullptr ? "MISSING" : "PRESENT_NOT_OWNING" );
               }
               add_vehicle_to_cache(elem);
           }
       }
   }
   ```

   Rebuild (step 2's command), re-run the **exact same** step-3 command once, then inspect
   `/tmp/vehfix_shard2/config/debug.log` for `VEHCACHE-LEAK` lines — each names the exact z-level,
   pointer, and recorded home submap position of a leaked vehicle. Use that provenance (which
   z-level, which abs submap position) to find the second code path that registers a vehicle into
   `vehicle_list` without a matching removal (grep `vehicle_list.insert(` and `.vehicle_list.erase(`
   across `src/map*.cpp` for asymmetric call sites at that location), apply the matching purge
   there (same pattern as step 1: clear before repopulate, or erase on removal), then remove this
   diagnostic block (restore `reset_vehicle_cache()` to its pre-diagnostic form, keeping only step
   1's fix) and re-run step 3 to confirm it now passes clean before continuing to step 4.

4. **Run the full acceptance test** (the doc's final closing bar) once step 3 passes clean:

   ```sh
   rm -rf test_user_dir
   timeout 1800 ./cata_test-tiles "~[coop]" --order decl --rng-seed 1 -r xml -o /tmp/vehfix_full.xml
   ```

   Run as a background job (this is the ~1000+ case unsharded suite; it previously took several
   minutes to reach case #483 under a debugger, longer un-instrumented). Expected: the process
   exits on its own (not via the `timeout` wrapper) with no SIGSEGV, and `/tmp/vehfix_full.xml`
   shows only the five accepted baseline failures listed in step 3 (no new failures, no crash). If
   it still SIGSEGVs past case #483 despite step 3 passing, that indicates a third, higher-threshold
   leak path — repeat the step 3a diagnostic procedure against this run's `test_user_dir/config/debug.log`
   instead of stopping.

5. **Close out the tracking docs** once step 4 passes:
   - `plans/vehicle-cache-sigsegv-handoff.md`: change the line 3 status line from `## Status: OPEN —
     root cause not found, not fixed. PRIORITY RAISED (2026-09-17): ...` to a `RESOLVED` status
     summarizing the root cause (missing `vehicle_list`/`zone_vehicles` purge in `map::load()`'s
     re-anchor path) and the fix (step 1's `clear_vehicle_list(z)` loop), and append a `## Resolution`
     section at the end of the file recording: the fix location/commit, the step-3 and step-4 run
     results (case counts, pass/fail), and — only if step 3a's contingency was needed — the second
     leak's provenance and fix.
   - `plans/merge-main-into-improvements.md`: update the "Open issue carried forward" paragraph
     (lines 27-41) and the S11 table row (line 24, the "**Shard 2 (314 cases) SIGSEGVs...**"
     sentence) to state the issue is resolved with a one-line pointer to the handoff doc's new
     `## Resolution` section; update the S6-outcome bullet (`lines 614-635`) from "**OPEN, root
     cause unconfirmed**" to resolved, and the "Gate verdict" paragraph (lines 643-653) to drop the
     "does NOT cover the unsharded single-process path" qualification once step 4 has actually run
     clean unsharded.

## Critical files & anchors

- `src/map.cpp:1544-1567` — `map::load()`, the fix location (step 1).
- `src/map.cpp:1885-1896` — `map::shift()`, the existing correct pattern being mirrored (clear
  `vehicle_list` per z before repopulating, `reset_vehicle_cache()` after).
- `src/map.cpp:2185-2218` — `loadn()`'s vehicle-registration block: confirms it only ever inserts
  into `vehicle_list`, never removes, which is why the pre-clear in step 1 is required.
- `src/map_vehicle.cpp:186-200` — `reset_vehicle_cache()`, touched only by the step 3a contingency.
- `src/map_vehicle.cpp:287-291` — `clear_vehicle_list(int zlev)`, the existing helper reused in
  step 1 (no new function to write).

## Assumptions & contingencies

- **Assumption (high confidence, not yet proven by a run):** the missing `vehicle_list` purge in
  `map::load()` is the actual root cause. Basis: it is a structural asymmetry against the already-
  correct `map::shift()` pattern in the same file, and `loadn()` is confirmed (by reading its body)
  to only ever insert, never remove, `vehicle_list` entries. Step 3's fast gate is the check: if it
  passes clean, the assumption holds; if not, step 3a's diagnostic is the pre-decided fallback —
  the implementer must not stall waiting for further direction, and must not skip straight to guessing
  a second fix without first capturing the `VEHCACHE-LEAK` provenance.
- **Non-goal, stated once so it is not attempted mid-fix:** the handoff doc's separate
  "per-tile → per-turn cache-membership batching" architecture idea (its own text: "does not by
  itself fix the reported SIGSEGV... a follow-on hardening pass, separate from this fix") is out of
  scope for closing this ticket and must not be implemented as part of this plan.

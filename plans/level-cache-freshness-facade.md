# Level cache freshness facade

Source of truth for this multi-session effort (mirrors wayfinder map #23; keep both in sync).

- Map: https://github.com/wavewarrior/Cataclysm-BN-Forked/issues/23
- Predecessor: `plans/level-cache-freshness.md` (tickets #9–#22 landed)
- Glossary: `GLOSSARY.md` — Property diff, Stamp door, Rebuild plan, The single door (commit `5ad6e6ce39`)

---

Consolidated plan for wayfinder map #23 ("Level cache freshness facade"). Every decision below was resolved in the map's tickets #24–#30; this file is the index — each cited ticket holds the full resolution. Vocabulary follows `GLOSSARY.md`.

## Problem Statement

The predecessor plan gave the freshness module a public interface of change kinds, but the facade has holes. Staleness leaks in and out at many sites: four hand-assembled property diffs around `ter_set`/`furn_set`/the two mapbuffer arms; ten public per-cache verbs whose subsets callers assemble by hand; twelve out-of-module raw bit writes through `mark`/`clear`/`assign`; raw submap-bool writes that disagree with the bitsets (the issue #7 shape); and three consumers that each re-derive the same freshness facts a different way — the CPU builder from bits, the GPU residency layer by generation polling, the render-frame gate by an XOR-fold plus file-static positional stamps. A caller must know the module's internals to use it correctly (shallow), the two freshness representations drift apart by construction, and the equivalence harness can only observe three disjoint derivations.

## Solution

Close the facade. Callers state facts — **change kinds** carrying old/new ids where the fact is a replacement — and the module computes the **Property diff** internally. The per-cache verbs go private; the public raisers are `report(...)`, `invalidate_level` (sole escape hatch), named **Stamp doors** for builder reporting, and a `translate` wrapper for the bubble shift: **The single door**. Bitset/flag agreement becomes a postcondition of every raise door. All three consumers receive one immutable **Rebuild plan** derived once per consumption point by `level_cache_freshness::plan_for`. Strict no-behaviour-change except the four adjudicated divergences (#24) and the coop bulk-swap superset (#25), each pinned.

## Implementation Decisions

- **Id-pair kinds, diff internalised** ([#24](https://github.com/wavewarrior/Cataclysm-BN-Forked/issues/24)). `terrain_replaced{at, old_id, new_id}` and `furniture_replaced{at, old_id, new_id}`; `report` computes the whole Property diff inside the module. The four production diff copies (`map::ter_set`, `map::furn_set`, both mapbuffer arms) collapse to constructing one kind each; effect-knowing sites (fields, traps, weather, vehicle parts) keep constructing `terrain_changed` directly. `sun_roof_above` unified — always computed (provably dead on terrain: zero carriers in data). Two deliberate corrections: lightmap raises on furniture opacity flips; `terrain_replaced` always queues the support check. Absorption: the diff evaluates the three predicates uniformly; raising is a per-kind policy field — off for local setters, on for remote-apply arms; the redundant z−1 mark drops (the `mark_floor` cascade covers it).
- **Verb layer private** ([#25](https://github.com/wavewarrior/Cataclysm-BN-Forked/issues/25)). The ten verbs (level_cache_freshness.h:190-227) move private. Public raisers after closure: `report`, `invalidate_level`, the stamp doors, `translate`. Kind-or-ADR rule governs future verb needs. Conversions: editmap regen → `regenerated_level{at}` (mirrors the five verbs exactly); post-report `mark_absorption` calls retire into the remote-apply policy; the signature detector (map.cpp:3462) → `report(light_changed{})`; memory sites → `memory_forgotten` (tile/level overloads); coop respawn → `report(world_replaced{area})`.
- **Stamp doors** ([#26](https://github.com/wavewarrior/Cataclysm-BN-Forked/issues/26)). The twelve raw bit writes convert to value-parameterised doors: `stamp_suspension_initialised`, `stamp_vehicle_floor(cache, bool)`, `stamp_veh_range(cache, bool)`, `stamp_gpu_download(cache, parts)` (colored-light value computed inside the door; the wipe is the negative value — positives and negatives are one concept). Worker own-level rule survives. The `vehicle_caching_internal_above` duplicates (map.cpp:2583, map_cache.cpp:631) merge. Afterwards `mark`/`clear`/`assign` + `freshness_parts` + `level_cache_part` shrink to module-private; `translate` keeps a public wrapper.
- **Submap flags owned** ([#27](https://github.com/wavewarrior/Cataclysm-BN-Forked/issues/27)). The module owns the raise, the submap keeps the bools, readers stay raw (no getter — fails the deletion test). Module-private `raise_submap_flags(submap&, parts)` lifted from `report(world_replaced)`'s marking loop; every public raise door funnels through it; bitset/flag agreement is a documented postcondition. `loadn`'s incremental arm becomes kind `submap_replaced{at}` (`pf_dirty` stays a raw call-site write — pathfinding dirt is not freshness). Coop tile-sync becomes `report(world_replaced{bbox})` — the issue #7 payoff; today's omission of `absorption_dirty` is pinned via the kind's absorption field. `mark_post_pass_changed` / `mark_submap_caches_dirty` stay in mapbuffer (non-resident staging, no bitset to agree with).
- **One rebuild plan** ([#28](https://github.com/wavewarrior/Cataclysm-BN-Forked/issues/28)). `level_cache_freshness::plan_for(map, viewer_pose, lightmap_policy)` derives one immutable value per consumption point:

```cpp
struct rebuild_plan {
    bool structure;
    std::vector<int> structure_levels;
    std::vector<int> transparency_levels, floor_levels,
                   vehicle_floor_levels, vehicle_obscured_levels;
    lightmap_disposition lightmap;   // { process, defer_without_escalation }
    bool visibility;
    residency_delta residency;       // per-level generation deltas + shift delta
    pose_stamps pose;                // compared opaquely
};
auto plan_for( const map&, const viewer_pose&, lightmap_policy ) -> rebuild_plan;
```

  `build_map_cache` takes `const rebuild_plan&` and stops re-deriving (its four `gpu_*_dirty_levels` locals become plan fields); the 16 direct callers go through the plan too. The escalation law lives in the type: a `skip_lightmap` refresh can only derive `defer_without_escalation`; the entity-light signature sample moves inside `plan_for`. GPU residency switches from pull to push: `apply_residency_events(plan.residency)` replaces `poll_lighting_residency`; GPU-side residency *validity* stays private to gpu_lm. The frame gate drops its file-static stamps and reads `plan.pose`/`plan.structure`; the plan is frame-granular and the module compares poses opaquely, never interpreting drift.
- **Superseding ADR** (text locked in [#28](https://github.com/wavewarrior/Cataclysm-BN-Forked/issues/28)'s resolution): *Retire the GPU pull-on-generations seam in favour of the pushed rebuild plan* — supersedes predecessor ticket #21's pull seam, preserves #20's residency-event guarantee, carried by `residency_delta` and pinned by the rewired jump-count tests. Lands as `docs/adr/0002-retire-gpu-pull-seam.md` with the GPU-push execution ticket.
- **Full-stack equivalence capture** ([#29](https://github.com/wavewarrior/Cataclysm-BN-Forked/issues/29)). `capture()`/`capture_generations()`/`record_queues()` loop `-OVERMAP_DEPTH..OVERMAP_HEIGHT`; `z_lo`/`z_hi` deleted; baseline becomes an `OVERMAP_LAYERS` array; generation-delta lines emit only when non-zero. A wrong-level dirty now fails the pin. Lands first, test-only.
- **Vocabulary** ([#30](https://github.com/wavewarrior/Cataclysm-BN-Forked/issues/30)). Property diff, Stamp door, Rebuild plan, The single door — in `GLOSSARY.md`; pins asserting "no raw writes remain" are single-door pins.
- **Lightmap signature producer** (the map's last fog patch, resolved at consolidation). Under the plan decision, `plan_for` samples the signature itself, so the `lightmap_needs_rebuild` → `map::invalidate_lightmap_if_light_state_changed` → `note_lightmap_source_signature` recursion dissolves: the detector verb retires (#25) and `lightmap_needs_rebuild` retires with the plan. The *producer* `map::current_lightmap_source_signature` (map.cpp:3287) **stays on map** — it is a hash walk over map contents (vehicles, items, light sources), the same category as `outside_checksum`, which also stays on map — while the module keeps owning the signature's storage and comparison. `plan_for` calls the map method directly; no code moves, the recursion dies.

## Testing Decisions

- One seam, unchanged from the predecessor: mutate world/viewer → standard refresh → query visibility/lighting/generations. The equivalence harness (`tests/level_cache_change_kinds_test.cpp`) is the review gate for every conversion; after #29 it captures the full z-stack, and once the plan exists it captures the `rebuild_plan` value itself — one capture format for all three consumers.
- Every new kind and every stamp door ships with an equivalence pin in the same commit: the pin replays the primitive sequence the conversion replaces. Sabotage-prove each pin (break the door, watch the pin fail).
- Deliberate-divergence pins (behaviour changes, recorded not frozen): opaque-furniture placement raises lightmap; support-removing terrain without `NO_FLOOR` queues the support check; coop bulk swap raises the superset including absorption.
- Single-door pins: after the privacy cutover, `mark`/`clear`/`assign`/`freshness_parts`/verbs are compiler-inaccessible outside the module TU — the compiler is the test; no whitelist comments.
- Postcondition test for #27: after any public raise door, bitset and submap bools agree for the covered submaps.
- #20's residency guarantee: jump-count tests (tests/level_cache_freshness_test.cpp:424-511, 573-591) rewire from `poll_lighting_residency` to `apply_residency_events` without weakening.
- Suite gate on every ticket: full `[level_cache_freshness]` + `[vision]` green against the recorded baseline; build `cataclysm-bn-tiles` alongside the test binary.

## Migration order

Frontier order (each ticket independently buildable, testable, revertable):

1. **T1 capture widening** (test-only, #29) — no production change.
2. **T2 id-pair kinds** (#24) — kinds + internal diff + the two corrections + absorption policy; four copies collapse; test reference arms lose the fifth copy.
3. **T3 submap flags** (#27) — `raise_submap_flags` + `submap_replaced` kind + coop `world_replaced` conversion; rides on T2's kinds.
4. **T4 stamp doors** (#26) — twelve sites + duplicate merge; primitives shrink to private at its tail.
5. **T5 verb privacy** (#25) — the last verbs converted (`regenerated_level`, `memory_forgotten`, detector → `light_changed`), verbs go private, single-door pins land.
6. **T6 rebuild plan** (#28) — `plan_for` + type + `build_map_cache` signature + 16-caller sweep + ADR-0002.
7. **T7 GPU push** (#28) — `apply_residency_events` replaces the poll; jump-count tests rewired; ADR status flipped to superseded-in-effect.
8. **T8 frame gate** (#28) — file-static stamps dissolved, gate reads the plan; capture gains the plan value.

T2–T5 are the single-door closure; T6–T8 are the plan closure. T3/T4 parallelise after T2; T7/T8 parallelise after T6.

## Out of Scope

- Execution of the closures themselves happens in the T1–T8 tickets filed on the tracker (this map's destination is the plan + filed tickets).
- Splitting `invalidate_level`'s 25 sites by trigger (rejected in #25 as ceremony).
- A `stale(submap, part)` getter (rejected in #27, deletion test).
- GPU residency *validity* tracking in the plan (stays private to gpu_lm, #28).
- The other architecture-review candidates (frame assembly, turn plan, action dispatch, screen registry, devui knobs, panel registry).

## Further Notes

- House constraint inherited: strict no-behaviour-change default; every deviation is an adjudicated micro-decision with a pin, never a silent diff.
- The coop absorption divergence (today's swap omits `absorption_dirty`) is a suspected latent bug the conversion deliberately widens away; if play-testing ever shows a problem, the pin is the revert map.
- `plan_for` must stay cheap (counter reads + bitset scans + one signature walk amortised by the existing detector cadence); it is called per presented frame by the gate.

## Implementation tickets (published 2026-10-02)

Filed as ordinary issues on this repo, children of map #23. Frontier order: #32 → #33 → {#34, #35 in parallel} → #36 → #37 → {#38, #39 in parallel}. Blocking lives in each issue's body (`Blocked by:` line); the REST dependencies endpoint proved unreliable on this repo.

| Issue | Ticket | Blocked by |
|---|---|---|
| [#32](https://github.com/wavewarrior/Cataclysm-BN-Forked/issues/32) | T1: Widen the equivalence capture to the full z-stack | — |
| [#33](https://github.com/wavewarrior/Cataclysm-BN-Forked/issues/33) | T2: Id-pair kinds and the internal property diff | #32 |
| [#34](https://github.com/wavewarrior/Cataclysm-BN-Forked/issues/34) | T3: Module-owned submap flags, submap_replaced kind | #33 |
| [#35](https://github.com/wavewarrior/Cataclysm-BN-Forked/issues/35) | T4: Stamp doors replace the raw bit writes | #33 |
| [#36](https://github.com/wavewarrior/Cataclysm-BN-Forked/issues/36) | T5: Verbs private - the single door | #34, #35 |
| [#37](https://github.com/wavewarrior/Cataclysm-BN-Forked/issues/37) | T6: One rebuild_plan; build_map_cache consumes it | #36 |
| [#38](https://github.com/wavewarrior/Cataclysm-BN-Forked/issues/38) | T7: Push residency events; retire the GPU poll | #37 |
| [#39](https://github.com/wavewarrior/Cataclysm-BN-Forked/issues/39) | T8: Frame gate consumes the plan | #37 |

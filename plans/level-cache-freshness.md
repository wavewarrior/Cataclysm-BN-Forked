# Level cache freshness

Source of truth for this multi-session effort (mirrors GitHub issue #8; keep both in sync).

- Map: https://github.com/wavewarrior/Cataclysm-BN-Forked/issues/1
- Spec: https://github.com/wavewarrior/Cataclysm-BN-Forked/issues/8
- Related co-op bug (separate): https://github.com/wavewarrior/Cataclysm-BN-Forked/issues/7
- Glossary: `GLOSSARY.md`

---

Spec for wayfinder map #1 ("Level cache invalidation owner"). All decisions below were resolved in the map's tickets (#2 inventory, #3 ordering, #4 plan sweep, #5 characterisation, #6 interface). Vocabulary follows `GLOSSARY.md`.

## Problem Statement

The upkeep of the per-z-level **Level cache** has no owner. Roughly two dozen dirty/valid bits and counters are written from many places, some through helpers and some by direct assignment, and read by different consumers with different assumptions about freshness. The consequences, as a developer experiences them:

- Whether visibility is recomputed depends on a map-wide flag that is never cleared. It starts true and stays true, so it works only because it accidentally forces the full map cache rebuild on every refresh. Nobody can safely change it, because that accidental trigger is what keeps visibility correct after the player moves or changes z-level.
- Different mutators raise different subsets of bits. Some terrain edits and some entity-light updates change visibility inputs but raise no visibility bit, so visibility can be read stale on paths that skip the full rebuild.
- Several bits are write-only and two functions are dead, which hides the real contract.
- The GPU lighting layer learns about CPU changes through several unrelated paths (direct invalidate calls from three places, plus internal staleness writers, plus two generation counters polled by the renderer). The vehicle-move path changes transparency without bumping the generation the renderer polls.
- Tests re-implement the invalidate, build, update sequence by hand in many places, so a change in the contract breaks tests unpredictably and no test states the contract.

## Solution

Introduce one module, **Level cache freshness**, that owns whether each Level cache is fresh and what must be rebuilt. Code that changes the world or the viewer reports a **change kind** from a small closed set; the module derives every bit and generation. The accidental "always rebuild" aggregate is replaced by an explicit **view stale** condition that preserves exactly the rebuilds the accidental behaviour provided and no more. Visibility readers keep their current optimistic answer while a level is stale, but that becomes a documented contract (**geometry-only visibility**). The GPU lighting layer stops being called into and instead polls per-level generations that the module guarantees to advance on every change kind. Tests use one shared helper that goes through the same public refresh path production uses.

## User Stories

1. As a gameplay developer changing terrain, I want to report "terrain changed here" and nothing else, so that I never have to know which cache bits to raise.
2. As a gameplay developer adding a new light source, I want to report "light changed", so that visibility and lighting update without me touching cache internals.
3. As a vehicle developer, I want a moving vehicle to report one change kind per move, so that transparency, floor, outside, visibility and GPU residency all stay consistent.
4. As a developer, I want the set of change kinds to be small and closed, so that I can read it once and know every way the world or viewer can affect the Level cache.
5. As a developer, I want no bit-level dirty setters to exist after migration, so that a future mutator cannot raise a wrong subset again.
6. As a player moving through the world, I want what I can see to be correct immediately after I move or change z-level, so that I never act on stale visibility.
7. As a player in a vehicle, I want my view to stay correct as the vehicle moves, so that I can drive safely.
8. As a player targeting a spell or ranged weapon, I want visibility checks inside the targeting loop to be cheap, so that targeting stays responsive.
9. As a player watching a projectile animation, I want the animation to use up-to-date visibility without redundant full recomputes per frame.
10. As a player, I want terrain I change (walls destroyed, doors opened) to affect line of sight and lighting on the next relevant refresh, including edits that today raise no visibility bit.
11. As a player near a burning creature or a lit character, I want the light they carry to be reflected in what I see without waiting for an unrelated change.
12. As a player in co-op, I want my client to rebuild caches after a full tile sync, so that swapped terrain looks and behaves correctly. (The tile-sync defect itself is tracked separately; this module must make the correct report possible.)
13. As a maintainer, I want one place that documents which caches depend on which, so that the phase order is not tribal knowledge.
14. As a maintainer, I want the phase order of the full cache build (floor, then top-down outside, then transparency, then per-level parallel caches, then serial suspension, then serial vehicles, then lightmap, then absorption, then seen) preserved exactly, so that existing correctness constraints survive the migration.
15. As a maintainer, I want the serial constraints (suspension, vehicles writing neighbouring levels, outside being top-down) to remain serial, so that no data race is introduced.
16. As a maintainer, I want change-kind calls restricted to the main thread and asserted, so that misuse from a worker thread fails loudly instead of racing.
17. As a maintainer, I want parallel per-level builders to keep clearing only their own level's state, so that the parallel region stays race-free.
18. As a renderer developer, I want every change kind to advance a per-level generation I can poll, so that the GPU residency layer never misses a change.
19. As a renderer developer, I want the polled generations to advance even when the content ends up identical, so that residency work that depends on the event rather than the content still happens.
20. As a renderer developer, I want the existing structure-rebuild gate to keep working unchanged, so that the SDF and sky-visibility gating does not regress.
21. As a renderer developer, I want direct calls from map code into the GPU residency layer removed, so that map code no longer depends on the GPU layer.
22. As a developer of GPU lighting, I want GPU readback of seen/visibility/lightmap state to update the module's bookkeeping through the module, so that CPU and GPU views cannot silently diverge.
23. As a test author, I want a single test helper that brings the Level cache to a fresh state, so that I do not hand-roll invalidate/build/update sequences.
24. As a test author, I want that helper to call the same public path production calls, so that tests prove production behaviour rather than a test-only path.
25. As a test author, I want to assert "after change kind X, visibility reads correctly after the standard refresh", so that the contract is tested through its external behaviour.
26. As a test author, I want a pinning test that shows when a refresh is required and when skipping it leaves visibility stale, so that the behaviour change in the aggregate replacement is gated by evidence.
27. As a test author, I want a test that counts expensive visibility recomputations per turn, so that the claimed saving from removing redundant refreshes is measured, not assumed.
28. As a reviewer, I want each migration stage to be independently buildable and testable, so that I can review and revert one stage at a time.
29. As a reviewer, I want the behaviour change confined to a single named stage plus the stale-bug fixes, so that behaviour-preserving stages can be reviewed as pure refactors.
30. As a maintainer, I want write-only bits deleted (the four identified) so that no one infers meaning from state nothing reads.
31. As a maintainer, I want the dead clean-aggregate function and the dead cache-validity predicate deleted, so that dead code stops suggesting a contract that does not exist.
32. As a developer reading visibility code, I want the documentation of the dirty-read behaviour to state plainly that a stale level yields line-of-sight-only answers, so that I know when to refresh first.
33. As a developer of AI or stealth features, I want to know that exact visibility requires a refresh first, so that I choose the right call.
34. As a developer, I want the module's vocabulary to match the repo glossary ("structure rebuild", "view stale", "change kind"), so that code, plans and tests speak the same language.
35. As a developer of the amortised non-player z-rebuild plan, I want this module to expose per-level freshness cleanly, so that a future budgeted rebuild can skip levels without a second dirty-tracking mechanism.
36. As a maintainer concerned with the GPU compute path, I want its internals untouched, so that this effort does not reopen GPU pass design.
37. As a maintainer, I want the unused GPU visibility passes left alone, so that this effort does not entangle with their separate plan.
38. As a co-op developer, I want the co-op full tile sync to be able to report a world-replaced change kind for the affected area, so that the follow-up fix for the sync defect is a one-line report rather than new cache logic.
39. As a performance engineer, I want per-turn invalidation to remain per-submap rather than blanket, so that the shipped incremental lightmap and shift-translate gains are not lost.
40. As a performance engineer, I want no deferral of non-player z-level rebuilds introduced, so that falling, z-stack rendering, 3D vision and the solar cascade keep their freshness assumptions.
41. As a maintainer, I want the global lightmap readiness latch to stay global, not per level, so that the existing fail-bright fix is not undone.
42. As a maintainer, I want the render-only, headless and co-op checksum invariants preserved, so that no gameplay-visible or network-visible state changes through the GPU subscriber.
43. As a developer, I want a vehicle move to keep forcing the lightmap rebuild it forces today, so that batching of vehicle-move notifications keeps its semantics.
44. As a developer, I want player movement within a submap and z-level changes to rebuild what visibility needs, so that removing the aggregate does not break them.
45. As a maintainer, I want the module to be testable without a graphics device, so that headless test builds cover it.
46. As an upstream-conscious maintainer, I want the change to stay within this fork's issue tracker and branches, so that nothing is filed against the upstream project.

## Implementation Decisions

- **New module: Level cache freshness.** Owns every dirty/valid bit and generation counter of the Level cache, the map-level companions (last seen origin, lightmap source signature, solar stamps), and the knowledge of which caches depend on which. Its interface is the closed set of change kinds, a view-stale query, per-level generation reads, and the entry points that run the standard refresh. Everything else is implementation.
- **Change kinds.** A small closed set: terrain changed, light changed, vehicle moved, player moved, z-level changed, map shifted, plus a world-replaced kind for bulk replacement of an area (needed so the co-op tile sync can report correctly). Each kind carries only the location/level information the module needs. No bit-level setters remain in the public surface after migration.
- **Aggregate replaced by view stale.** The never-cleared map-wide visibility aggregate is deleted. A view-stale condition derived from the last origin and z-level the cache was built for, plus dirty inputs, decides whether the full map cache rebuild must run before visibility is read exactly. This must reproduce the rebuilds the aggregate accidentally forced on player move and z-level change, and must not rebuild more than necessary within a turn. This is the only intentional behaviour change in the main line, and it is gated by the pinning test.
- **Geometry-only visibility is a contract.** While a level's visibility is stale, visibility queries answer from line of sight only (optimistic). The contract is documented at the interface; readers needing exact results refresh first. No lazy refresh inside read queries, no pessimistic fallback.
- **GPU seam is pull.** The GPU residency layer polls per-level generations. The module guarantees that **every change kind advances a per-level residency generation**, including vehicle moves, map shifts and load, which today bypass the transparency generation. The residency generation is event-based (advances even if content ends up identical) and is distinct from the existing content-gated outside generation, which keeps its current meaning for the renderer's structure-rebuild gate. The existing transparency generation keeps its meaning. The direct invalidate calls from map code and the internal staleness writers are replaced by this polling. The GPU compute passes themselves are unchanged.
- **Threading.** Change-kind calls are main-thread only and asserted, following the existing worker-thread assertion pattern. Parallel per-level builders continue to clear only their own level's state. No locks or atomics are added.
- **Phase order preserved.** The full cache build order and its serial/parallel split are unchanged; the module encodes the dependency between phases in one place rather than changing it.
- **Dead and write-only state removed.** The four write-only bits, the never-called clean function and the inverted never-called validity predicate are deleted in the final stage.
- **Stale-bug fixes in scope.** Terrain edits that change visibility inputs without raising a visibility bit, and entity-light updates to the lightmap that never raise the visibility bit, are fixed as a stage after the pinning test lands. Because change kinds raise the right dependents by construction, the fix is the correct mapping from change kind to dependents. The co-op tile-sync defect is tracked separately; this module only provides the world-replaced kind it will use.
- **Migration stages**, each independently buildable and testable: (0) pinning tests and the shared test helper; (1) route the direct visibility writers and bit setters through the module, behaviour-preserving; (2) change-kind verbs replace bit setters at call sites; (3) view-stale replaces the aggregate (behaviour change, gated by stage 0); stale-bug fixes follow here; (4) GPU generation pull replaces the direct invalidate callers and internal staleness writers; (5) delete write-only state and dead functions.
- **Constraints inherited from existing plans.** Do not introduce a second dirty-tracking mechanism (derive any GPU dirty lists from the persistent state); do not reintroduce blanket per-turn or per-player-move invalidation; do not defer non-player z-level rebuilds; keep the lightmap readiness latch global; keep the structure-rebuild and visibility-rebuild gates fed by the same generations; keep the GPU subscriber render-only and checksum-neutral.
- **Vocabulary.** Terms are defined in the repo glossary: Level cache, Level cache freshness, Change kind, Structure rebuild, View stale, Geometry-only visibility.

## Testing Decisions

- **What makes a good test here:** assert external behaviour only. Given a change kind and the standard refresh, what does a visibility, line-of-sight or lightmap query return, and which generations advanced. Never assert which internal bit was set or the order of internal calls.
- **One seam:** the existing map-level surface callers already use (mutate world or viewer, refresh, query visibility/lighting/generations), plus one shared **test-only helper** that performs the standard refresh through the same public path production uses. No production-only "settle" entry point is added. No new seam beyond this.
- **Stage 0 pinning tests** (before any behaviour change): visibility after player move, after z-level change, after terrain edit, after light change, and after a vehicle move, each followed by the standard refresh; a test that a refresh without the full rebuild leaves a stale answer where the characterisation says it does; a measurement of expensive visibility recomputations per turn to quantify any saving.
- **Generation tests:** every change kind advances the per-level residency generation; the content-gated outside generation still does not advance when content is unchanged; the transparency generation behaviour is unchanged.
- **Contract test for geometry-only visibility:** a stale level answers optimistically from line of sight; after refresh it answers exactly.
- **Helper adoption:** the existing hand-rolled sequences (a small number of full trios across a handful of test files and a larger number of looser sites) migrate to the helper in stage 0 or as each stage lands.
- **Build flavour:** visibility behaviour under test requires the tiles/GPU-enabled test build; tests that depend on it are guarded accordingly, consistent with existing tests.
- **Prior art:** the z-level visibility cache tests, the vision tests, the ranged aiming dirty-cache test, and the vehicle autodrive test that sets the aggregate; the window-portal direction test as an example of testing derived light behaviour through the map surface.
- **Not tested directly:** GPU compute internals, phase-order internals, and the co-op network layer (covered by its own bug).

## Out of Scope

- The window-portal light rule duplicated across CPU lightmap, GPU lightmap and the emitter snapshot.
- A single producer for the viewer's visibility parameters.
- Deleting the unused GPU visibility and sight-pair compute passes.
- Amortised or budgeted rebuild of non-player z-levels.
- GPU compute pass internals and shader changes.
- The co-op bulk tile sync defect (tracked as its own bug); this spec only provides the world-replaced change kind it will call.
- Any change to gameplay rules of visibility beyond making the existing optimistic behaviour an explicit contract.

## Further Notes

- The characterisation showed the always-true aggregate is load-bearing, so the aggregate must not be "fixed" by simply clearing it. Evidence and per-mutator verdicts are in the map's characterisation ticket.
- The "pull" seam needs the event-based residency generation described above; this was derived during spec writing from the finding that vehicle moves currently reach the GPU layer only through a direct call and never bump the transparency generation. If implementers prefer reusing an existing generation instead of adding one, the requirement that every change kind advances something the GPU layer polls still holds.
- Ordering risk: stage 3 touches the paths used by spell targeting, animations, creature hits and autodrive, which call the refresh helper without the full rebuild. The stage 0 test must cover those call patterns before stage 3 merges.
- Map, tickets and evidence: issue #1 and its closed child tickets; co-op defect: #7.

## Implementation tickets (published 2026-10-01)

Frontier order: #9 -> #10 -> #11; then #12-#15 in parallel; #16 after all four; #17 and #20 after #16; #18, #19 after #17; #21 after #20; #22 last.

| Issue | Ticket | Blocked by |
|---|---|---|
| #9 | Pinning tests and shared test helper | none |
| #10 | Module takes ownership of dirty/valid bits | #9 |
| #11 | Expand: change kinds alongside bit setters | #10 |
| #12 | Migrate terrain, furniture, field, trap, weather | #11 |
| #13 | Migrate light sources | #11 |
| #14 | Migrate vehicles | #11 |
| #15 | Migrate viewer, shift, load, activity | #11 |
| #16 | Contract: remove bit-level setters | #12-#15 |
| #17 | View stale replaces the aggregate | #16 |
| #18 | Fix stale visibility, terrain edits | #17 |
| #19 | Fix stale visibility, entity lights | #17 |
| #20 | Per-level residency generation | #16 |
| #21 | GPU layer polls generations | #20 |
| #22 | Delete write-only bits and dead functions | #17, #21 |

Landed: #18 (e77ab629f8), #19 (98f124b097 + 282bfd2988, sabotage-proven; `[vision]` gate
unchanged vs baseline), #20 (0dba51deec + 35d24efa68, sabotage-proven; suite 179/30).
Remaining frontier: #21, then #22.

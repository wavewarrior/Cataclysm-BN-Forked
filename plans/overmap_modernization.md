## STATUS (reviewed 2026-06-27)
**0% DONE — KEEP (not started, gated).** `overmap.cpp` still **6,700 lines**; no `overmap_generate.cpp` / `_specials.cpp` / `_connections.cpp` / `_mongroups.cpp`; no `tests/overmap_determinism_test.cpp`. Plan is the *post-review* rewrite (real method names verified, RNG-ordering risk model sound) and matches reality. Hard-gated on Phase 0 determinism harness — explicit "abandon if non-deterministic" exit clause makes this honest, not aspirational fluff. Keep, but note the whole thing is blocked behind a non-trivial Phase 0.

# Overmap Decomposition — Plan

> Scope rewritten 2026-06-23 after review. The prior version named ~10 functions
> that do not exist (`path_to`, `cost_for_path`, `get_connection_to`,
> `place_terrain`, `place_buildings`, `Spawn_rotation`, `place_extra`,
> `build_connections`, `connect_road_t_types`, `note_string`), invented two data
> members (`std::vector<overmap_special_placement>`, `std::vector<overmap_connection> connections`),
> and a non-existent `from_legacy()` shim. Real names and the actual risk model
> are below.

## Context

`overmap.cpp` is ~6700 lines — second-largest gameplay-core file. The only
defensible win here is **decomposition for navigability and compile time**.
There is **no perf benefit**: every target (`generate`, `place_*`,
`build_connection`) runs at world/OMT first-load and is then cached
(`mapbuffer::generate_omt`). State that plainly so nobody mistakes this for a
runtime optimization.

### The real risk: worldgen RNG call-ordering

Overmap generation is seeded RNG. The order of RNG-consuming calls determines
the map. A file split that *only* moves whole functions to new translation units
is RNG-safe. But promoting the **14 file-local `static` helpers** in
`overmap.cpp` (anonymous-namespace / TU-local functions) to shared headers, or
reordering anything, can perturb call order and silently change every
same-seed map. There is currently **no same-seed regression harness**. Building
one is therefore Phase 0 and gates everything else.

## Phases

### Phase 0 — Same-seed regression harness (gate)

Before touching any code, add a test that pins worldgen determinism:
- Construct an `overmap` at a fixed seed/point, run generation, and hash a
  canonical serialization (oter ids per tile + city list + connection list +
  special placements).
- Store the golden hash. Test fails if generation output changes.
- Run it green on `main` first to prove it is stable, then keep it green through
  every subsequent phase. **If Phase 0 cannot be made deterministic, stop** —
  the decomposition is too risky to verify and should be abandoned.

Verification: test passes twice in a row on unchanged source (no hidden
nondeterminism from global state / time / `rng()` seeding).

### Phase 1 — Move whole functions to new TUs (no helper promotion)

Split by real, verified method groups. Move only complete `overmap::` member
definitions; leave all TU-local `static` helpers where they are *unless* a moved
function is their sole caller (then move the helper with it, still TU-local in
the new file). Do **not** promote helpers to headers in this phase.

| New file | Real `overmap::` methods to move |
|---|---|
| `overmap_generate.cpp` | `place_cities`, `place_building`, `pick_random_building_to_place`, `place_forests`, `place_forest_trails`, `place_forest_trailheads`, `place_swamps`, `place_lakes`, `place_rivers`, `place_river`, `place_roads`, `polish_rivers` |
| `overmap_specials.cpp` | `place_specials`, `place_special`, `place_special_attempt`, `place_special_forced`, `place_special_custom`, `random_special_rotation`, `overmap_special_at` |
| `overmap_connections.cpp` | `build_connection` (both overloads), `lay_out_connection`, `lay_out_street`, `populate_connections_out_from_neighbors`, `set_electric_grid_connections`, `is_path` — merge with existing `overmap_connection.cpp` if cohesive |
| `overmap_mongroups.cpp` | `place_mongroups`, `process_mongroups`, `signal_hordes`, `move_hordes`, `monster_check`, `mongroup_check`, `place_nemesis`, `move_nemesis`, `remove_nemesis`, `signal_nemesis` |

Notes:
- `overmap.cpp` keeps ctor/dtor, serialize/deserialize, `generate` (the
  orchestrator), notes (`has_note`/`mark_note_dangerous`/etc.), and accessors.
- The actual overmap *travel* pathfinding is `overmapbuffer::get_travel_path()`
  (`overmapbuffer.cpp:1086`), not in overmap.cpp — there is **no**
  `overmap_pathfinding.cpp` to create. `lay_out_*` just calls the header-only
  `pf::directed_path` template from `simple_pathfinding.h`.
- Do **not** merge `add_note`/`delete_note` (player map notes) into the existing
  `overmap_label.cpp` — that file is an `oter_type_str_id`→string *terrain-label*
  registry, an unrelated system (and a third file `overmap_label_note.cpp`
  exists). Notes stay in `overmap.cpp`.

Verification: Phase 0 harness green (identical hash). `wc -l src/overmap.cpp`
drops. `src/CMakeLists.txt` updated.

### Phase 2 — Encapsulate the author-flagged public members (optional)

`overmap.h:433` carries `// TODO: make private` over: `radios`, `vehicles`,
`cities`, `connections_out`, `connection_cache`. These are the real targets (the
prior plan's `overmap_special_placements` is **already private** at `overmap.h:473`
and is an `unordered_map`, not a vector).

Reality check that caps the value: `friend class overmapbuffer;`
(`overmap.h:457`) already grants the one significant external consumer full
access (only `om->cities` at `overmapbuffer.cpp:1807-1808` reads these directly;
`overmap_ui.cpp` / Lua bindings have zero direct hits). So encapsulation here is
near-cosmetic. **Do this phase only if Phase 1 lands clean and the churn is
judged worth it** — otherwise skip.

Verification: Phase 0 harness green. Build green.

## Out of scope (cut from prior plan)

- "Complete JSON migration / remove `from_legacy()`" — no such shim exists
  (grep: 0 hits). The `overmap.h:160` TODO is about an ore-spawn rate table,
  unrelated. Dropped.
- File-wide C++23 sweep (ranges/std::expected/options-structs) on cold worldgen
  code — pure churn, RNG-ordering risk, no payoff. Dropped.

## Files

| File | Phase |
|---|---|
| `tests/overmap_determinism_test.cpp` (new) | 0 |
| `src/overmap.cpp` (shrinks) | 1 |
| `src/overmap_generate.cpp` / `_specials.cpp` / `_connections.cpp` / `_mongroups.cpp` (new) | 1 |
| `src/CMakeLists.txt` | 1 |
| `src/overmap.h`, `src/overmapbuffer.cpp` | 2 (optional) |

## Effort
- Phase 0: 1–2 days (the hard part — making worldgen deterministically testable)
- Phase 1: 2–4 days (mechanical once Phase 0 guards it)
- Phase 2: 1 day (optional)

If Phase 0 proves worldgen non-deterministic in a way you can't pin, abandon the
whole plan — an unverifiable 6700-line split is not worth the regression risk.

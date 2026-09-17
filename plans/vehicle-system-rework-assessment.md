# Vehicle system rework assessment

The question asked: is Cataclysm-BN-Forked's vehicle system a candidate for a full rework —
evaluating every option including radical ones (e.g. decoupling vehicles from the tile grid,
Project-Zomboid style)? The verdict reached: **no.** The vehicle data model, part system and JSON
content interface show no measured pathology, and the 104-case test suite — including ~345 frozen
drag constants and a per-prototype efficiency table — is an expensive calibration asset a rewrite
would destroy with no oracle to recover it. Two things *are* pathological: an identity/notification
system with thirteen unsynchronised raw-pointer holders around a submap-owned object, and an
unfinished Box2D-authority migration leaving six live position representations reconciled every
turn. Grid-decoupled continuous movement and rotation is an adopted goal — the vehicle layer is
already roughly 70% there — but it is reachable only as an ordered six-stage program, not a
vehicle-scoped rewrite.

## Scope and method

Read in-tree at HEAD `030cfc6576` (branch `feature/merge-dev-into-improvements`, verified current):
`src/vehicle*.{cpp,h}`, `src/map_vehicle.cpp`, `src/vehicle_query.cpp`, `src/vehicle_parts.cpp`,
`src/physics/*`, `src/mapbuffer.{cpp,h}`, `src/map.{cpp,h}`, `src/map_cache.cpp`, `src/lightmap.cpp`,
`src/creature_tracker.{cpp,h}`, `src/coop_client.cpp`, `src/coop_server.{cpp,h}`, `src/coop_proto.h`,
`src/cata_tiles.cpp`, `src/cata_tiles_draw_layers.cpp`, `src/compute/gpu_lm.{cpp,h}`, five lighting
compute shaders, `src/ballistics.cpp`, `src/explosion.cpp`, `src/game.cpp`, `src/game_movement.cpp`,
`src/game_world_tick.cpp`, `src/player.cpp`, `src/character.cpp`, `src/monster.cpp`,
`src/vehicle_move.cpp`, and the full `tests/vehicle_*.cpp` / `tests/coop_vehicle_test.cpp` /
`tests/npc_vehicle_muscle_test.cpp` / `tests/ranged_vehicle_recoil_test.cpp` suite, plus
`plans/box2d-vehicle-physics-implementation.md`, `plans/vehicle-cache-sigsegv-handoff.md`,
`plans/merge-main-into-improvements.md` and `VEHICLE_CACHE_SIGSEGV_PLAN.md`.

Not done: no build and no test run. An `osx-arm-slim` build is ~20 minutes and this assessment does
not depend on runtime numbers — every claim below is either a static code fact or a citation to an
already-recorded test/benchmark result. Where a stage's design needs a runtime number that does not
exist yet (e.g. actual per-turn cache-churn counts), that is stated explicitly as unmeasured, with the
exact command that will measure it, rather than guessed.

Every claim below carries a `path:line` citation. All citations were re-read this session against
the HEAD above; two errors found in an earlier evidence draft are corrected in place (E3, E9) rather
than carried forward silently.

## Measured state

### E1 — Size and churn

|Metric|Value|Source|
|---|---|---|
|Vehicle subsystem size|24,777 lines across `src/vehicle*.{cpp,h}`, `src/map_vehicle.cpp`, `src/physics/*`|`wc -l`, this session|
|Largest units|`vehicle.cpp` 3275, `vehicle_move.cpp` 2324, `vehicle_use.cpp` 2252, `vehicle.h` 1917, `map_vehicle.cpp` 1836, `vehicle_physics.cpp` 1285|`wc -l`, re-verified this session — exact match|
|Debt markers (TODO/FIXME/HACK/XXX/legacy/temporary/workaround)|168 across those files; worst `vehicle.cpp` 32, `vehicle_part.cpp` 24, `vehicle_use.cpp` 22, `vehicle_move.cpp` 18|regex scan|
|Commits touching `vehicle.cpp`/`vehicle_move.cpp`/`map_vehicle.cpp`/`vehicle_physics.cpp`/`physics/` since 2026-03-01|120 of 2640 total commits in that window (4.5%)|`git log --oneline --since=2026-03-01`, re-verified this session — exact match (120/2640)|
|Of those, `fix`/`revert`-prefixed|45 (37.5% of vehicle commits)|re-verified this session — exact match|
|Box2D build gating|none left: zero `BOX2D_ENABLED` occurrences as a compile guard in `src/` (one appears inside an explanatory comment, `src/CMakeLists.txt:251`, stating explicitly there is no guard); `BOX2D` the token appears 11× total: `CMakeLists.txt:1055-1056` (FetchContent options), `src/CMakeLists.txt:251` (comment), `src/debug_menu.cpp` ×3, `src/map.h` ×2, `src/physics/physics_world.h` ×1, `tests/vehicle_efficiency_test.cpp` ×1, `tests/vehicle_test.cpp` ×1|re-verified this session by full-repo scan — exact match (11)|

### E2 — Coupling surface (why "just decouple from the grid" is not vehicle-scoped)

|Metric|Value|
|---|---|
|`veh_at(` callsites|370 across 116 files (of 1264 `src/` files)|
|`vpart_position` mentions|399 across 176 files|
|`vehicle *` raw-pointer declarations|412 across 110 files|
|`part_with_feature`|270 across 68 files|

All four counts re-derived this session by regex scan of every `src/*.cpp`/`src/*.h` file (1264
files scanned): `veh_at\s*\(` → 370/116; `vpart_position` → 399/176; `vehicle\s*\*` → 412/110 — exact
match to the figures above.

Heaviest `veh_at(` consumers: `mapgen.cpp` 17, `activity_item_handling.cpp` 12, `map_bash.cpp` 11,
`avatar_action.cpp` 11, `iexamine_electronics.cpp` 10, `game_action.cpp` 9.

`veh_at(` callsites grouped by consuming subsystem (filename-bucket classification, 370 total):
map/terrain/mapgen 95 · player actions & activities 93 · items/inventory/crafting 49 · vehicle
subsystem itself 24 · ranged/melee/ballistics 19 · AI/monsters/NPC/pathfinding 16 · render/UI 9 ·
fields/sounds/weather 5 · lua/coop 1 · 59 in 23 unbucketed files (largest:
`iexamine_electronics.cpp` 10, `game_misc.cpp` 7, `monmove.cpp` 6, `game.cpp` 6, `bionics.cpp` 5).
The distribution matters for the verdict: "is there a vehicle at this tile" is a *world query* used
by nearly every gameplay system, not a vehicle-internal detail, so its contract cannot be changed
cheaply — but it also means fixing that one query's safety fixes every consumer at once.

### E3 — Position/motion state: six live representations

|Representation|Field|Anchor|
|---|---|---|
|Owning submap address|`vehicle::abs_sm_pos`|`src/vehicle.h:1754`|
|Tile offset inside submap (serialized)|`vehicle::sm_ms_pos`|`src/vehicle.h:1756`|
|Discrete heading / intent|`turn_dir`, `last_turn`, `face`, `move` (`tileray`)|`src/vehicle.h:1781`, `:1783`, `:1820`, `:1822`|
|Turn budget (legacy mover)|`of_turn`, `of_turn_carry`|`src/vehicle.h:1785`, `:1787`|
|Continuous physics transform|`physics_pos` (tile units, float), `physics_angle` (rad), `angular_velocity_rads`|`src/vehicle.h:1793`, `:1795`, `:1790`|
|Render residual|`render_offset_x/y` (fractional part of `physics_pos`, not serialized)|`src/vehicle.h:1799`, `:1800`|
|Whole-vehicle rotation cache|`vehicle::pivot_rotation[2]`, `vehicle::pivot_anchor[2]`|`src/vehicle.h:1812`, `:1818`|
|Per-part rotation cache (double-buffered)|`vehicle_part::precalc[2]`|`src/vehicle_part.h:225`|
|Box2D body|`PhysicsWorld::vehicle_bodies_` (`vehicle*` → `b2BodyId`)|`src/physics/physics_world.h:188`|

*(Correction: an earlier draft attributed `pivot_rotation`/`pivot_anchor` and `precalc` jointly to
`vehicle_part` at `vehicle.h:1812,1818`. Re-read this session: `pivot_rotation` and `pivot_anchor`
are `vehicle` members at those exact lines; `precalc` is a separate `vehicle_part` member at
`vehicle_part.h:225`, a different class in a different file. The count of live representations —
six — is unaffected; only the ownership label is corrected.)*

Authority switch: `vehicle::box2d_position_authority` (`src/vehicle.h:1805`) — set by
`on_vehicle_added()`, cleared by `on_vehicle_removed()`; `PhysicsWorld` separately tracks
`authority_revoked_by_unload_` (`src/physics/physics_world.h:196`) because a self-opted-out vehicle
(e.g. every vehicle in `tests/vehicle_ramp_test.cpp`) is indistinguishable from an unload revocation
by the flag alone.

### E4 — The continuous model is already live, and already drives the renderer

`map::vehmove()` (`src/map_vehicle.cpp:472-871`) does, in order: fund `of_turn` from the per-z
`vehicle_list` caches, run `act_on_map()` in an `of_turn` priority queue, then
`phys_world->step_turn(1.0f)` — a full game second, internally sub-stepped to avoid tunneling 1-tile
terrain bodies (`src/physics/physics_world.h:128-139`) — and then *walk the tile anchor one tile at a
time* toward `lround(physics_pos)` through `move_vehicle()`, bounded at 64 tiles
(`src/map_vehicle.cpp:700-777`), rewinding `physics_pos` and the body when the walk stops short, and
finally writing `render_offset_x/y` from the sub-tile residual. The renderer consumes that residual:
`src/cata_tiles_draw_layers.cpp:660-662` (vehicle sprite transform, confirmed this session — comment
reads "Vehicle smooth render offset (Box2D sub-tile residual)") and `:883-885` (rider sprite
transform, confirmed this session — comment reads "Propagate vehicle sub-tile offset so passengers
slide with the vehicle").

So Box2D already decides *where and how far*, and `move_vehicle()` decides *what happens on the way*
— that split is deliberate and documented in-place (`src/map_vehicle.cpp:733-749`), because deriving
bash force from Box2D contact impulses "would invent a balance model with nothing to validate it
against".

### E5 — The real defect class: one object, thirteen unsynchronised raw-pointer holders

Ownership is `std::vector<std::unique_ptr<vehicle>> submap::vehicles` (`src/submap.h:314`) — i.e.
vehicles are owned by the submap they are anchored in, and freed when `MAPBUFFER` evicts it.
Raw `vehicle*` is additionally held by:

|Holder|Coordinate frame|Invalidation|
|---|---|---|
|`level_cache::vehicle_list`|per-z set|manual (`clear_vehicle_list`, `src/map_vehicle.cpp:287`)|
|`level_cache::zone_vehicles`|per-z set|manual (same function)|
|`level_cache::veh_cached_parts`|bubble-relative `tripoint_bub_ms`|manual (`clear_vehicle_point_from_cache`, `:239`)|
|`level_cache::veh_exists_at` + `veh_in_active_range`|bubble-relative bitset|manual, must stay in sync with the above|
|`map::cached_veh_rope`|bubble-relative (`src/map.h:2453`)|manual, column-wise|
|`map::dirty_vehicle_list`, `map::last_full_vehicle_list`|raw ptr set/list (`src/map.h:1994`, `:2451`)|manual|
|`mapbuffer::loaded_vehicles_`|registry (`src/mapbuffer.h:916`)|`register/unregister_vehicle`|
|`mapbuffer::vehicle_footprint_by_location_`|**absolute** `tripoint_abs_ms` (`src/mapbuffer.h:917`)|self-validating on lookup|
|`mapbuffer::vehicle_footprint_locations_`|reverse index (`src/mapbuffer.h:919`)|paired with the above|
|`PhysicsWorld::vehicle_bodies_`|body registry|`on_vehicle_removed`|
|`PhysicsWorld::authority_revoked_by_unload_`|set|submap load/unload|
|`coop_server::vehicle_id_map_` / `_rev_`|uint32 vid ↔ `vehicle*` (`src/coop_server.h:272-274`)|cleared on session reset (confirmed this session: `src/coop_server.cpp:1802-1803`)|
|`overmapbuffer` vehicle tracking|tracked-vehicle list (`src/map_vehicle.cpp:385`)|`remove_vehicle` on detach|

`map::veh_at()` resolves through the *fragile* one: `veh_in_active_range` → `veh_exists_at` bitset →
`veh_cached_parts` (confirmed this session, `src/map_vehicle.cpp:1478-1506`), returning whatever raw
pointer it finds via `it->second.first` with no liveness check — the only guard is an
`inbounds`/active-range check, not an object-liveness check. That is the crash in
`plans/vehicle-cache-sigsegv-handoff.md`.

### E6 — A correct pattern for the same query already exists in-tree

`mapbuffer::indexed_vehicle_part_at_unlocked()` (`src/mapbuffer.cpp:1327-1366`) answers the identical
question — "which vehicle part is at this tile" — keyed on **absolute** `tripoint_abs_ms`, and on every
lookup prunes entries whose vehicle is not in `loaded_vehicles_`, whose part index is out of range,
whose part is `removed`, or whose part no longer maps to the queried tile (`:1336-1347`). It is
self-healing: a stale entry yields `nullopt` instead of a dangling dereference. Index maintenance is
`index_vehicle_footprint_unlocked` / `unindex_vehicle_footprint_unlocked` (`:1281-1325`), driven by
`register_submap_vehicles` / `unregister_submap_vehicles` (`:1254-1279`) and
`refresh_vehicle_footprint` (called from `map.cpp:1670` on re-anchor).

Consumers today are the out-of-bubble paths only: `mapbuffer::get_abs_tile_with_vehicle`
(`src/mapbuffer.cpp:1697`), `mapbuffer::move_cost` (`:2256`), and pathfinding
(`src/pathfinding.cpp:98`, `:659`). In-bubble gameplay still goes through the fragile duplicate.
Confirmed this session: any tile inside the active bubble is instead delegated straight back to the
fragile path — `mapbuffer.cpp:2232-2233` and `:2246-2247` both read `return g->m.veh_at( *local );`
when `active_reality_bubble_local(p)` succeeds.

### E7 — The hard blocker for grid-decoupling is not vehicles, it is creatures and items

- Passengers are moved by **tile teleport** on every tile the vehicle crosses:
  `psg->setpos(psgp)` where `psgp` is derived from `precalc[1]` plus mount/terrain z (confirmed this
  session, `src/map_vehicle.cpp:1646-1693`, including the O(riders²) retry loop and the
  "Part/passenger position mismatch" debugmsg).
- Creature position is globally tile-quantized and tile-indexed:
  `creature_tracker::monsters_by_location` is `std::unordered_map<tripoint_abs_ms, …>`
  (`src/creature_tracker.h:97`).
- Vision/lighting and sound caches are tile-indexed arrays per z-level (`level_cache` in
  `src/map.h:364-495`, including `veh_exists_at`, transparency/outside caches and the sound
  absorption cache).
- Co-op sends vehicle state as **integer tiles**: the client's `vehicle_state` packet carries `vid`,
  `ax/ay/az`, `face_x/face_y`, `velocity` as JSON integers (confirmed this session,
  `src/coop_client.cpp:290-293`), parsed the same way server-side (`src/coop_server.cpp:375-380`).
- Save anchoring is submap-relative: `abs_sm_pos` + `sm_ms_pos` (`src/vehicle.h:1754-1757`), with
  vehicles serialized inside their owning submap (`src/submap.h:314`).
- The **renderer cannot draw a rotated vehicle at all**: each part is a per-tile sprite whose
  rotation is collapsed to 4-way (`true_rota = 3 - face.dir4()` for `C_VEHICLE_PART`, confirmed this
  session verbatim at `src/cata_tiles.cpp:2013`, comment at `:2010-2011`), fed from
  `part_display_direction` rounded to a whole-degree integer (confirmed this session,
  `src/cata_tiles_draw_layers.cpp:643-644`). The only continuous visual channel is the whole-vehicle
  pixel offset `render_offset_x/y` applied to the sprite transform (confirmed this session,
  `:660-662` for vehicle parts and `:883-885` for riders). So an off-grid vehicle would also need a
  new art/composition pipeline, not just a new position model.

So making vehicles truly off-grid means making creatures, items, vision, sound, co-op wire format and
the save anchor sub-tile capable. That is a world-model rewrite, not a vehicle rework.

### E8 — How much of the migration is actually unfinished

|Fact|Evidence|
|---|---|
|Authority is granted to everything except rail-capable vehicles|`v.box2d_position_authority = !v.can_use_rails();` — `src/physics/physics_world.cpp:131`|
|Authority is revoked and re-granted around submap residency changes, with self-opt-outs remembered separately|`src/physics/physics_world.cpp:437-438`, `:484-490`, `:502-506`, `physics_world.h:192-196`|
|The legacy mover still owns every z-change and fall|`act_on_map()` returns early only when `box2d_position_authority && !should_fall && requested_z_change == 0` — `src/vehicle_move.cpp:1682`|
|Ramp behaviour is only covered by tests that opt *out* of authority|`tests/vehicle_ramp_test.cpp:106`, `:147`; acknowledged in-tree by a companion Box2D-authority ramp spec's header comment: "vehicle_ramp_test.cpp sets box2d_position_authority = false for every vehicle it builds (lines 109 and 159), so its 83 failing assertions all exercise the legacy tile-step path… Nothing covered the ramp on that path" (confirmed this session, `tests/vehicle_test.cpp:912-918`)|
|Collider construction is known-incomplete and pinned red rather than suppressed|`TEST_CASE("box2d_map_load_does_not_accumulate_colliders", "[!shouldfail][vehicle][box2d]")`, with an in-line explanation that the harness cannot yet build colliders at all so the invariant is "unmeasurable rather than violated" (confirmed this session, `tests/vehicle_test.cpp:1000-1013`)|
|Vehicle test population|104 `TEST_CASE`s across 17 files (re-counted this session by exact `TEST_CASE(` occurrence — see E12); 1 `[!shouldfail]`; no `WARN`-instead-of-`CHECK` suppressions and no `BOX2D`-conditional assertions remain|scan this session|

### E9 — Box2D is no longer vehicle-only infrastructure (so "revert it" is not cheap either)

|Consumer|Use|Evidence|
|---|---|---|
|Ballistics|raycast creature-hit detection against physics bodies (`ray_ctx`/`ray_creature_hit`, resolved through `here.get_physics_world()`)|confirmed this session, `src/ballistics.cpp:511-528`|
|Creature tracking|every creature add registers a kinematic body; move and remove update/drop it|confirmed this session: add → `src/creature_tracker.cpp:100-101` (`pw->on_creature_added`); move → `src/character.cpp:843-844` and `src/monster.cpp:473-474` (`pw->on_creature_moved`)|
|Z-level / vertical movement|both `game::vertical_shift` (stairs/ramps) and `game`'s z-changing-travel path call `PhysicsWorld::on_zlevel_changed` to rebuild terrain colliders for the destination level|confirmed this session, `src/game_movement.cpp:2345-2347`, `src/game.cpp:2885-2889` (the latter's comment explains this second call site exists because `load_map()`-driven loads test the wrong z)|
|Map lifecycle|world/submap/shift hooks construct, populate and tear down the physics world across the whole map lifecycle, not just for vehicles|confirmed this session: `map::map()` constructs `phys_world` (`src/map.cpp:560`); `on_submap_loaded`/`on_submap_unloaded` notify it (`:684-686`, `:711-718`); `map::load()` clears all bodies before rebuilding (`:1553`); `map::shift()` calls `on_map_shifted` (`:2044`); `mapbuffer::clear()` drops every collider and vehicle body on world teardown, with an in-line comment explaining why (`src/mapbuffer.cpp:1381-1390`)|
|Debug/render|Box2D shape overlay drawn through the GPU debug-line pass, gated by a debug toggle, independent of any vehicle|confirmed this session, `src/cata_tiles.cpp:1838-1846`|

*(Correction: an earlier draft cited `src/explosions.cpp:2689-2693` for "explosions invalidate
physics sources." That file does not exist — the actual file is `src/explosion.cpp` (2236 lines,
no line 2689), and it contains no reference to `physics_world`, `PhysicsWorld`, or any Box2D type.
The real mechanism with a similar name, `explosion_queue::invalidate_source()`
(`src/explosion_queue.h:86-90`, called from `src/creature_tracker.cpp:335,344`, `src/game.cpp:2693`,
`src/game_world_tick.cpp:112`), nulls a dangling `Creature*` inside a *queued explosion* when its
originating creature dies before the explosion fires — an unrelated, non-Box2D bookkeeping concern.
This row is dropped from the evidence rather than restated inaccurately; the remaining five rows
above are independently sufficient to support O7's rejection.)*

### E10 — Vehicle geometry is baked into tile-indexed vision/lighting data, CPU and GPU

|Channel|Type / indexing|Evidence|
|---|---|---|
|`level_cache::vehicle_floor_cache`|`std::vector<char>`, one entry per tile per z|confirmed this session, `src/map.h:454-458`|
|`level_cache::vehicle_obscured_cache`|`std::vector<diagonal_blocks>` (per-tile NW/NE diagonal blockers)|confirmed this session, `src/map.h:464-466`|
|`level_cache::vehicle_obstructed_cache`|same shape, movement rather than light|confirmed this session, `src/map.h:468-469`|
|Player-specific vision patch|temporarily rewrites transparency + obscured entries around the occupant so they can see out of an opaque vehicle|`map::apply_vision_transparency_cache` — `src/lightmap.cpp:2360-2408`, `src/map.h:2183-2186`|
|GPU mirrors|`vehicle_floor`, `vehicle_obscured` structured buffers + dirty-level lists|`src/compute/gpu_lm.cpp:361-363`, `:440-442`, `:825-828`; `src/compute/gpu_lm.h:171-175`|
|Shader consumers|five compute shaders read the vehicle buffers by tile index|`data/shaders/.../lm_seen_compute.hlsl:50-51`, `lm_seen_walls_compute.hlsl:33-34`, `lm_raytrace_compute.hlsl:45`, `lm_color_raytrace_compute.hlsl:45`, `lm_ambient_compute.hlsl:58`|

Two consequences. First, a fractional vehicle position is not representable in any of these channels
without redesigning the vision/lighting data model down to the shaders. Second, these caches are
already **cleared and rebuilt wholesale** every refresh precisely because vehicles leave stale
entries behind — confirmed this session verbatim in-place: "vehicle_floor_cache,
vehicle_obscured_cache, and vehicle_obstructed_cache all retain stale entries once a vehicle leaves
the level … All three must be cleared unconditionally" (`src/map_cache.cpp:959-964`). That is the
same hand-maintained-invalidation pathology as E5, in a different subsystem, and it is why the
identity fix is the highest-value work.

### E11 — Co-op vehicle sync is dead code, so co-op does not constrain a rework (verified, not assumed)

|Fact|Evidence|
|---|---|
|Client emits vehicle state in a **flat** envelope: `{"t":42,"vid":…,"ax","ay","az","face_x","face_y","velocity"}`|confirmed this session verbatim, `src/coop_client.cpp:290-293`|
|Server parses that packet from a nested `"d"` object, so the flat form yields zeroes|confirmed this session, `src/coop_server.cpp:371-383` (`pkt.get_object( "d" )` then reads `d.get_int("vid",0)` etc. — a flat top-level packet has no `"d"` member, so every field defaults)|
|The client-side id table is only ever *read*|`coop_vehicle_map_inv_.find()` at `src/coop_client.cpp:283-284`; no writer found anywhere in `src/` this session|
|The host-side id table is only ever populated by a **test seam**|confirmed this session verbatim, `register_vehicle_for_test` — `src/coop_server.h:126-132`; a repo-wide scan for `vehicle_id_map_.emplace` found only this one call site|
|`vehicle_sync = 21` (host → client vehicle delta) is declared and never emitted or handled|confirmed this session, `src/coop_proto.h:21`; a repo-wide scan for the token `vehicle_sync` found only this declaration, no emitter or dispatcher|
|Vehicles actually reach the client inside whole-submap JSON, applied by swapping the submap's `vehicles` vector wholesale|confirmed this session: full-tile sync serializes each submap via `sm->store(jout)` (`src/coop_server.cpp:1465-1487`); the client applies it via `submap::swap`, which swaps `vehicles` along with every other per-submap field (`src/submap.cpp:83`)|
|The `[coop][vehicle]` tests exercise the relay seam and hand-wrap the `"d"` envelope, so they pass while the production path is broken|`tests/coop_vehicle_test.cpp` (3 cases, re-counted this session)|

This cuts two ways and both belong in the verdict: the integer-tile wire format is **not** a real
constraint on a continuous-position model, because nothing production-side uses it; and the
wholesale `vehicles`-vector swap on sync is another raw-pointer invalidation bomb of exactly the E5
class (every client-side `vehicle*` is churned per full sync with no index update).

### E12 — The behavioural oracle: what any rework must reproduce

|Suite|Contract pinned|Evidence|
|---|---|---|
|`tests/vehicle_drag_test.cpp`|69 vehicle prototypes × 5 numbers (`c_air`, `c_rolling`, `c_water`, `safe_velocity`, `max_velocity`) — ~345 hard-coded constants|rows starting at `tests/vehicle_drag_test.cpp:249`, checker `test_vehicle_drag` at `:131-134`|
|same|the table is machine-regenerable: on mismatch the test prints the new row in source form|confirmed this session verbatim: `cata_printf("    test_vehicle_drag( \"%s\", %f, %f, %d, %d );\n", …)` at `:132-133`|
|`tests/vehicle_efficiency_test.cpp`|distance-per-fuel per prototype, with `[.]`-tagged generator cases|4 cases (re-counted this session): `vehicle_efficiency_movement_keeps_vehicle_on_valid_terrain`, `vehicle_find_efficiency [.]`, `make_vehicle_efficiency_case [.]`, `vehicle_efficiency`|
|`tests/vehicle_collision_test.cpp`|collision termination, occupant damage with a stale `in_vehicle` flag, and Box2D-authority vehicle-vs-vehicle|6 cases (re-counted this session)|
|`tests/vehicle_rails_test.cpp`|11 rail-motion cases (straight, diagonal entry/exit, crossing, fork, shifting, ramp) — the behaviour rail vehicles get *because* they are denied Box2D authority|11 cases (re-counted this session)|
|`tests/vehicle_ramp_test.cpp`|11 cases: grabbed-cart push/pull on flat/ramp plus three ramp-transition cases, all with authority explicitly disabled|11 cases (re-counted this session); `set_ramp(60, …)` at `:106`, `:147`|
|`tests/vehicle_cargo_perf_test.cpp`|4 `[.][benchmark]` cases (crafting-inventory rebuild near cargo-heavy vehicle, cargo recharge, idle with solar+batteries, solar backfill)|re-confirmed this session — usable as the stage-B churn gate|

Two readings matter for the verdict. The oracle is large and mechanical — ~345 drag constants plus
per-prototype efficiency numbers — which is precisely the asset a rewrite would destroy. But it is
also *self-regenerating*, and `plans/box2d-vehicle-physics-implementation.md:235-237` records that
re-baselining it once already concealed a real regression ("Left red deliberately. Do not re-baseline
it to the ON numbers — that is exactly how the regression was concealed the first time" — confirmed
this session verbatim), so any stage must be gated on the existing numbers, never on regenerated ones.

Full vehicle-tagged test population, re-counted this session by exact `TEST_CASE(` occurrence across
the 17 candidate files: `vehicle_box2d_test.cpp` 11, `vehicle_cargo_perf_test.cpp` 4,
`vehicle_collision_test.cpp` 6, `vehicle_drag_test.cpp` 3, `vehicle_efficiency_test.cpp` 4,
`vehicle_interact_test.cpp` 2, `vehicle_ladder_test.cpp` 1, `vehicle_part_test.cpp` 1,
`vehicle_power_test.cpp` 4, `vehicle_ramp_test.cpp` 11, `vehicle_rails_test.cpp` 11,
`vehicle_split_test.cpp` 2, `vehicle_test.cpp` 25, `vehicle_turrets_test.cpp` 4,
`coop_vehicle_test.cpp` 3, `npc_vehicle_muscle_test.cpp` 6, `ranged_vehicle_recoil_test.cpp` 6 —
**total 104 across 17 files**, exact match to the figure used throughout this document.

### E13 — What one crossed tile actually costs (the stage-B case)

Per tile crossed, the readback walk calls `move_vehicle()` → `displace_vehicle()`, which performs
(`src/map_vehicle.cpp:1581-1828`): passenger teleport loop (`:1646-1693`), old/new footprint bounds
computation over all parts (`:1699-1717`), per-part map-memory overlay clearing (`:1719-1755`),
`advance_precalc_mounts` (`:1756`), `add_vehicle_to_cache(&veh)` — a full re-insert over every part
(`:1797`, again at `:1803` on z-change), `update_vehicle_list` on z-change (`:1805`),
`on_vehicle_moved(...)` (`:1787`, `:1825`) and `phys_world->on_vehicle_moved(veh)` (`:1826`).

`map::on_vehicle_moved` (confirmed this session, `src/map_vehicle.cpp:400-470`) then, for the union of
old and new footprint: sets the per-z vehicle-cache dirty flag (`:413`), invalidates lightmap caches
(`:414`), resets the solar cache (`:415`), sets seen-cache + visibility dirty (`:416-417`),
invalidates GPU lighting transparency levels (`:419`), marks transparency/floor/pathfinding dirty per
occupied submap (`:435-445`), marks `outside_cache` dirty over a one-submap-expanded region
(`:449-456`), and repeats seen/visibility/floor dirtying for `z+1` (`:458-469`).

That whole sequence runs **once per crossed tile**, and the walk is bounded at 64 tiles per vehicle
per turn (`src/map_vehicle.cpp:700`) — roughly 5-6 tiles/turn at 10 m/s in normal play. Stage B's
claim is that membership and invalidation can be computed once per vehicle per turn over the swept
footprint while consequence checks stay per tile; the measurement in stage B decides whether the
saving is material.

### E14 — Which test layers survive a rework, and where coverage is missing

|Oracle layer|Portable under a position/identity rework?|Evidence|
|---|---|---|
|Physical calibration (drag table, fuel/tiles)|Yes — numbers describe outcomes, not mechanism|`tests/vehicle_drag_test.cpp:249+`; `tests/vehicle_efficiency_test.cpp`|
|Box2D readback math|Yes — pure unit tests|`tests/vehicle_box2d_test.cpp` (11 cases)|
|Physics-world bookkeeping|Yes|`tests/vehicle_test.cpp:885` (collider teardown), `:1013` (accumulation spec, currently `[!shouldfail]`)|
|**Tile-anchor structural contracts**|**No — these are what blocks single authority**|ramp per-mount z-transition and exact anchor deltas (`tests/vehicle_ramp_test.cpp`, authority disabled at `:106`, `:147`); rail exact pivot-tile equality (`tests/vehicle_rails_test.cpp`); split tile-set disjointness (`tests/vehicle_split_test.cpp`); efficiency metric defined on the integer tile anchor|
|Vehicle-vehicle consequences|Only reachable through the readback walk's `move_vehicle()`|`tests/vehicle_collision_test.cpp`|
|**Index/lifetime safety**|**Absent** — no test asserts `veh_cached_parts` / `vehicle_list` / `zone_vehicles` / footprint-index consistency; the only related spec is the `[!shouldfail]` collider-accumulation case|`tests/vehicle_test.cpp:1000-1013`|
|Per-turn churn gate|**Absent** — the cargo perf file is `BENCHMARK_ADVANCED`-only, asserts nothing, and never calls `vehmove()`|`tests/vehicle_cargo_perf_test.cpp` (confirmed this session: all 4 cases are pure `BENCHMARK_ADVANCED` chronometers)|

### E15 — The concrete dangling mechanism, read out of `detach_vehicle`

`map::destroy_vehicle()` is just `detach_vehicle()` with the returned `unique_ptr` dropped
(`src/map_vehicle.cpp:398`), so the object is freed at that statement. `detach_vehicle`
(confirmed this session, `src/map_vehicle.cpp:309-396`) cleans up as follows:

|Step|Line|Scope of the cleanup|
|---|---|---|
|physics body removal|`:315`|all|
|`ch.vehicle_list.erase` / `ch.zone_vehicles.erase`|`:379-380`|**only `z = veh->abs_sm_pos.z()`** (`:317`, `:376`)|
|`reset_vehicle_cache()`|`:381`|rebuilds `veh_cached_parts` for **every** z from that z's `vehicle_list`|
|`mapbuffer::unregister_vehicle`|`:384`|all|
|overmap tracking removal|`:385`|only when `tracking_on`|
|`dirty_vehicle_list.erase`|`:386`|all|
|early return when the owning submap is not found|`:368-375`|unregisters and marks dirty but never erases `vehicle_list` / `zone_vehicles`|
|early return when the vehicle is not in its submap's vector|`:393-395`|no cleanup at all|

All eight rows re-verified this session against the actual source; the two early-return branches
match exactly as described. So any entry for this vehicle in a *different* z-level's `vehicle_list` —
which `map::update_vehicle_list()` can create, since it inserts into the z of whatever submap it is
handed (`src/map_vehicle.cpp:296-307`) — survives the free, and the very next `reset_vehicle_cache()`
walks that list and calls `elem->adjust_zlevel(...)` plus `add_vehicle_to_cache(elem)` on the freed
pointer (`src/map_vehicle.cpp:195-198`), re-seeding `veh_cached_parts` with it. `map::veh_at()` then
returns it unchecked (`src/map_vehicle.cpp:1489-1506`). That is the crash chain in
`plans/vehicle-cache-sigsegv-handoff.md`, and it is exactly what a generation-checked handle makes
unrepresentable: `reset_vehicle_cache` would resolve the stale handle to `nullptr` and drop the entry
instead of dereferencing it.

### E16 — Lifetime-mutating operations vs. the indices they must notify

There is exactly one place in the tree that performs the full removal sequence — `map::loadn`'s
no-part purge (`src/map.cpp:2196-2223`): `reset_vehicle_cache()` → `mapbuffer::unregister_vehicle`
→ `overmapbuffer::remove_vehicle` → `dirty_vehicle_list.erase` → erase from `submap::vehicles`.
Every other mutator does a subset.

|Operation|Location|Gap visible in the code|
|---|---|---|
|`map::clear_vehicle_list`|`src/map_vehicle.cpp:287-294`|confirmed this session: clears `vehicle_list` + `zone_vehicles` only; `veh_cached_parts` / `veh_exists_at` keep serving the same pointers through `veh_at_internal`. `editmap::cleartmpmap` hand-clears all four because no single-reset API exists (`src/editmap.cpp:1849-1854`)|
|`map::detach_vehicle`|`src/map_vehicle.cpp:309-396`|per-z erase only (E15); no prune of `mapbuffer::vehicle_footprint_by_location_` / `_locations_`, none of `coop_server::vehicle_id_map_rev_`|
|`map::add_vehicle_to_map`|`src/mapgen.cpp:6078-6147`|confirmed this session: wreck fusion **returns a different `vehicle *`** than it was given, via a recursive call at `:6138` and again at `:6145` on the failure path; any caller holding the original pointer is silently stale|
|`vehicle::part_removal_cleanup`|`src/vehicle_parts.cpp:893`|never calls `PhysicsWorld::on_vehicle_parts_changed`, documented as deliberate in-place: "Part removal does not call this … a shrinking footprint was never tracked before, so keeping it out avoids putting shape churn on that path" (confirmed this session verbatim, `src/physics/physics_world.h:63-66`)|
|`vehicle::merge_rackable_vehicle`|`src/vehicle_parts.cpp:605`|appends parts without routing through `install_part`, which is the site that *does* notify the physics world, so the carrier's collider is not rebuilt for the merged mass|
|`vehicle::split_vehicles`|`src/vehicle_parts.cpp:1166-1345`|inserts the new vehicle into `dirty_vehicle_list` and dirties transparency/seen caches; no `add_item` / `active_items` call appears anywhere in the range, i.e. transferred cargo does not re-register in the new vehicle's per-vehicle `active_items` cache (`src/vehicle.h:1724`)|
|Towing|`towing_data { vehicle *towing; vehicle *towed_by; }` — `src/vehicle.h:166-176`, member at `:1816`|the only raw cross-vehicle reference outside the caches; set in `src/vehicle_items_tow.cpp:980-993`, defensively re-validated in `do_towing_move` (`:803-830`)|
|Grabbing|`src/vehicle_grab.cpp`, `src/avatar_action.cpp:791-811`|already position-keyed: re-resolves through `veh_at(pos)` each move — the pattern the rest should follow|
|Part-index holders|flush is once per turn inside `vehmove()` (`src/map_vehicle.cpp:836-848`)|part indices stay stale for the remainder of the turn after a removal; the cross-turn holder is `Character::activity_vehicle_part_index` (`src/activity_item_handling.cpp:815`, `:872`, compared at `:747-752`)|

The pattern across these rows is that the recurring defects are **notification gaps**, not merely
pointer-type gaps: grow/shrink collider desync and un-pruned reverse maps would still occur with
handles if the mutators kept choosing which indices to tell. That is why stage A below makes the
registry the mandatory choke point (add / remove / footprint-changed) rather than only swapping the
stored pointer type.

### E17 — Consequence inventory: what is keyed to an integer tile crossing

All of the following execute inside the tile-walk replay, not inside the contact solve, so a
continuous-position rework must re-home every one of them:

|Consequence|Formula location|
|---|---|
|collision sweep and target selection|`src/vehicle_move.cpp:391-470`|
|terrain bashing|`bash_vehicle_tile` — `src/vehicle_move.cpp:531-578`|
|part/creature collision: impulse↔damage constants, density factor, critter damage and stun|`part_collision` — `src/vehicle_move.cpp:578-1010`, including the identity-based occupant/pet exclusion checks confirmed this session at `:610-621` and `:672-676`|
|Box2D terrain-impulse consumption (the one physics output that reaches gameplay directly)|`src/vehicle_move.cpp:1000-1008` ← `PhysicsWorld::resolve_terrain_impulse` (`src/physics/physics_world.cpp:804-908`)|
|traps|`handle_trap` — `src/vehicle_move.cpp:1091-1174`|
|ramp / z transitions|`shift_zlevel`, `check_on_ramp`, `adjust_zlevel` — `src/vehicle_move.cpp:1734-1860`; whole-z cache rebuild in `map::shift_vehicle_z` (`src/map.cpp:1617+`)|
|wheel traction|`src/vehicle_move.cpp:1907-1975`|
|occupant shake and throw-from-seat|`map::shake_vehicle` — `src/vehicle_move.cpp:1974-2085`|
|slowdown / thrust per step|`src/vehicle_move.cpp:98`, `:139`|
|vehicle-vehicle momentum exchange|`map::vehicle_vehicle_collision` `src/map_vehicle.cpp:1214-1401`, cluster solve `:1406+`|
|per-crossing part offsets the formulas index off|`vehicle::refresh_precalc` (`src/vehicle_query.cpp:930+`), `advance_precalc_mounts` (`src/vehicle.cpp:2974-3020`), pivot displacement (`:1559-1580`)|

### E18 — Occupant handling: what "the controlled vehicle becomes the player entity" would buy

**Already true today, so no saving is available from it:**

|Claim|Reality|Evidence|
|---|---|---|
|Occupant identity is data, not a pointer|`vehicle_part::passenger_id` is a `character_id` with a `passenger_flag` state bit (confirmed this session, `src/vehicle_part.h:35`); the vehicle resolves the live creature id-first (`g->critter_by_id<player>(part.passenger_id)`, confirmed this session, `src/vehicle_query.cpp:1067-1069`). The header itself documents the coupling: "Driver doesn't know what vehicle he drives … you need to ask a map if there's a vehicle at driver/passenger position" (confirmed this session verbatim, `src/vehicle.h:360-364`)|
|Occupant health is pure data, synced only on damage events|Nothing about HP is touched per tile step; damage reaches occupants only through `part_collision`'s critter branch and `shake_vehicle`'s throw-from-seat, both gated on a non-zero collision impulse|`src/vehicle_move.cpp:925-1010`, `:1974-2085`|

**The actual per-tile cost is occupant *position*, and it is worse than a field write:**

|Work done per crossed tile|Evidence|
|---|---|
|`get_riders()` walked in a retry loop (`for( i = 0; !complete && i < riders.size(); i++ )`) — O(riders²) worst case — with a `g->critter_at( psgp )` probe per rider per pass|confirmed this session, `src/map_vehicle.cpp:1646-1693`|
|`Character::setpos` also pushes the creature's Box2D body via `on_creature_moved`|confirmed this session, `src/character.cpp:840-849`|
|`monster::setpos` rehashes the `creature_tracker` location index (`g->update_zombie_pos`), pushes its body, and — when a coop mutation log is active — logs a `creature_moved` event|confirmed this session, `src/monster.cpp:466-484`|
|For the avatar, `setpos` can trigger a **full reality-bubble shift from inside the tile walk**|confirmed this session: `update_map_after_player_setpos` (`src/player.cpp:16-37`) calls `g->update_map(who)` on submap crossing and `g->vertical_shift_notify` on z change; `vehmove` already defends against this ("A map shift can occur mid-loop when the player is a vehicle passenger", `src/map_vehicle.cpp:619`), and `displace_vehicle` captures its footprint in absolute coordinates first precisely because bubble coordinates go stale mid-function (`:1696-1699`, confirmed this session)|

So the prize is *deferring occupant repositioning to a turn boundary* (and with it, moving the bubble
shift out of the middle of the walk), not representing health as data.

**What full absorption (occupant position derived from the seat, no stored position) would cost:**

|Constraint|Measurement|
|---|---|
|Occupant position is the hottest accessor in the tree|`bub_pos()` — 2919 calls in 191 files; `abs_pos()` — 244 in 81; `critter_at(` — 129 in 55; `setpos(` — 162 in 55; `in_vehicle` — 180 in 51 (scan this session)|
|Creatures aboard must stay findable by tile or they leave combat entirely|`critter_at` is how melee, ranged, explosions, fields and AI find targets (55 files)|
|One occupant-exclusion check is position-based and would invert|players/NPCs are excluded from their own vehicle's collision **by identity** (`ph->in_vehicle` + a `get_passenger` scan proving they ride *this* vehicle, confirmed this session `src/vehicle_move.cpp:610-621`), but pets are excluded by asking what vehicle they are *standing on* (confirmed this session, `src/vehicle_move.cpp:672-676`) — defer repositioning and a vehicle runs over its own pet unless this check is converted to identity too|
|A related desync already caused a bug that is now pinned by a test|`TEST_CASE("vehicle_collision_hits_occupant_with_stale_in_vehicle_flag", "[vehicle][collision]")`, `tests/vehicle_collision_test.cpp` (confirmed this session)|
|It needs a creature→vehicle back-reference|today there is none (the header comment above says so); adding a raw `vehicle *` to every creature would add an N-th dangling-pointer holder, so this is only safe once the registry/handle of stage A exists|

**Does it open the path to grid-decoupled movement?** Partially and honestly: it removes the
*passenger* blocker from E7 — a vehicle could carry occupants as data through continuous motion. It
does not touch the other blockers: creatures *outside* the vehicle are still tile-indexed
(`src/creature_tracker.h:97`), the vision/movement caches and their five shader consumers are still
tile-indexed (E10), every gameplay consequence is still computed per tile crossing (E17), and vehicle
art is still 4-way-rotation sprites (`src/cata_tiles.cpp:2013`). It is one of five blockers, and the
cheapest of them.

## Options considered

|#|Option|Mechanism|Verdict to record|
|---|---|---|---|
|O1|**Status quo + targeted bug fixes**|Keep the dual model; fix each dangling-pointer leak as it is found (`VEHICLE_CACHE_SIGSEGV_PLAN.md`)|**Necessary now, insufficient as the answer.** Thirteen raw-pointer holders with hand-written invalidation (E5); merely *adding* `set_vehicle_cache_dirty()` call sites in an unrelated merge moved the crash threshold from ~483 test cases to ~22, confirmed this session in `plans/vehicle-cache-sigsegv-handoff.md:152-161` — which is the signature of a latent class, not a single bug.|
|O2|**One vehicle registry owning identity and notification** (selected — stage A)|A generation-checked handle replaces raw `vehicle*` in every index, RAII-registered in `vehicle`'s ctor/dtor; the registry becomes the only mutator, fanning out add / remove / footprint-changed to every dependent index|**Selected.** `vehicle` is non-copyable and non-movable (`src/vehicle.h:480-483`) with a single converging constructor and a trivial destructor, so RAII registration is airtight and cheap, and resolution stays O(1) (slot index + generation compare) on the hot `veh_at` path the code explicitly protects ("This function is called A LOT", `src/map_vehicle.cpp:1490`). The registry half is what closes E16's notification gaps — only `map::loadn`'s purge performs the full removal sequence today; handles alone would leave the collider grow/shrink desyncs and un-pruned reverse maps in place.|
|O3|**Move ownership out of submaps into a world-level vehicle store**|`submap::vehicles` (`src/submap.h:314`) holds ids; a map/world store owns the `unique_ptr`s; eviction of a submap no longer frees vehicles|**Rejected for now, retained as the stage-A falsifier's answer.** It fixes lifetime by construction but forces a save-format change: vehicles are serialized *inside* their submap and anchored by `abs_sm_pos` + `sm_ms_pos` (`src/vehicle.h:1754-1757`). O2 gets the same crash-immunity with no save migration, so O3 is only justified if handles prove insufficient.|
|O4|**Finish the Box2D authority migration** (selected — stage D)|Remove the surviving authority opt-outs and retire the legacy mover fields, leaving exactly one position authority|**Selected, after A, B and C.** Authority is already granted to everything except rail-capable vehicles (`src/physics/physics_world.cpp:131`), but the legacy mover still owns every z-change and fall (`src/vehicle_move.cpp:1682`) and every ramp test opts out (`tests/vehicle_ramp_test.cpp:106`, `:147`). This is the documented endpoint of `plans/box2d-vehicle-physics-implementation.md`, whose own status table shows Phase 10 Steps 1-5 done and Steps 6/Phase 12 (legacy retirement) still blocked — confirmed this session, `plans/box2d-vehicle-physics-implementation.md:673-679`. Stage D is that completion, not new design.|
|O5|**Continuous off-grid vehicles, Project-Zomboid style**|Vehicles keep only a continuous transform; the grid supplies surface properties and queries; occupants ride as vehicle data|**Adopted as the program's destination, not as a single rework.** It is a scheduled product goal, and the vehicle half already exists (E4: `step_turn(1.0f)`, `physics_pos`/`physics_angle` authority, `render_offset_*` sub-tile rendering). It is reached by stages A→F below, never in one move, because the remaining cost sits outside the vehicle code: creatures aboard are tile-teleported per crossing (E7, addressed by stage B), creatures *outside* are tile-indexed (`src/creature_tracker.h:97`), vehicle geometry is baked into four tile-indexed vision/movement caches and their GPU mirrors read by five compute shaders by tile index (E10), every gameplay consequence is computed per crossed tile (E17), and vehicle art is per-part sprites with rotation collapsed to 4-way (confirmed this session, `src/cata_tiles.cpp:2013`) — a vehicle at 37.4° cannot be drawn today, which is why stage E is a parallel renderer/art track rather than an afterthought. Contact-impulse-derived consequences stay rejected: the tile walk is the only balance model with an oracle (`src/map_vehicle.cpp:733-749`, E12).|
|O6|**Full rewrite of the vehicle subsystem**|New data model, new part/cargo/power/turret systems, new save format|**Rejected.** 24.8k lines; 370 `veh_at(` callsites in 116 files; 399 `vpart_position` mentions in 176 files (E1/E2); 104 test cases across 17 files, including ~345 frozen drag constants and the per-prototype efficiency table (E12), encoding calibrated fuel economy, collision balance, rails, ramps, towing and turret recoil. The expensive asset is that calibration, and a rewrite has no oracle to recover it against. Nothing in E1–E18 indicts the data model; every measured failure is identity/notification or the unfinished migration.|
|O7|**Revert the Box2D migration; return to a single tile-step authority**|Delete `src/physics/*` and the physics hooks, restore `move_vehicle()`/`of_turn` as the only mover|**Rejected.** Box2D is no longer vehicle-only infrastructure (E9): ballistics raycasts creature bodies (`src/ballistics.cpp:511-528`), every creature add/move maintains a kinematic body (`src/creature_tracker.cpp:100-101`, `src/character.cpp:843-844`, `src/monster.cpp:473-474`), z-level travel rebuilds terrain colliders through it (`src/game_movement.cpp:2345-2347`, `src/game.cpp:2885-2889`), and map/submap/shift lifecycle hooks are wired throughout `map` (`src/map.cpp:560`, `:684-686`, `:711-718`, `:1553`, `:2044`) plus world teardown (`src/mapbuffer.cpp:1381-1390`). Reverting therefore costs at least as much as finishing, and additionally throws away continuous motion (T-bone spin, sub-tile render offsets) that already works.|
|O8a|**Deferred occupant reposition**|Occupants stay tile-positioned creatures but are repositioned once per vehicle per turn instead of once per crossed tile|**Superseded by O8b.** It captures the same measured per-tile cost (E18) with a smaller change, but leaves occupant position writable by anyone, so it does not establish the single-writer invariant stage F needs. Recorded so the fallback is known: if stage B's ownership invariant proves too invasive, land O8a's timing change alone — the measured saving is identical.|
|O8b|**Occupant absorption: the vehicle owns its occupants' position** (selected — stage B)|Boarded creatures' position becomes a vehicle-owned derived value with the vehicle as sole writer, committed once per turn; both occupant classes are identified by data (`passenger_id` for characters, a stored monster reference for pets) instead of by tile|**Selected as stage B, before the churn and authority stages.** Two thirds of the premise are already true — occupants are identified by `character_id` and HP is never synced per tile (E18) — so the win is concentrated in position: it removes the O(riders²) retry loop with a `critter_at` probe per rider per tile, the per-occupant Box2D body push, the `creature_tracker` rehash per riding monster, and — the structural prize — the reality-bubble shift that currently fires from *inside* the tile walk (`src/player.cpp:16-37`, defended against at `src/map_vehicle.cpp:619`). It also removes the passenger blocker from the off-grid goal, because a vehicle can then carry occupants through continuous motion and publish their tiles only at the committed anchor.|

## Verdict

> The vehicle system is **not** a candidate for a from-scratch rewrite: its data model, part system
> and JSON content interface show no measured pathology, and 104 test cases — including ~345 frozen
> drag constants and a per-prototype efficiency table (E12) — are the expensive asset a rewrite would
> destroy with no oracle to recover it. Two things *are* pathological. **(1) Identity and
> notification**: a submap-owned object (`src/submap.h:314`) referenced by thirteen unsynchronised
> raw-pointer indices across five subsystems, where each mutator hand-picks which indices to update
> and only `map::loadn`'s purge does the full sequence (E16) — the direct cause of the open SIGSEGV,
> of its ~22× easier recurrence after an unrelated merge, and of the collider grow/shrink desyncs.
> **(2) An unfinished authority migration**: Box2D already owns continuous position for all non-rail
> vehicles while the legacy tile-stepper remains the sole authority for consequences, z-changes and
> falls, leaving six live position representations reconciled every turn.
>
> Grid-decoupled continuous movement and rotation **is** an adopted goal, and it is reachable — the
> vehicle layer is already ~70% there (E4) — but not as a vehicle-scoped change and not in one move.
> It requires, in order: one registry that owns vehicle identity and fans out every notification (A);
> occupant absorption, so the vehicle owns its occupants' position and the bubble shift leaves the
> tile walk (B); per-turn swept-footprint cache membership instead of per-tile churn (C); a single
> position authority with the rails/ramp opt-outs gone (D); and a parallel renderer/art track that
> can draw a vehicle at an arbitrary angle at all, since today it cannot (E) — converging in (F).
> Consequences stay tile-granular throughout: deriving bash and creature damage from Box2D contact
> impulses would invent a balance model with nothing to validate it against.

Falsifiers this document records (what would overturn this verdict or reorder the program):

- If, after stage A, a dangling-`vehicle*` crash still occurs from a holder the registry cannot
  cover, the lifetime problem is ownership-shaped and **O3** (world-level vehicle store, with the
  save migration it implies) becomes the next step before B.
- If stage B's single-writer invariant forces changes to more than a handful of the 2919 `bub_pos()`
  callsites, fall back to **O8a** (same commit-once-per-turn timing, no ownership invariant) and
  carry the invariant into stage F instead.
- If stage D cannot restore rails and ramp behaviour under single authority without inventing balance
  constants that no test can validate, the migration's endpoint is wrong: make the dual model
  explicit and permanent (documented contract, one reconciliation point) instead of finishing it.
- If stage C's measurement shows per-tile cache churn is immaterial once stage B has removed the
  occupant work, drop stage C rather than doing it for tidiness.
- If stage E's composited-texture rendering cannot hit frame budget at the tile counts the game draws,
  stage F ships continuous *collision and motion* with rendering snapped to the committed anchor, and
  the art track continues separately; it does not block A-D.

## End state

The per-tile consequence dispatch is deliberately retained at every stage. "The vehicle becomes one
entity" is easy to read as more than the program delivers, so the contract at each stage is stated
explicitly:

|After|A driving vehicle with passengers is…|Still true|
|---|---|---|
|A|one registry-owned object whose identity cannot dangle; every index notified through one choke point|occupants are repositioned per crossed tile|
|B|a single entity for occupant purposes: occupants are vehicle-owned data while boarded, positions published once per turn at the committed anchor, and they become independently positioned creatures again only on unboard / throw / dismount / death|the vehicle still advances one tile at a time through `move_vehicle()`|
|C|a single entity for cache purposes too: index membership and cache invalidation computed once per vehicle per turn over the swept footprint|consequence checks remain per crossed tile|
|D|driven by exactly one position authority (Box2D), rails/ramps/falls included; six position representations reduced to three|consequence checks remain per crossed tile|
|E+F|continuously posed and continuously drawn (composited texture rotated by `physics_angle`), with vision/lighting published at the committed tile anchor|consequence checks remain per crossed tile|

Terrain bashing, creature collision and damage, traps, ramp z-transitions and wheel traction are
computed by sweeping the tiles crossed (E17), because the alternative — deriving them from Box2D
contact impulses — was tried and rejected as "inventing a balance model with nothing to validate it
against" (`src/map_vehicle.cpp:733-749`), and the validation asset that would be discarded is ~345
frozen drag constants plus the per-prototype efficiency table (E12). So the end state is: one entity,
one position authority, continuous pose, occupants as data, cache work once per turn — a vehicle that
still *queries* the tiles it sweeps to decide what it hit. Removing that query is a separate balance
project with its own oracle problem and is not part of this program.

## Recommended program

A → B → C → D is a hard dependency chain: B needs A's handle type for the creature→vehicle
back-reference, C's measurement is only meaningful once B has removed the occupant work from the
per-tile path, and D's conversion of the rails/ramp assertions assumes the committed-anchor semantics
B and C establish. **E runs in parallel with A–D and blocks only F**, and needs its own
implementation plan before execution. Stage A subsumes `VEHICLE_CACHE_SIGSEGV_PLAN.md`: the narrow
`map::load()` purge is A's stop-gap step, not separate work afterward.

### Stage A — vehicle identity that cannot dangle

1. Land the narrow fix first as a stop-gap: the per-z `clear_vehicle_list(z)` purge in `map::load()`
   from `VEHICLE_CACHE_SIGSEGV_PLAN.md` step 1, mirroring `map::shift()` (`src/map.cpp:1885-1896`),
   so the tree stops crashing while A is built.
2. Introduce a generation-checked handle (`{ slot, generation }`) plus a registry that a `vehicle`
   enters in its constructor (`src/vehicle.cpp:426`) and leaves in its destructor (`:473`, currently
   `= default`). RAII on the destructor is what makes this airtight: it fires no matter which path
   frees the object, including MAPBUFFER submap eviction, which is exactly the path the current
   manual invalidation misses.
3. Make that registry the **mandatory notification choke point**, not just a pointer store, because
   the recurring defects in E16 are notification gaps that handles alone would not have prevented.
   Give it exactly three mutators — `add( vehicle & )`, `remove( vehicle & )` and
   `footprint_changed( vehicle & )` — and have the registry fan out to every dependent index: the
   per-z `vehicle_list` / `zone_vehicles`, the tile part index, `mapbuffer`'s
   `vehicle_footprint_by_location_` / `_locations_`, `PhysicsWorld::on_vehicle_added` /
   `on_vehicle_removed` / `on_vehicle_parts_changed`, the overmap tracking list, and the co-op vid
   table. Then convert the mutators that currently pick a subset to call only the registry:
   `map::clear_vehicle_list` (`src/map_vehicle.cpp:287-294`), `map::detach_vehicle` (`:309-396`),
   `vehicle::part_removal_cleanup` (`src/vehicle_parts.cpp:893`), `vehicle::merge_rackable_vehicle`
   (`src/vehicle_parts.cpp:605`), `vehicle::split_vehicles` (`src/vehicle_parts.cpp:1166-1345`), and
   `map::add_vehicle_to_map`'s wreck-fusion return path (`src/mapgen.cpp:6078-6147`, which hands back
   a *different* vehicle than it received). `map::loadn`'s purge (`src/map.cpp:2196-2223`) is the
   existing full sequence — use it as the reference for what the registry's `remove` must do.
4. Replace the stored `vehicle *` with the handle in `level_cache::veh_cached_parts`, `vehicle_list`,
   `zone_vehicles`, `map::cached_veh_rope`, `map::dirty_vehicle_list` / `last_full_vehicle_list`,
   `mapbuffer::loaded_vehicles_` / footprint indices, and `coop_server::vehicle_id_map_` (its uint32
   `vid` becomes the handle slot, which also removes that map's own staleness window).
   `map::veh_at_internal` resolves the handle and returns `nullptr` on a stale generation instead of
   the current unchecked dereference.
5. Do **not** reuse `safe_reference<T>` (`src/safe_reference.h:70`) for this: its invalidation is
   driven by `cata_arena<T>` hooks and it carries id/json-persistence machinery, while vehicles are
   plain `unique_ptr`s in `submap::vehicles`, so it would require arena-allocating vehicles — a
   larger change than the handle. Record this so it is not re-litigated during execution.
6. Note explicitly: `mapbuffer::veh_at` and `vehicle_part_at_loaded_tile` delegate back into
   `g->m.veh_at()` for any tile inside the active bubble (confirmed this session,
   `src/mapbuffer.cpp:2232-2233`, `:2246-2247`), so the self-validating footprint index (E6)
   currently protects only out-of-bubble lookups — in-bubble callers, including pathfinding, still
   land on the fragile path. Handles fix both at once.
7. Add the regression test that does not exist today. No test anywhere asserts consistency of
   `veh_cached_parts` / `vehicle_list` / `zone_vehicles` / the mapbuffer footprint index — the only
   index-adjacent specs cover terrain colliders, plus the `[!shouldfail]` accumulation spec — so the
   crashing path has zero coverage. Add to `tests/vehicle_test.cpp` a `"[vehicle][cache]"` case that:
   spawns a vehicle, records its handle, resolves `veh_at()` on one of its tiles (expect the part),
   destroys it via `map::destroy_vehicle`, then asserts `veh_at()` on that same tile returns
   `nullopt` and that resolving the recorded handle returns `nullptr`; and a second case that
   re-anchors the bubble (`g->m.load( … )`, the path `clear_map()` uses) with a vehicle present and
   asserts the same two properties for every tile the vehicle occupied. Both must fail before step 2
   lands and pass after.

**Gate:** the two new cases pass; the handoff doc's fast repro
(`timeout 90 ./cata_test-tiles "~[.]" --order decl --rng-seed 1 --shard-count 4 --shard-index 2
--user-dir=<scratch>`) exits on its own with no SIGSEGV; the unsharded `~[coop]` run completes; all
failures are a subset of the five accepted baseline failures in
`plans/merge-main-into-improvements.md:24` (confirmed this session: `flung creatures stop at the
reality bubble edge`, `vision_wall_obstructs_light`, `vision_single_tile_skylight`,
`vision_see_out_of_vehicle`, `vision_see_into_vehicle`).

### Stage B — the vehicle owns its occupants' position

1. Make *both* occupant classes data-identified. Characters already are (`vehicle_part::passenger_id`,
   a `character_id`, resolved id-first by `vehicle::get_passenger` at `src/vehicle_query.cpp:1060-1071`).
   Pets are not: `vehicle::get_pet` resolves by asking what monster stands on the part's tile
   (`g->critter_at<monster>( bub_part_location( p ), true )`, `src/vehicle_query.cpp:1074-1081`). Add
   to `vehicle_part` a non-serialized `weak_ptr_fast<monster> animal_ref` beside the existing
   `animal_flag` (`src/vehicle_part.h:36`) and have `get_pet` resolve `animal_ref.lock()`.
   `weak_ptr_fast<monster>` is the established pattern for holding monsters across objects —
   `Creature_tracker::monster_faction_map_` uses exactly it (`src/creature_tracker.h:26-33`).
   Monsters have no persistent id (only `temporary_id`, `src/creature_tracker.h:52-59`), which is why
   a weak pointer and not an id.
2. Add the creature→vehicle back-reference: on `Creature`, a `vehicle_handle boarded_vehicle` plus
   `int boarded_part = -1`, using stage A's handle type — never a raw `vehicle *`, which would make a
   fourteenth dangling-pointer holder (E5). Set in `map::board_vehicle` (`src/map_vehicle.cpp:1512-1543`,
   beside the existing `passenger_flag` / `passenger_id` writes) and on the animal-boarding path;
   cleared in both `map::unboard_vehicle` overloads (`:1545-1579`).
3. Establish the single-writer invariant. While `boarded_part >= 0`, the owning vehicle is the only
   legitimate writer of that creature's position. Keep `Creature::position` as an O(1) cache rather
   than deriving it on read: `bub_pos()` has 2919 callsites across 191 files and `critter_at(` 129
   across 55 (E18), so a computed accessor would put a branch plus a handle resolve on the hottest
   read in the tree. Enforce ownership instead of deleting the field: any `setpos` on a boarded
   creature that does not come from the vehicle's commit path emits a `debugmsg` and force-unboards,
   so a desync fails loudly — this is the exact failure class already pinned by
   `TEST_CASE("vehicle_collision_hits_occupant_with_stale_in_vehicle_flag")`.
4. Add `vehicle::commit_occupants()` and call it once per vehicle per turn from the end of the
   readback walk in `map::vehmove()`, inside the existing per-vehicle resync block that already
   re-syncs pivot and render offsets (`src/map_vehicle.cpp:812-831`). For each occupied boardable
   part it derives the tile exactly as `bub_part_location` does — anchor + `precalc[0]` +
   `mount.z() + z_terrain[0]` (confirmed this session, `src/vehicle_query.cpp:1098-1102`) — and
   writes it through the creature's normal `setpos`, so the `creature_tracker` index, the Box2D
   creature body and the co-op mutation log still all fire, once per turn instead of once per crossed
   tile. Then, exactly once, if the avatar's submap changed call `g->update_map( u )` and if its z
   changed call `g->vertical_shift_notify( old_z, new_z )` — the two effects `update_map_after_player_setpos`
   currently triggers mid-walk (`src/player.cpp:16-37`). This sync point is correct for the turn
   order: `game::do_turn` runs `m.vehmove()` (confirmed this session, `src/game.cpp:1105-1110`)
   *before* `m.build_map_cache( get_levz(), true )` (`:1152`) and before `monmove()` (`:1165-1169`),
   so committed occupant tiles are already in place when vision caches rebuild and when monster AI
   picks targets in the same turn.
5. Delete the per-tile rider loop from `map::displace_vehicle` (`src/map_vehicle.cpp:1646-1693`) —
   the `get_riders()` retry loop, its per-rider `g->critter_at( psgp )` probe and its `psg->setpos`.
   Blocking resolution moves to commit: if an occupant's target tile holds a creature that is not an
   occupant of this vehicle, displace that creature with the existing ray-based helper already used
   for this in `part_collision` (`src/vehicle_move.cpp:685-689`); if that fails, place the occupant
   on the nearest free tile of this vehicle's own footprint; if the footprint has none, unboard them
   to the nearest passable tile outside it. Every fallback logs through the existing "Part/passenger
   position mismatch" debug path (`src/map_vehicle.cpp:1663-1670`) rather than silently no-opping.
6. Convert the pet collision exclusion from position-based to identity-based. Today a pet is spared
   only because it is found standing on a BOARDABLE part of this vehicle (confirmed this session,
   `if( ovp && ( &ovp->vehicle() == this ) && get_pet( ovp->part_index() ) )`,
   `src/vehicle_move.cpp:672-676`); with commits deferred, a pet at a not-yet-updated tile would fail
   that test and be run over by its own vehicle. Replace it with the identity test the player/NPC
   branch already uses — scan this vehicle's boardable parts for the critter (`src/vehicle_move.cpp:610-621`)
   — now cheap because `get_pet` resolves `animal_ref` without a map query.
7. Write immediately, not at commit, on every path where an occupant stops riding or must be
   positioned now: `map::shake_vehicle`'s throw-from-seat (`src/vehicle_move.cpp:1974-2085`),
   occupant death inside `part_collision` (`:925-1010`), both `map::unboard_vehicle` overloads
   (`src/map_vehicle.cpp:1545-1579`), `monster::forced_dismount`, vehicle destruction and
   `split_vehicles` (`src/vehicle_parts.cpp:1166-1345`), and ramp z-change (`shift_zlevel`,
   `src/vehicle_move.cpp:1734-1860`). Each clears `boarded_part` where the occupant leaves the
   vehicle.
8. No save-format change. `passenger_id` is already serialized; rebuild `animal_ref` and both
   back-references in one post-load pass over parts carrying `passenger_flag` / `animal_flag`,
   resolving characters by id and monsters by the part's tile — the position-based lookup being
   removed from the hot path is still correct exactly once, at load.

**Gate** — four new tests, each must fail before this stage and pass after:
(a) `"[vehicle][occupant]"`: drive a vehicle carrying the avatar, an NPC and a pet at cruise for 10
turns; after each turn every occupant's `bub_pos()` equals `bub_part_location` of its part,
`g->critter_at` finds each of them there, and a `setpos` counter probe reports exactly one write per
occupant per turn (before the change it is one per crossed tile).
(b) `"[vehicle][occupant][collision]"`: drive over a tile holding an unrelated monster with a pet
aboard — the unrelated monster takes the `part_collision` hit, the pet takes none.
(c) `"[vehicle][occupant][ramp]"`: drive up a ramp with passengers; occupants' z follows the vehicle.
(d) `"[vehicle][occupant][throw]"`: a collision hard enough to throw an occupant leaves that occupant
outside the vehicle with `in_vehicle == false`, `boarded_part == -1` and a real position.
Plus: `tests/vehicle_collision_test.cpp`'s stale-`in_vehicle` case still passes, and the
`tests/vehicle_drag_test.cpp` table passes with its constants unchanged (never regenerated).

### Stage C — per-turn cache membership instead of per-tile churn

Each of up to 64 tile crossings per vehicle per turn (`src/map_vehicle.cpp:700-777`) runs the full
`displace_vehicle` + `on_vehicle_moved` sequence enumerated in E13. Compute the turn's swept footprint
once and update index membership and cache invalidation once per vehicle per turn, leaving
bash/creature-collision/ramp checks tile-granular exactly as they are.

Measure it properly first: `tests/vehicle_cargo_perf_test.cpp` cannot serve as the gate — its four
cases are `BENCHMARK_ADVANCED` chronometers with no thresholds and they never call `vehmove()`.
Instead add a counter to `map::on_vehicle_moved` (`src/map_vehicle.cpp:400`) exposed as a
read-and-reset accessor on `map`, and a `"[vehicle][perf]"` test that drives one vehicle at cruise for
10 turns and records invocations per turn. Expected before the change: roughly one invocation per
tile crossed (≈5-6 at 10 m/s, up to 64). Required after: exactly one per vehicle per turn. Drop the
stage if the before-number is already ~1, i.e. the churn is immaterial.

**Gate:** the new counter test shows 1 invocation/vehicle/turn, and the `"[vehicle]"` suite plus the
`tests/vehicle_drag_test.cpp` table (unchanged constants, never regenerated) still pass.

### Stage D — single position authority

Remove the rails opt-out (`src/physics/physics_world.cpp:131`), bring z-changes and falls under
authority (`src/vehicle_move.cpp:1682`), retire the legacy `of_turn`/`of_turn_carry`/`turn_dir` mover
fields per the Phase 12 retirement checklist in `plans/box2d-vehicle-physics-implementation.md`, and
remove the test-side opt-outs (`tests/vehicle_ramp_test.cpp:106`, `:147`) rather than keeping suites
that exercise a path players no longer drive on (acknowledged in-tree at `tests/vehicle_test.cpp:912-918`).

The blocking assertions are known and must be converted deliberately, in this order: rail exact
pivot-tile equality and its 15° turn-step contract (`tests/vehicle_rails_test.cpp`); ramp per-mount-point
z-transition with exact anchor deltas (`tests/vehicle_ramp_test.cpp`); the integer-tile-anchor
distance metric in `tests/vehicle_efficiency_test.cpp`. Each becomes an assertion on the tile the
vehicle *commits to* (`lround(physics_pos)`) rather than on the legacy mover's step. Do not
re-baseline the drag or efficiency constants to make this pass — `plans/box2d-vehicle-physics-implementation.md:235-237`
records that re-baselining once already hid a real regression.

**Gate:** `"[vehicle]"`, `"[vehicle][ramp]"`, `"[vehicle][railroad]"`, `vehicle_efficiency_test` and
`vehicle_collision_test` pass with no `box2d_position_authority` opt-out remaining in `src/` or
`tests/`, and `tests/vehicle_test.cpp`'s `[!shouldfail]` collider-accumulation spec (`:1013`) either
passes (drop the tag) or is restated with its blocking reason.

### Stage E — renderer and art track for continuous rotation (parallel, does not block A-D)

Vehicles cannot currently be drawn off-grid at all: `draw_vpart` emits one sprite per part per tile
(`src/cata_tiles_draw_layers.cpp:608+`) and `draw_from_id_string` collapses the part's degree rotation
to 4-way for `C_VEHICLE_PART` (confirmed this session verbatim, `true_rota = 3 - face.dir4()` at
`src/cata_tiles.cpp:2013`); the only continuous channel is the whole-vehicle pixel offset
`render_offset_x/y` (confirmed this session, `src/cata_tiles_draw_layers.cpp:660-662`, riders at
`:883-885`). Two decisions fix this stage's shape, so it is not re-litigated during execution:

(i) **composite-then-rotate**: draw each vehicle's parts once into an off-screen render target in
vehicle-local space, then draw that target rotated by `physics_angle`, instead of teaching the
per-part path arbitrary rotation — a vehicle is one rigid body, so one texture and one rotation per
vehicle replaces N sprite rotations, and borrowing a render-target texture is already an in-tree
pattern for GPU-drawn content;

(ii) **vision and lighting data stay tile-quantized**: vehicle geometry continues to be published to
`vehicle_floor_cache` / `vehicle_obscured_cache` / `vehicle_obstructed_cache` and their GPU mirrors
(E10) at the *committed* tile anchor, not at the continuous pose, so the five compute shaders that
index those buffers by tile need no data-model change. Sub-tile visual/logical divergence of at most
half a tile is accepted and recorded as the cost of this decision.

This stage needs its own implementation plan before execution — this document records its scope, the
two decisions above and the gate, not a file-level spec.

**Gate:** a vehicle driven in a circle renders with continuous heading (measured by a pixel-diff A/B
at two headings 20° apart producing a non-zero diff outside the 4-way set), at no frame-time
regression on the shipping preset measured with the existing `[render][perf]` probe.

### Stage F — converge and reassess against recorded numbers

With A-D landed (and E landed or explicitly deferred), re-run the E1/E2 metric commands and re-count
representations. Expected values, so the next reader can check delivery: position representations six
→ three (physics transform, save anchor, render residual); raw-`vehicle *` index holders thirteen →
one registry plus handle-valued indices; authority opt-out sites in `src/` → zero; occupant position
writes per vehicle per turn → one; `on_vehicle_moved` invocations per vehicle per turn → one. Then
decide whether remaining off-grid work (continuous collision against terrain without the tile walk)
is worth opening, using the same measurement discipline rather than a fresh opinion.

## Open questions deliberately left to execution

- **Runtime numbers not yet measured**: if runtime numbers are demanded before stage B/C begin (e.g.
  actual per-turn cache-churn counts to size stage B), the route is the existing `[sim][perf]` probe
  plus `vehicle_cargo_perf_test`, run as a background job per the repo's never-kill-a-build rule; this
  assessment ships with the claim marked as unmeasured until then.
- **Phase 12 status drift**: if `plans/box2d-vehicle-physics-implementation.md` shows Phase 12 was
  completed after this session's HEAD (`030cfc6576`), stage D's scope shrinks to removing the
  rails/ramp opt-outs only — re-checked this session and *not* yet the case (Phase 10 Step 6 and
  Phase 12 both still show as blocked/pending in the plan's own status table,
  `plans/box2d-vehicle-physics-implementation.md:673-679`), so stage D's full scope stands as written.

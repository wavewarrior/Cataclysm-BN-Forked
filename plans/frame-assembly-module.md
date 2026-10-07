# Frame-assembly module

Source of truth for the design. Mirrors the wayfinder map [Map: Frame-assembly module](https://github.com/wavewarrior/Cataclysm-BN-Forked/issues/55); every decision below is recorded in a closed ticket of that map (linked in "Decision record"). Execution is tracked under the spec issue [#104](https://github.com/wavewarrior/Cataclysm-BN-Forked/issues/104). Line numbers are at HEAD `ce3cc290c4` (2026-10-07) and will drift; the closed tickets carry the evidence.

Vocabulary: `GLOSSARY.md`, section "Frame assembly". Decision on the test seam: `docs/adr/0003-frame-plan-test-seam.md`.

## Destination

A deep module that owns the render frame: the pass sequence and its ordering laws, one input struct (camera derived once), previous-frame gate state, the `render_state` accessor boundary and ownership of the lighting knobs. Pass order and skip reasons are assertable in `cata_test-tiles` without a GPU. Out of scope: the bodies of the large passes (`render_world_pass_w` and the shader and composition algorithms, pinned by `plans/lighting-composition-pipeline.md`), a recording fake `gpu_device`, and architecture candidates 3, 4, 5 and 7.

Standing constraint: strict no-behaviour-change. Each deviation is an adjudicated micro-decision with a test (listed under "Deviations"). `rebuild_plan` pose, structure and occluder stamps are consumed as given (ADR-0002); there is no second freshness mechanism.

## Shape

```
refresh_display()                      render_frame(inputs)    <- driver present_frame, tests
   |                                         |
   |  begin_frame (precondition, impure)     |
   |    no frame -> frame_report{ reason }   |   reasons: test_mode, not_ready, invalid_ctx, no_drawable
   v                                         |
build frame_inputs  (builder: the impure edge; reads g, tilecontext, mouse, env, trigger file)
   v
frame_plan = plan(frame_inputs)        pure: ordered steps, run|skip + reason
   v
commit(plan)                           stamps -> frame_history, clear force-once, bump counter (today's order)
   v
executor: for step in plan             resolves dependent gates, times each step
   v
frame_report                           per-step status, reason, ms; capture result; camera_mode; no-op reasons
```

### frame_inputs (by value, built once after `begin_frame` succeeds)

- `frame_camera`: the integer clip rect the lighting build uses (`floor(player + view_offset) - POSX/POSY`, subtile omitted, `sdl_render_frame.cpp:316-338`) and the float `camera_off`, pixel offset and `tile_px` the overlay and world passes use (`:748-760`), each computed as today and carried side by side; `camera_mode { cropped, whole_bubble }` replaces the `cam_w == 0` convention. In `whole_bubble` the camera holds exactly what the unwritten `s_emo` holds today: `tile_px = 0` and zero offsets (consumers keep their own fallbacks).
- Window size, sun, time and weather values, `frame_number` (device frame count + 1).
- Gate knobs, by value: `force_rebuild`, `sky_sun_enable`, `gi_enable`, `rc_readback`, `dbg_lighting`, `glow_enable`, `shaft_enable`, `dust_enable`, `bloom_enable`, `rain_enable`, diagnostic-view (mode >= 6), `overmap_view_open`, `devui_visible`, `cursor_light.enabled`, sound-pulse visibility and speed/radius, and phase 9's `hud_part_enable/force/mask_play` and the five `hud_part_*_enable`. About 45 value knobs (`debug_params.*`, glow/shaft/dust/bloom values, grade, tonemap) stay read live by pass bodies through `const lighting_settings&`.
- Readiness struct: the per-pass `ready()` predicates and the queue-empty checks that no phase of the same frame writes. `tile_sprites_empty` and `ui_rects_empty` are carried as pre-frame values (the menu background writes the first; the overlays write the second).
- `rebuild_plan` (derived once with `lightmap_policy::skip`, `level_cache_freshness::plan_for`, which takes `map&` and asserts the main thread, so a pure plan cannot call it), the viewer pose and the occluder stamp.
- A copy of `frame_history`; an optional `capture_request`; the cursor and hover world positions (computed from the mouse and `frame_camera`).

### frame_history (header-only, member of `render_state`)

The five stamps `last_struct_pose`, `last_vis_pose`, `last_occluder`, `last_struct_px/py` (today function-local statics, `sdl_render_frame.cpp:208-220`; no reset anywhere). The rebuild decision is a pure function (history, rebuild_plan stamps, gate knobs) -> (flags, next history). Not in history: GI pending iterations, `last_frame_inputs_`, `populated_`, the splat LRU, the target dirty flags (their owners reset them) and the perf accumulators.

### frame_plan

Steps and the legacy lap each belongs to (the ten lap names stay the `[render][perf][phase avg/max ms]` vocabulary):

| Lap name | Plan steps |
|---|---|
| begin | `begin_frame` (precondition, outside the plan) |
| build_light | `build_lighting` |
| flush_gather | `sdf`, `gpu_sdf`, `sky_sun`, `gi`, `gi_feedback`, `rc_readback` |
| assemble | `assemble` (begin_lighting_frame) |
| menu_bg | `menu_background` |
| overlays | `overlays` |
| ui_a | `ui_composite`, `avatar_composite`, `vehicle_composite` |
| world_w | `world_pass` (sub-gates: glow, shafts, dust, bloom, rain, decals, debug lines, sound pulses) |
| tonemap | `tonemap` |
| swap_b | `swapchain_composite`, `capture` (inside submit), `map_snapshot` (after submit) |

A second, two-step plan (`tile_sprites`, `ui`) runs the offscreen state view on the same executor.

Plan laws, asserted in `cata_test-tiles` without a GPU:
1. `assemble` precedes every step that begins a pass (the old "begin_lighting_frame must be called BEFORE begin_pass").
2. Within the lighting half: SDF, then sky, then GI; a dead GI pipeline must not take the sky step down.
3. Commit order: stamps, force-once clear and rebuild counter are written after the decision and before any pass.
4. A dependent skip reason follows from its cause (for example `world_pass` still runs on the main menu because the menu background filled the tile queue).

Eight values are written by an earlier phase and read by a later phase's gate in the same frame, so they are executor-resolved dependencies recorded in the report, not plan-time facts: `rc_rebuild` (`:342`), the emitter snapshot (`:360`), world-target dirtiness (`:1035` to `:1127`), debug sound pulses (`:1026-1030` to `:1129`), `last_frame_inputs_` (`:904`), the sky dispatch count (`:618` to `:843`), tile-sprite queue emptiness (menu background `:938` to world pass `:1128`) and UI-rect queue emptiness (overlays `:964-1015` to UI composite `:1049`). The UI and world target `dirty_` flags are read in the plan through a new non-destructive `ui_composite_target::dirty()`; the executor consumes them at today's points.

### frame_report

Per step: status, reason, duration. Plus: the `begin_frame` no-frame reason; the resolved dependent gates; not-executed lighting-build reasons (`collector`, `sdf_not_ready`, `undersized_cache`); the capture result (paths, size, frame number, no-drawable); `camera_mode`.

### Capture

Five channels today, all consumed in the device layer: the driver's armed capture (a process-global, `gpu_device.cpp:164-181`), the F13 counter, `/tmp/cata_dump_trigger` (polled every frame), `CATA_FRAME_DUMP` (frame number against `frame_count_`), and the map-only `CATA_MAP_DUMP`. All become one `capture_request` on the inputs and a field on the report; the plan has `capture` (inside submit, because the copy shares the command buffer) and `map_snapshot` steps. Pin: `frame_count_` has one increment site (`gpu_device.cpp:185`, after the swapchain-texture guard) and `submit_frame` is called once (`sdl_render_frame.cpp:1937`), so a frame that reaches the builder will be frame number `frame_count() + 1`; the submit step asserts it. If that assertion ever fires, keep the env and F13 channels at submit time and move only the driver request (reopen the capture ticket).

### lighting_settings and the knob table

One header-only value (no SDL or RmlUi includes) owns every lighting knob: what the passes and shaders read, plus `crt_params`' knobs, the cursor light's `enabled/radius/intensity` and `menu_emitter_tuning`. `debug_params` stays its verbatim 272-byte cbuffer member (`static_assert` at `sprite_batcher.cpp:62`; copied by value into `frame_light_inputs` each frame, `sdl_render_frame.cpp:838`). The knob table (name, type, default, min/max/step, kind `value|toggle|pulse|mode`, writing channels) replaces the 39-name `/tmp/cata_knob` chain and `lighting_dbg_range` (`sdl_lighting_devui.h:37-40`). Keys and the panel honour the ranges; the file channel stays raw (the measurement escape hatch). A test compares the table with the `data/gui/devui.rml` min/max/step attributes. The debug mode is stored once (it is stored three times today and seeded twice from `CATA_DBG_MODE`). `g_force_rc_rebuild`/`g_rebuild_once` become one `force_rebuild { none, every_frame, once }`; pulse knobs have `take()`. The executor passes `const lighting_settings&` beside `rs` and `ctx` (live reads, as today).

Boundary: configuration moves; frame-written state does not. The cursor's world position (`cursor_light_emitter::wx/wy/wz`, written `sdl_render_frame.cpp:286-292`) and the hover tile become values computed by the inputs builder; `dev_test_lights` (place mode, hover publication, placed lights, sound pulses) stays with its owner. Panel-only state (visibility flag, int proxies, placed count, runic panel state) stays in the F4 adapter.

### render_state accessor boundary

Pass-only accessors (`gi, sky, rain, tonemap, sound_waves, emitter_glow, godray_shafts, dust_motes, hud_particles, gpu_sdf`, plus `sdf, bloom, shadow_mask, ui_target, world_target, world_ldr_target` and the readiness predicates) become private, with the executor and builder as friends. New named methods: `resize_targets(w, h)` (`sdl_window.cpp:438-451`), `sdf_stats()` (`sdl_lighting_devui.cpp:1060-1061`), an upload method or friend for the emitter collector (`emitter_collector.cpp:144-145`). Public and unchanged: producer feeds (`queue_tile_sprite`, `queue_ui_rect`, `tile_batcher`, `emitter_events`, `occluders`, `debug_lines`, `current_sun`, splat-map and decal feeds, `palette_acc`), lifecycle, and `begin_lighting_frame` / `frame_light_inputs` (the GPU regression test `tests/render_regression_test.cpp` is the only outside caller). `build_and_submit_lighting` (one caller) becomes module-internal with a `lighting_build_request` options struct in and a not-executed reason out.

### Entry points

`refresh_display()` (31 statement call sites in 24 files, unchanged) and `render_frame(const frame_inputs&, const lighting_settings&) -> frame_report` (driver `present_frame` and tests; the settings parameter arrives with the settings foundation ticket). A driver producer uses the module's live `lighting_settings`, which `/tmp/cata_knob` and `CATA_DBG_MODE` write without the F4 panel; only tests inject their own.

## Decision record

| Ticket | Gist |
|---|---|
| [Test seam: pure frame_plan value or recording fake device](https://github.com/wavewarrior/Cataclysm-BN-Forked/issues/56) | pure `frame_plan` executed by one loop; no fake device (ADR-0003) |
| [Measure the frame-time baseline and prove the bnplay renderer-Trial equivalence gate](https://github.com/wavewarrior/Cataclysm-BN-Forked/issues/57) | baseline from an interactive free-running launch; changed-pixel-count gate |
| [frame_inputs contract: one input struct, camera derived once](https://github.com/wavewarrior/Cataclysm-BN-Forked/issues/58) | by-value inputs after `begin_frame`, one `frame_camera`, eight dependent gates |
| [Cross-frame gate state ownership](https://github.com/wavewarrior/Cataclysm-BN-Forked/issues/59) | `frame_history` in `render_state`, commit after planning |
| [Lighting knob ownership: settings module and the three write channels](https://github.com/wavewarrior/Cataclysm-BN-Forked/issues/60) | one `lighting_settings`, knob table, per-group cutover |
| [frame_build / render_state accessor boundary](https://github.com/wavewarrior/Cataclysm-BN-Forked/issues/61) | private pass accessors, internal lighting build, executor timing |
| [Frame producers: windowless, windowed, state-view and one-shot capture](https://github.com/wavewarrior/Cataclysm-BN-Forked/issues/103) | capture as input/report, state-view plan, `camera_mode` |

Amendments recorded as comments on the closed tickets: the stale-camera reachability check and `op_x` inconsistency (`frame_inputs`), the queue-emptiness premise correction (`frame_inputs`), the cursor-light scope boundary (knobs), the frame-number pin and the fifth capture channel (producers), the `rebuild_plan`/no-op reasons (history).

## Deviations (adjudicated micro-decisions, each with its own test)

1. `shadow_steps`: a `/tmp/cata_knob shadow_steps N` write now survives while the F4 panel is open (today `last_ss` is never reassigned, `sdl_lighting_devui.cpp:1174-1175`, so the panel's int proxy overwrites it every frame). No pixel change in the default state. Lands in the settings foundation ticket.
2. The debug mode, force-rebuild bools and their seeds collapse to single storage (same observable behaviour from every channel).

Everything else in "Follow-ups" is explicitly not changed by the migration.

## Equivalence gate

Procedure (measured 2026-10-07 on osx-arm-slim; assets in `plans/frame-assembly-module/`):
- Build: a full background build of `cataclysm-bn-tiles` and `cata_test-tiles`; the binary under test lives at `out/build/osx-arm-slim/src/`, never the repo-root copy. If every translation unit fails with `malformed or corrupted precompiled file ... probe-dxc`, delete `src/CMakeFiles/cataclysm-bn-tiles-common.dir/cmake_pch.hxx.pch`, run `CCACHE_RECACHE=1 ninja <that .pch edge>`, check `strings -a <pch> | grep -c build/probe-` is 0, then rebuild (cause: commit `cab7aec3e8` shares PCHs across differently named build directories).
- Trial: `equivalence.trial.toml` (windowed, 1280x720 window = 2560x1440 capture, Bairdford fixture, night). After `start`: freeze animation (`flicker_gain 0`, `cloud_strength 0`, `dust_enable 0`, `shaft_enable 0`), write `/tmp/cata_build_gi_scene` (7x7 room with a light, gives the lighting passes occluders), then `force_rc_rebuild 2`; one knob per `/tmp/cata_knob` write, each consumed by a `state` request; verify each `knob <name> = <v>` acknowledgement (no `(unknown)`) and each capture's rebuild/steady branch (`[flash][gpu] rebuild: struct=1 ... rc=1` before a rebuild capture) in the Episode `debug.log` after `stop`.
- Metric: pixels with any channel difference outside an A/A noise mask (three or more same-state frames, dilated two pixels), their share of the frame, the largest delta and their bounding box. Not the whole-frame mean difference: it averages a local regression away.
- Noise floor: 36 to 1,815 changed pixels (0.001 to 0.037%) within a launch and across launches; consecutive frames of an unchanged state differ by a toggling strip (about 1,871 pixels). Gate rule: pass at or under about 1,500 changed pixels (0.04%) against a two-launch mask; a positive control must exceed 5,000 (`ao_strength 0` 8,221; `sky_scale 0` 4,735; `vis_radius 2` 89,067). A regression smaller than about a 40x40 block is invisible unless the compare is cropped.
- Scene coverage: the windowed night scene sees AO, GI, sky, `vis_radius`, `ramp_enable`, `crt_world`; an interactive daytime run at the real viewport sees sun, sky, normals, glow and GI. `shadow_mask_str` is visible in neither. The windowed driver runs the whole-bubble camera path (see Follow-ups), so any camera-dependent change also needs the interactive daytime run.
- The main menu is not pixel-gateable (A/A noise 2.7e-3 mean difference exceeds a `ramp_enable` change there); the plan tests cover it plus one interactive look.
- Frame-time baseline: interactive free-running launch, `CATA_MEASURE_IMMEDIATE=1`, `force_world_redraw 1`, scratch user directory with `AUTOSAVE` off; 45 windows of 120 frames: `render_body` median 17.98 ms, `frame_period` 21.38 ms (~46.8 fps), `begin` 16.29 ms (91%), CPU phases excluding `begin` and `swap_b` 1.18 ms. A windowed driver Episode is not a frame-time instrument (one frame per request).

## Execution tickets

Filed under the spec [#104](https://github.com/wavewarrior/Cataclysm-BN-Forked/issues/104), label `ready-for-agent`. Order is outside-in: the executor and plan arrive first and every later ticket adds assertable facts. Each ticket is independently buildable and revertable; blocking is a `Blocked by` body line.

| T | Ticket | Blocked by | Gate |
|---|---|---|---|
| T0 | [Frame equivalence gate: committed Trial and a `bnplay compare` pixel-count operation](https://github.com/wavewarrior/Cataclysm-BN-Forked/issues/105) | none | tool tests |
| T1 | [The frame runs as an executor over a plan of named steps, with the ten phases unchanged](https://github.com/wavewarrior/Cataclysm-BN-Forked/issues/106) | T0 | order laws; pixel; perf-line diff |
| T2 | [Previous-frame gate state is a frame_history value committed right after planning](https://github.com/wavewarrior/Cataclysm-BN-Forked/issues/107) | T1 | decision-function tests; pixel |
| T3 | [The frame derives its camera once](https://github.com/wavewarrior/Cataclysm-BN-Forked/issues/108) | T1 | camera tests; pixel + daytime |
| T4 | [Frame inputs are built once per frame and the frame can be rendered from injected inputs](https://github.com/wavewarrior/Cataclysm-BN-Forked/issues/109) | T2, T3 | builder tests; pixel + daytime |
| T5 | [The lighting half of the frame is planned: run and skip reasons for SDF, sky and GI](https://github.com/wavewarrior/Cataclysm-BN-Forked/issues/110) | T4 | scenario tests; pixel + daytime |
| T6 | [The world and composite half of the frame is planned, including the mid-frame dependent gates](https://github.com/wavewarrior/Cataclysm-BN-Forked/issues/111) | T4 | menu and dependent-gate tests; pixel + daytime |
| T7 | [Lighting build reports why a planned rebuild did not execute](https://github.com/wavewarrior/Cataclysm-BN-Forked/issues/112) | T2, T3 | bail-out tests; pixel + daytime |
| T8 | [Frame capture is a request on the frame inputs and a field of the frame report](https://github.com/wavewarrior/Cataclysm-BN-Forked/issues/113) | T4 | byte-identical dumps per channel |
| T9 | [The frame report and the driver's capture response state which camera branch ran](https://github.com/wavewarrior/Cataclysm-BN-Forked/issues/114) | T3, T8 | bnplay tests |
| T10 | [The state view renders through a plan variant on the same executor](https://github.com/wavewarrior/Cataclysm-BN-Forked/issues/115) | T4 | tests; identical output |
| T11 | [Lighting settings foundation: one owner, a knob table and single-source debug mode](https://github.com/wavewarrior/Cataclysm-BN-Forked/issues/116) | T1, T2 | table tests; pixel + daytime |
| T12 | [GI knobs move into lighting settings](https://github.com/wavewarrior/Cataclysm-BN-Forked/issues/117) | T11 | pixel + daytime |
| T13 | [Bloom, tonemap and grade knobs move into lighting settings](https://github.com/wavewarrior/Cataclysm-BN-Forked/issues/118) | T11 | pixel + daytime |
| T14 | [Shaft, dust and glow knobs move into lighting settings](https://github.com/wavewarrior/Cataclysm-BN-Forked/issues/119) | T11 | pixel + daytime |
| T15 | [Rain and splat knobs move into lighting settings](https://github.com/wavewarrior/Cataclysm-BN-Forked/issues/120) | T11 | pixel + daytime |
| T16 | [HUD particle knobs move into lighting settings](https://github.com/wavewarrior/Cataclysm-BN-Forked/issues/121) | T11 | pixel + daytime |
| T17 | [CRT, cursor-light and menu-emitter knobs move into lighting settings](https://github.com/wavewarrior/Cataclysm-BN-Forked/issues/122) | T11 | audit; pixel + daytime |
| T18 | [Remaining knob globals move and the DebugParams cbuffer is asserted against the shader](https://github.com/wavewarrior/Cataclysm-BN-Forked/issues/123) | T12-T17 | cbuffer assertion |
| T19 | [Pass accessors on render_state become private to the frame-assembly module](https://github.com/wavewarrior/Cataclysm-BN-Forked/issues/124) | T5, T6, T7, T8, T10 | project builds |

Parallel tracks: T0 first; then T1; after T1 the camera/history/inputs/plan track (T2-T10) and the settings track (T11-T18, from T11 once T1 and T2 are done) are independent; T19 closes the contract last. Knob-group tickets T12-T17 are independent of each other but touch the same files, so merge them one at a time.

## Follow-ups (filed as separate bugs, no fix attempted, not part of the migration)

- [First frame after re-entering a world can skip the lighting rebuild](https://github.com/wavewarrior/Cataclysm-BN-Forked/issues/125) (blocked by T2): nothing resets `frame_history` on world change; the scout's trace is not reproduced.
- [A planned lighting rebuild that cannot execute is recorded as done](https://github.com/wavewarrior/Cataclysm-BN-Forked/issues/126) (blocked by T7).
- [Sound pulse positions add the pixel offset twice](https://github.com/wavewarrior/Cataclysm-BN-Forked/issues/127) (suspected, dev-only; `sdl_render_frame.cpp:1572-1573` against the comment at `:982`).
- [Windowed driver frames run the whole-bubble camera fallback](https://github.com/wavewarrior/Cataclysm-BN-Forked/issues/128).
- [Disabling GI leaves the previous GI field composited](https://github.com/wavewarrior/Cataclysm-BN-Forked/issues/129).

## Unverified items (the execution tickets must check before relying on them)

- Whether any frame runs with `rs.ready()` true and `rs.collector()` null (`assemble_light_inputs` returns at `:743` without writing the camera mirror). T3/T4 add a startup-frame test.
- Which `crt_params` fields are knobs and which are runtime state; every reader of `cursor_light_emitter::wx/wy/wz`. T17 audits both first.
- That the flush methods do not clear their queues (the state view relies on it; documented at `src/lighting/CLAUDE.md:177-179`, not re-verified in code). T10 checks.
- The `plan_for` non-skip branch's mutation of the Level cache (the frame never takes it); `fonts()`/`atlas()` accessors were not part of the accessor audit.
- No numeric budget was set for the per-frame cost of building inputs and the plan; T4 records the measured cost against the 1.18 ms CPU-phase baseline.
- Persistence of the knob globals other than `g_dbg_params` (none found for `g_dbg_params`).
- Interactive injection of the GI scene left the frame unusable (not diagnosed); the GI scene is used only in the windowed Trial.

# Co-op priority + Graveyard-Keeper lighting

## Context

New top priority: **co-op multiplayer in a fully working state**. Secondary, done first as a fast decisive simplification: **drop the broken/fancy dynamic lighting** and settle on a Graveyard-Keeper "smoke and mirrors" look.

User decisions (locked this session):
- **Sequencing:** one plan. Phase 1 = neutralize the fancy lighting (bounded, reversible; de-risks the render pipeline the co-op live test runs through). Phase 2 = the main effort, co-op stabilization.
- **Lighting cut depth:** *disable/bypass* (keep the code, small reversible diff; delete later once the look is dialed in). **Drop:** SDF sun-shadow march, GI (indirect bounce), bloom, volumetric fog. **Keep as the lighting mechanism:** silhouette shadow-casters (GK-style sprite cast shadows), procedural normal maps, per-sprite facing. Base ambient falls back to the flat sky/CPU-lightmap path.
- **Co-op "done" bar:** BOTH the `[coop]` test suite green (no suite-aborting crash, all `[coop]` cases pass under a pinned seed) AND a live two-instance session running a multi-minute gameplay loop with no desync or crash.

Build preset is `osx-arm-slim`. Test + game binaries link to the **repo root** (`./cata_test-tiles`, `./cataclysm-bn-tiles`), not `out/build/...`. `sprite.frag.hlsl` and `sky_sun.comp.hlsl` live under `data/shaders/lighting/src/` and are compiled at runtime via shadercross — editing the `.hlsl` is the live change, no rebuild (still relaunch). Per repo AGENTS.md, once write access is restored the executor should also copy this plan to `plans/coop-priority-gk-lighting.md` as the permanent record.

Scope boundary: pre-existing non-co-op failures (`vehicle_efficiency_test`, `vehicle_test`, `vehicle_ramp_test`, `vision_test`, `ranged_aiming_test`, `explosion_balance_test`, `translation_text_style_check*`) are out of scope — they are not `[coop]` and are not caused by this work.

## Approach

### Phase 1 — Neutralize the fancy lighting (GK smoke-and-mirrors)

The compute passes' output buffers are zero-initialized at pass init and the sprite pipeline binds all storage buffers unconditionally (D3D12/Metal root-signature requirement). So the clean disable is: **skip the compute dispatch** (buffers stay neutral) and **make the fragment shader treat a missing pass as a defined, supported state** rather than an error. The `max(cpu_tint, gpu_total)` composite already degrades toward the CPU lightmap when the GPU term is small; the only hard dependencies that break with the sun march off are the sun *shadow* and the directional sky-access, plus a magenta safety-tint.

Steps (independent of each other; do all four, then verify together):

1. **Gate off the four dropped passes in the frame orchestrator** — `src/sdl_render_frame.cpp`.
   - Bloom and volumetric already sit behind `g_bloom_enable` (used at ~L1184) and `g_vol_enable` (used at ~L1169). Change their **definitions' default to `false`** (the static/anon-namespace block near the top of the TU, same block as `g_vol_params`/`g_vol_density` around L69).
   - Add two new file-local `bool` flags in that same block, `g_sky_sun_enable = false` and `g_gi_enable = false` (mirror the existing `g_bloom_enable` declaration style exactly).
   - Wrap the **sky/sun compute dispatch** (`rs.sky().record( ... )`, in the celestial block ~L526–L556) in `if( g_sky_sun_enable ) { ... }`. Leave the surrounding readiness bookkeeping (`sky_valid` assembly, `rs.sky()` allocation) untouched so `sky_valid` correctly reports `0.0` when the pass did not dispatch.
   - Wrap the **GI compute dispatch** (`rs.gi().record( ... )`, in the GI block ~L591–L633) in `if( g_gi_enable ) { ... }`. Leave `rs.gi()` allocation/binding intact (buffer stays zeroed).
   - These flags remain runtime-visible where bloom/vol already are (F4/debug), so the executor can flip any pass back on for an A/B comparison without recompiling.

2. **Fragment fallback: sun stays lit, sky falls back to flat, when the sun march is off** — `data/shaders/lighting/src/sprite.frag.hlsl`.
   - `sky_valid` (per-frame uniform, set in `sdl_render_frame.cpp` ~L773 from `sky_sun_pass::ready()/dispatches()`) already distinguishes "pass ran" from "pass off". Use it as the branch key.
   - **Directional sky-access → flat ambient (L828).** Currently `sky_contrib = float3(sky_r,sky_g,sky_b) * sky_intensity * sky_dir.rgb;` where `sky_dir = sky_bilinear(shade_pos)` reads SkyBuf. Replace `sky_dir.rgb` with the flat open/roofed field when the pass is off: compute `float3 sky_access = (sky_valid > 0.5) ? sky_dir.rgb : sky_vis.xxx;` and use `sky_access`. `sky_vis` is SkyVisBuf (slot 2, CPU-uploaded from `outside_cache`, independent of the sun march) and is already in scope here.
   - **Sun shadow (L938).** `const float sun_shadow = sun_occl;` (`sun_occl` from `sky_dir.a`) → `const float sun_shadow = (sky_valid > 0.5) ? sun_occl : 1.0;`. With the march off the sun is fully lit and the silhouette `sun_mask_vis` (L891/L955, from `ShadowMask`) becomes the sole sun shadow — the GK intent. The open-sky gate `sun_sky_vis` (L879, from SkyVisBuf) is unchanged, so the sun still only reaches open tiles. Do not touch the `is_face` branch at L883–884 (its SkyBuf read is moot once `sun_shadow` is forced to 1.0).
   - **Remove the magenta safety tint (L1243–1245).** The block `if (sdf_map_w > 0u && sky_valid < 0.5 && mode_gpu_lit) { final_rgb = lerp(final_rgb, float3(1,0,1), 0.5); }` exists only to flag an *unexpected* dead sky pass; with the pass intentionally off it would turn the whole gpu-lit world magenta. Delete this `if` block.
   - Before editing, confirm no committed `sprite.frag.msl`/`sky_sun.comp.msl` shadows the `.hlsl` (`glob data/shaders/**/sprite.frag.msl`). If one exists it must be edited in lockstep; if not (expected — these are runtime-compiled), the `.hlsl` edit is the live change.

3. **GI read is inert once the dispatch is gated** — no extra frag edit needed. `GiBuf` (slot 3) stays zeroed because `g_gi_enable=false` from startup means the GI compute never writes it; the frag's `indirect_bilinear` term then reads a zero field and contributes nothing. (Optional belt-and-suspenders: default the `gi_strength` knob in `DebugParams`/`g_dbg_params` to `0`, which also skips the frag GI read work. Not required for correctness.)

4. **Keep the GK mechanism wired (no edits — verify only).** Do not touch: `render_state::flush_shadow_casters` (`src/lighting/render_state.cpp:965`), the `shadow_mask_` target (init ~L230) and `tile_batcher_.set_shadow_mask` (~L448); the emitter/point-light path and the SDF pass (`sdf_pass`/`gpu_sdf_pass`, needed for emitter shadows); `normal_gen` (atlas normals); the `face_amt` per-sprite facing varying; and `tonemap_pass` (HDR→LDR resolve; leave on). These are the silhouette-shadow + normal-map + facing GK look the user wants to keep.

After Phase 1 the world must render with: flat sky/CPU-lightmap ambient + a directional sun on open tiles + GK silhouette cast shadows for trees/creatures + normal-map relief on terrain, and **no** GI bounce, bloom, fog, or SDF sun-march. Not magenta, not black.

### Phase 2 — Co-op to a fully working state (main effort)

Co-op is a large, mostly-implemented subsystem (`src/coop_{session,client,server,rollback,reconcile,checksum,net,packets,overmap,mutation_log,fiber,menu,input_window}.*`) with ~20 `[coop]` test files. "Fully working" here is a stabilization loop: establish the current failure set by running (state changes between sessions, so it cannot be enumerated from source alone), fix each concrete failure to root cause, and prove it on a live two-instance session. The methodology, tools, commands, and per-step success criteria below are fully specified; the individual fixes are discovered by the specified reproduce→debug→fix loop.

1. **Establish ground truth (build + run the co-op suite).**
   - Build both targets as a background job, ≥1200 s, never kill (a killed build corrupts `.ninja_deps`): `cmake --build --preset osx-arm-slim --target cataclysm-bn-tiles cata_test-tiles`.
   - From the repo root, run the co-op suite with a pinned seed and record the seed and the full failure set file:line-by-file:line: `./cata_test-tiles "[coop]" --rng-seed 1`.
   - Separately confirm whether the suite still aborts mid-run on a SIGSEGV (historically `coop_inproc_test.cpp:100`, the `REQUIRE(srv.send_initial_sync())` in `inproc_harness::setup`). If `[coop]` terminates early instead of printing a normal Catch2 summary, the abort is live and is the first thing to fix.
   - Record this failure set as the Phase-2 backlog. Each subsequent fix is verified against `./cata_test-tiles "[coop]" --rng-seed 1` re-run.

2. **Fix the suite-aborting crash first (if present).** A SIGSEGV that terminates the run blocks every downstream case. Reproduce under a debugger — `xd://debug` (lldb-dap adapter) launching `./cata_test-tiles` with args `["[coop][inproc]","--rng-seed","1"]`, or `lldb -- ./cata_test-tiles "[coop][inproc]"` — capture the stack, and fix the root cause (candidates: proxy-NPC spawn/apply-sync lifetime, a transport peer UAF like the one the `inproc_harness` dtor at L132–143 already documents, or a null `g`/proxy pointer). Success: `[coop]` runs to a normal Catch2 summary with no SIGSEGV.

3. **Drive the remaining `[coop]` failures to green**, each fixed to root cause (not by weakening the assertion), using `skill://coop-audit-checklist` as the review gate for every co-op edit:
   - Packet format symmetric on both directions (sender JSON shape == receiver parse), wrapped in `try/catch(JsonError)`.
   - Thread safety: receiver/IO thread never writes game state directly — parse → mutex/atomic pending buffer → drain in `coop_world_tick()`. `std::atomic` fields `.load()` before formatting.
   - Reconnection: host resets `last_confirmed_seq_=0`, client resets `next_seq_=1`; no blocking `SDL_Delay` on the main thread; short (~100 ms) accept-reconnect recv timeout.
   - Serialization: `SDL_GetTicks`/timestamps as `int64_t` (24-day overflow), no `get_int64` (doesn't exist — string workaround).
   - Coordinate frames (known-fragile, per prior work): proxy-NPC positions from the network are absolute and must be applied map-relative — verify no avatar-bubble-relative read feeds a map-relative write in `coop_client.cpp`/`coop_server.cpp`/`coop_rollback.cpp`/`coop_checksum.cpp`. Spot-check with a grep for avatar-relative position reads applied to `setpos`/`spawn_proxy_npc` in those files; if the previously-fixed sites regressed, re-fix.
   - Note `coop_session::reset()` is already wired into `clear_states` (`tests/state_helpers.cpp:64`) — do not re-add; the cross-test session leak is handled.
   - Success: `./cata_test-tiles "[coop]" --rng-seed 1` reports **all cases passing**. If any pre-existing *non-`[coop]`* family shows up, attribute per `skill://cbn-test-regression-attribution-by-seed` and leave out of scope.

4. **Live two-instance verification (the user-facing bar)** — follow `skill://cbn-drive-two-instance-coop` exactly:
   - Two sandbox copies of the install tree (exclude `save/config/`, copy minimal config + the freshly built binary); never point at the user's real `save/`.
   - Host: create a *safe* Play-Now (Default Scenario) evac-shelter character and persist it with ESC → `S` → `Y` (a `pkill` loses it). Host from a **freshly launched** process (`o`→ENTER→ENTER), wait for `[coop] listening on port 8080` plus fresh `[coop][tick=…]`, then hands off the host entirely.
   - Client: `o`→ENTER→`j`, wait for the IP popup to render, then `127.0.0.1`+ENTER in a separate input batch. Success chain (host side): `client TCP connected` → `handshake complete` → `proxy NPC spawned: Partner` → `client join finalized — session active`; (client side): `received world_seed` → `applied world_seed to avatar`.
   - Drive a multi-minute loop exercising movement (all four cardinals), a pickup/drop, and a melee/attack, on both instances. Success criteria: client sidebar shows real limb bars (HEAD/TORSO/limbs — not just STA/MANA, which means a body-less proxy); host and client agree on ambient temperature and the `COND` string and both advance; `coop_checksum` converges; no desync/crash popups; `config/debug.log` `[coop]` lines show no errors and no `crash.log.dmp` appears.
   - Cleanup (mandatory): `pkill -9 cataclysm` (verify `pgrep`), delete sandboxes/temp artifacts, confirm the user's real install (binary mtime + `save/`) is untouched.

## Critical files & anchors

- `src/sdl_render_frame.cpp` — frame orchestrator (file-local `assemble_light_inputs`/`render_world_pass_w`). Enable-flag block ~L69; `sky_valid` assembly ~L773; sky/sun dispatch `rs.sky().record` ~L526–556; GI dispatch `rs.gi().record` ~L591–633; volumetric gate L1169; bloom gate L1184. Gate the two dispatches; default all four enables false.
- `data/shaders/lighting/src/sprite.frag.hlsl` — `sky_contrib` L828; `sun_mask_vis` (silhouette, keep) L891 + L955; `sun_shadow` L938; magenta safety tint to delete L1243–1245. Runtime-compiled HLSL.
- `src/lighting/render_state.cpp` — `flush_shadow_casters` L965, `shadow_mask_` init L230, `set_shadow_mask` L448. Keep wired; verify only.
- `tests/coop_inproc_test.cpp` — `inproc_harness` L54–144; `setup()` crash locus at `send_initial_sync()` L100; dtor UAF-avoidance note L132–143. Start of the suite-abort investigation.
- `src/coop_session.h` — session singleton + `reset()` (L63, already wired at `tests/state_helpers.cpp:64`).

## Verification

- **Phase 1 (rendering).** Relaunch the built `./cataclysm-bn-tiles`, reach in-game daylight. Expected observable: world renders with flat ambient + directional sun + GK silhouette shadows on trees/creatures + normal-map terrain relief; **no** magenta, no black scene, no bloom halo, no volumetric shafts, no GI colour bleed. Confirm the four dropped passes are not dispatching (F4 shows them off / `[render][perf]` frame cost drops versus the pre-change build). A/B check: temporarily flip `g_sky_sun_enable`/`g_gi_enable`/`g_bloom_enable`/`g_vol_enable` back on at runtime and confirm the fancy effects return (proves the disable is the cause, and that re-enable is intact). Use `tools/visual_verify` / the `computer` tool per the repo's visual-verification rules rather than reading raw screenshots.
- **Phase 2 (co-op).** (a) `./cata_test-tiles "[coop]" --rng-seed 1` from repo root runs to a normal summary with **all `[coop]` cases passing** and no SIGSEGV abort. (b) Live two-instance session reaches `client join finalized — session active`, the client avatar shows real limb bars, host/client `COND`+temperature agree and advance, checksums converge, and a multi-minute movement/pickup/combat loop completes with no desync or crash in `[coop]` debug.log. Verify binary freshness first: `ls -l ./cata_test-tiles` mtime newer than the last `git log -1`, and run from repo root (the `out/build/...` copy is stale).

## Assumptions & contingencies

- **Assumed:** disabling the sun march + GI while keeping the silhouette `ShadowMask`, normal maps, and `face_amt` produces an acceptable GK look with the frag fallback above. *If* the scene reads too flat/dark after Phase 1 (no directional read at all), the minimal in-scope tuning is to raise the ambient floor (`amb_floor`/`day_floor`) and/or `sun_scale` knobs — do **not** re-enable the dropped passes. If the look genuinely needs a dropped pass, stop and surface it rather than silently reverting the disable.
- **Assumed:** the `[coop]` suite is the correct proxy for co-op correctness and the live two-instance session is the acceptance bar. *If* the `[coop]` suite is already green and does not abort at Phase-2 step 1, skip steps 2–3 and go straight to live verification (step 4); any live-only desync then becomes the backlog, fixed via the same audit-checklist loop.
- **Assumed:** `sprite.frag.hlsl`/`sky_sun.comp.hlsl` are runtime-compiled with no committed `.msl` shadow. *If* a `.msl` for either exists, edit it in lockstep with the `.hlsl` (macOS cannot regenerate `.msl` without shadercross).

## Execution log

- **Phase 1 implementation deviation (found during execution, not in original plan):** `sun_occl` (computed at `sprite.frag.hlsl` ~L888) is read in TWO places, not one — as the direct `sun_shadow` term (~L938, as the plan anticipated) AND, separately and rawly, in the post-clamp `sun_shad_mul` floor (~L1052: `lerp(shad_floor, 1.0, min(sun_occl, sun_mask_vis))`). Gating only the first read (as the plan's literal instruction specified, "do not touch the is_face branch") would leave the second read seeing a permanently-zero `SkyBuf.a`, clamping the whole scene to `shad_floor` (0.65–1.0) even with the march intentionally off — the opposite of the "sun stays lit" intent. Fixed by gating `sun_occl` at its source (`sky_valid > 0.5 ? ... : 1.0`) instead of downstream, which correctly propagates to both consumers.
- New F4-bindable flags: `sky_sun_enable`, `gi_enable` (declared in `sdl_lighting_devui.cpp`/`.h` alongside `bloom_enable`/`vol_enable`, not in `sdl_render_frame.cpp` as the plan's anchor estimate suggested — that TU only *consumes* these flags, `sdl_lighting_devui.cpp` is their actual home and F4-binding site). Checkboxes added to `data/gui/devui.rml`. `g_bloom_enable` default flipped `true → false`; `g_vol_enable` was already `false`.

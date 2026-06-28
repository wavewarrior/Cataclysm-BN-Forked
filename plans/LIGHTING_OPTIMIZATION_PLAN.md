# Lighting GPU Optimization — fix high-enemy-density crash

## STATUS (reviewed 2026-06-27)
Done ~70% — the crash fix shipped. P0✅ P1✅ P2✅ P3✅ verified in code; P4/P5/P6 = NOT started (all optional defense). KEEP for the open P4-P6 items, but the primary deliverable (P1+P2 TDR fix) is complete.
STALE refs: P1 + the Files table point at `rc.frag.hlsl` — that file was DELETED (GI moved to compute; see GI_COMPUTE_AND_PERF_PLAN). The rc.frag gate is moot; the equivalent guard now lives in `gi_field.comp`/`gi_bounce.comp`. P4 says on-fire radius is "hardcoded 8" but actual code is `6.0f` (snapshot.cpp:284/307) — still hardcoded/unmerged, just a different value.

## Context (read once)
Win11/D3D12 crashes (instant close = **GPU TDR**: one frame > ~2s watchdog → device removed)
when nearing 10+ enemies. Cause: every on-fire/glowing monster is a radius-8 omni **emitter**
(`snapshot.cpp:295-308`); `sprite.frag.hlsl:402` loops ALL emitters per pixel, geometric cull
only (`:414`), then **unconditional `trace_shadow` march** (`:429`, 16 steps ×2 inner loops,
4-tap SDF each). Horde overlap → 10-20 marches/pixel × 8.3M px (4K) → TDR. Same N-scaling in
`rc.frag.hlsl:91` gather. Creatures are NOT SDF occluders (SDF reads `transparency_cache` =
terrain/furniture/field/vehicle only); they are emitters only.
Shaders are runtime-loaded (`load_lighting_shader_source`) → P0/P1/P2 = HLSL-only, **no C++ rebuild**, hot-reload.
P1+P2 = the crash fix. P3+ = CPU/defense.

---

## P0 — Hygiene (1 edit, do first) ✅ DONE — `lit_seg` log block removed (grep=0)
- [x] `src/lighting/sprite_batcher.cpp:~733` — delete the per-frame `DebugLogFL(DL::Info, DC::Main) << "sprite_batcher lit_seg…"` block (`:725-745`). Writes disk every frame.
- Verify: `debug.log` no longer grows per frame in-game.

## P1 — Contribution-gated march (PRIMARY crash fix; invisible) ✅ DONE — `LIGHT_EPS_DEFAULT=0.004` + gate at sprite.frag.hlsl:443
Skip the march when the emitter's attenuated contribution ≈ 0. Edge-of-radius lights dominate the overlap *count* but add ~0 brightness.
- [x] `data/shaders/lighting/src/sprite.frag.hlsl` emitter loop: after `atten` (`:415`) + `lambert` (`:423`), **before** `trace_shadow` (`:429`), add:
      `if (atten * lambert <= LIGHT_EPS) continue;`  (`static const float LIGHT_EPS = 0.004;` near the top consts.) — implemented as `LIGHT_EPS_DEFAULT` const + `light_eps` knob, gate at `:443`.
- [x] ~~`rc.frag.hlsl` gather loop~~ — MOOT: rc.frag DELETED (GI now compute `gi_field.comp`/`gi_bounce.comp`).
- Verify (Mac, hot-reload): normal lit scene + F4 cursor-light **pixel-identical** to current. Then Win11: walk the horde that crashed → no crash; F4 emitter readout climbs, frame stays alive.

## P2 — Per-pixel march budget (hard TDR bound; tiny visual risk) ✅ DONE — K-strongest `top_val[64]`/`k_max` at sprite.frag.hlsl:417-467
Guarantees an upper bound regardless of density.
- [x] `sprite.frag.hlsl`: accumulate unshadowed light for all in-range emitters, but run `trace_shadow` only for the **K strongest** by `atten*lambert` (track K-max inline, no sort). Weaker in-range lights add unshadowed. — implemented with inline `top_idx`/`top_val[64]` min-replace.
- [x] Expose `K` (+ promote `LIGHT_EPS`) as F4 knobs: `max_shadow_k` + `light_eps` in `debug_params` (sprite_batcher.h:171/173); `light_eps` slider bound in sdl_lighting_devui.cpp:446.
      - `src/lighting/sprite_batcher.h:127` `struct debug_params` (currently **128 B**, alignment-packed) — add the slot(s); Phase-1 `LIGHT_EPS` can stay a shader const to avoid a struct change. If growing: keep 16-B aligned, update the HLSL `DebugParams` cbuffer layout to match.
      - assemble in `src/sdl_render_frame.cpp:~186`; slider in `src/sdl_lighting_devui.cpp` (pattern: `dbg_slider(...)` ~`:257`).
- Verify: drop K low in a horde → cost capped, only nearest-K cast shadows; raise back → look returns.

## P3 — Gate SDF rebuild on transparency change, not turn (combat CPU; user-requested) ✅ DONE — `transparency_generation` gate
The 16×-supersampled SDF DT rebuilds whenever `now != last_turn` (`sdl_render_frame.cpp:131`) — fires every combat **action** even on static terrain (creatures moving don't change the SDF). Gate on real transparency dirtiness instead.
- [x] **Gotcha:** `build_transparency_cache` resets `transparency_cache_dirty` during the sim build... Add a `uint64 transparency_generation` to `level_cache`, bump it in `map::set_transparency_cache_dirty`. — `transparency_generation` in map.h:342, bumped at map.cpp:459/588.
- [x] `src/sdl_render_frame.cpp`: replace the `now != last_turn` driver with `gen != last_gen` ... Track `static uint64 last_gen`. — implemented at sdl_render_frame.cpp:159/181/193. (NOTE: follow-on `LIGHTING_PERF_PLAN.md` in src/lighting/ proposes splitting the single `rebuild_pertile` gate per-buffer — separate, not-started.)
- Verify: stand still in a horde fight (terrain static) → SDF rebuild count stays flat across turns; smash a window/start a fire → rebuilds once. No visual change.

## P4 — Trim emitter N at source (optional defense) — NOT started
- [ ] `src/lighting/snapshot.cpp` (on-fire) — radius is hardcoded `6.0f` (snapshot.cpp:284 monster, :307 player; plan said `8` — value differs but still hardcoded); lower it, and/or merge co-located same-color omnis (hordes stack near-identical emitters on adjacent tiles). Reduces N for BOTH GPU loops. No merge logic present.

## P5 — Throttle GI/vol passes when dense (optional, only if P1+P2 short)
- [ ] In `src/sdl_render_frame.cpp` pass-driving: when `rs.collector().last_count()` high, update RC gather/bounce + volumetric every Nth frame (cascade already retained on skip).

## P6 — DEFERRED: tile light grid (Forward+)
Only if P1+P2 insufficient. CPU-bin emitters into screen tiles; `sprite.frag` iterates only its tile's list → removes the global per-pixel loop. Largest effort/risk on this D3D12 path.

---

## Files
| File | Phase |
|---|---|
| `data/shaders/lighting/src/sprite.frag.hlsl` | P1 gate `:429`, P2 K-budget |
| `data/shaders/lighting/src/rc.frag.hlsl` | P1 gate `:91` |
| `src/lighting/sprite_batcher.cpp` | P0 del `:725-745` |
| `src/lighting/sprite_batcher.h` | P2 `debug_params` slot `:127` |
| `src/sdl_render_frame.cpp` | P2 cbuffer `:186`; P3 gate `:123-135`; P5 throttle |
| `src/sdl_lighting_devui.cpp` | P2 sliders `~:257` |
| `src/lightmap.cpp` / `src/map.cpp` | P3 generation counter (`lightmap.cpp:240`, `map.cpp:452/578`) |
| `src/lighting/snapshot.cpp` | P4 fire radius `:304` |

## Verification (Win11 is the only TDR judge — Mac can't measure GPU frame time)
1. **Look-unchanged (Mac, hot-reload):** after P1, normal scene + F4 cursor-light pixel-identical; EPS/K at default = no-op.
2. **Crash-fixed (Win11):** load the horde save that crashed, walk in → no close/freeze.
3. **Regression:** molotov'd horde, glowing-enemy nest, dense urban night (streetlights+fires).
4. Commit per phase (shader phases hot-reload / `git revert` clean) for bisect.

# Lighting GPU Optimization — fix high-enemy-density crash

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

## P0 — Hygiene (1 edit, do first)
- [ ] `src/lighting/sprite_batcher.cpp:~733` — delete the per-frame `DebugLogFL(DL::Info, DC::Main) << "sprite_batcher lit_seg…"` block (`:725-745`). Writes disk every frame.
- Verify: `debug.log` no longer grows per frame in-game.

## P1 — Contribution-gated march (PRIMARY crash fix; invisible)
Skip the march when the emitter's attenuated contribution ≈ 0. Edge-of-radius lights dominate the overlap *count* but add ~0 brightness.
- [ ] `data/shaders/lighting/src/sprite.frag.hlsl` emitter loop: after `atten` (`:415`) + `lambert` (`:423`), **before** `trace_shadow` (`:429`), add:
      `if (atten * lambert <= LIGHT_EPS) continue;`  (`static const float LIGHT_EPS = 0.004;` near the top consts.)
- [ ] `data/shaders/lighting/src/rc.frag.hlsl` gather loop (`:91`): same pre-march `atten`-based `continue`.
- Verify (Mac, hot-reload): normal lit scene + F4 cursor-light **pixel-identical** to current. Then Win11: walk the horde that crashed → no crash; F4 emitter readout climbs, frame stays alive.

## P2 — Per-pixel march budget (hard TDR bound; tiny visual risk)
Guarantees an upper bound regardless of density.
- [ ] `sprite.frag.hlsl`: accumulate unshadowed light for all in-range emitters, but run `trace_shadow` only for the **K strongest** by `atten*lambert` (track K-max inline, no sort). Weaker in-range lights add unshadowed.
- [ ] Expose `K` (+ promote `LIGHT_EPS`) as F4 knobs:
      - `src/lighting/sprite_batcher.h:127` `struct debug_params` (currently **128 B**, alignment-packed) — add the slot(s); Phase-1 `LIGHT_EPS` can stay a shader const to avoid a struct change. If growing: keep 16-B aligned, update the HLSL `DebugParams` cbuffer layout to match.
      - assemble in `src/sdl_render_frame.cpp:~186`; slider in `src/sdl_lighting_devui.cpp` (pattern: `dbg_slider(...)` ~`:257`).
- Verify: drop K low in a horde → cost capped, only nearest-K cast shadows; raise back → look returns.

## P3 — Gate SDF rebuild on transparency change, not turn (combat CPU; user-requested)
The 16×-supersampled SDF DT rebuilds whenever `now != last_turn` (`sdl_render_frame.cpp:131`) — fires every combat **action** even on static terrain (creatures moving don't change the SDF). Gate on real transparency dirtiness instead.
- [ ] **Gotcha:** `build_transparency_cache` resets `transparency_cache_dirty` (`lightmap.cpp:240`) during the sim build, BEFORE render — so the bitset reads empty at render time. Add a `uint64 transparency_generation` to `level_cache`, bump it in `map::set_transparency_cache_dirty` (`map.cpp:452,578`). Read at render time.
- [ ] `src/sdl_render_frame.cpp:123-135`: replace the `now != last_turn` driver with `gen != last_gen` (keep `imgui_layer::visible()`, `z`, `origin` terms; keep `now`/time only for the sun cbuffer, which is NOT the per-tile rebuild). Track `static uint64 last_gen`.
- Verify: stand still in a horde fight (terrain static) → SDF rebuild count stays flat across turns; smash a window/start a fire → rebuilds once. No visual change.

## P4 — Trim emitter N at source (optional defense)
- [ ] `src/lighting/snapshot.cpp:304` (on-fire) — radius hardcoded `8`; lower it, and/or merge co-located same-color omnis (hordes stack near-identical emitters on adjacent tiles). Reduces N for BOTH GPU loops.

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

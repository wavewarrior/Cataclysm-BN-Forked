# Dynamic Lighting Rework — Cataclysm-BN (Stoneshard-tier)

## Context

Current lighting: per-turn CPU shadowcasting (`src/lightmap.cpp`, `src/shadowcasting.cpp`) — Beer-Lambert decay, 4-quadrant per-tile lightmap, parallel SMX collection, ~5–15 ms rebuild, ~1.2 MB/Z-level. Drives SDL_Renderer tile output via CPU color-modulation on pre-baked atlas variants (`src/cata_tiles.cpp:4297-4431`). Curses ASCII parallel path. Result: per-tile granularity, no flicker, no soft shadows, no bounce, no atmosphere.

**Goal**: high-fidelity dynamic lighting at Stoneshard-tier or better — per-pixel light propagation, soft penumbra shadows, RGB colored lights, normal-mapped tile relief, volumetric god-rays through smoke, indirect bounce GI (world-anchored), AO, reflections, bloom + AgX tonemap, smooth day/night cycle. GPU offload mandatory. Solo-dev project — no soak periods, no opt-in flags, no backward compat.

> **Reconciled 2026-06-01 against shipped code (`feature/improvements`).** This
> doc had drifted hard: it claimed Phase 2 was mid-cutover and phases 3–14 were
> "pending", and it specified a ~16-pass GPU pipeline. Reality: the bridge is
> gone, scissor + screenshot shipped, and direct shading / soft shadows / sun /
> 24h cycle / fake GI / a full vision rework all ship **inline in one fragment
> shader** — none of the planned pass files exist. See **Architecture pivot** and
> **Decisions superseded by implementation** below. Three locked decisions
> (#13, #14, #24) were silently overridden in code; they are flagged for grill,
> not silently rewritten.

## Status

| Phase | State | Notes |
|---|---|---|
| 1. Curses + Android removal | ✅ done | commit `e96086b658`. 169 files, -13042 lines. |
| 2. SDL_GPU cutover | 🟢 near-complete | Bridge removed 2026-05-22. Scissor (7d) + screenshot (7e) landed. **Remaining**: pixel_minimap still on SDL_Renderer (7b), loading-image GPU path disabled for crash (7c), SDL_Renderer not yet deletable (7f — atlas still needs SDL_Texture key). See "Phase 2 finish" table. |
| 3. Snapshot + emitter collector + event queue | 🟢 shipped (single-thread) | `snapshot`, `emitter_collector`, `event_queue` all exist. Transient flashes WIRED: `explosion.cpp:1565,1683`, `weather.cpp:480`. **Not** on a dedicated thread (decision #14 overridden). `ranged.cpp` flash NOT wired. |
| 4. SDF + transparency upload | 🟢 shipped (CPU BFS) | `sdf_pass.cpp` — CPU **Chebyshev BFS** (~17ms), not GPU JFA (decision #24 overridden). Uploaded as fragment storage buffer. |
| 5/6. Direct shading + RGB + soft shadows | 🟢 shipped (inline) | Per-pixel emitter loop, RGB colour, shared `trace_shadow` SDF sphere-trace + world-locked Bayer dither. All in `SPRITE_FRAG_HLSL`. |
| 7. Normal maps / Lambert relief | 🔴 absent | Hardcoded flat normal `(0,0,1)`; comment-only. `normal_gen` never created. |
| 8. Sun + skylight + 24h LUT | 🟢 shipped (inline) | 24h keyframe LUT, sphere-trace sun shadow, sky-vis gate. **Weather multiplier absent.** |
| 9. GI + AO | 🟡 GI shipped (CPU diffusion), AO absent | GI = CPU 2-pass wall-gated diffusion + `gi_strength` knob, NOT probe grid (decision #13 overridden). AO not built. |
| 10–13. Volumetric / reflection / underwater / bloom+tonemap | 🔴 absent | None built. All require an HDR render target + post stack that does not exist yet. |
| 14. Z-level 2.5D holes | 🟡 implicit | Per-z SDF + emitter z-filter handle cross-Z; no explicit `hole_up/down` upload. |
| — Vision rework | 🟢 shipped (inline) | `plan_vision_rework.md` effectively done: vis-edge + radial falloff, memory desaturate-fade, night/day ambient floor (frag shader). |
| — ImGui debug panel | 🟢 shipped (not in original plan) | F4 dev panel (`imgui_layer`), all tuning knobs live. |

### Phase 2 finish — remaining commits

Bridge approach (2i-B-3) already meets the plan's pixel-parity gate. To get
to the row table's ambition ("SDL_Renderer deleted; legacy files removed"),
each remaining draw site needs migration. Ordering forced by atlas dependency:

| Commit | State | Notes |
|---|---|---|
| 2i-B-6 | ✅ landed | CachedTTFFont + BitmapFont glyphs route through font_engine queue. Verified Win11 D3D12. |
| 2i-B-5 | ✅ landed | dynamic_atlas dual-back + cata_tiles `draw_sprite_at` unrotated path → tile_batcher + sdl_geometry rect → ui_batcher. Verified Win11 D3D12. Rotated sprites still fall through to legacy SDL_RenderTextureRotated. |
| 2i-B-7 | ⏳ pending | Cleanup. **Cannot fully delete SDL_Renderer** until rotation lands on GPU. Smaller bounded steps: |
|   2i-B-7a | ✅ landed | Added `rotation` field + 12 bytes padding to sprite_instance (struct now 64 B). sprite.vert.hlsl rotates the dst quad around its centre via cos/sin in radians. draw_sprite_at routes every rotation through the GPU path; legacy SDL_RenderTextureRotated remains only as the find_gpu_texture_full-miss fallback. Awaiting Win11 verify. |
|   2i-B-7b | ⏳ | pixel_minimap NOT migrated + invisible: `render()` blits `main_tex` via `RenderCopy(renderer,…)` (`pixel_minimap.cpp:324`) to the no-op'd display_buffer target. The line-190 `queue_ui_rect` comment is stale (no such call); a May-22 GPU migration was reverted. Migrate to tile_batcher, or drop the minimap. |
|   2i-B-7c | 🟡 partial | loading_image GPU upload built, but the enqueue path is `if(false && …)` "DIAG: disabled to isolate crash" (`loading_ui.cpp:398,420`); legacy `RenderCopy` is the live path. Root cause documented in `src/lighting/CLAUDE.md` (separate-CB barrier race). Re-enable = upload on the render CB or fence the copy CB. |
|   2i-B-7d | ✅ landed | `sprite_batcher::set_scissor` + `SDL_SetGPUScissor` (`sprite_batcher.cpp:898-909,1067-1075`). Clip wrappers can route through GPU. |
|   2i-B-7e | ✅ landed | `save_screenshot` uses SDL_GPU copy-pass readback, no `SDL_RenderReadPixels`/`display_buffer` (`sdltiles.cpp:3920-3960`). |
|   2i-B-7f | ⏳ | Mechanical delete blocked. `SDL_Renderer_Ptr renderer` + `SDL_CreateRenderer` still live (`sdltiles.cpp:129,350,363`); `display_buffer` still referenced (`sdltiles.cpp:3781`) **despite memory claiming "7f Part A removed it" — memory stale**. `copy_surface_to_dynamic_atlas` still creates SDL_Textures used as the `find_gpu_texture_full` key → SDL_Renderer cannot die until the atlas switches to a pure GPU key. |

Estimate: 6 small commits, ~−3000 LOC net once mechanical delete runs.
Each step keeps the game playable: until 7f, the bridge is alive for
whatever consumers haven't migrated yet. After 7a, the bridge is empty
in normal play because all common sprite rotations + all rects + all
text are on GPU; legacy fallback only fires for 45° tricks. After 7b
the minimap is on GPU. After 7c-d UI surfaces are migrated. After 7e
the screenshot path stops needing SDL_RenderReadPixels. 7f is the
final removal.

**Pattern noted while planning**: the bridge in 2i-B-3 exists *because*
these subsystems aren't cleanly separable in this codebase. Repeated
attempts to find a "smaller first migration" (loading_ui, vehicle_preview,
geometry-alone, atlas-alone) each discovered a coupling forcing the next
neighbour to come with it. 2i-B-5 (cata_tiles + atlas + rects atomic
cutover) confirmed the same shape — it's now one commit that landed
clean. Remaining 7a-f are independently sized.
| 3–14 | pending | see Phasing below |

### Phase 2 progress (branch `feat/lighting-phase2-sdl_gpu`)

| Sub | SHA (prefix) | Files | Verified |
|---|---|---|---|
| 2a | `b3879e93` | `src/lighting/gpu_device.{h,cpp}`, CMake `-I src` | standalone obj + nm vs libSDL3 |
| 2b | `58bd1200` | `src/lighting/sprite_batcher.h` | header-only |
| 2c | `675f7d1f` | SDL_shadercross FetchContent, `shader_compiler.{h,cpp}`, `sprite.{vert,frag}.hlsl`, runtime DLL post-build | standalone obj |
| 2d | (next) | `sprite_batcher.cpp`, `sprite.vert.hlsl` (instance_base uniform) | standalone obj, all 23 SDL_GPU syms in libSDL3 |
| 2e | (next) | `gpu_atlas.{h,cpp}` | standalone obj |
| 2f | (next) | `font_engine.{h,cpp}` (TTF_CreateGPUTextEngine; ALPHA only) | obj + nm vs libSDL3_ttf 3.2.2 |
| 2g | (next) | `gpu_geometry.{h,cpp}` (1×1 white tex → sprite_batcher) | standalone obj |

Total foundation: ~3.2 kLOC new code, every translation unit compiles
clean and links against the actually-fetched SDL3 + SDL3_ttf +
SDL_shadercross. **No call sites yet — game still uses the legacy
SDL_Renderer path 100 %.**

Deferred from phase 1: `.github/workflows/*` still reference `TILES` env var (doesn't block local compile).

### Tech baseline (verified 2026-05-20)

- **SDL3 = 3.4.8** (CMake FetchContent: `release-3.4.8.tar.gz`). `SDL_gpu.h` available — SDL_GPU API landed in 3.2.0.
- **SDL3_ttf = 3.2.2** — exposes `TTF_CreateGPUTextEngine` + `TTF_GetGPUTextDrawData`. Glyph upload to `SDL_GPUTexture` is first-class; no need to keep an `SDL_Renderer` alive for text.
- **SDL3_image = 3.4.4**, **SDL3_mixer = 3.2.0**.
- **SDL_shadercross — strategy B: runtime translation.** Pin
  `libsdl-org/SDL_shadercross @ 6b06e55c` (no tagged release yet) via
  `FetchContent` with `SDLSHADERCROSS_VENDORED=ON` so DXC and SPIRV-Cross are
  built in-tree — no system Vulkan SDK needed. Ship HLSL sources under
  `data/shaders/lighting/src/`; translate on first use through
  `lighting::compile_graphics_shader` (`src/lighting/shader_compiler.*`).
  Backend coverage:
    * macOS / Metal — HLSL → SPIR-V → MSL.
    * Windows 11 / D3D12 — HLSL → SPIR-V → DXIL via DXC. **Primary play
      target: RTX 4090.** Vulkan path remains available.
    * Linux / Vulkan — HLSL → SPIR-V.
  Runtime DLL/dylib (`SDL3_shadercross-shared` + `dxcompiler*`) auto-copied
  next to the game executable by a `POST_BUILD` step that follows
  `$<TARGET_RUNTIME_DLLS:...>` so a fresh Windows checkout requires no manual
  Vulkan SDK install.
- Hard-cutover "build will not compile until phase 2 done" → revised: phase 2 runs as **a single long branch (~10 commits)** with build green at every commit. New GPU code lands inert (no call-sites) until sub-phase 2i flips the switch and removes `SDL_Renderer`.

### Phase 2 sub-decomposition

Each row = one commit on `feat/lighting-phase2-sdl_gpu`. Build green at every step.

| # | Commit | Touches | Wired? |
|---|---|---|---|
| 2a | GPU device + window claim + swapchain (`gpu_device.{h,cpp}`) | new | no |
| 2b | Sprite batcher header + draw-cmd POD (`sprite_batcher.h`) | new | no |
| 2c | SDL_shadercross FetchContent + `shader_compiler.{h,cpp}` + HLSL sources | new | no |
| 2d | Sprite batcher impl + per-pipeline graphics pipelines | new | no |
| 2e | `dynamic_atlas` → `SDL_GPUTexture` backing (parallel path; old kept) | mod | no |
| 2f | Font path → `TTF_CreateGPUTextEngine` (parallel) | mod | no |
| 2g | Geometry renderer → GPU pipeline (parallel) | mod | no |
| 2h | Pixel-parity golden-image harness (`tests/lighting_gpu_test.cpp`) | new | no |
| 2i | **Cutover commit** — `sdltiles.cpp` switches `WinCreate` to GPU device; `SDL_Renderer*` deleted; `sdl_wrappers.h` `SDL_Renderer_Ptr` removed; legacy `dynamic_atlas`/`sdl_font`/`sdl_geometry` paths deleted. | mass-mod | yes |

### Phase 2 atlas semantics (advisor concern #5)

Legacy: `dynamic_atlas::allocate_sprite` creates `SDL_TEXTUREACCESS_TARGET` textures and **renders into them via `SDL_SetRenderTarget` + draw calls** (uses CPU staging surface that gets copied with SDL_Renderer ops).

GPU equivalent: `SDL_GPUTexture` + `SDL_GPUTransferBuffer`. No render-target-into-atlas pattern; instead **upload via transfer buffer** (`SDL_UploadToGPUTexture`). Staging surface remains CPU-side (`SDL_Surface` for IMG_Load), then `SDL_MapGPUTransferBuffer` → memcpy → `SDL_UploadToGPUTexture`. Render-target-style writes (active_tile_data animated overlays etc.) move to compute-shader or per-frame uploaded sub-texture. Spelled out in sub-phase 2e commit.

## Decisions (locked w/ user, 20 grill rounds)

| # | Axis | Choice |
|---|---|---|
| 1 | Visual approach | **GPU pixel-shader pipeline via SDL_GPU** (SDL3). Per-pixel forward+ lighting. |
| 2 | Cadence | **Per-frame**. Sim shadowcasting unchanged on its turn cadence (truth for AI/visibility). Shader evaluates independently each frame. |
| 3 | Tile-sim algorithm | **Keep shadowcasting** unchanged. Beam-cast dropped. |
| 4 | Render API | **Single device: SDL_GPU**. SDL_Renderer **removed entirely**. Tile atlas + UI both via SDL_GPU instance batcher. |
| 5 | Curses | **Removed**. All `#ifndef TILES` paths deleted. |
| 6 | Mobile/Android | **Dropped**. CMake/CI/packaging cleared. |
| 7 | Min spec | Modern desktop. Vulkan 1.2 / Metal 3 / D3D12. RTX 3000 / M1 baseline. |
| 8 | Z-levels | **2.5D**. `hole_down[z][xy] = !floor_cache[z][xy]`; `hole_up[z][xy] = !floor_cache[z+1][xy]`. Packed RG8 per-Z. |
| 9 | Gameplay impact | **None**. Sim `lit_level` (incl. memorized + visibility) gates all shader output. No wallhacks via bounce/AO/volumetric. |
| 10 | Emitter set | Split **static** (terrain/furniture/field/stationary monster) vs **dynamic** (player-held, vehicle headlights, mobile monster glow, on-fire). |
| 11 | Emitter shape | `gpu_emitter { pos, rgb, radius, falloff, cone_dir, cone_half_angle, shape, flicker_seed }` w/ `shape ∈ {OMNI, CONE, DIRECTIONAL}`. Item JSON: `light_shape` (default omni). Lamps = omni. Flashlights = cone. |
| 12 | Cone direction | Vehicle = `vehicle::face`. Hand-held = cursor/aim vector when active, else last-movement direction. |
| 13 | GI architecture | **World-space probe grid** (40×40 per Z × reality-bubble Z, 8-tile spacing, RGB16F). Brute-force **full GPU rebake every frame** from emitter SSBO + SDF. No CPU bake step, no dirty tracking. |
| 14 | Concurrency | **Dedicated `std::thread`** (NOT existing `thread_pool`) for emitter collector. Sleeps on `condition_variable`. **Snapshot model**: main copies handle-list (~10KB) at frame start; collector resolves on its thread — never touches live game state. Double-buffered SSBO. |
| 15 | Normal maps | **Auto-generate via alpha-aware Sobel + edge-fade-to-flat at sprite silhouettes**. Per-tileset content-hash disk cache. **`_n.png` override path** present from day one for tileset artists. |
| 16 | Emissive | **Auto-detect from emitter co-location** — any tile hosting an OMNI/CONE emitter receives `emitter_rgb × intensity` multiplied over sprite pixels in composite, before bloom. Cone lights opt out (lens shouldn't glow). |
| 17 | Sun + sky | **Sun = directional light + ray-marched SDF shadows (cap 32 steps, angular-diameter penumbra)**. Separate **skylight ambient** = `sky_visibility × sky_color` where `sky_visibility = AND(!floor_cache[z+1..])` chain. Both colors from **24h RGB LUT**. Weather multiplier on top. |
| 18 | Memorized + visibility | Per-tile `lit_level` uploaded as R8 enum. Shader branch: `LIT/BRIGHT/LOW` → full shading; `MEMORIZED` → sepia, skip shading; `DARK/BLANK` → black. **All shader effects gated by visibility mask** — no wallhack via bounce. |
| 19 | Atlas variants | Pre-baked atlas variants collapsed to **memorized + underwater only**. Shadow/night derived live. |
| 20 | Volumetric | **Half-res ray-march + bilateral upsample + blue-noise dither + adaptive steps**. Quality tiers OFF/LOW/MED/HIGH/ULTRA, default MED. Global `outdoor_fog_density` uniform feeds volumetric for weather-driven god-rays. Density texture from field tiles + outdoor fog. |
| 21 | Reflection | **Water-reflects-sky + wet-floor sheen + rain-induced wetness**. No mirror-tile scene reflection (gameplay-tangled, low ROI). Per-tile `tile_reflectivity` ∈ {0..1} from JSON or material flag. |
| 22 | Underwater | **Fullscreen post effect when submerged** (blue tint, wave UV distortion, chromatic dispersion, vignette) **+ water-column light attenuation** (per-tile material absorption). No caustics v1. |
| 23 | Bloom + tonemap | **6-mip dual-filter bloom, threshold 1.5× diffuse-max, AgX tonemap default** (ACES/Reinhard also available). **Per-tileset `grade.cube` 3D LUT** support post-tonemap. |
| 24 | SDF | **4 texels per tile (1296×1296 per Z layer), R16F, JFA per-frame full rebuild**. ~1.5 ms/Z. |
| 25 | Transient events | **Lighting event queue**. Sim pushes `{pos, rgb, intensity, duration_ms}` for explosions, muzzle flashes, lightning, sparks. Collector drains + ages out per frame. ~20 sim call sites. |
| 26 | Mod compat | **Soft fallback**. Missing `_n.png` → Sobel auto-gen. Missing `grade.cube` → neutral LUT. Missing `light_shape` → omni. Missing `tile_reflectivity` → 0. All community tilesets work day one at lower fidelity. |
| 27 | Bundled tilesets | Ship Sobel-auto normals on day one. Artist-tune later. |
| 28 | Animated sprites | Sprite-batcher: instance buffer per layer, CPU computes frame index per anim tile per frame. Vehicle multi-part = many instances. Smoke writes both sprite-instance (faint base) + density texture (volumetric input). |
| 29 | Shipping | Solo dev. No soak periods, no opt-in flags between phases. Each phase lands → use immediately. Phase 2 = hard cutover, no legacy renderer remains. |

## Decisions superseded by implementation

Three decisions locked in the original table were overridden in shipped code to
ship the cheap version fast. **Grilled + resolved 2026-06-01** against a quality
bar (bevy-magic-light-2d reference + observed artifacts): the cheap CPU GI/SDF
produce a squarish penumbra and blocky bounce the reference does not. Perf is not
the issue; **quality is** — so #13 and #24 are *retargeted* to GPU, #14 is
ratified with a gate.

| # | Locked choice | Shipped (cheap) | **Resolution (grilled)** |
|---|---|---|---|
| 13 | World-space probe-grid GI | CPU 2-pass wall-gated per-tile diffusion (a tile-res box blur — not light transport → blocky, grid-aligned bounce) | **RETARGET → Radiance Cascades** (Sannikov 2023; noise-free *without* temporal — fits the discrete tile-scroll camera, which would ghost under raymarch+temporal). Bilinear-Fix variant; port from a known-good open impl (Hybrid46/tlegoc); D3D12/shadercross smoke-test *before* building around it. Runs as fragment ping-pong on the RT backbone. |
| 14 | Dedicated `std::thread` + double-buffered snapshot | Single-thread main collect; SDF/GI/vis rebuilt **every** `refresh_display` incl. UI-only redraws (`sdltiles.cpp:766-897`, no turn gate) | **RATIFY single-thread** (no observed hitches at play map size; a thread solves a problem we don't have) + **ADD dirty-gate**: key on `{turn, z, camera-origin}`, skip the rebuild + re-upload when unchanged; **bust the gate while the F4 panel is visible** so debug tuning stays realtime. F4 knobs are per-frame uniforms (outside the gated block) so they already stay live. |
| 24 | GPU JFA SDF, R16F | CPU **Chebyshev** BFS (chessboard metric → square isolines → squarish penumbra; memory file admits it) | **RETARGET → GPU JFA SDF** (Euclidean → round isolines → smooth penumbra). Fragment ping-pong on the RT backbone. The inline shader already *samples* the SDF, so shadows improve with **no shader change** once the buffer is Euclidean. (CPU Euclidean-DT quick-win was offered and declined — going straight to GPU JFA.) |

**Why CPU at all (root cause):** no compute pipeline exists (`shader_compiler`
supports only VERTEX/FRAGMENT) and no multi-pass RT wiring was live; CPU BFS +
box-diffusion were the expedient stand-ins against the one graphics pipeline.
**Compute is not required** to fix this — JFA and Radiance Cascades both run as
**fragment ping-pong over render targets**, and the RT machinery is half-built
(`ui_composite_target` + the stubbed `sprite_batcher.color_target_format`).

## Architecture pivot (single-shader, CPU-fed) — supersedes the 16-pass design

The original §Architecture below specified ~16 discrete GPU passes with one
file each. **That pipeline was not built.** Instead, every cheap lighting effect
lives **inline in `SPRITE_FRAG_HLSL`** (`src/lighting/sprite_batcher.cpp`), fed
by 5 CPU-built fragment storage buffers:

| Slot | Buffer | Built by | Carries |
|---|---|---|---|
| 0 | Emitters | `emitter_collector` | `gpu_emitter[]` (pos, rgb, radius, falloff; `cone_*` unused) |
| 1 | SDF | `sdf_pass` (CPU Chebyshev BFS) | per-tile distance, bilinear-sampled |
| 2 | SkyVis | `sdf_pass` | per-tile `outside_cache` (sun/sky gate) |
| 3 | Indirect | CPU diffusion (in `refresh_display` SDF block) | per-tile RGB bounce |
| 4 | Vis | snapshot | per-tile `seen_cache` (≥0 live, <0 memorized) |

Inline in the fragment shader today: direct RGB emitter shading, soft SDF
shadows (`trace_shadow` sphere-trace, shared emitter+sun) + world-locked Bayer
dither, sun/sky 24h LUT, fake 1-bounce GI, and the full vision rework. Tuning
knobs live in `debug_params` (96 B) / `light_params` / `sun_params`, driven by
the **F4 ImGui panel** (`imgui_layer`).

**Forced inflection for the rest of the roadmap:** everything past normal maps
(bloom, AgX tonemap, LUT, volumetric, underwater, reflection) needs the lit
result in a sampleable **RGBA16F render target**, not direct-to-swapchain. The
original §Architecture composite/post passes assumed that target. It still
doesn't exist — but `ui_composite_target.{h,cpp}` already provides reusable
offscreen `COLOR_TARGET` + dirty-tracking machinery to build it from. The old
16-pass table below is **retained as the design reference for that post stack**,
not as the literal plan.

## Architecture

### CPU side
- Existing `lightmap.cpp` shadowcasting + `apparent_light_at` **unchanged**. Continues producing `lit_level` per tile + `seen_cache` for AI/gameplay truth.
- **Emitter collector** (`src/lighting/emitter_collector.{h,cpp}`, new) — dedicated `std::thread`. Reads frame-start snapshot of emitter source handles (terrain/furniture, fields, items, creatures, vehicles, sun proxy). Resolves to `gpu_emitter` SSBO. Includes lighting event queue drain (transient flashes). Double-buffered upload.
- **Snapshot builder** (`src/lighting/snapshot.{h,cpp}`, new) — main thread, at frame start, walks reality bubble + drains queue → produces handle-list. ~10KB/frame.
- **Lighting event queue** (`src/lighting/event_queue.{h,cpp}`, new) — sim pushes transient flashes (`g->lighting.push_flash(pos, rgb, intensity, ms)`). Wire ~20 call sites: `explosion.cpp`, `ranged.cpp`, weather lightning, electrical sparks.

### GPU side — SDL_GPU pipeline

> **ORIGINAL DESIGN — superseded; retained as the target for the post stack
> (phases 11–14).** Passes 1, 4, 6b are shipped inline in `SPRITE_FRAG_HLSL`
> (not as separate passes); 5, 7, 9, 10, 11, 13, 14 are not built. Shaders are
> compiled at **runtime** via SDL_shadercross from HLSL embedded in
> `sprite_batcher.cpp` + `data/shaders/lighting/src/*.hlsl`; **no `.spv` is
> shipped** (the "pre-compiled .spv" line below never happened).

Single device. All shaders SPIR-V, compiled via SDL_shadercross at build, pre-compiled `.spv` shipped.

| # | Pass | Output | Input / Notes |
|---|---|---|---|
| 1 | Tile composite | Albedo RT (RGBA8) + per-tile metadata (`lit_level` R8, memorized flag, reflectivity R8, sky_visibility R8) | SDL_GPU instance batcher draws tile sprites + vehicle parts + animated frames + field overlays. One draw call per atlas page per layer. |
| 2 | Normal-map gen (tileset load only) | Per-tile normal atlas RG8 | Alpha-aware Sobel + edge-fade-to-flat. Cached to disk by tileset content hash. Overridden by `_n.png` when present. |
| 3 | Transparency + density upload | Transparency `texture2DArray` (R8/Z) + smoke density `texture2DArray` (R8/Z) + outdoor fog uniform | From existing `level_cache::transparency_cache` + field tiles. |
| 4 | SDF (JFA) | Distance field R16F, 4 texels/tile, ~1296²/Z | Per-frame full rebuild. ~log2(1296) ≈ 11 passes. |
| 5 | Forward+ light bin | Per-screen-tile light index list (R32U, 16×16-px tiles) | Compute shader bins emitter SSBO. Adjacent-Z emitters included when `hole_up/down` along ray. Per-bin cap N=32 lights w/ importance sort (`intensity × radius / dist²`). |
| 6a | Sun + skylight | Sun/sky RGB16F | Sky-visibility AND-chain, 24h LUT (sun_color, sky_color separate), weather mult. SDF march along sun_dir, cap 32 steps, angular penumbra. |
| 6b | Direct shading | Lit RT RGB16F | Albedo × normal Lambert × (sum of binned emitters × soft shadow via SDF ray-march). Penumbra step ∝ light_radius / hit_dist. Gated by visibility mask. Memorized → sepia branch. |
| 7 | GI probe rebake | World-space probe grid RGB16F (40×40 × Z × bubble-Z) | Per-probe rays sample SDF + emitter SSBO for direct + 1 bounce. Full rebake every frame. World-anchored — camera pan free. |
| 8 | GI sample | Bounce RT RGB16F | Per-pixel trilinear sample of probe grid via world coords. Includes dynamic-emitter bounce w/ blue-noise jitter (handles emitter motion, not camera). |
| 9 | AO | AO factor R8 | SDF + normal multi-tap cone-trace, 3×3 kernel. |
| 10 | Volumetric | Scatter RT RGB16F half-res | Density texture array + emitter SSBO + SDF + outdoor fog. Blue-noise dithered ray-march 24-64 steps (tier-dependent), adaptive step length. Bilateral upsample. |
| 11 | Reflection | Reflect RT RGBA16F | `tile_reflectivity` mask + sky_color + lit-RT offset sample. Water/wet/rain. |
| 12 | Composite | HDR final RGB16F | `(direct + sun + bounce) × AO + volumetric + reflection + emissive`. Day/night ambient blend uniform. |
| 13 | Underwater post | RGB16F | When player submerged: blue tint + UV wave distortion (scrolling 2D noise) + chromatic dispersion + vignette. Skipped above water. |
| 14 | Post: bloom + tonemap + grade + dust | Screen RGB8 | 6-mip dual-filter bloom (threshold 1.5×) → AgX tonemap → per-tileset 3D LUT → optional dust particles. |
| 15 | UI overlay | Screen | UI elements via SDL_GPU instance batcher (separate atlas), drawn over tonemapped output. |

### Frame budget (1080p, RTX 3060 / M1, MED tier)

| Pass | ms |
|---|---|
| Snapshot + emitter collect (own thread, hidden) | 0 |
| Sim shadowcasting (own cadence, kept) | 0 (per-turn, off frame) |
| SDF JFA (3 Z layers) | ~4 |
| Tile composite | ~1 |
| Forward+ binning | ~0.5 |
| Sun + skylight | ~1 |
| Direct shading + soft shadows | ~3 |
| GI probe rebake | ~2 |
| GI sample | ~0.5 |
| AO | ~0.5 |
| Volumetric (half-res MED) | ~3 |
| Reflection | ~0.5 |
| Composite + underwater + post | ~1.5 |
| UI | ~1 |
| **Total render** | **~18 ms** |

Target: 50-60 Hz on RTX 3060 baseline at MED. ULTRA needs 4070+/M2+. Volumetric tier is the primary quality dial.

## Critical files (existing) touched

- `src/lightmap.cpp:553-1018` — keep verbatim for `lit_level`/sim. Extract emitter-enumeration helper for re-use by collector.
- `src/lightmap.h:350-410` — `level_cache`: no new bitset needed (Z-holes derived from `floor_cache`).
- `src/cata_tiles.cpp:4297-4431` — `draw_sprite_at`: removed entirely. Replaced by SDL_GPU instance batcher.
- `src/sdltiles.cpp:120-394` — replace SDL_Renderer init w/ SDL_GPU device init. Strip software-renderer fallback.
- `src/cursesport.{h,cpp}`, `src/wcwidth.{h,cpp}`, `#ifndef TILES`-gated files — deleted (phase 1, done).
- `#ifdef __ANDROID__`-gated code, Android CMake, Android CI — deleted (phase 1, done).
- `src/options.{h,cpp}` — new: `LIGHTING_VOLUMETRIC_TIER` (off/low/med/high/ultra), `LIGHTING_TONEMAP` (agx/aces/reinhard), `LIGHTING_GI_QUALITY`, `LIGHTING_DUST`. No on/off master toggle — new pipeline is the only pipeline.
- `src/game.cpp` — spawn emitter collector thread; frame-start snapshot hook.
- `src/explosion.cpp`, `src/ranged.cpp`, `src/weather.cpp` — push lighting events to queue.
- `src/dynamic_atlas.{h,cpp}` — strip shadow/night atlas variants; keep memorized + underwater only.
- Item JSON loaders: new optional `light_shape` field.
- Tile JSON loaders: new optional `tile_reflectivity`, material flag mappings.

## New files — planned vs actual

**Created and shipping:**
- `src/lighting/snapshot.{h,cpp}` — main-thread frame-start handle collection.
- `src/lighting/emitter_collector.{h,cpp}` — SSBO build (single-thread, not worker).
- `src/lighting/event_queue.{h,cpp}` — transient flashes (wired: explosion, weather).
- `src/lighting/gpu_device.{h,cpp}` — SDL_GPU init, swapchain.
- `src/lighting/sprite_batcher.{h,cpp}` — instance batcher **+ all inline lighting** (`SPRITE_FRAG_HLSL`).
- `src/lighting/sdf_pass.{h,cpp}` — **CPU Chebyshev BFS** (not JFA) + SkyVis.
- `data/shaders/lighting/src/sprite.{vert,frag}.hlsl` — HLSL sources (also embedded in sprite_batcher.cpp). **No `.spv` shipped** — runtime-compiled.

**Created, not in original plan** (infrastructure the pivot required):
- `render_state.{h,cpp}` — singleton owning the whole GPU stack.
- `gpu_atlas.{h,cpp}`, `font_engine.{h,cpp}`, `gpu_geometry.{h,cpp}`, `shader_compiler.{h,cpp}`.
- `gpu_emitter.h` — emitter struct + shape enum.
- `imgui_layer.{h,cpp}` — F4 dev/debug panel.
- `ui_composite_target.{h,cpp}` — offscreen UI RT (reusable HDR-RT machinery).
- `ui_adaptor_draw_slices.h` — retained per-adaptor draw slices (partial-redraw fix).

**Planned, never created** (folded into the fragment shader or deferred to the post stack):
- `normal_gen` (deferred — Bucket A), `forward_plus`, `sun_sky_pass`, `direct_pass`, `gi_pass`, `ao_pass`, `volumetric_pass`, `reflection_pass`, `composite_pass`, `underwater_pass`, `post_pass`.

**Tests — not created:** `tests/lighting_emitter_test.cpp`, `lighting_gpu_test.cpp`, `lighting_bench.cpp`. (Verification has been manual + in-game so far.)

## Phasing

**Done (1–8 + GI + vision, see Status table):** curses/Android removal, SDL_GPU
cutover (bar minimap/loading/renderer-delete), emitter collector + event queue,
CPU SDF, inline direct shading + soft shadows, sun/sky 24h, fake GI, vision
rework, ImGui panel.

### Re-planned roadmap for what remains

Split by the **forced HDR render-target inflection**: everything past normal
maps needs the lit result in a sampleable RGBA16F target, not direct-to-swapchain.

**Bucket A — stays inline (no new render target):**
- **A1. Normal maps + per-pixel Lambert** — Sobel auto-gen + `_n.png` override, uploaded as a normal atlas (NOT the planned `normal_gen` pass; sample alongside the albedo atlas). Replaces the hardcoded flat normal. *First win, zero infra.*
- **A2. Cone / directional emitters** — wire the already-present-but-unused `gpu_emitter.cone_dir` / `cone_half_angle`; flashlights/headlights stop being omni.
- **A3. Weather multiplier** on sun/sky intensity (currently absent).
- **A4. Cheap inline AO** — SDF + normal short cone-trace, a few taps.

**Bucket B — HDR-RT + fullscreen post backbone (one-time infra, unblocks all of C):**
- Add an offscreen RGBA16F scene target reusing `ui_composite_target`'s `COLOR_TARGET` machinery; render the existing sprite/lighting pass into it; add a fullscreen post pass sampling it to the swapchain. This is the prerequisite the old §Architecture composite/post passes assumed.

**Bucket C — fullscreen post stages built on B:**
- **C1. Bloom + AgX tonemap + per-tileset LUT** (+ optional dust).
- **C2. Volumetric / god-rays** — half-res ray-march + bilateral upsample.
- **C3. Reflection** — water/wet-floor/rain.
- **C4. Underwater post** + water-column attenuation.
- Re-introduce the `LIGHTING_VOLUMETRIC_TIER` / `LIGHTING_TONEMAP` / `LIGHTING_GI_QUALITY` / `LIGHTING_DUST` options here (none exist in `options.cpp` yet).

### Confirmed sequence (grilled 2026-06-01) — backbone-first

Backbone-first, **not** normals-first: GPU JFA (#24) and Radiance Cascades (#13)
both live on the RT backbone, so the backbone is the prerequisite for GI/shadow
*quality*, not merely for bloom.

0. **Pre-backbone cleanup (one bundled commit, before B).** Same code region, all prerequisites:
   - Extract the `refresh_display` SDF/GI/vis CPU-build block (`sdltiles.cpp:766-897`) → `lighting/frame_build.{h,cpp}` (pulls lighting out of the windowing file).
   - **Dirty-gate** that block: key `{turn, z, camera-origin}`, skip rebuild + re-upload when unchanged; **bust while F4 panel visible**.
   - **Single-source the shaders**: load the live HLSL from external `.hlsl` at runtime; delete the dead `data/shaders/lighting/src/*.hlsl` duplicates and the embedded string. (−~400 lines from `sprite_batcher.cpp`; establishes the file-based pattern before authoring B/JFA/RC shaders.)
1. **B — RT backbone + AgX tonemap (mandatory together).** RGBA16F scene target reusing `ui_composite_target` + the stubbed `sprite_batcher.color_target_format`; fullscreen tonemap pass to swapchain. *HDR without a tonemap clips/looks washed — tonemap is not optional polish.*
2. **GPU JFA SDF (#24).** Fragment ping-pong on the backbone. Inline shadows get round penumbra for free (shader already samples the SDF).
3. **Radiance Cascades GI (#13).** Bilinear-Fix; replaces the CPU diffusion. Headline quality win.
4. **Bloom + per-tileset LUT.** Now that tonemap exists.
5. **Inline Bucket A** — normals (A1), cone emitters (A2), weather (A3), cheap AO (A4). Backbone-independent; lower priority than the GI core.
6. **C2 volumetric → C3 reflection → C4 underwater.**

**Cross-cutting, independent, do whenever:** wire `ranged.cpp` muzzle-flash into the event queue; finish Phase 2 tail (minimap 7b, loading-image 7c, SDL_Renderer delete 7f).

### Architecture / ergonomics principles (dev + AI friendly)

Driven by file-size reality: `lighting/` is 28 files, median ~110 lines —
already right-sized. `cata_tiles.cpp` is **7169** lines, `sdltiles.cpp` **4055**.

- **A GPU stage earns its own file/class when it needs its own render target or is inherently multi-pass** (JFA, RC, tonemap, bloom, volumetric, reflection, underwater → each `{name}_pass.{h,cpp}` + `{name}.frag.hlsl`). **Cheap per-fragment shading stays inline** in the forward sprite shader (direct, shadow-sample, sun dot, vision, tone grade) — deferring it would only add RT bandwidth. Split at RT boundaries, not maximally — the original 16-pass design was over-decomposed for a 2D tile game.
- **Right-size, don't minimize.** ~150–500 line cohesive modules. Many-tiny-files is a context anti-pattern (chase-hell); 1000+ god-files are load-hell. Don't split the existing `lighting/` files.
- **Co-locate lighting in the module.** The real context win is *extracting* lighting out of `cata_tiles.cpp` / `sdltiles.cpp` (step 0 starts this), not splitting `lighting/`. Goal: "to work on lighting, read `src/lighting/`."
- **Watch, don't act:** `render_state.cpp` (651) is the forming god-object (device + 3 queues + atlas + fonts + geometry + sampler); extract queue mgmt if it crosses ~900. As the forward `.frag.hlsl` grows with JFA/RC sampling, split *the shader* via `#include` (common + lighting), not the `.cpp`.
- **One source of truth** (kills the dead-shader-file trap) + **keep `src/lighting/CLAUDE.md` current** (the per-module map that lets an AI skip the giants — currently stale on minimap/display_buffer).

## Risks / unresolved

1. **SDL_GPU maturity** — SDL3 + SDL_GPU recent, driver issues possible on older Mesa. Solo dev → user picks platforms.
2. **Auto-gen normal quality on sprite-heavy tilesets** — may look mediocre. `_n.png` override unblocks artistic upgrade path.
3. **Frame budget on 1080p MED** — tight at ~18 ms on RTX 3060. Volumetric tier is the slider.
4. **Soft shadow penumbra max radius** — too large → noise, too small → hard-edged. Empirical tune in phase 6.
5. **Lighting event queue back-pressure** — many simultaneous explosions could blow queue. Cap size, drop oldest.
6. **Memorized state save format** — unchanged; `memorized_terrain` already in save. Shader reads existing `lit_level::MEMORIZED`.
7. **Stale memory files** (flag, not a fix for this doc): `project_rendering_pipeline.md` + `src/lighting/CLAUDE.md` are now wrong on (a) pixel_minimap ("stubbed no-op" — actually still uses SDL_Renderer), (b) "7f Part A removed `display_buffer`" (still referenced at `sdltiles.cpp:3781`). Refresh those in a separate pass.
8. **Vision rework** (`plan_vision_rework.md`) is effectively shipped inline in `SPRITE_FRAG_HLSL` (vis-edge + radial falloff, memory desaturate-fade, night/day ambient floor). That plan can be marked done and folded here.

## Verification

- **Phase 1**: TILES=1 build green on linux/macos/windows. CI cleaned. (Done locally; CI workflows still reference TILES env, cleanup deferred.)
- **Phase 2**: pixel-diff vs legacy on golden scenes ≤ 1 LSB tolerance for atlas-batcher rounding.
- **Phase 3**: `tests/lighting_emitter_test.cpp` — collector emitter list matches `lightmap.cpp` enumeration tile-for-tile.
- **Phase 5+**: golden-image regression per pass. Reference scenes: dark room + torch, fire field, smoke + flashlight, mirror corridor, stair shaft, dawn-cycle, underwater swim.
- **Perf**: `tests/lighting_bench.cpp` — assert ≤ 18 ms total render @ 1080p MED on RTX 3060.
- **Manual**: walk-through capture per phase: torch in dark room → fire spreads → smoke grenade → mirror reflects torchlight → dawn breaks through windows w/ god-rays → swim down → muzzle-flash lights cave → explosion HDR bloom.

## Files to read before starting phase 2+

- `src/lightmap.cpp:553-1018` — emitter enumeration, apparent light.
- `src/cata_tiles.cpp:4297-4431` — legacy render hook.
- `src/sdltiles.cpp:120-394,4256` — SDL3 init.
- `src/map.h:340-410` — `level_cache` + `floor_cache`.
- `src/explosion.cpp`, `src/ranged.cpp`, `src/weather.cpp` — transient-event sites.
- `src/dynamic_atlas.h` — atlas variant model.
- SDL_GPU docs + SDL_shadercross.
- Stoneshard reference shots.
- bevy-magic-light-2d (JFA SDF + probe GI reference).
- DDA #23996 (kept as future perf-only option).

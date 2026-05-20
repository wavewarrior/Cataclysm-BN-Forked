# Dynamic Lighting Rework — Cataclysm-BN (Stoneshard-tier)

## Context

Current lighting: per-turn CPU shadowcasting (`src/lightmap.cpp`, `src/shadowcasting.cpp`) — Beer-Lambert decay, 4-quadrant per-tile lightmap, parallel SMX collection, ~5–15 ms rebuild, ~1.2 MB/Z-level. Drives SDL_Renderer tile output via CPU color-modulation on pre-baked atlas variants (`src/cata_tiles.cpp:4297-4431`). Curses ASCII parallel path. Result: per-tile granularity, no flicker, no soft shadows, no bounce, no atmosphere.

**Goal**: high-fidelity dynamic lighting at Stoneshard-tier or better — per-pixel light propagation, soft penumbra shadows, RGB colored lights, normal-mapped tile relief, volumetric god-rays through smoke, indirect bounce GI (world-anchored), AO, reflections, bloom + AgX tonemap, smooth day/night cycle. GPU offload mandatory. Solo-dev project — no soak periods, no opt-in flags, no backward compat.

## Status

| Phase | State | Notes |
|---|---|---|
| 1. Curses + Android removal | ✅ done | commit `e96086b658` on `feat/lighting-phase1-curses-android-removal`. 169 files, -13042 lines. |
| 2. SDL_GPU device + sprite batcher | ⏳ next | hard cutover — build will not compile until done |
| 3–14 | pending | see Phasing below |

Deferred from phase 1: `.github/workflows/*` still reference `TILES` env var (doesn't block local compile).

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

## Architecture

### CPU side
- Existing `lightmap.cpp` shadowcasting + `apparent_light_at` **unchanged**. Continues producing `lit_level` per tile + `seen_cache` for AI/gameplay truth.
- **Emitter collector** (`src/lighting/emitter_collector.{h,cpp}`, new) — dedicated `std::thread`. Reads frame-start snapshot of emitter source handles (terrain/furniture, fields, items, creatures, vehicles, sun proxy). Resolves to `gpu_emitter` SSBO. Includes lighting event queue drain (transient flashes). Double-buffered upload.
- **Snapshot builder** (`src/lighting/snapshot.{h,cpp}`, new) — main thread, at frame start, walks reality bubble + drains queue → produces handle-list. ~10KB/frame.
- **Lighting event queue** (`src/lighting/event_queue.{h,cpp}`, new) — sim pushes transient flashes (`g->lighting.push_flash(pos, rgb, intensity, ms)`). Wire ~20 call sites: `explosion.cpp`, `ranged.cpp`, weather lightning, electrical sparks.

### GPU side — SDL_GPU pipeline
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

## New files

- `src/lighting/snapshot.{h,cpp}` — main-thread frame-start handle collection.
- `src/lighting/emitter_collector.{h,cpp}` — worker thread, SSBO build.
- `src/lighting/event_queue.{h,cpp}` — transient flashes.
- `src/lighting/gpu_device.{h,cpp}` — SDL_GPU init, swapchain.
- `src/lighting/sprite_batcher.{h,cpp}` — tile/vehicle/UI instance buffers.
- `src/lighting/normal_gen.{h,cpp}` — Sobel + edge-fade, content-hash cache.
- `src/lighting/sdf_pass.{h,cpp}` — JFA.
- `src/lighting/forward_plus.{h,cpp}` — light tile-binning.
- `src/lighting/sun_sky_pass.{h,cpp}` — directional sun + skylight + 24h LUT.
- `src/lighting/direct_pass.{h,cpp}` — direct shading + soft shadows + normals.
- `src/lighting/gi_pass.{h,cpp}` — probe grid + sample.
- `src/lighting/ao_pass.{h,cpp}`.
- `src/lighting/volumetric_pass.{h,cpp}`.
- `src/lighting/reflection_pass.{h,cpp}`.
- `src/lighting/composite_pass.{h,cpp}`.
- `src/lighting/underwater_pass.{h,cpp}`.
- `src/lighting/post_pass.{h,cpp}` — bloom + tonemap + LUT + dust.
- `data/shaders/lighting/*.{vert,frag,comp}` + pre-compiled `*.spv`.
- `tests/lighting_emitter_test.cpp` — snapshot + collector parity.
- `tests/lighting_gpu_test.cpp` — headless SDL_GPU golden-image regression.
- `tests/lighting_bench.cpp` — frame budget validation.

## Phasing

Solo dev → each phase lands and is used immediately. Phase 2 is hard cutover; no legacy renderer survives.

1. **Curses + Android removal** ✅ — done in `e96086b658`.
2. **SDL_GPU device + sprite batcher** — replace SDL_Renderer end-to-end w/ SDL_GPU instance batcher for tiles + vehicles + animated frames + UI. Pixel-equivalent parity gate vs legacy render.
3. **Snapshot + emitter collector + event queue** — own thread, SSBO upload. No shader use yet. Parity vs `lightmap.cpp` enumeration.
4. **SDF (JFA) + transparency upload** — distance field pass + density texture array. Visualized as debug overlay.
5. **Forward+ direct shading + RGB emitters + emissive auto-detect** — replaces last vestige of legacy color-mod. First visible win: smooth per-pixel light + colored lights + bloomable emitters.
6. **Soft shadows (SDF ray-march, penumbra)** — Stoneshard signature look.
7. **Normal-map auto-gen + per-pixel Lambert + `_n.png` override** — tile relief.
8. **Sun + skylight + 24h LUT + weather mult** — outdoor scenes come alive.
9. **GI probe grid + AO** — depth, ambient occlusion in corners.
10. **Volumetric (smoke, god-rays, outdoor fog)** — atmosphere.
11. **Reflection (water + wet floor + rain)**.
12. **Underwater post + water-column attenuation**.
13. **Bloom + AgX tonemap + per-tileset LUT + dust** — final polish.
14. **Z-level 2.5D holes** — `hole_up/down` upload, shader cross-Z sampling.

## Risks / unresolved

1. **SDL_GPU maturity** — SDL3 + SDL_GPU recent, driver issues possible on older Mesa. Solo dev → user picks platforms.
2. **Auto-gen normal quality on sprite-heavy tilesets** — may look mediocre. `_n.png` override unblocks artistic upgrade path.
3. **Frame budget on 1080p MED** — tight at ~18 ms on RTX 3060. Volumetric tier is the slider.
4. **Soft shadow penumbra max radius** — too large → noise, too small → hard-edged. Empirical tune in phase 6.
5. **Lighting event queue back-pressure** — many simultaneous explosions could blow queue. Cap size, drop oldest.
6. **Memorized state save format** — unchanged; `memorized_terrain` already in save. Shader reads existing `lit_level::MEMORIZED`.

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

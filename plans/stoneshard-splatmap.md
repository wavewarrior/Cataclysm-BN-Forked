# Stoneshard Extraction + CBN Sub-tile Splatmap System

## Context

Two deliverables. (1) Extract Stoneshard's VFX sprites (blood/gore/fire/particles) and its
decompiled blood GML for reference. (2) Implement a world-locked GPU splatmap in CBN: a
persistent per-submap RGBA8 texture accumulating rotated/scaled sub-tile decal stamps,
composited over terrain and under entities, replacing the grid-locked `fd_blood` tile
sprite visually while leaving field gameplay untouched.

This file supersedes `plans/Stoneshard Extraction + CBN Sub-tile S.md`, which contains
factually wrong paths and a macOS/Wine tool section. Copy this file to
`plans/stoneshard-splatmap.md` and delete the old one as the first action.

**Feasibility on this machine: YES**, with three corrections to the old plan (all verified
below): the extraction tooling is native Windows (no Wine), the shader directory is
`data/shaders/lighting/src/`, and the "composite between terrain and entities" step
requires splitting Pass W because all tile sprites currently render in ONE batcher pass.

## Machine feasibility (verified this session)

| Item | Status |
|---|---|
| Stoneshard install | `C:\Program Files (x86)\Steam\steamapps\common\Stoneshard` — `data.win` 160,967,968 B, header `FORM`/`GEN8`, **bytecodeVersion 17** (GM 2.3+, fully supported by UMT). `vanilla.win` (149 MB) is the unmodded backup; the install is currently modded (loose `gml_GlobalScript_table_*.gml`, `stoneshard_gui.py`, `setup (Windows).bat`). |
| UndertaleModTool | NOT installed. Release **0.9.1.2** (2026-07-13) is live and reachable: `UndertaleModTool_v0.9.1.2-Windows-SingleFile.zip` (93 MB) and `UTMT_CLI_v0.9.1.2-Windows.zip` (62 MB). Native win-x64 — **Wine/SpriteRipper/`modbranch` are irrelevant on this box**. |
| .NET | 8.0.204 SDK + Microsoft.NETCore.App 6/7/8 runtimes present. |
| Network | GitHub API reachable (HTTP 200). |
| CBN build | `out/build/windows-tiles-sounds-x64-msvc/src/Release/cataclysm-bn-tiles.exe` exists (2026-07-06, 56.7 MB). Branch `feature/improvements`, working tree clean except ` D mods` (pre-existing gitlink quirk). |
| Python / Deno | `C:\Python312\python.exe` present; `deno` NOT on PATH (only needed for `deno task docs:gen`, which this work does not touch). |

Corrections to the old plan's factual claims:

- Shaders live in **`data/shaders/lighting/src/*.hlsl`**, not `data/shaders/`. They are
  loaded and cross-compiled **at runtime** by `lighting::load_lighting_shader_source()`
  (`src/lighting/shader_compiler.cpp:42`, path = `PATH_INFO::datadir() + "shaders/lighting/src/"`).
  New shader files need **no CMake registration** — but the binary must run with CWD = repo
  root for `datadir()` to find them.
- UMT script is `Scripts/Resource **Exporters**/ExportAllSprites.csx` (not "Resource Unpackers").
- `gfx/MSX++UnDeadPeopleEdition/tile_config.json` is 35,811 lines (not 35,812).
- `render_state.h` anchors: `world_target_` at :437, `rain_effect rain_` at :460.

## Approach

Steps 1–2 are code-free and independent of everything else. Steps 3–8 are strictly ordered;
the tree builds and the existing suite passes after each. Step 9 is independent of 3–8.

### Step 1: Extract Stoneshard assets (no code changes)

Download `UndertaleModTool_v0.9.1.2-Windows-SingleFile.zip` from the GitHub release and
unpack to `C:\Tools\UndertaleModTool\`. Work on a **copy** of the data file — never open the
one Steam launches:

```
mkdir C:\WORK\stoneshard-extract
copy "C:\Program Files (x86)\Steam\steamapps\common\Stoneshard\vanilla.win" C:\WORK\stoneshard-extract\vanilla.win
```

Use `vanilla.win` (unmodded) so extracted art is pure base-game and reproducible; if it fails
to parse, fall back to `data.win`.

Sprites: open the copy in the UMT GUI → `Scripts > Resource Exporters > ExportAllSprites.csx`.
Blood GML: `Scripts > Resource Exporters > ExportAllCode.csx`, then search the dumped text for
`surface_set_target`, `draw_sprite_ext`, `blood`, `splatter` to recover the stamp algorithm
(spawn particle → on settle, `surface_set_target(decal_surface)` → `draw_sprite_ext` with random
rotation/scale/alpha → `surface_reset_target` → destroy). This is reference only; Step 5
implements the equivalent on the GPU.

The dump is thousands of files and stays **outside the repo** (`C:\WORK\stoneshard-extract\`).
Nothing from this directory is committed; only the hand-picked stamps promoted in Step 2 are.

### Step 2: Stage stamp assets with an opt-in manifest

Create `gfx/splatmap/stamps/` and copy in the chosen blood/gore PNGs from the extraction dump,
renamed `blood_01.png … blood_NN.png`, `gibs_01.png … gibs_NN.png`. Each must be square and
≤ 32×32 with alpha (larger art: downscale to 32×32 nearest-neighbour first).

Create the opt-in manifest `gfx/splatmap/stamps.json` — **only files listed here are loaded**,
so enabling/disabling a stamp is a one-line edit with no rebuild:

```json
{
  "cell_size": 32,
  "groups": {
    "blood": [ "blood_01.png", "blood_02.png", "blood_03.png" ],
    "gibs":  [ "gibs_01.png", "gibs_02.png" ]
  }
}
```

Hard cap: 64 files total across all groups (8×8 atlas of `cell_size` cells). Exceeding it logs
a `DL::Error` and drops the overflow — do not silently grow the atlas.

Add `stoneshard-extract/` to `.gitignore` in case the dump is ever placed inside the repo.

### Step 3: `splatmap_pass` skeleton — atlas + per-submap texture cache

New files `src/lighting/splatmap_pass.h` / `.cpp`. Game-agnostic like `rain_effect` (zero game
headers — submaps are addressed by an opaque packed key). Header contents, exactly:

```cpp
namespace lighting {

// Splatmap resolution. 96x96 per 12x12-tile submap; bump to 16 for 192x192 if
// 8 px/tile reads mushy (single-constant change; texture size derives from it).
inline constexpr int SPLAT_PX_PER_TILE = 8;
inline constexpr int SPLAT_TEX_DIM = SPLAT_PX_PER_TILE * 12; // SEEX == 12

// One decal stamp, in SUBMAP-LOCAL splatmap pixels.
struct splat_stamp {
    float px = 0.f, py = 0.f;   // centre, 0..SPLAT_TEX_DIM
    float size = 8.f;           // quad edge in splatmap px
    float rotation = 0.f;       // radians
    float alpha = 1.f;          // 0..1 coverage multiplier
    float mask_r = 1.f, mask_g = 0.f, mask_b = 0.f; // target channel mask
    int cell = 0;               // atlas cell index, 0..63
};

// One visible submap's composite quad, in LOGICAL projection pixels.
struct splat_quad {
    float dst_x = 0.f, dst_y = 0.f, dst_w = 0.f, dst_h = 0.f;
    std::uint64_t key = 0;
};

class splatmap_pass {
public:
    splatmap_pass() = default;
    splatmap_pass(const splatmap_pass&) = delete;
    splatmap_pass& operator=(const splatmap_pass&) = delete;
    ~splatmap_pass();

    bool init(gpu_device& dev, SDL_GPUTextureFormat world_format);
    void shutdown() noexcept;
    bool ready() const noexcept;

    // Number of atlas cells actually loaded per group (for stamp selection).
    int cell_count(bool gibs) const noexcept;
    int cell_base(bool gibs) const noexcept;

    // Look up / create the submap entry. Returns true when the entry ALREADY
    // existed; false when freshly created (caller must then seed history).
    bool touch(std::uint64_t key);
    void add_stamp(std::uint64_t key, const splat_stamp& s);

    // Render pending stamps into their submap textures. MUST be called with NO
    // render pass open.
    void flush_stamps(SDL_GPUCommandBuffer* cb);

    // Alpha-composite the given submap quads over world_tex. Opens its own
    // LOADOP_LOAD pass; call between the two halves of the split Pass W.
    void composite(
        SDL_GPUCommandBuffer* cb, SDL_GPUTexture* world_tex, std::uint32_t proj_w,
        std::uint32_t proj_h, const std::vector<splat_quad>& quads);
};

} // namespace lighting
```

Implementation in this step (rendering comes in Step 5):

- **Atlas load.** Read `gfx/splatmap/stamps.json` with the project JSON reader; for each listed
  file call `load_image()` (`src/sdl_wrappers.h:94`) and blit into one `create_surface_32(256, 256)`
  (`src/sdl_utils.h:207`) at cell `(i % 8, i / 8) * cell_size`. Upload via
  `render_state::create_rgba_gpu_texture(256, 256)` + `upload_surface_subregion_to_gpu_texture()`
  (`src/lighting/render_state.h:130` and :122). Blood cells occupy `[0, n_blood)`, gibs
  `[n_blood, n_blood + n_gibs)` — that is what `cell_base`/`cell_count` report. Missing or empty
  manifest → log `DL::Error`, leave `ready()` false, and the whole feature no-ops.
- **Per-submap texture.** Reuse `lighting::ui_composite_target` (`src/lighting/ui_composite_target.h`)
  — `init(dev, SPLAT_TEX_DIM, SPLAT_TEX_DIM, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, 0)`; the
  default usage `COLOR_TARGET|SAMPLER` is exactly right. Do NOT write a new texture wrapper.
- **Cache.** `std::list<entry>` + `std::unordered_map<std::uint64_t, iterator>` LRU, cap
  `MAX_SUBMAPS = 64` (~2.4 MB at 96×96). `entry` = `{ ui_composite_target tex; std::vector<splat_stamp> pending; bool cleared; }`.
  `cleared == false` means the texture has never been rendered into, so its first stamp pass uses
  LOADOP_CLEAR to transparent black; subsequent passes use LOADOP_LOAD. Eviction releases the
  target and drops the stamps (the submap re-seeds from fields on next visibility).
- **Linear sampler.** Create one `SDL_CreateGPUSampler` with LINEAR min/mag + CLAMP_TO_EDGE in
  `init()`, owned by the pass; the composite uses it so 8 px/tile does not read as hard blocks.
  The stamp pass reuses it for the atlas.

Also add `splatmap_pass splatmap_;` to `render_state` next to `rain_` (`render_state.h:460`), an
accessor `splatmap_pass& splatmap() noexcept`, and an `init` call in `render_state::init()`
alongside `rain_.init(...)` (`render_state.cpp:203`) passing the same `world_fmt`.

### Step 4: Shaders (4 new files in `data/shaders/lighting/src/`)

Register spaces are fixed by shadercross and verified against `sprite.vert.hlsl` /
`sprite.frag.hlsl`: **vertex** storage buffers `space0`, vertex cbuffers `space1`; **fragment**
texture+sampler `space2`, fragment cbuffers `space3`. NDC conversion must be copied verbatim
from `sprite.vert.hlsl:198-200`:
`ndc = float2(px.x / target_size.x * 2 - 1, px.y / target_size.y * -2 + 1)`.

**`splat_stamp.vert.hlsl`** — instance struct is byte-identical to `rain_droplet.vert.hlsl`'s
16-float `SpriteInstance` (reuse the layout so `quad_instance` in `rain_effect.cpp` can be
mirrored): `dst_*` = quad in splatmap px, `src_*` = atlas cell UV rect, `tint_rgb` = channel
mask, `tint_a` = coverage alpha, `rotation` = radians. `StructuredBuffer<SpriteInstance> : register(t0, space0)`,
`cbuffer FrameParams : register(b0, space1) { float2 target_size; uint instance_base; uint pad; }`
with `target_size = (SPLAT_TEX_DIM, SPLAT_TEX_DIM)`. Same 6-vertex `quad_uv` table and same
centre/rotation math as `rain_droplet.vert.hlsl:33-45`. Outputs `uv` = atlas UV
(`src_u + c.x*src_uw`, `src_v + c.y*src_vh`) and `tint`.

**`splat_stamp.frag.hlsl`** — `Texture2D<float4> Atlas : register(t0, space2)`,
`SamplerState AtlasSmp : register(s0, space2)`. Body:
`const float cov = Atlas.Sample(AtlasSmp, i.uv).a * i.tint.a; return float4(i.tint.rgb * cov, cov);`
No cbuffers.

**`splat_composite.vert.hlsl`** — no storage buffer. `cbuffer CompositeParams : register(b0, space1) { float4 rect; float2 target_size; float2 pad; }`
where `rect = (dst_x, dst_y, dst_w, dst_h)` in logical projection px. Emits the 6-vertex quad,
`uv = c` (0..1 over the submap).

**`splat_composite.frag.hlsl`** — `Texture2D<float4> Splat : register(t0, space2)`,
`SamplerState SplatSmp : register(s0, space2)`,
`cbuffer SplatColors : register(b0, space3) { float4 blood_color; float4 strengths; }`
(`strengths = (blood, wet, snow, pad)`). Output is PREMULTIPLIED alpha (matching the pass blend
below):

```hlsl
const float4 s = Splat.Sample( SplatSmp, i.uv );
const float  b = s.r * strengths.x;   // blood: tint + darken
const float  w = s.g * strengths.y;   // wetness: pure darken
const float  n = s.b * strengths.z;   // snow: white overlay
const float3 rgb = blood_color.rgb * b + float3( 1.0, 1.0, 1.0 ) * n;
const float  a   = saturate( b + w + n );
if( a < 0.004 ) { return float4( 0.0, 0.0, 0.0, 0.0 ); }
return float4( rgb, a );
```

Only the R channel is ever non-zero today; G/B are wired for future wetness/snow and cost
nothing. Defaults: `blood_color = (0.30, 0.02, 0.02, 1)`, `strengths = (0.85, 0.0, 0.0, 0)`.
The 0.85 is deliberately strong because the composite lands in the HDR `world_target` **before**
AgX tonemap + exposure 0.35, which crushes subtle darkening (same reason
`rain_splash.frag.hlsl:31` uses `DARK_BOOST = 3.0`).

### Step 5: `splatmap_pass` rendering

Copy the pipeline/buffer scaffolding from `rain_effect.cpp` (`init` shader compile at :100-127,
`make_buffers` transfer+storage pair, `upload_instances` at :246-277).

**Stamp pipeline.** Target format `R8G8B8A8_UNORM`. Blend: `enable_blend = true`,
`src_color = ONE`, `dst_color = ONE`, `color_blend_op = SDL_GPU_BLENDOP_MAX`, same three for
alpha. MAX is the load-bearing choice: the fragment writes 0 into non-target channels, so a MAX
blend leaves them untouched — one pipeline serves all three channels with no colour write masks,
and overlapping stamps saturate toward 1 (organic buildup) instead of washing out.

`flush_stamps(cb)`: for each cached entry with a non-empty `pending`, convert stamps to the
16-float instance layout (`dst_x = px - size/2`, `dst_y = py - size/2`, `dst_w = dst_h = size`,
`src_*` = cell UV rect from `cell % 8`, `cell / 8` at `1/8` steps, `tint_rgb` = mask,
`tint_a = alpha`), upload, then `begin_pass` on that entry's texture with
`clear = cleared ? nullptr : {0,0,0,0}`, one instanced draw, `end_pass`, set `cleared = true`,
`pending.clear()`. Instance cap `MAX_STAMPS_PER_FLUSH = 1024`; overflow stays in `pending` for
the next frame.

**Composite pipeline.** Target format = the `world_format` passed to `init` (HDR RGBA16F).
Blend: `src_color = ONE`, `dst_color = ONE_MINUS_SRC_ALPHA`, `BLENDOP_ADD`, alpha
`ONE / ONE_MINUS_SRC_ALPHA` — identical to `rain_effect`'s premultiplied blend.

`composite(...)`: no-op when `quads` is empty. Open ONE render pass on `world_tex` with
`LOADOP_LOAD`; for each quad, skip if its key is absent from the cache or its entry is not
`cleared`, bind that entry's texture + the linear sampler, push `CompositeParams`
(`rect` = the quad, `target_size = (proj_w, proj_h)`), push the fragment `SplatColors` from the
devui globals, draw 6 vertices. `end_pass`.

### Step 6: Game-side glue and queueing

New files `src/splatmap_stamps.h` / `.cpp` — a small pure-function header rather than edits to
`map.h` (which has >10 usages, per repo standards). Header, exactly:

```cpp
#pragma once

#include "coordinates.h"
#include "type_id.h"

namespace splatmap
{

/// Packed cache key for a submap: 24 bits x, 24 bits y, 8 bits z (biased).
auto key_of( const tripoint_abs_sm &sm ) -> std::uint64_t;

/// True when the splatmap renders decals for this field type, so its
/// grid-locked tile sprite must be suppressed.
auto covers_field( const field_type_id &type ) -> bool;

/// Queue decal stamps for a splatter of `type` at `where` (map-bub coords).
/// No-op when the renderer is absent (tests) or the type is not covered.
auto queue_splatter( const tripoint_bub_ms &where, const field_type_id &type,
                     int intensity ) -> void;

/// Seed a freshly created submap entry from current field data. `origin` is the
/// submap's top-left tile in map-bub coords.
auto seed_submap( const tripoint_bub_ms &origin, std::uint64_t key ) -> void;

} // namespace splatmap
```

`covers_field` returns true for exactly these eight ids (all confirmed in
`src/field_type.h:299-330`): `fd_blood`, `fd_blood_veggy`, `fd_blood_insect`,
`fd_blood_invertebrate`, `fd_gibs_flesh`, `fd_gibs_veggy`, `fd_gibs_insect`,
`fd_gibs_invertebrate`. The `fd_gibs_*` four select the `gibs` atlas group, the `fd_blood_*` four
the `blood` group; if the selected group is empty, fall back to the other, and if both are empty
do nothing.

`key_of`: `(std::uint64_t( sm.x() + 0x80'0000 ) << 40) | (std::uint64_t( sm.y() + 0x80'0000 ) << 16) | std::uint64_t( sm.z() + 10 )`.

Stamp generation (shared by `queue_splatter` and `seed_submap`) — one helper:

- Guard on `tilecontext && g` and `render_state.splatmap().ready()`; bail otherwise.
- `const tripoint_abs_ms abs = get_map().bub_to_abs( tile );`
  `const tripoint_abs_sm sm = project_to<coords::sm>( abs );` → `key_of( sm )`.
- Local tile within the submap: `lx = tile.x() % SEEX`, `ly = tile.y() % SEEY` (both
  non-negative — bub coords are `0..mapsize*SEEX`).
- `n = std::clamp( intensity, 1, 3 )` stamps.
- RNG: a **file-local** `std::mt19937` seeded per tile so the game RNG stream is never
  perturbed (same rule `rain_rng` follows at `sdl_render_frame.cpp:815`). Seed =
  `std::hash` mix of `abs.x(), abs.y(), abs.z(), type.to_i()`. Deterministic seeding is what
  makes save/reload regeneration visually stable.
- Per stamp: `cell = cell_base + rng % cell_count`;
  `px = lx * SPLAT_PX_PER_TILE + SPLAT_PX_PER_TILE * 0.5f + jitter`, jitter uniform in
  `±0.4 * SPLAT_PX_PER_TILE`; same for `py`; `rotation` uniform `0 .. 2π`;
  `size = SPLAT_PX_PER_TILE * uniform(0.7f, 1.3f)`; `alpha = uniform(0.6f, 1.0f)` scaled by
  `1.0f - 0.25f * stamp_index` so trail/overlap stamps fade; mask `(1,0,0)`.
- `add_stamp(key, s)`.

`seed_submap( origin, key )` loops the 12×12 block from `origin`, and for each tile whose
`get_map().field_at( tile )` contains a covered type, calls the same helper with that field's
intensity. Because the seed RNG is keyed on absolute coords, a reload reproduces the same
pattern for the same field data.

Call sites:

- `map::add_splatter()` (`src/map.cpp:560`) — after `mod_field_intensity( where, type, intensity )`
  at :574, add `splatmap::queue_splatter( where, type, intensity );`. Do NOT add it on the
  vehicle-blood early-return path at :569-570 (blood goes onto the vehicle part, not the ground).
  `add_splatter_trail` (:577) and `add_splash` (:605) route through `add_splatter`, so they need
  no edits — the trail's decreasing `remainder` intensity already produces the fading spray the
  old plan described.
- `emit_impact_particle()` (`src/animation.cpp:589`) — after the existing
  `tilecontext->particles().emit(...)`, add
  `splatmap::queue_splatter( pos, fd_blood, 1 );` guarded by `if( blood )`. The transient particle
  stays; this adds the permanent decal.

### Step 7: Split Pass W and drive the composite

`flush_tile_sprites` currently drains the whole queue (`render_state.cpp:733-753`), and Pass W
opens exactly one batcher pass (`sdl_render_frame.cpp:743-752`), so the composite has nowhere to
sit. Fix by cutting the queue at the terrain/entity boundary.

1. **Ranged drain.** Add `void flush_tile_sprites(sprite_batcher& dst, SDL_GPUSampler* sampler, std::size_t begin, std::size_t end);`
   to `render_state`; the existing 2-arg overload delegates with `(0, tile_sprite_queue_.size())`.
   Body is the current loop over `tile_sprite_queue_ | drop(begin) | take(end - begin)`; keep the
   "drain WITHOUT clearing" contract and the `set_texture`-dedupe.
2. **Per-frame splat state on `render_state`.** Add
   `void set_splat_frame( std::size_t cut, std::vector<splat_quad> quads );`,
   `std::size_t splat_cut() const noexcept;`, `const std::vector<splat_quad>& splat_quads() const noexcept;`
   plus the two members. Default `cut` = `SIZE_MAX` meaning "no cut this frame". Reset both in
   `clear_frame_queues()` (`render_state.h:280`).
3. **Record the cut in `cata_tiles::draw()`.** In the z loop at `cata_tiles.cpp:1105`, immediately
   after the Pass-1 row `while` loop closes (`cata_tiles.cpp:1130`) and only when
   `z == center.z()`:
   - `const std::size_t cut = rs.tile_sprite_count();` (`render_state.h:176`)
   - Build the quad list: collect the distinct `(x/SEEX, y/SEEY)` submap indices over
     `draw_points` entries with `p.pos.z() == center.z()`; for each, the bub origin tile is
     `point_bub_ms( ix * SEEX, iy * SEEY )`, the screen top-left is
     `player_to_screen( origin )` (`cata_tiles.h:1209`), and the quad is
     `{ .dst_x = sx, .dst_y = sy, .dst_w = 12.f * tile_width, .dst_h = 12.f * tile_height, .key = splatmap::key_of( { here.get_abs_sub().x() + ix, here.get_abs_sub().y() + iy, z } ) }`.
     Using `player_to_screen` is what guarantees pixel-exact alignment with the tile sprites —
     do NOT re-derive the projection from `camera_off`.
   - For each key, `if( !rs.splatmap().touch( key ) ) { splatmap::seed_submap( origin, key ); }`.
   - `rs.set_splat_frame( cut, std::move( quads ) );`
   Only the player's z-level composites; the z loop ascends to `center.z()`, so this is the last
   iteration and the cut index is unambiguous.
4. **Split the pass** in `render_world_pass_w` (`sdl_render_frame.cpp:743-752`). Replace the
   single begin/flush/end with:
   - `rs.splatmap().flush_stamps( ctx.cmd_buffer );` — BEFORE any pass opens (a render pass
     cannot nest). Place it right after `flush_shadow_casters` at :741.
   - Pass W-a: `begin_pass( ..., clear_black, proj_w, proj_h, wt->format() )`, then
     `flush_tile_sprites( batcher, sampler, 0, cut )` when `have_tiles`, then `end_pass()`.
   - `rs.splatmap().composite( ctx.cmd_buffer, wt->texture(), proj_w, proj_h, rs.splat_quads() )`
     when `g_splatmap_enable && rs.splatmap().ready()`.
   - Pass W-b: `begin_pass( ..., /*clear*/ nullptr, proj_w, proj_h, wt->format() )` — a null clear
     colour yields `SDL_GPU_LOADOP_LOAD` (`sprite_batcher.cpp:599`), preserving W-a — then
     `flush_tile_sprites( batcher, sampler, cut, rs.tile_sprite_count() )`, then `end_pass()`.
   - When `cut == SIZE_MAX` (no tiles drawn, or a frame `cata_tiles::draw` did not run), keep the
     single-pass path exactly as today.
5. **Devui toggles.** In `src/lighting/sdl_lighting_devui.h` next to `g_rain_enable` (:83) declare
   `extern bool g_splatmap_enable; extern float g_splat_blood_strength;`; define them in
   `sdl_lighting_devui.cpp` next to :80 as `true` and `0.85f`, and bind them next to :469 as
   `c.Bind( "splatmap_enable", &g_splatmap_enable );` and
   `c.Bind( "splat_blood_strength", &g_splat_blood_strength );`.

### Step 8: Suppress the grid-locked splatter sprite

In `cata_tiles::draw_field_or_item` (`src/cata_tiles_draw_layers.cpp:398`), the field-sprite
branch at :411 becomes:

```cpp
if( ( fld_overridden || !invisible[0] ) && fld.obj().display_field
    && !( g_splatmap_enable && splatmap::covers_field( fld ) ) ) {
```

This removes the double-render (32×32 grid sprite + decal) while keeping the item branch at :439
untouched. With the toggle off, the old look returns exactly. Field gameplay (slipperiness, NPC
reactions, mopping) is untouched because `mod_field_intensity` still runs.

### Step 9 (independent of 3–8): terrain and foliage opt-in

Grass: promote chosen Stoneshard floor/grass PNGs to 32×32 and append them as additional weighted
entries in the existing `center` array of the `t_grass` block in
`gfx/MSX++UnDeadPeopleEdition/tile_config.json` (which already carries 5 weighted centers plus
full multitile edge/corner variants). Tileset-only change, no code. Foliage: same treatment for
`f_underbrush`, `f_bush`, `f_tree_*` where the art holds up at 32×32; skip anything that clashes.
Keep every promoted file under `gfx/splatmap/`-style separate staging so the opt-in stays
reviewable — do not overwrite existing UnDeadPeople sprites in place.

**Outcome (implemented).** 30 sprites promoted across three new opt-in sheets. Two of this
step's original premises were wrong and are corrected here:

1. **Foliage is not 32×32.** `t_shrub` / `t_underbrush` / `t_tree_*` are all defined in
   `large_terrain.png` at **64×80 with `sprite_offset (-16,-48)`**. A `tile_type` applies ONE
   offset to its whole `fg` list, so a promoted variant MUST match that geometry or it renders
   misplaced. Measured anchor: CBN shrub art occupies exactly `(16,48)-(47,79)` — a 32×32 tile
   footprint, bottom-right inside the 64×80 cell. Promoted art is fitted to that anchor.
2. **Foliage does NOT clash.** CBN's own trees bake drop shadows and are drawn oblique/side-on,
   exactly like Stoneshard's — so the "baked shadow / perspective" objection was unfounded.

| Sheet | Base | Cell | Promoted |
|---|---|---|---|
| `stoneshard_terrain.png` | 19736 | 32×32 | 5 grass centers → `t_grass` (weight 12 vs 30) |
| `stoneshard_foliage.png` | 19752 | 64×80 | 6 → `t_shrub`, 4 → `t_underbrush` (weight 10 vs 40) |
| `stoneshard_trees.png` | 23864 | 64×80 | 5 → `t_tree`, 3 → `t_tree_birch`, 3 → `t_tree_pine`, 3 → `t_tree_willow`, 1 → `t_tree_dead` |

- **Species routing, not dumping.** CBN has dedicated `t_tree_birch/_pine/_willow/_dead`
  terrains, so each promoted tree joins its own species. `s_linden_3` is bare, which makes it
  the correct `t_tree_dead` variant rather than a reject.
- **`stoneshard_trees.png` is registered AFTER `fallback.png` on purpose.** Sprite ids are
  assigned by cumulative sheet order, so appending last shifts no existing base. Verified: all
  19,000 previously-referenced ids still resolve to the same sheet + local index, and
  `fallback.png` stays at `(base 19768, 4096 slots)`.
- **Base indices are 0-based; the `//` "range" comments are 1-based.** Trusting the comment
  cost an off-by-one during the grass step — always re-derive by summing
  `(w/sprite_w)*(h/sprite_h)` over preceding sheets.
- **Palette matching is conditional.** Promoted art gets a per-channel mean/σ transfer onto the
  CBN sprite it joins, but ONLY when the max per-channel mean delta is < 40. Birch (63.2) and
  willow (46.2) have structurally bimodal references (white trunk / drooping fronds); forcing a
  transfer there turned birch trunks magenta, so that art ships as authored.
- Seasonal `*_season_*` ids are deliberately untouched — base ids only, to keep the diff small.

## Critical files & anchors

| File | Symbol / region | Why |
|---|---|---|
| `src/lighting/rain_effect.{h,cpp}` | `init` :82-175, `upload_instances` :246-277, `record` :279 | The exact template for `splatmap_pass`: runtime shader compile, transfer+storage instance buffers, premultiplied blend, init/shutdown/record lifecycle. Header comment :12-15 documents why the old screen-space splatmap failed — the new one is world-locked. |
| `src/sdl_render_frame.cpp` | `render_world_pass_w` :718-786 | The single-batcher-pass structure that must be split; `begin_pass` clear vs. null-clear is the whole mechanism. |
| `src/cata_tiles.cpp` | z loop :1105, Pass-1 end :1130, Pass 2 :1131 | The only clean terrain→entity boundary; where the cut index and quad list are recorded. |
| `src/lighting/render_state.{h,cpp}` | `flush_tile_sprites` :733, `tile_sprite_count` :176, `create_rgba_gpu_texture` :130, `upload_surface_subregion_to_gpu_texture` :122, `rain_` :460 | Ranged drain, cut index, atlas upload path, and the member slot for the new pass. |
| `data/shaders/lighting/src/sprite.vert.hlsl` | :103-106 quad table, :198-200 NDC | Register-space and NDC conventions the new shaders must match exactly, or the composite lands flipped/offset. |

## Verification

Working directory for every command: `C:/WORK/GIT_REPOS/Cataclysm-BN-Forked`.

1. **Extraction** — after `ExportAllSprites.csx`, the dump contains blood art:
   `ls C:/WORK/stoneshard-extract/Export_Sprites | grep -ci 'blood\|splatter\|gore'` ≥ 20. Open
   several: distinct splatter shapes with real alpha.
2. **Build** (never synchronously, never with a short cap — a killed ninja run corrupts
   `.ninja_deps`; run as a background job with ≥1200 s and poll):
   ```
   cmake --build --preset windows-msvc-release --target cataclysm-bn-tiles cata_test-tiles
   ```
   A new `tests/*.cpp` requires a cmake re-configure first (`tests/CMakeLists.txt:2` globs
   without `CONFIGURE_DEPENDS`).
3. **Unit test** — new `tests/splatmap_test.cpp`, tag `[splatmap]`: `covers_field` true for the
   eight covered ids and false for `fd_null`, `fd_web`, `fd_bile`; `key_of` distinct for
   `(0,0,0)`, `(1,0,0)`, `(0,1,0)`, `(0,0,1)` and stable for repeated calls with negative
   coordinates. Run: `./out/build/windows-tiles-sounds-x64-msvc/tests/Release/cata_test-tiles "[splatmap]"`.
4. **New behaviour, end to end** — launch with CWD = repo root (required for
   `PATH_INFO::datadir()` to resolve the new shaders):
   ```
   Start-Process -FilePath out/build/windows-tiles-sounds-x64-msvc/src/Release/cataclysm-bn-tiles.exe -WorkingDirectory C:/WORK/GIT_REPOS/Cataclysm-BN-Forked
   ```
   Confirm `config/options.json` has the `DEBUGLOG_*` classes off — debug logging quadruples load
   time. Debug menu → spawn a monster adjacent → melee it to death. Expected: blood appears as
   rotated sub-tile splatters that visibly straddle tile boundaries and do **not** snap to tile
   centres; the 32×32 grid-locked `fd_blood` sprite is gone. Kill several in one spot: stamps
   overlap and build up, saturating rather than washing out. Nothing appears if the atlas failed
   to load — check `config/debug.log` for `splatmap` errors.
5. **Persistence** — save, quit, reload, walk back: splatters are present in visually equivalent
   (not byte-identical) positions, regenerated from field data by `seed_submap`.
6. **No regression** — set `splatmap_enable = false` in the lighting devui: the old grid-locked
   `fd_blood` sprite returns immediately, proving the suppression is toggle-gated and fields are
   still set. Then `./out/build/windows-tiles-sounds-x64-msvc/tests/Release/cata_test-tiles "[field]"`
   passes.

## Assumptions & contingencies

- **`SPLAT_PX_PER_TILE = 8`** (96×96, ~36 KB/submap, 64 cached ≈ 2.4 MB). If 8 px/tile reads
  mushy even with the linear sampler, set it to `16` (192×192, ~144 KB, ≈9.4 MB cached) — the
  texture dimension and all stamp math derive from the constant, so it is a one-line change. Drop
  to `4` only if GPU memory is genuinely tight.
- **Extraction uses `vanilla.win`.** The install is modded (`data.win` ≠ `vanilla.win`). If
  `vanilla.win` fails to parse in UMT, use a copy of `data.win`; if that also fails, switch
  Stoneshard to the `modbranch` Steam beta and retry. Never open the file Steam launches.
- **UMT GUI over CLI.** The GUI single-file build is the path of least resistance and the
  decompiled-GML browsing in Step 1 needs it. If the GUI misbehaves under this Windows build,
  `UTMT_CLI_v0.9.1.2-Windows.zip` runs the same `.csx` scripts headlessly on .NET 8.0.204.
- **Extracted art is committed** under `gfx/splatmap/stamps/` and gated by
  `gfx/splatmap/stamps.json`, so individual assets are opt-in per the project owner's decision.
  This is Ink Stains Games' proprietary art: it blocks any upstream PR and must not be published.
  Keep the branch local to this fork.
- **Composite covers the player's z-level only.** Lower z-levels get no decal composite; their
  terrain is drawn under the player's floor and occluded anyway. If cross-z decals are ever
  wanted, record one cut + quad list per z and composite inside the z loop.
- **Blood strength 0.85 into HDR.** If splatters read too dark or too faint after AgX, tune
  `g_splat_blood_strength` live in the devui and bake the final value as the default; do not
  change the blend mode.
- **Fresh-texture LOADOP.** Reading an uninitialised `R8G8B8A8` target is undefined, hence the
  per-entry `cleared` flag driving CLEAR-then-LOAD. If any driver still shows garbage on the
  first stamp, clear every entry to transparent black immediately after `ui_composite_target::init`.

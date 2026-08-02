# Grid-Decoupled Lighting & Vision — Execution Plan

## Context

Cataclysm-BN-Forked already renders lighting continuously (per-screen-pixel Lambert, world-space float emitters, bilinear SDF sphere-march shadows, GPU compute GI, sun/sky march, volumetrics, sub-tile camera). The grid artefacts the game still shows do **not** come from the shading pipeline — they come from its **inputs**:

1. `data/shaders/lighting/src/jfa_seed.comp.hlsl:33-40` reads **one binary opaque flag per tile** (`TransBuf[tx*map_h+ty]`, `tx = sx/SDF_SS`) and replicates it into all **64** subcells. `SDF_SUPERSAMPLE = 8` (`src/lighting/sdf_pass.h:24`), so the SDF pays 64× the memory and flood cost to carry **tile-square, axis-aligned** geometry. Every tree, barrel, car and fence casts a square shadow.
2. Every emitter sits at exactly `tile + 0.5` (`src/lighting/snapshot.cpp:63-64`), including creatures and the player — whose sprites *do* slide sub-tile (`Creature::anim_state.slide_offset_x/y`, tile units, `src/creature.h:189-190`, consumed at `src/cata_tiles_anim.cpp:137`). Carrying a torch, your light snaps tile-to-tile while your body slides.
3. Player vision has **no GPU path at all**. `VisBuf`'s only reader was deleted (`data/shaders/lighting/src/sprite.frag.hlsl:62-68`); the bind array carries 5 buffers, not 6 (`src/lighting/sprite_batcher.cpp:800-803`); `vis_curve` and `vis_radius` are orphaned cbuffer lanes. Vision is rendered **only** by the discrete 7-level `lit_level` → full-tile overlay-sprite path (`src/cata_tiles.cpp:2370-2406`). Meanwhile `src/lighting/frame_build.cpp:286-338` still builds a 1056×1056 float field (**4.5 MB**), Gaussian-blurs it and uploads it on every player move **for nobody**.
4. Lighting is evaluated per **screen** pixel, and the Bayer dither is keyed to **screen** pixels (`sprite.frag.hlsl:628`, `i.world_pos * tile_pixel_size` where `tile_pixel_size = tilecontext->get_tile_width()`, the *zoomed* width — `src/sdl_render_frame.cpp:483-485`). At any zoom > 1:1 a 32×32 sprite receives a continuous gradient *across its own art texels*, and the dither cell size changes with zoom. That is the "HD lighting bolted onto pixel art" look.

End state: occluders are pixel-exact sub-tile silhouettes derived from the tileset art itself; emitters follow sub-tile motion; the vision boundary inside the visible region is a smooth sub-tile curve; light is evaluated and quantized on the **art texel grid** and resolved through per-palette shade ramps so shading stays inside the tileset's own colour family. Gameplay light/vision stays 100% tile-quantized and untouched.

### Not in scope, stated at the temptation point
- **Box2D colliders are NOT the occluder source.** `src/physics/terrain_body.cpp:13-24` classifies on *impassability*, not opacity: a window gets a collider but is transparent; a bush is passable so gets none but does attenuate. The shapes are plain axis-aligned `b2MakeBox(TILE_M*0.499, …)` per tile (`terrain_body.cpp:68-90`) — *the same tile-square geometry the JFA seed already has*, so zero fidelity gain. Bodies exist only for the player's current z (`src/map.cpp:374`), and the GPU cannot call `b2World_CastRay`. Vehicle hull rotation is the one thing Box2D would add, and Step 3 gets it from the vehicle's own sprite rotation instead.
- **Radiance Cascades is NOT in this plan.** `src/lighting/CLAUDE.md:128-136` records a fragment-RC attempt that failed D3D12 root-signature creation and was replaced by the working compute GI. Raising GI probe density before fixing the seed just buys higher-resolution *square* bounce. Step 6 fixes the dominant GI artefact for a few extra taps instead.
- **Gameplay consumers stay untouched**: `fine_detail_vision_mod`, `Character::sight_range`, `Creature::sees`, `map::ambient_light_at`, `map::sees`. Step 5's render field is a *product* with the tile grant, so it can only ever subtract visibility.

---

## Approach

**Before Step 0:** copy this file verbatim to `plans/grid-decoupled-lighting.md`. `AGENTS.md` requires the repo copy as the permanent record that survives session resets; plan mode could not create it.

Steps are ordered so the tree builds and the game runs after each one. **1 → 2 → 3** is a strict chain (3 is the payoff). **4, 5, 6** each depend only on 3 and are mutually independent. **7** depends only on 1.

### Step 0 — Grow `debug_params` once, up front

Every later step needs knobs, and the struct is wire-stable across three declarations. Do the growth once so no later step re-touches the ABI.

`src/lighting/sprite_batcher.h` — append **exactly 8 floats** to `struct debug_params` (currently 176 B / 44 floats, asserted at `src/lighting/sprite_batcher.cpp:61`; 176 + 32 = **208** = 16 × 13, so alignment holds):

```cpp
    // Pixel-art light quantisation (Step 1).
    float texels_per_tile = 32.0f; // DATA: tileset native tile width in art texels
    float light_quant = 1.0f;      // 1 = snap light sample to art texels, 0 = per-screen-pixel
    // Sub-tile occluders (Step 3/4).
    float occ_soft_gain = 1.0f;    // partial-occluder block gain (0 = hard occluders only)
    float self_eps_tall = 0.55f;   // trace_shadow self-shadow escape radius for TALL sprites
    // Palette shade ramps (Step 7).
    float ramp_enable = 1.0f;      // 0 = plain multiply, 1 = full ramp resolve
    float ramp_steps = 8.0f;       // shade steps per palette row (must match built LUT)
    float ramp_chroma = 0.35f;     // how much coloured light tints the ramped surface
    float dbg_pad2 = 0.0f;         // alignment
```

Update `static_assert(sizeof(debug_params) == 208, …)` at `sprite_batcher.cpp:61`.

Mirror the same 8 fields, in the same order, at the end of the `DebugParams` cbuffer in **both** `data/shaders/lighting/src/sprite.frag.hlsl` (`register(b2, space3)`, starts line 95) **and** `data/shaders/lighting/src/sprite.vert.hlsl` (`register(b2, space1)`, starts line 43). The vert shader ignores them but must declare them so earlier fields keep their byte offsets. Also correct the stale `// 152 bytes` comment at `sprite.vert.hlsl:39` to `208 bytes`.

Bind the tunable ones in `src/sdl_lighting_devui.cpp` next to the existing `c.Bind( "mem_dim", &g_dbg_params.mem_dim );` (line 592): `light_quant`, `occ_soft_gain`, `self_eps_tall`, `ramp_enable`, `ramp_steps`, `ramp_chroma`. Do **not** bind `texels_per_tile` — it is data, not a knob.

Feed `texels_per_tile` in `src/sdl_render_frame.cpp` in the block that already fills `in.tile_pixel_size` (lines 482-485):
```cpp
in.debug.texels_per_tile =
    ( tilecontext && tilecontext->current_tileset() )
    ? static_cast<float>( tilecontext->current_tileset()->get_tile_width() )
    : 32.0f;
```
`cata_tiles::current_tileset()` already exists and is public (`src/cata_tiles.h:1432`); `tileset::get_tile_width()` at `src/cata_tiles.h:502` returns the **native** tileset width (32 for MSX++), distinct from `cata_tiles::get_tile_width()` (line 1207) which is the zoomed screen width.

**Done when:** build is green, game renders identically (all new knobs are either data or currently unread), and the F4 panel shows the six new sliders.

### Step 1 — Evaluate light on the art-texel grid

Currently `i.light_pos` (== `i.world_pos` for ground sprites) varies per screen pixel. Snap it to the tileset texel lattice so light is constant across each art texel — 1/32 tile, four times finer than the 8-subcell SDF, so sub-tile shadow curvature survives while the shading reads as genuine pixel art.

In `sprite.frag.hlsl` `main()`, immediately after `frag_is_tall_n` is computed (line 393-394 — it compares `i.light_pos` against `i.world_pos`, so it **must** be computed from the unsnapped values), insert:

```hlsl
// Art-texel light quantisation. Light is resolved once per TILESET texel rather
// than per screen pixel, so a zoomed sprite shades in its own pixel blocks
// instead of receiving a continuous gradient across them. 1/32 tile is 4x finer
// than the SDF subcell grid, so sub-tile shadow curvature is preserved.
float2 shade_pos = i.light_pos;
if( light_quant > 0.5 && texels_per_tile > 0.5 ) {
    shade_pos = ( floor( i.light_pos * texels_per_tile ) + 0.5 ) / texels_per_tile;
}
```

Replace `i.light_pos` with `shade_pos` at every **lighting** read — and only those:
- `skyvis_bilinear( i.light_pos )` (line 401)
- the emitter loop's `dv = e_pos.xy - i.light_pos` in **both** PASS 1 (line 424) and PASS 2 (line 472)
- `trace_shadow( i.light_pos, … )` (line 485)
- `sky_bilinear( i.light_pos )` (line 507)
- `indirect_bilinear( … )` (the GI read, gated on `gi_strength`)
- the debug-mode fetches at lines 696-697

Leave `i.uv` (atlas sampling), `i.world_pos` (geometry / tall-sprite test) and the sun march untouched.

Re-key the dither from screen pixels to art texels at `sprite.frag.hlsl:628`:
```hlsl
const float bthr = dither_threshold( shade_pos * texels_per_tile );
```
This makes the 4×4 Bayer cell exactly 4 art texels wide at every zoom level. Update the `dither_threshold` doc comment (line 318-319) and the `src/lighting/CLAUDE.md:119-121` bullet, both of which say "world pixels".

**Done when:** at 2× zoom, a lantern's falloff across a wall renders as discrete 1-art-texel steps rather than a smooth gradient, and zooming in/out does not change the dither cell's size relative to the artwork. Toggle `light_quant` to 0 in F4 to A/B it live.

### Step 2 — Capture per-sprite occluder footprints (plumbing only, no visual change)

Build, each frame, the list of screen-space quads whose alpha defines the ground occluders. The tile sprite queue is populated by `cata_tiles::draw()` **before** `refresh_display` runs the lighting build — confirmed by the comment at `src/cata_tiles.cpp:1664-1666` ("refresh_display's lighting pass reads o/op … AFTER this callback"), so the list is complete when Step 3 consumes it.

New file `src/lighting/occluder_capture.h` / `.cpp` (no existing equivalent — `emitter_collector` is the closest pattern to copy for the per-frame accumulate/submit shape):

```cpp
namespace lighting {

/// One sprite's ground footprint. Wire-stable with OccQuad in occ_raster.comp.hlsl.
struct occluder_quad {          // 32 bytes
    float u0, v0, du, dv;       // atlas UV rect of the FOOTPRINT BAND only
    float tile_x, tile_y;       // bubble-local tile the footprint lands in (integer, as float)
    float block;                // 0..1 opacity multiplier for this tile
    float oq_pad;
};
static_assert( sizeof( occluder_quad ) == 32 );

/// Per-frame accumulator. Cleared by begin(), filled from cata_tiles' terrain/
/// furniture/vpart draws, drained by gpu_sdf_pass.
class occluder_capture {
    public:
        auto begin() -> void;
        auto push( const occluder_quad &q ) -> void;
        auto quads() const -> const std::vector<occluder_quad> &; // *NOPAD*
        /// Tile-res mask: 1 where at least one quad was captured, else 0.
        auto captured_mask() const -> const std::vector<std::uint8_t> &; // *NOPAD*
        auto resize( int w, int h ) -> void;
    private:
        std::vector<occluder_quad> quads_;
        std::vector<std::uint8_t> captured_;
        int w_ = 0, h_ = 0;
};

} // namespace lighting
```

Own one instance on `render_state` beside `collector()` (`src/lighting/render_state.h`), exposed as `occluders()`; `resize()` it wherever `sdf_pass` is resized.

**The capture hook.** In `cata_tiles::draw_sprite_at` (`src/cata_tiles.cpp`, the function whose tail is lines 2339-2343 — re-read it and anchor on its `enqueue_tile_sprite` call), immediately after the successful enqueue, add a block gated on a new private `bool occluder_capture_ = false;` member:

```cpp
if( occluder_capture_ ) { push_occluder_footprint( tile, p, destination, tex ); }
```

`push_occluder_footprint` is a new private `cata_tiles` method (put it in the **outer** class body — `enqueue_tile_sprite` lives on the nested `texture` class at `src/cata_tiles.h:230` and cannot see `o`/`op`/`tile_width`; see `src/lighting/CLAUDE.md:258-267`). It computes the footprint band by intersecting the sprite's destination rect with the tile's own ground square in screen space:

```cpp
// The ground footprint is the part of the sprite overlapping this tile's own
// screen square. For a 2-tile-tall tree anchored at its base that is the trunk
// band; for a depth-extruded wall it is the wall base. Handles tile_type::offset,
// offset_retracted and multi-tile sprites with no special cases.
const point sp = player_to_screen( p );              // tile's top-left in screen px
const float gx0 = static_cast<float>( sp.x );
const float gy0 = static_cast<float>( sp.y );
const float gx1 = gx0 + static_cast<float>( tile_width );
const float gy1 = gy0 + static_cast<float>( tile_height );
const float ix0 = std::max( destination.x, gx0 );
const float iy0 = std::max( destination.y, gy0 );
const float ix1 = std::min( destination.x + destination.w, gx1 );
const float iy1 = std::min( destination.y + destination.h, gy1 );
if( ix1 <= ix0 || iy1 <= iy0 ) { return; }           // sprite misses this tile's square
// Map the intersection back to normalised atlas UV within the sprite's src rect.
const float fu0 = ( ix0 - destination.x ) / destination.w;
const float fu1 = ( ix1 - destination.x ) / destination.w;
const float fv0 = ( iy0 - destination.y ) / destination.h;
const float fv1 = ( iy1 - destination.y ) / destination.h;
```
then combine with the sprite's normalised source rect (same `srcrect / atlas_w|atlas_h` math `enqueue_tile_sprite` performs at `src/cata_tiles.h:237-250`, including the flip sign convention) to produce `u0/v0/du/dv`, and set `block` from map data:

```cpp
// block = how much of the light this tile stops, independent of the art.
// transparency_cache is an attenuation coefficient with LIGHT_TRANSPARENCY_SOLID
// (0.0) as the exact opaque sentinel (see frame_build.cpp:143-165); larger values
// attenuate more. Hard occluders are exact; everything else gets Beer-Lambert over
// one tile relative to open air. Glass/bars land near 0 so they keep NOT occluding,
// matching today's rule; hedges/smoke land high and now dapple.
const float t = mc.transparency_cache[ idx ];
const float blk = ( t <= LIGHT_TRANSPARENCY_SOLID )
                  ? 1.0f
                  : std::clamp( 1.0f - std::exp( -( t - LIGHT_TRANSPARENCY_OPEN_AIR ) ), 0.0f, 1.0f );
```

Set `occluder_capture_ = true` around the bodies of `draw_terrain`, `draw_furniture` and `draw_vpart` in `src/cata_tiles_draw_layers.cpp`, and `false` on exit (RAII guard). Deliberately **excluded**: items, creatures, fields, overlays. Creatures must not enter the SDF — the SDF rebuild is gated on `transparency_generation` / camera-origin change (`src/sdl_render_frame.cpp` gate described in `src/lighting/LIGHTING_PERF_PLAN.md:41-53`), and per-frame creature motion would force a rebuild every frame. Creature sun shadows already exist via the screen-space silhouette pass (`shadow.vert.hlsl` / `shadow.frag.hlsl`).

Call `rs.occluders().begin()` at the top of `lighting::build_and_submit_lighting` (`src/lighting/frame_build.cpp:67`) — no: `begin()` must run **before** `cata_tiles::draw()`, so call it from `cata_tiles::draw()`'s own prologue right where `clear_tile_queue()` is called.

**Done when:** a `dbg(DL::Info)` line logs a plausible quad count (roughly the number of visible terrain+furniture+vpart sprites, low thousands at 1080p) and `captured_mask` sums to roughly the visible tile count. Rendering is byte-identical — nothing consumes the list yet.

### Step 3 — Rasterise sprite alpha into the SDF seed (the payoff)

Replace the tile-binary seed with a supersampled coverage field derived from the artwork.

**3a. New GPU field.** In `src/lighting/gpu_sdf_pass.h/.cpp` add an `SDL_GPUBuffer* occ_ss_` of `max_w*SDF_SS * max_h*SDF_SS` **uint32** (same element count as `seed_a_`, one uint each), usage `COMPUTE_STORAGE_READ | COMPUTE_STORAGE_WRITE`. Coverage is stored as `uint` fixed-point `cov * 65535` so concurrent writes from overlapping sprites resolve with `InterlockedMax` — order-independent and deterministic.

**3b. Base pass** — new `data/shaders/lighting/src/occ_base.comp.hlsl`, one thread per subcell:
```hlsl
// Seeds the tile-square fallback ONLY where no sprite footprint was captured
// (off-camera tiles). On-camera tiles are zeroed here and filled by occ_raster,
// so the alpha silhouette REPLACES the square rather than unioning with it.
const uint tile_idx = (uint)( tx * map_h + ty );
const float base = ( CapturedBuf[tile_idx] != 0u ) ? 0.0
                 : ( ( TransBuf[tile_idx] < 0.5 ) ? 1.0 : 0.0 );
OccSS[sx * (int)( map_h * SDF_SS ) + sy] = (uint)( saturate( base ) * 65535.0 );
```
Inputs: `TransBuf` (the existing tile-res float transparency, unchanged) and a new tile-res `CapturedBuf` (`StructuredBuffer<uint>`, from `occluder_capture::captured_mask()`, 17 KB upload). This is what keeps off-screen walls casting shadows into view — a real regression risk otherwise, since capture only covers drawn tiles.

**3c. Raster pass** — new `data/shaders/lighting/src/occ_raster.comp.hlsl`, `numthreads(8,8,1)` (== `SDF_SS`×`SDF_SS`), dispatched `(quad_count, 1, 1)`: **one thread group per quad, one thread per subcell of its tile.**
```hlsl
// Each subcell covers (texels_per_tile / SDF_SS) art texels per axis (4 at 32/8).
// Average that block's alpha so a chainlink fence's real holes become fractional
// coverage instead of aliasing to all-or-nothing.
const OccQuad q = Quads[gid.x];
const float2 f0 = float2( tid.x, tid.y ) / (float)SDF_SS;        // subcell UV in tile
const float2 f1 = float2( tid.x + 1u, tid.y + 1u ) / (float)SDF_SS;
float acc = 0.0; float n = 0.0;
[loop] for( uint sy = 0u; sy < TAPS; ++sy ) {
    [loop] for( uint sx = 0u; sx < TAPS; ++sx ) {
        const float2 fr = lerp( f0, f1, ( float2( sx, sy ) + 0.5 ) / (float)TAPS );
        const float2 uv = float2( q.u0 + q.du * fr.x, q.v0 + q.dv * fr.y );
        acc += Atlas.SampleLevel( AtlasSmp, uv, 0 ).a; n += 1.0;
    }
}
const float cov = ( acc / max( n, 1.0 ) ) * q.block;
const int gx = (int)q.tile_x * SDF_SS + (int)tid.x;
const int gy = (int)q.tile_y * SDF_SS + (int)tid.y;
InterlockedMax( OccSS[gx * (int)( map_h * SDF_SS ) + gy], (uint)( saturate( cov ) * 65535.0 ) );
```
`TAPS = 4` as a shader constant. `Atlas` is the same GPU atlas page the sprite pass samples; bind it as a compute sampler. Quads live in a new `SDL_GPUBuffer` uploaded from `occluder_capture::quads()` (32 B × a few thousand = well under 128 KB).

**3d. Seed rewrite.** Replace the body of `data/shaders/lighting/src/jfa_seed.comp.hlsl:32-45` with a coverage read plus a world-locked Bayer threshold:
```hlsl
// Coverage → occupancy via a world-locked 4x4 Bayer threshold at SUBCELL
// resolution. Solid coverage (1.0) seeds every subcell, identical to the old
// binary path. Partial coverage (a hedge, a chainlink lattice, a smoke field)
// seeds a stable fraction of subcells, so the SDF develops real holes and the
// existing sphere-march produces dappled light with no new field and no consumer
// change. Deterministic and shift-stable: map::shift moves the bubble by whole
// submaps (12 tiles = 96 subcells) and 96 % 4 == 0, so the Bayer phase is
// preserved across shifts and the pattern never crawls.
const uint  idx = (uint)( sx * (int)( map_h * SDF_SS ) + sy );
const float cov = (float)OccSS[idx] * ( 1.0 / 65535.0 );
const float thr = ( k_bayer4[ ( sy & 3 ) * 4 + ( sx & 3 ) ] + 0.5 ) / 16.0;
SeedBuf[idx] = ( cov > thr ) ? float2( (float)sx, (float)sy ) : float2( -1.0, -1.0 );
```
`k_bayer4` goes in `data/shaders/lighting/src/jfa_shared.hlsl` (already included by the seed) so the flood/resolve shaders share it. **`SDF_SS` there must stay 8** to match `SDF_SUPERSAMPLE` (`src/lighting/sdf_pass.h:24`) and `sprite.frag.hlsl:138`.

**3e. Dispatch order** in `gpu_sdf_pass::record`, each its own `BeginGPUComputePass`/`End` so SDL_GPU inserts the write→read barriers: `occ_base` → `occ_raster` → `jfa_seed` → flood ping-pong (unchanged) → `jfa_resolve` (unchanged). Wire it at the existing JFA record site, `src/sdl_render_frame.cpp:393-425`.

**3f. Self-shadow guard.** `trace_shadow`'s escape (`sprite.frag.hlsl:286`, `sdf_bilinear(origin) < 0.05`) assumes a tall sprite's base tile centre is inside its own occluder. With a slab or thin footprint the centre can now be *outside* it, so a fence would self-shadow. Take the threshold as a parameter and pass `frag_is_tall_n ? self_eps_tall : 0.05`:
```hlsl
float trace_shadow( float2 origin, float2 dir, float dist_to_light,
                    float k, int steps, bool directional, float self_eps )
```
Update both call sites (emitter PASS 2 at line 485, and the sun march). `vol.frag.hlsl` has its own copy of the shared trace — pass `0.05` there (fog receivers are never tall sprites).

**No consumer changes.** `sdf_bilinear` in `sprite.frag.hlsl`, `gi_field.comp.hlsl`'s sphere-march, `sky_sun.comp.hlsl` and `vol.frag.hlsl` all keep reading `sdf_storage_` at the same layout, stride and tile units — they simply start seeing real geometry. Retire nothing: `TransBuf` is still the off-camera fallback.

**Done when:** standing next to a tree, the shadow is round and the trunk's shadow is trunk-width, not tile-width; a chainlink fence throws dappled light; a car parked at an angle casts a shadow along its actual hull instead of a tile staircase; and F4 debug mode 6 (SDF visualisation) shows curved iso-lines instead of diamonds. `occ_soft_gain = 0` must reproduce hard-occluder-only behaviour.

### Step 4 — Sub-tile emitter placement

Independent of 5 and 6; depends on 3 only because 3f changes the `trace_shadow` signature.

In `src/lighting/snapshot.cpp`, give `make_omni`/`make_cone` float positions instead of ints:
```cpp
static gpu_emitter make_omni( float px, float py, int lz, float radius,
                              float r, float g, float b );
```
Keep the existing `+ 0.5` convention by having callers pass `x + 0.5f`. `flicker_seed` currently hashes `lx * 31337 + ly * 7919` (line 80) — hash the **integer** tile, not the float position, or a walking light's flicker phase will jitter.

**4a. Creature-carried lights follow the sliding sprite.** `collect_character` (line 238) and the monster loop (line 257) and the player block (line 274) all use `c.bub_pos()`. Add the render slide offset, which is in tile units and is the *exact* value the sprite used this frame (`src/cata_tiles_anim.cpp:134-137` calls `update_animation_state` then applies `a.slide_offset_x * tile_width`; `cata_tiles::draw()` runs before the lighting build, so `anim_state` is already current):
```cpp
const animation_state &a = c.anim_state;
const float ex = static_cast<float>( pos.x() ) + 0.5f + a.slide_offset_x;
const float ey = static_cast<float>( pos.y() ) + 0.5f + a.slide_offset_y;
```
Apply to NPCs, monsters and the player identically. On no-input frames neither the sprite queue nor `anim_state` advances, so light and sprite stay locked by construction.

**4b. Push emitters off their own occluder.** A wall-mounted lamp sits at its opaque tile's centre, so its own tile occludes it and `trace_shadow`'s escape fires. Where an emitter's own tile is opaque, offset it toward the open side:
```cpp
// A light inside an opaque tile (wall lamp, lit sign, glowing terrain) belongs on
// the wall FACE, not at the wall centre. Average the directions of the four
// orthogonal neighbours that transmit light, normalise, and push 0.45 tile.
// No transparent neighbour (fully enclosed) → leave at centre.
```
Use the same `transparency_cache[idx] > LIGHT_TRANSPARENCY_SOLID` test the rest of the engine uses. Applies to the terrain/furniture/field emitters in `collect_zlev` (lines 111-134), not to creatures.

**Done when:** walking with a lit torch, the pool of light glides with your sprite and the shadow it throws sweeps continuously instead of jumping a tile at a time; and a wall lamp lights the corridor in front of it rather than being swallowed by its own wall.

### Step 5 — Continuous vision carve, and delete the dead vis field

Independent of 4 and 6.

**5a. Delete the dead path.** In `src/lighting/frame_build.cpp` remove the whole `rebuild.vis` block (lines 286-344): the `vtile` build, the `gaussian_blur_tilefield` call and the `SW×SH` SS replication — 1,115,136 floats (4.5 MB) written and uploaded on every player move with no reader. Drop `vis` from the `rs.collector()->submit(…)` argument list (line 353-356) and the `vision_blur` parameter from `build_and_submit_lighting` (`frame_build.h:71-73`, caller at `src/sdl_render_frame.cpp:280-282`), plus `g_vision_blur` (`src/sdl_lighting_devui.cpp:102`, bind at line 622, extern at `sdl_lighting_devui.h:163`). In `src/lighting/sdf_pass.{h,cpp}` remove `visbuf_storage_`, `xfer_vis_f_`, `vis_buffer()` and their init/upload/shutdown blocks. Keep `gaussian_blur_tilefield` only if `skylight_bleed` still uses it; otherwise delete it too.

**5b. Add the carve.** The shader needs no new buffer — `player_x`/`player_y` are already in `DebugParams` (`sprite.frag.hlsl:115-116`), and `trace_shadow` already marches the SDF. Insert in `main()` **immediately after the memory-distance-fade block closes at line 661**, before the debug-visualisation block at 663. `frag_is_tall_n` (line 393) and `final_rgb` (line 640) are both still in scope there:

```hlsl
// Sub-tile vision carve. The CPU already decided, per tile, whether this tile is
// drawn at all (lit_level → apply_vision_effects skips non-CLEAR tiles), so this
// term can only ever SUBTRACT within an already-granted tile — it is structurally
// incapable of revealing anything gameplay says is unseen. What it buys is the
// SHAPE: a wall corner cutting your sight now produces a smooth sub-tile curve
// instead of a tile staircase, because it marches the same sub-tile SDF the
// shadows use.
if( vis_curve > 0.001 ) {
    const float2 eye = float2( player_x, player_y );
    const float2 ev  = eye - shade_pos;
    const float  ed  = length( ev );
    const float  los = ( ed < 0.5 ) ? 1.0
                     : trace_shadow( shade_pos, ev / ed, ed, shadow_k,
                                     (int)shadow_steps, false,
                                     frag_is_tall_n ? self_eps_tall : 0.05 );
    float v = pow( saturate( los ), vis_curve );
    if( vis_radius > 0.001 ) {
        v *= saturate( 1.0 - smoothstep( vis_radius * 0.6, vis_radius, ed ) );
    }
    // Fade toward the memory look rather than to black, so the carve reads as the
    // same material the unseen region already uses.
    final_rgb *= mem_dim + ( 1.0 - mem_dim ) * v;
}
```
`vis_curve` and `vis_radius` are the **orphaned lanes** — reusing them is semantically exact and costs zero ABI churn. Set `vis_radius`'s default to **`0.0f`** in `src/lighting/sprite_batcher.h:167` (it is currently `16.0f` but unread; wiring a reader at 16 would silently darken daylight scenes). Keep `vis_curve = 1.0f`.

Delete the now-truly-dead declarations in the shader comment block (`sprite.frag.hlsl:19`, `62-68`) that still describe `VisBuf`, and fix the stale slot list at `src/lighting/sprite_batcher.cpp:786-787` and `641-644`, which name `VisBuf` even though the bind array at line 800-803 carries five buffers.

The **outward** frontier (a CLEAR tile's neighbour that is DARK/HIDDEN) keeps a tile edge in this step. ~~Deliberately out of scope.~~ **SUPERSEDED by Step 8 below** — the premise was wrong on two counts: the loudest outward artefact is not the `lighting_*` overlay at all but the `lit_level::LOW` GREYSCALE ATLAS SWAP, and memorized terrain *is* already drawn beneath its overlay, so there was something to fade after all.

**Done when:** looking down a corridor past a door frame, the lit wedge's edge is a smooth curve at art-texel resolution rather than a staircase, and the `[lighting][perf] vis_rebuild` log line is gone. `vis_curve = 0` restores the pre-step look exactly.

### Step 6 — SDF-guided bilateral GI upsample

Independent of 4 and 5. The GI buffer is one probe per tile (`src/lighting/gi_compute_pass.h`, 4 floats/tile), read by `indirect_bilinear` in `sprite.frag.hlsl` (around line 201-243). Plain bilinear across a wall bleeds interior bounce outdoors and reads blocky at wall edges. Weight the four taps by SDF similarity instead of raising probe density 4×:

```hlsl
// Bilateral GI upsample: reject taps whose SDF differs sharply from the sample
// point's, so bounce light does not cross a wall. Fixes the dominant GI artefact
// (blocky bounce at wall edges, indoor light bleeding outside) for four extra SDF
// taps, where raising probe density to 2x2/tile would cost 4x the compute.
const float sd_c = sdf_bilinear( p );
float wsum = 0.0; float3 acc = 0.0;
// per tap: w = bilinear_weight * exp( -abs( sdf_bilinear( tap_centre ) - sd_c ) / GI_BILAT_SIGMA )
```
`GI_BILAT_SIGMA = 0.35` (tiles) as a shader constant. Fall back to the unweighted result when `wsum` underflows (`wsum < 1e-4`) so a fully-rejected neighbourhood cannot produce black. This only matters once Step 3 lands — with tile-square occluders the SDF has no sub-tile contrast to key on.

**Done when:** standing outside a lit window, the ground outside no longer receives a square patch of interior bounce, and the interior's bounce stops at the wall line instead of a tile boundary.

### Step 7 — Per-palette shade ramps

Depends only on Step 1 (it needs `shade_pos`/quantised light to look right). Land it last: it is the largest aesthetic commitment and the easiest to tune wrong.

**7a. Palette + ramp construction.** New `src/lighting/palette_ramp.h` / `.cpp`:

```cpp
namespace lighting {

/// Procedural ramp generation (no per-tileset authoring). Standard pixel-art
/// convention: shadows shift hue toward the cool end and lose value faster than
/// saturation; highlights shift warm, gain value and lose saturation at the top.
struct ramp_gen_params {
    int steps = 8;
    float shadow_hue_shift = -0.055f; // fraction of the hue circle at step 0
    float light_hue_shift = 0.030f;   // at the brightest step
    float shadow_value = 0.28f;       // value multiplier at step 0
    float light_value = 1.30f;        // at the brightest step
    float shadow_sat = 1.20f;         // saturation multiplier at step 0
    float light_sat = 0.72f;          // at the brightest step
};

struct palette_ramp_data {
    std::vector<std::uint32_t> ramp;  // palette_size * steps, RGBA8 (0xAABBGGRR)
    std::vector<std::uint32_t> index; // 32*32*32, palette row per quantised RGB
    int palette_size = 0;
    int steps = 0;
};

/// Accumulates colours from every tileset sheet, then bakes ramps + the lookup.
class palette_accumulator {
    public:
        auto add_surface( const SDL_Surface &s ) -> void;
        auto build( const ramp_gen_params &gen ) const -> palette_ramp_data;
    private:
        std::unordered_map<std::uint32_t, std::uint32_t> hist_; // RGB888 → count
};

} // namespace lighting
```

`add_surface` histograms every pixel with `a >= 128`, keyed on RGB888. `build` keeps the **256** most frequent colours as palette rows, generates `steps` ramp entries per row from `ramp_gen_params`, then fills the `32³` index by nearest-neighbour in **OkLab** (perceptual nearest; plain RGB distance picks visibly wrong rows on saturated art). Add a small `srgb_to_oklab` helper in the same TU — no existing equivalent in the repo.

**Hook site:** `tileset_loader::create_textures_from_tile_atlas` (`src/cata_tiles_tileset.cpp:1150-1156`) is the single choke point every sheet surface passes through on **both** the `DYNAMIC_ATLAS` and legacy paths. Call `acc.add_surface( *tile_atlas )` at its top. Build once at the end of `cata_tiles::load_tileset` (`src/cata_tiles_tileset.cpp:578`) and hand the result to `render_state` for upload.

**7b. Upload as storage buffers, not samplers.** Two new fragment storage buffers, appended so **no existing slot renumbers**:
```hlsl
StructuredBuffer<uint> RampBuf   : register(t7, space2); // palette_size*steps RGBA8
StructuredBuffer<uint> PalIdxBuf : register(t8, space2); // 32^3 palette rows
```
Storage buffers, not sampled textures, for three reasons that are all recorded in this repo: `src/lighting/CLAUDE.md:278-289` documents shadercross mis-binding sampler textures on Metal (silent zeros); adding samplers would shift the storage-texture and all five storage-buffer registers, the exact lockstep hazard at `docs/lighting/GI_PERF_REMAINDER_ONESHOT.md:79-84`; and the lookup wants nearest/integer indexing anyway.

Grow the bind array at `src/lighting/sprite_batcher.cpp:800-803` from 5 to **7** entries in one `SDL_BindGPUFragmentStorageBuffers(rp, 0, sbufs, 7)` call, and extend the null guard to cover both new buffers. Allocate both unconditionally at init (even with an empty tileset) — `sprite.frag.hlsl:62-68` records that a declared-but-stripped fragment storage buffer punched a hole in the SRV range and killed the whole pipeline with `E_INVALIDARG`. For the same reason **both buffers must be read unconditionally**: gate on `ramp_enable` with a `lerp`, never an `if`.

**7c. Resolve.** Insert the ramp resolve in `sprite.frag.hlsl` **immediately after line 640** (`float3 final_rgb = texel.rgb * combined;`) — i.e. before the memory-distance fade at 651-661 and before the debug-visualisation block at 663, so memory fade and debug modes still operate on the final colour. `gpu_total` (line 634) and `final_rgb` (line 640) are the existing locals; there is **no `luma()` helper in this shader**, so add one next to `dither_threshold`:

```hlsl
float luma( float3 c ) { return dot( c, float3( 0.2126, 0.7152, 0.0722 ) ); }
float3 unpack_rgba8( uint p ) {
    return float3( ( p & 0xFFu ), ( ( p >> 8 ) & 0xFFu ), ( ( p >> 16 ) & 0xFFu ) ) * ( 1.0 / 255.0 );
}
uint pal_index_of( float3 rgb ) {   // 5 bits/channel → 32^3
    const uint3 q = (uint3)clamp( rgb * 31.0 + 0.5, 0.0, 31.0 );
    return ( q.r * 32u + q.g ) * 32u + q.b;
}
```

Then the resolve itself:
```hlsl
// Palette shade ramp. The lighting result selects a SHADE STEP within the base
// texel's own ramp, so a red surface darkens toward its palette's dark red rather
// than toward grey. Coloured light still reads: ramp_chroma re-tints the ramped
// colour by the light's normalised chroma. Both buffers are read unconditionally
// (D3D12 strips unread fragment storage buffers and breaks the root signature).
const uint   pal_row = PalIdxBuf[ pal_index_of( texel.rgb ) ];
const float  shade_f = saturate( luma( gpu_total ) ) * ( ramp_steps - 1.0 )
                       + ( dither_threshold( shade_pos * texels_per_tile ) - 0.5 );
const uint   shade_i = (uint)clamp( shade_f + 0.5, 0.0, ramp_steps - 1.0 );
const float3 ramped  = unpack_rgba8( RampBuf[ pal_row * (uint)ramp_steps + shade_i ] );
const float3 lit_chroma = normalize( max( gpu_total, 1e-4 ) ) * 1.7320508;   // ×sqrt(3)
const float3 ramp_rgb = lerp( ramped, ramped * lit_chroma, ramp_chroma );
final_rgb = lerp( final_rgb, ramp_rgb, ramp_enable );
```
The ordered-dither term on `shade_f` is what stops 8 steps from banding; error diffusion is not available in a fragment shader and the world-locked Bayer helper already exists.

**7d. Bypass AgX when ramping.** Ramp output is display-referred, so AgX would re-map it and break the palette contract. There is a spare lane already sized for this — **both sides must move together or the cbuffer offsets silently corrupt every tonemap value**:

1. `data/shaders/lighting/src/tonemap.frag.hlsl:20` — rename `float tm_pad;` → `float tm_ramp_enable;` (size unchanged, 16 B).
2. `src/lighting/tonemap_pass.cpp:100-105` — the cbuffer is a **function-local** struct inside `record`:
   ```cpp
   struct TonemapParams { float exposure; float min_ev; float max_ev; float pad0; }
       params{ exposure, min_ev, max_ev, 0.0f };
   ```
   Rename `pad0` → `ramp_enable` and initialise it from a new parameter instead of `0.0f`.
3. `src/lighting/tonemap_pass.h:61-64` and `tonemap_pass.cpp:94-96` — append `float ramp_enable` to `record`'s parameter list after `max_ev`.
4. `src/sdl_render_frame.cpp:978-979` — the only call site; pass `g_dbg_params.ramp_enable`.
5. In the shader body, `lerp` between the AgX result and a plain exposure-scaled `saturate()` on `tm_ramp_enable`.

Bloom stays on its own path unchanged and is *correct* to leave un-ramped — glow is light, not surface, so halos leaving the palette is desirable. Volumetric fog is additive after the sprite pass and likewise stays un-ramped.

**Done when:** a red couch under a dim lamp darkens toward dark red, not grey; the whole scene's colours are visibly drawn from the tileset's own palette; `ramp_enable = 0` in F4 restores the current look pixel-for-pixel; and `ramp_steps` sweeps 4→16 without banding thanks to the dither term.

---

## Critical files & anchors

| File | Region | Why it disambiguates |
|---|---|---|
| `data/shaders/lighting/src/jfa_seed.comp.hlsl:32-45` | the whole `main` body | The exact five lines that throw away 64× of SDF resolution. Step 3d replaces them; everything downstream is unchanged. |
| `data/shaders/lighting/src/sprite.frag.hlsl:58-80, 371-401, 612-660` | binding block, `main` prologue, dither + final combine | The binding block records why fragment storage buffers must stay contiguous and every one must be read (Steps 3, 5, 7). The prologue is where `shade_pos` is introduced; the combine is where the carve and the ramp land. |
| `src/lighting/sprite_batcher.cpp:39-61, 780-805` | `light_params`/`debug_params` asserts, `bind_lighting_resources` | The wire-stable ABI (`debug_params == 176` today) and the all-or-none 5-buffer bind that Steps 0 and 7 must move in lockstep with the shader registers. |
| `src/cata_tiles.cpp:1437, 2339-2406` | draw-loop vision gate, `draw_sprite_at` tail / `apply_vision_effects` | Proves the terrain sprite is *skipped* for non-CLEAR tiles (bounding Step 5) and is the anchor for the Step 2 capture hook. Note the nested-class trap: the hook helper must go in the **outer** `cata_tiles` body. |
| `src/lighting/frame_build.cpp:136-165, 286-344` | transparency pack, dead `rebuild.vis` block | The `LIGHT_TRANSPARENCY_SOLID` semantics Step 2's `block` mapping must respect, and the 4.5 MB dead upload Step 5a deletes. |

---

## Verification

Build (never synchronously, never with a short cap — a killed ninja run corrupts `.ninja_deps`):
```sh
cmake --build --preset windows-tiles-sounds-x64-msvc --target cataclysm-bn-tiles cata_test-tiles &
```
Shader reflection gate — run before declaring **any** shader step done; it catches the storage-buffer contiguity failures that otherwise present as a black screen:
```sh
cmake --build out/build/win-rel-deb --target shader_reflect_check \
  && out/build/win-rel-deb/tools/shader_check/shader_reflect_check
```
Expected reflection after Step 7: `sprite.frag` samplers=1, storage_textures=1, storage_buffers=7. `vol.frag` stays samplers=0, storage_buffers=2.

Runtime log (`config/debug.log`; `DL::Debug` is filtered — use `dbg(DL::Info)`):
```sh
grep -E "\[lighting\]\[perf\]|\[render\]\[perf\]|ERROR|WARN" config/debug.log | tail -n 60
```

**Per-step in-game proof.** The build is a Windows install driven unattended — follow `skill://cbn-drive-installed-game` for launch/focus/screenshot/cleanup and `skill://cbn-input-harness-pixel-proof` for the PowerShell pixel-diff harness. Every visual claim below is proven by a labelled before/after screenshot pair plus a pixel diff, not by eyeballing.

| Step | Scene to reach | Concrete input → expected observable output |
|---|---|---|
| 0 | main menu | Build green; F4 shows six new sliders; frame is byte-identical to pre-change (pixel diff ≈ 0). |
| 1 | any lit interior, zoom 2× | Toggle `light_quant` 0→1: a lamp's falloff across a wall changes from a smooth gradient to discrete 1-art-texel steps. Zoom 1×→3× with it on: the Bayer cell stays 4 art texels wide (measure against a known 32 px sprite). |
| 2 | walk 10 tiles outdoors | `dbg(DL::Info)` quad count is in the low thousands and tracks the visible sprite count; `captured_mask` sum ≈ visible tile count; pixel diff vs Step 1 == 0. |
| 3 | stand next to a tree, then an angled parked car, then a chainlink fence, at night with a torch | Tree shadow is round and trunk-width; car shadow follows the hull, not a tile staircase; fence throws dappled light. F4 debug mode 6 shows curved SDF iso-lines. `occ_soft_gain = 0` reproduces hard-occluder-only. |
| 4 | hold a lit torch, walk 5 tiles | The light pool glides with the sprite (no tile-to-tile jump) and the cast shadow sweeps continuously. Capture consecutive frames mid-step and diff: the light centroid must move by a fraction of a tile, and must not lead or lag the sprite. |
| 5 | look down a corridor past a door frame | The lit wedge edge is a smooth curve at art-texel resolution, not a staircase. `[lighting][perf] vis_rebuild` no longer appears in the log. `vis_curve = 0` restores the Step 4 frame exactly (pixel diff ≈ 0). |
| 6 | stand outside a lit window at night | No square patch of interior bounce on the ground outside; interior bounce terminates at the wall line. |
| 7 | a room with a red couch under a dim lamp | The couch darkens toward dark red, not grey. `ramp_enable = 0` restores the Step 6 frame pixel-for-pixel. Sweep `ramp_steps` 4→16: no banding (the dither term absorbs it). |

**Regression guards** (run once, after Step 7):
- `cata_test-tiles "[map] [vehicle] [ranged] [item]"` — Steps 1-7 touch no gameplay path, so these must be unchanged. Confirm the binary actually contains the tests first (`--list-tests` and check the count) — the VS generator only re-evaluates the `tests/*.cpp` glob on *configure*, so a stale `.vcxproj` can silently omit tests.
- Grep-zero gate on the deletions in Step 5a: `grep -rn "vis_buffer\|xfer_vis_f_\|visbuf_storage_\|g_vision_blur\|VisBuf" src data/shaders` must return nothing.

---

## Assumptions & contingencies

- **Vehicle hull rotation comes from the sprite, not Box2D.** Step 3 gets a rotating car's occluder for free because `draw_vpart` already enqueues rotated sprites and the capture reads their alpha. *If* the rotation turns out to be baked per-direction into separate sprites rather than applied via `sprite_instance.rotation`, the footprint is still pixel-exact for that facing — no fallback needed, the result is simply quantised to the sprite set the tileset ships.
- **Off-camera occluders.** Capture only covers drawn tiles, so Step 3b's `CapturedBuf` fallback is load-bearing, not optional: without it, a wall just outside the viewport stops casting its shadow into view. *If* the fallback still shows a visible seam at the screen edge, widen the drawn region rather than the fallback — `cata_tiles::draw` already has `min_visible_x/max_visible_x` bounds; extend them by 2 tiles and let the extra ring draw only terrain.
- **`InterlockedMax` on a `RWStructuredBuffer<uint>`** is assumed available on D3D12/Vulkan/Metal through shadercross — *unverified; confirm with `shader_reflect_check` before building on it*. If Metal rejects it, serialise instead: sort the quad list CPU-side by tile index and dispatch one group per **tile** rather than per quad, iterating that tile's quads in-thread. Same result, no atomics.
- **Compute-stage atlas sampler.** Step 3c binds the GPU atlas to a compute pass, which the codebase has not done before — *unverified*. `src/lighting/CLAUDE.md:278-289` records shadercross mis-binding *fragment* sampler textures on Metal. If the compute sample returns zeros, read the atlas as a `Texture2D` with `.Load` (integer texel fetch, no sampler) — the UV rects are already normalised, so multiply by the atlas dimensions and `.Load` the integer texel. That is also strictly what pixel art wants.
- **Palette size 256 with 8 steps** is assumed sufficient for MSX++UnDeadPeopleEdition. If the histogram shows a long tail such that the 256th row's frequency is still significant, raise to 512 rows — the buffers are tiny (512×8 uints = 16 KB, plus 32³ = 128 KB) and only `ramp_steps`/`palette_size` plumbing changes. Do **not** raise the index LUT beyond 32³ without measuring; 48³ is 442 KB and the perceptual gain is small.
- **Procedural ramps may read wrong for a specific tileset.** All seven `ramp_gen_params` fields are F4-tunable, so the contingency is tuning, not code. If procedural ramps cannot be made to look right at all, the escape hatch is `ramp_enable = 0`, which restores the Step 6 pipeline exactly — Step 7 is designed to be fully revertible at runtime.
- **`self_eps_tall = 0.55`** is a starting value for Step 3f. If tall sprites still self-shadow after Step 3, raise it via F4 before touching code; if *nearby* walls stop shadowing tall sprites, lower it. The knob exists precisely so this is not a rebuild.

---

## Step 8 — Sub-tile vision FRONTIER (added 2026-08-02, after user report)

Steps 0-7 shipped, and the user reported: *"vision is still very much tile stepped, which now collides with the nice lighting."* Correct — and Step 5's scope note above got the reason wrong. Diagnosis, in the order the evidence forced it:

1. Added `debug_mode == 15`, a frontier view that colours each fragment by which whole-tile vision treatment it carries. This was the load-bearing step: **three successive hypotheses died to it.** The `lighting_*` overlay moved 0.128% of pixels (not the cause). The CPU lightmap tint gate read 0 everywhere (not the cause). In the daylight scene the whole view was "ordinary visible" — the staircase the screenshot showed there is a genuine lawn/road albedo boundary, i.e. map geometry, not a vision artefact at all.
2. At **night** the same view turned uniformly "desaturate", which located the real mechanism: `cata_tiles.cpp:2167`, `lit_level::LOW` swaps the sprite for a whole GREYSCALE ATLAS VARIANT. A binary, per-tile colour change — the exact thing that reads as a staircase against continuous GPU light. A carried light makes it unmistakable: the lit region is a **razor-sharp 3x3 square**.

**Fix.** Keep the normal sprite for LOW and reproduce `color_pixel_grayscale` in `sprite.frag`, feathered across the boundary tile. Same treatment for the other two whole-tile decisions (memory dim + tint passthrough; overlay alpha). `cata_tiles::frontier_mask` packs the 8 neighbours' "same side" bits; the shader bilinearly interpolates the four corner means and remaps with `smoothstep(0.5, 1.0, cov)`.

The remap bound is not cosmetic: on a straight frontier both corners on the shared edge average to exactly 0.5, so any band centred on 0.5 leaves that edge at half-treatment against an untreated neighbour — a softer staircase, but still a staircase. Anchoring at 0.5 keeps the whole feather inside the treated tile.

Costs no new vertex attribute, cbuffer field or GPU buffer: the mask rides the **negative** range of `sprite_instance.pad2` (the `outline` lane), which is otherwise only ever tested `> 0.5`. Bit 8 selects desaturate vs hide/dim.

**Verified:** night scene, atomic lamp on the ground. `vis_edge` 1→0 moves **46.9%** of the lamp region (meandelta 7.0) against a **0.005%** same-state drift floor; the hard square becomes a graded falloff. Deep-LOW interior is unchanged by construction (`cov = 1` → the original filter, ±1/255 truncation), which a whole-band A/B confirmed at literally 0 changed pixels. Evidence: `out/verification/grid-decoupled-lighting/2{0,1}-step8-vision-frontier-{ON,OFF}.png`. Knob: `vis_edge` (default 1). Tests: 141,451 assertions / 64 cases, pass.

**Harness lessons that cost real time here:**
- The dev **cursor light is GPU-only** — it never touches the CPU lightmap, so it cannot create a `lit_level` boundary. Worse, it follows the mouse, so if it is left enabled it silently contaminates every A/B where the drive script clicks a panel between captures. Untick it before measuring.
- After a debug-menu **time change the CPU lightmap settles late**; the first frames are stale. One A/B pair here showed an 86% delta that was entirely stale-vs-settled lighting. Always run knob→capture as a **1→0→1 triplet** and require the third frame to match the first.
- F4 slider **drags only register right-to-left** (a left-to-right drag misses the thumb), so "restore" silently fails and the next pair is measured in the wrong state.
- The debug menu is **F12**, and `Change time` is main-menu `c` → `m` → `t` → `h`.

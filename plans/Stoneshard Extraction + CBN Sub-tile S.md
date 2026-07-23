# Stoneshard Extraction + CBN Sub-tile Splatmap System

## Context

CBN's blood/splatter system is tile-resolution: `map::add_splatter()` toggles a field type per tile (`fd_blood`), rendered as a grid-locked 32x32 sprite. Stoneshard uses a persistent off-screen surface where blood sprites are stamped at sub-pixel positions with random rotation/scale/alpha, producing organic, high-detail splatters.

Build two things: (1) extract Stoneshard's VFX sprites (blood, fire, particle frames) and decompile its blood GML logic for reference, and (2) implement a GPU splatmap rendering system in CBN that composites sub-tile decal stamps over the terrain layer — reusable for blood, wetness trails, snow accumulation, and other surface effects.

## Approach

### Step 1: Extract Stoneshard assets (manual, no code changes)

Stoneshard is a GameMaker Studio 2 game. All sprites + compiled GML scripts live in `data.win`.

**Tool:** UndertaleModTool (UMT) — https://github.com/UnderminersTeam/UndertaleModTool

On macOS Apple Silicon, use Wine to run the GUI (Bleeding Edge nightly build, 64-bit Wine prefix) or the native `UndertaleModCLI`. Fallback: SpriteRipper (Python, macOS-native). If UMT fails to parse, switch Stoneshard to the `modbranch` Steam beta.

**Extract sprites:** `Scripts > Resource Unpackers > ExportAllSprites.csx` dumps thousands of PNGs. Filter by keywords: `blood`, `splatter`, `gore`, `fire`, `flame`, `spark`, `smoke`, `dust`, `hit`, `impact`, `fx`, `vfx`, `grass`, `floor`, `tile`, `tree`, `bush`, `leaf`. Keep all blood/splatter sprites — these are the stamp textures for the splatmap.

**Extract blood logic:** UMT decompiles GML bytecode. Navigate to `Code`, search for scripts referencing `blood`, `splatter`, `surface_set_target`, `draw_sprite_ext`. Export the decompiled code to text files. The key algorithm is: spawn blood particle with random velocity from attack angle → when settled, call `surface_set_target(decal_surface)` → `draw_sprite_ext()` with random rotation (0–360°), scale (0.8–1.2), alpha, and color jitter → `surface_reset_target()` → destroy the particle. This is the reference for the stamp logic in Step 3.

### Step 2: Design the splatmap render pass

The splatmap is a persistent, world-locked GPU texture that accumulates sub-tile effect stamps (blood, wetness, snow). It composites over terrain but under entities during the world render pass.

**Why world-space, not screen-space:** An earlier CBN revision tried a screen-space ping-pong splatmap for rain wet spots and removed it — "a screen-space accumulator rains indoors (no sky gate) and smears across the terrain when the world scrolls" (`src/lighting/rain_effect.h:13`). The splatmap must be anchored to world coordinates, not screen coordinates.

**Unit: submap.** The splatmap is keyed per-submap (12×12 tiles, `SEEX=SEEY=12`), not per-OMT (24×24 = 2×2 submaps). Submaps are the load/unload and field-data unit, making regeneration on load natural. The OMT size (`SEEX*2 = 24`) is used elsewhere for mapgen but is NOT the right granularity here — submaps load/unload individually and field data (`fd_blood`) is stored per-submap.

**Architecture:**

```
Per-submap GPU texture (RGBA8, 96×96 = 8 sub-tile pixels per tile on a 12×12-tile submap)
   R channel: blood intensity (0–255)
   G channel: wetness (for future: rain puddles, wet footprints)
   B channel: snow/frost (for future: snow accumulation, trails)
   A channel: reserved (char tracks, scorch marks, etc.)

Math: 12 tiles × 8 px/tile = 96 px per axis
Size: 96 × 96 × 4 bytes (RGBA8) = ~36 KB per submap
Visible submaps: ~4–9 depending on reality bubble size → ~144–324 KB GPU total
```

8× sub-tile resolution gives 8 distinct stamp positions per tile axis (64 per tile), enough for organic-looking overlapping splatters without excessive memory. Tile boundaries align exactly: pixel 0 of the splatmap = tile 0, pixel 8 = tile 1, etc.

**Render pass ordering** (follows `rain_effect` pattern in `src/lighting/render_state.h`):
1. Terrain tiles drawn to `world_target` (existing)
2. **NEW: splatmap composite pass** — alpha-blends splatmap textures over `world_target`, positioned using the same `camera_off` + `tile_pixel_size` projection as `rain_effect`
3. Fields, items, entities drawn on top (existing)

### Step 3: Implement `splatmap_pass` — the GPU render pass

New files: `src/lighting/splatmap_pass.h`, `src/lighting/splatmap_pass.cpp`, `data/shaders/splatmap.vert.hlsl`, `data/shaders/splatmap.frag.hlsl`.

Follow the `rain_effect` pattern (same file structure, same init/shutdown/record lifecycle):

**`splatmap_pass` class (header):**
```cpp
struct splatmap_stamp {
    float world_x, world_y;     // sub-tile world position (e.g. 5.3, 12.7)
    float rotation;             // degrees, 0–360
    float scale;                // 0.5–1.5
    float alpha;                // 0.0–1.0
    uint8_t channel;            // 0=blood, 1=wetness, 2=snow
    uint16_t sprite_index;      // index into the stamp sprite atlas
};

struct splatmap_params {
    float camera_off_x, camera_off_y;   // world→screen projection
    float tile_pixel_size;
};
```

**Stamp atlas:** A single GPU texture containing all splatter sprite variants packed in a grid (e.g. 8×8 grid of 32×32 stamps = 256×256 atlas). Each stamp is a Stoneshard blood splatter PNG. The atlas is loaded once at init from a tileset-like PNG.

**Stamp operation:** When `map::add_splatter()` is called, instead of only modifying field intensity, it also queues a `splatmap_stamp` with randomized sub-tile offset, rotation, scale, and alpha. The `splatmap_pass::record()` method renders queued stamps into the appropriate submap splatmap textures using render-to-texture (SDL_GPU blit-to-color-target with premultiplied alpha blend). Once stamped, the stamp is consumed — the texture retains the result persistently.

**Shaders:**
- Vertex shader: fullscreen quad positioned at the stamp's world coordinates, scaled and rotated per instance.
- Fragment shader: samples the stamp atlas at the sprite_index UV region, multiplies by alpha, writes to the appropriate channel of the splatmap texture.

**Composite shader:** During the world pass, a fullscreen quad samples visible splatmap textures and alpha-blends them over terrain. Blood channel → dark red tint. Wetness → slight darkening + specular highlight. Snow → white overlay. This shader reads the R/G/B channels and applies per-channel color/blend modes.

### Step 4: Integrate splatmap with the game logic layer

**Modify `map::add_splatter()`** (`src/map.cpp:560`):
- Keep the existing field-intensity logic (fields still drive gameplay: slipperiness, NPC reactions, mopping)
- Add: call `splatmap_pass::queue_stamp()` with sub-tile position jitter. Convert the tile position + attack direction into a precise world coordinate with ±0.4 tile random offset. Randomize rotation (0–360°), scale (0.7–1.3), alpha (0.6–1.0).

**Modify `map::add_splatter_trail()`** (`src/map.cpp:577`):
- For each tile along the trail, queue 1–3 stamps with decreasing alpha and slight perpendicular scatter, simulating directional spray.

**Modify `emit_impact_particle()`** (`src/animation.cpp:589`):
- In addition to the existing short-lived particle animation, queue a permanent splatmap stamp at the impact position for the persistent decal.

**Wire into render_state:**
- Add `splatmap_pass splatmap_;` member to `render_state` (`src/lighting/render_state.h:458`)
- Call `splatmap_.init()` in `render_state::init()` alongside the other passes
- Call `splatmap_.record()` in the refresh_display world pass, after terrain draw and before field/entity draw

### Step 5: Splatmap persistence across submap load/unload

When a submap unloads (player walks away), its splatmap texture data must survive for when the submap reloads:

**Option chosen: regenerate from fields on load.** Do NOT serialize the splatmap texture to disk. Instead, when a submap loads, scan its field data for blood/wetness fields and re-stamp the splatmap with deterministic random stamps (using the tile coordinates as RNG seed). This means the exact splatter pattern won't be identical across save/load, but it will be visually equivalent. This avoids adding ~36 KB per submap to save files.

If a submap has no splatter-type fields, its splatmap texture is all-zeros (transparent) and can be skipped entirely.

**Deterministic regeneration:** For tile (x, y) with field `fd_blood` at intensity N, seed RNG with `hash(submap_abs_x, submap_abs_y, x, y)`, generate N stamps with random rotation/scale/alpha/offset. Same seed → same visual each time.

### Step 6: Terrain tile integration (grass, foliage)

**Grass/floor tiles** from Stoneshard (extracted in Step 1): resize to 32×32 with nearest-neighbor, add to the UnDeadPeople tileset (`gfx/MSX++UnDeadPeopleEdition/tile_config.json`) as additional weighted center variants for `t_grass` — extending the existing pattern at line 24280 which already has 5 weighted center sprites (13755–13759) plus full multitile edge/corner variants. Add Stoneshard grass as additional `weight` entries in the `center` array. This is purely a tileset sprite swap, no code changes.

**Foliage:** Evaluate extracted Stoneshard tree/bush sprites at CBN's 32×32 scale. Use those that fit as `f_underbrush`, `f_bush`, `f_tree_*` furniture sprites. Skip anything that clashes stylistically.

## Critical files & anchors

| File | Symbol/Region | Why |
|------|--------------|-----|
| `src/lighting/rain_effect.h` | `rain_effect` class, `rain_splash` struct | Template for the splatmap_pass: world-locked GPU rendering with camera projection, instance buffer pattern, init/shutdown/record lifecycle. **Also line 13:** documents the removed screen-space splatmap and why it failed — the new system must be world-space. |
| `src/map.cpp:560` | `map::add_splatter()` | Primary integration point: currently only modifies field intensity. Must also queue splatmap stamps. |
| `src/animation.cpp:589` | `emit_impact_particle()` | Secondary integration: emits transient sprite particles. Must additionally stamp the splatmap for persistent blood decals. |
| `src/lighting/render_state.h:437` | `world_target_`, pass ordering | Where the splatmap_pass is wired into the render pipeline — after terrain, before entities. |
| `src/particle_system.h:12` | `struct particle` | Existing transient particle system. The splatmap is a separate persistent system; particles remain for short-lived VFX, splatmap for permanent decals. |

## Verification

1. **Extraction:** After running `ExportAllSprites.csx`, `ls Export_Sprites/ | grep -ci 'blood\|splatter\|gore'` returns 20+ matching files. Open several — they should be distinct blood splatter shapes with alpha transparency.
2. **Splatmap renders:** Build CBN. Shoot a zombie at close range. Instead of a grid-locked `fd_blood` tile sprite, a sub-tile blood splatter appears at the impact position with random rotation — visually overlapping tile boundaries. The splatter persists across turns.
3. **Multiple splatters accumulate:** Kill several enemies in one area. Blood splatters overlap and build up organically, not snapping to tile centers.
4. **Persistence:** Save the game, reload. Walk back to the area. Blood splatters regenerate in visually similar (not identical) positions from the field data.
5. **No regression:** Existing field-based blood gameplay (slipperiness, NPC reactions, mopping) still works — `fd_blood` fields are still set normally.

## Assumptions & contingencies

- **Splatmap resolution 8× (96×96 per 12×12 submap):** 12 tiles × 8 sub-pixels = 96 pixels per axis. Clean alignment: pixel N maps to tile N÷8, sub-position N%8. 64 distinct sub-tile positions per tile. If visual quality is insufficient, increase to 16× (192×192, ~144 KB per submap). If GPU memory is a concern on weaker hardware, drop to 4× (48×48, ~9 KB per submap).
- **RGBA8 channel packing (R=blood, G=wetness, B=snow):** If more than 3 effect types are needed later, switch to a second splatmap texture or use RGBA16. For now, 3 channels + 1 reserved is sufficient.
- **Regenerate-from-fields persistence (no serialization):** The splatmap is visual-only; gameplay state lives in fields. If pixel-perfect persistence across save/load becomes desired later, add a compressed (zlib) texture dump to the submap save format — but this is explicitly deferred.
- **Stoneshard sprite naming:** If Stoneshard uses non-standard naming (no `spr_` prefix), browse the full list in UMT and identify blood sprites visually. The sprites are recognizable regardless of name.
- **UnDeadPeople tileset:** Stoneshard terrain/foliage sprites are added to a copy of `gfx/MSX++UnDeadPeopleEdition/`, which is the active tileset. Extends the existing `tile_config.json` (35812 lines, full multitile grass/blood/terrain variants). CBN only supports one active tileset at a time.

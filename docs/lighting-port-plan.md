# DDA Colored Lighting Port Plan for BN-Forked

## Progress

| Phase                    | Status  | Notes                                                                                                    |
| ------------------------ | ------- | -------------------------------------------------------------------------------------------------------- |
| 1. Core Data Structures  | ✅ Done | light_color_rgb struct, terrain/furniture/fields/vehicle extensions                                      |
| 2. JSON Parsing          | ✅ Done | mapdata.cpp, field_type.cpp, veh_type.cpp                                                                |
| 3. Lightmap Engine       | ✅ Done | level_cache extension, add_light_source/apply_light_source/arc color support, dual-pass fusion, box blur |
| 4. Dawn/Dusk Tint        | ✅ Done | integrated into lightmap generation + renderer overlay                                                   |
| 5. Renderer Overlay      | ✅ Done | colored light tint + dawn/dusk warm color temperature in cata_tiles.cpp                                  |
| 6a. Core BN-Forked Items | ✅ Done | ~30 entries across fields, furniture, terrain, vehicle parts                                             |
| 6b. Mod Entries          | ✅ Done | Arcana_BN (8), Jump Pad (1), MagicalNights (1)                                                           |

**All implementation phases complete.**

## Architecture Comparison: DDA vs BN-Forked

| Aspect              | DDA                                                               | BN-Forked                                                 |
| ------------------- | ----------------------------------------------------------------- | --------------------------------------------------------- |
| Shadowcasting       | Template-based `castLight<T, calc, check>` with template params   | Function-pointer `light_model` struct (cleaner for color) |
| Cache storage       | `cata::mdarray<light_color_rgb>`                                  | **std::vector<X-outer layout>** (BN convention)           |
| Light source buffer | `buffered_light_source { luminance, color }`                      | `std::vector<float>` scalar only                          |
| Tile render info    | `tile_render_info::com` with `needs_tint`, `tint_color`, `bounds` | Simple struct — no com sub-struct                         |
| Draw loop           | Row-based pre-pass → layer loop → tint overlay pass               | Z-level outer loop → tile inner loop → layers inline      |
| Vehicle luminance   | Part type name matching (headlight, taillight)                    | **bonus field** (BN style)                                |

## Decisions Made

- ✅ **Vehicle lights**: Add light_color to veh_type.h, luminance from bonus field (BN style)
- ✅ **Cache storage**: Use std::vector<X-outer layout> matching BN-Forked conventions
- ✅ **Data population**: Only existing items in BN-Forked + mods in data/mods/ + CataMods repo
- ✅ **Dawn/dusk**: Same as DDA — sun altitude + sine-eased fade, 25°→45° HSV
- ✅ **Full refactor**: Fuse dual-pass into single traversal via light_model pattern
- ✅ **Testing**: Visual testing only

---

## Phase 1: Core Data Structures (~80 lines)

### 1.1 Add `light_color_rgb` struct to `src/lightmap.h`

```cpp
// Per-tile accumulated light color energy (float RGB).
// Stored as raw energy, not display-ready; the renderer converts to uint8.
struct light_color_rgb {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;

    bool is_colored() const { return r > 0.0f || g > 0.0f || b > 0.0f; }

    light_color_rgb &operator+=(const light_color_rgb &rhs) {
        r += rhs.r; g += rhs.g; b += rhs.b;
        return *this;
    }

    light_color_rgb operator*(float scale) const {
        return { r * scale, g * scale, b * scale };
    }

    bool operator==(const light_color_rgb &rhs) const {
        return r == rhs.r && g == rhs.g && b == rhs.b;
    }
    bool operator!=(const light_color_rgb &rhs) const { return !(*this == rhs); }

    // HSV → RGB conversion (H in degrees, S/V in [0,1])
    static light_color_rgb from_hsv(float h, float s, float v);
};

// Dawn/dusk tint: returns cached warm color for twilight or empty outside.
light_color_rgb dawn_dusk_color_for_lightmap(std::string_view dimension);
```

### 1.2 Extend terrain/furniture structs in `src/mapdata.h`

Add to both terrain and furniture structs:

```cpp
light_color_rgb light_color = {};
```

Default empty (no color) — backward compatible. Existing items without `light_color` JSON field will have zero RGB → treated as white light (no tint).

### 1.3 Extend field_type intensity levels in `src/field_type.h`

Add to the intensity level struct:

```cpp
light_color_rgb light_color;
```

Inherit from previous level when not explicitly set (DDA behavior at field_type.cpp line ~250).

### 1.4 Extend vehicle parts in `src/veh_type.h`

Add to vehicle part type struct:

```cpp
light_color_rgb light_color = {};
```

Luminance still comes from the `bonus` field (BN-Forked style, not DDA's name-matching).

---

## Phase 2: JSON Data Loading (~50 lines)

### 2.1 Terrain/furniture JSON parsing in `src/mapdata.cpp`

Add to terrain and furniture loading (after existing light_emitted parser):

```cpp
if( jo.has_array( "light_color" ) ) {
    JsonArray jarr = jo.get_array( "light_color" );
    light_color.r = jarr.get_int( 0 ) / 255.0f;
    light_color.g = jarr.get_int( 1 ) / 255.0f;
    light_color.b = jarr.get_int( 2 ) / 255.0f;
}
```

### 2.2 Field type JSON parsing in `src/field_type.cpp`

Add to intensity level loading:

```cpp
if( jao.has_array( "light_color" ) ) {
    JsonArray jarr = jao.get_array( "light_color" );
    intensity_level.light_color.r = jarr.get_int( 0 ) / 255.0f;
    intensity_level.light_color.g = jarr.get_int( 1 ) / 255.0f;
    intensity_level.light_color.b = jarr.get_int( 2 ) / 255.0f;
} else {
    // Inherit from previous level (or fallback)
    intensity_level.light_color = fallback_intensity_level.light_color;
}
```

---

## Phase 3: Lightmap Engine (~400 lines modified/added)

### 3.1 Extend `level_cache` in `src/map.h`

Add to the level_cache struct (after existing light_source_buffer):

```cpp
// Accumulated colored light energy per tile. Populated during generate_lightmap
// alongside lm/sm. Zero = uncolored (white) light only.
std::vector<light_color_rgb> light_color_cache;
// True when at least one light source on this z-level has non-white color.
// Used to skip the color blur pass when all lights are white.
bool has_colored_lights = false;
```

Change `light_source_buffer` from `std::vector<float>` to a struct:

```cpp
struct buffered_light_source {
    float luminance = 0.0f;
    light_color_rgb color;
};
std::vector<buffered_light_source> light_source_buffer;
```

### 3.2 Update `level_cache` constructor in `src/map.cpp`

Add initialization matching BN-Forked's X-outer vector pattern:

```cpp
light_color_cache( static_cast<size_t>( mx * my ), light_color_rgb{} ),
```

### 3.3 Modify `add_light_source()` signature and implementation

New signature:

```cpp
void map::add_light_source(const tripoint &p, float luminance, const light_color_rgb &color = {})
```

Implementation changes:

- Store color in buffer alongside luminance (brighter sources dominate hue via additive accumulation weighted by luminance)
- Set `cache.has_colored_lights = true` when non-empty color provided

### 3.4 Modify `apply_light_source()` and `apply_light_arc()` signatures

Add color propagation parameter:

```cpp
void map::apply_light_source(const tripoint &p, float luminance, const light_color_rgb &color = {})
void map::apply_light_arc(const tripoint &p, units::angle angle, float luminance,
                          units::angle wideangle, const light_color_rgb &color = {})
```

**Key change: Add color model to k_light_model.** BN-Forked uses function-pointer-based `light_model` — we add parallel "color update" functions:

```cpp
// Color update callback for castLight. Per-channel max prevents octant
// boundary seams (two adjacent octants hitting the same tile would double
// the color with +=, but scalar light uses max() so it stays clean).
static void update_light_color(light_color_rgb &tile_color, const float &intensity, quadrant) {
    const light_color_rgb contrib = g_current_source_color * intensity;
    tile_color.r = std::max(tile_color.r, contrib.r);
    tile_color.g = std::max(tile_color.g, contrib.g);
    tile_color.b = std::max(tile_color.b, contrib.b);
}

// Extended light_model with color update function pointer:
static const light_model k_light_model_with_color = {
    light_calc, light_check, nullptr, update_light_quadrants, light_from_lookup,
    accumulate_transparency,  // existing fields
    &update_light_color       // NEW: color update callback
};
```

**Actually, cleaner approach:** Since BN-Forked's `light_model` uses function pointers and the output cache type is determined at call site (float* vs four_quadrants*), we can add a parallel color propagation path that runs alongside the scalar castLight. The key insight from studying BN-Forked's architecture:

1. `castLightOctants_q()` writes to `four_quadrants *output_cache`
2. We pass a pointer to `light_color_rgb *color_cache` as an optional extra parameter
3. Inside the shadowcasting recursion, when writing to output_cache, also write to color_cache if non-null

This avoids modifying the light_model struct and keeps the change localized.

### 3.5 Add 3x3 box blur on color cache (conditional)

In `generate_lightmap()`, after all light sources are applied:

```cpp
if( map_cache.has_colored_lights ) {
    // 3x3 box blur to soften octant boundary seams
    auto &lcc = map_cache.light_color_cache;
    std::vector<light_color_rgb> blur_buf(lcc.size(), light_color_rgb{});
    for(int x = 1; x < cache_x - 1; ++x) {
        for(int y = 1; y < cache_y - 1; ++y) {
            if(!lcc[x * cache_y + y].is_colored()) continue;
            light_color_rgb sum; int count = 0;
            for(int dx = -1; dx <= 1; ++dx)
                for(int dy = -1; dy <= 1; ++dy) {
                    sum += lcc[(x+dx) * cache_y + (y+dy)];
                    ++count;
                }
            blur_buf[x * cache_y + y] = sum * (1.0f / count);
        }
    }
    for(int x = 1; x < cache_x - 1; ++x)
        for(int y = 1; y < cache_y - 1; ++y)
            if(blur_buf[x * cache_y + y].is_colored())
                lcc[x * cache_y + y] = blur_buf[x * cache_y + y];
}
```

---

## Phase 4: Dawn/Dusk Tint (~120 lines added)

### 4.1 Integrate dawn/dusk into lightmap generation in `src/lightmap.cpp`

Add cached twilight color function (same as DDA):

```cpp
static light_color_rgb cached_twilight_color() {
    static time_point cached_turn = calendar::before_time_starts;
    static light_color_rgb cached_color;
    if(cached_turn == calendar::turn) return cached_color;
    cached_turn = calendar::turn;
    if(!is_twilight(calendar::turn)) { cached_color = {}; return cached_color; }

    const units::angle alt = sun_azimuth_altitude(calendar::turn).second;
    constexpr float lo = -6.0f, hi = -1.0f;
    const float progress = std::clamp(
        static_cast<float>(to_degrees(alt) - lo) / (hi - lo), 0.0f, 1.0f);
    const float hue = 25.0f + progress * 20.0f; // orange→gold
    const float ease = std::sin(progress * M_PI);
    return light_color_rgb::from_hsv(hue, 0.8f, 1.0f) * (ease * 0.35f);
}
```

Apply during sunlight phase of `generate_lightmap()` (before artificial sources):

- Compute twilight color once per turn via caching
- For each tile with sunlight reaching it (lm max > indoor baseline), add tint
- Uses per-channel max blending (same as colored lights)
- Respects visibility, roofs, windows — rides same paths as sunlight

---

## Phase 5: Renderer Overlay (~150 lines added to cata_tiles.cpp)

### 5.1 Add colored light overlay rendering in `src/cata_tiles.cpp`

**Insertion point:** After the main z-level/tile drawing loop (around line ~3540), before zone overlays and override drawing.

The approach differs from DDA because BN-Forked's draw loop is structured differently:

- DDA: Row-based pre-pass → layer loop → tint overlay pass
- BN-Forked: Z-level outer → tile inner → layers inline

**BN-Forked approach:** After all terrain/furniture/creature layers are drawn for a z-level, iterate the visible tiles and draw colored rects on top. This is simpler than DDA's pre-pass because we don't need bounds tracking — we just draw SDL rects over already-rendered tiles.

```cpp
// Colored light overlay — after all sprite layers for this z-level
const level_cache &zlev_cache = here.access_cache(cur_zlevel);
if(zlev_cache.has_colored_lights && !iso_mode) {
    for(const tile_render_info &p : draw_points) {
        if(p.pos.z != cur_zlevel) continue;
        if(p.ll == lit_level::DARK || p.ll == lit_level::BLANK || p.ll == lit_level::MEMORIZED)
            continue;

        const light_color_rgb &lc = zlev_cache.light_color_cache[map_cache.idx(p.pos.x, p.pos.y)];
        if(!lc.is_colored()) continue;

        // Extract chromatic (saturated) component — white light produces no tint
        const float min_ch = std::min({lc.r, lc.g, lc.b});
        const float sat_r = lc.r - min_ch;
        const float sat_g = lc.g - min_ch;
        const float sat_b = lc.b - min_ch;
        const float sat_mag = std::max({sat_r, sat_g, sat_b});
        if(sat_mag < 0.01f) continue;

        // Alpha: saturated energy relative to total scalar light
        const four_quadrants &lm_val = zlev_cache.lm[map_cache.idx(p.pos.x, p.pos.y)];
        const float scalar = lm_val.max();
        const float ratio = scalar > 0.1f ? std::min(1.0f, sat_mag / scalar) : 0.0f;
        const Uint8 alpha = static_cast<Uint8>(ratio * 80.0f);
        if(alpha == 0) continue;

        // Normalize saturated color to full brightness for SDL tint
        const SDL_Color tint = {
            static_cast<Uint8>(sat_r / sat_mag * 255.0f),
            static_cast<Uint8>(sat_g / sat_mag * 255.0f),
            static_cast<Uint8>(sat_b / sat_mag * 255.0f),
            alpha
        };

        const point screen = player_to_screen(p.pos.xy());
        const SDL_Rect draw_rect{screen.x, screen.y - p.height_3d, tile_width, tile_height};
        SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        geometry->rect(renderer, draw_rect, tint);
        SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    }
}
```

### 5.2 Add dawn/dusk overlay rendering in `src/cata_tiles.cpp`

After the colored light overlay pass (same insertion point):

```cpp
// Dawn/dusk warm color temperature overlay on outside tiles during twilight
if(is_twilight(calendar::turn) && cur_zlevel >= 0) {
    const auto &outside_cache = here.access_cache(cur_zlevel).outside_cache;
    // ... iterate visible tiles, check outside_cache, draw warm tint
}
```

### 5.3 Isometric mode support

Skip colored light tint overlay when `iso_mode` is true (same as DDA's 952ae182bf commit). The reason: isometric rendering uses a silhouette mask path that requires render target switches — too expensive for the current SDL setup.

---

## Phase 6: JSON Data Population (~30+ entries)

### Existing BN-Forked items with light_emitted → add light_color:

**Fields (field_type.json):**

| Field               | Color [R,G,B]   | Rationale                     |
| ------------------- | --------------- | ----------------------------- |
| small fire / fire   | [255, 100, 30]  | Warm orange flame             |
| raging fire         | [255, 60, 20]   | Hotter, redder                |
| angular ripple      | [200, 220, 255] | Cold blue-white               |
| fire vent           | [255, 120, 40]  | Magma glow                    |
| sparks              | [95, 204, 247]  | Electric cyan (DDA match)     |
| electric crackle    | [146, 217, 245] | Electric blue (DDA match)     |
| electric cloud      | [182, 231, 250] | Brighter electric (DDA match) |
| faint plasma        | [255, 100, 255] | Faint magenta                 |
| glowing plasma      | [255, 150, 255] | Pink plasma                   |
| glaring plasma      | [255, 200, 255] | Bright pink                   |
| spotlight           | [255, 250, 230] | Warm white                    |
| dazzling            | [255, 200, 100] | Yellow-white                  |
| swirl of fog        | [200, 210, 220] | Cool gray                     |
| smoke               | [180, 170, 160] | Warm gray (incendiary)        |
| airborne incendiary | [255, 100, 30]  | Hot orange                    |
| lava                | [255, 80, 20]   | Magma red-orange              |

**Furniture/Terrain:**

| Item ID                     | Color [R,G,B]   | Rationale                      |
| --------------------------- | --------------- | ------------------------------ |
| f_floor_lamp_on             | [255, 240, 210] | Warm incandescent              |
| f_grid_floodlight_on        | [230, 240, 255] | Cool white floodlight          |
| f_street_light_rewired_on   | [255, 200, 120] | Sodium vapor orange            |
| f_space_heater_on           | [255, 80, 30]   | Red glow heater                |
| f_space_heater_large_on     | [255, 60, 20]   | Hotter large heater            |
| f_olight_on                 | [240, 250, 255] | Fluorescent white              |
| t_thconc_floor_olight       | [240, 250, 255] | Fluorescent ceiling            |
| t_floor_olight              | [240, 250, 255] | Fluorescent ceiling            |
| t_metal_floor_olight        | [240, 250, 255] | Fluorescent ceiling            |
| t_utility_light             | [230, 245, 255] | Cool industrial                |
| f_hot_spring                | [180, 220, 255] | Steamy blue-white              |
| f_alien_tendril             | [0, 255, 100]   | Alien green (DDA match)        |
| f_alien_anemone             | [255, 230, 210] | Warm alien bioluminescence     |
| f_alien_pod_organ           | [255, 230, 210] | Same as anemone                |
| f_alien_table               | [255, 0, 255]   | Purple alien altar (DDA match) |
| f_alien_nerve               | [255, 230, 210] | Warm nerve cluster             |
| f_console_table             | [120, 200, 255] | Monitor blue glow              |
| f_artifact_analyzer_console | [120, 200, 255] | Monitor blue glow              |
| f_glowingbulb               | [200, 255, 200] | Plant bioluminescence green    |
| t_lava                      | [255, 80, 20]   | Magma red-orange               |
| t_console                   | [120, 200, 255] | Monitor blue glow              |
| t_dimensional_portal        | [200, 100, 255] | Purple portal (DDA match)      |
| f_emitter_migo              | [0, 255, 0]     | Migo green (DDA match)         |

**Migo furniture (furniture-migo.json):**

| Item                        | Color [R,G,B] | Rationale           |
| --------------------------- | ------------- | ------------------- |
| migo altar/emitter variants | [0, 255, 0]   | Classic alien green |

**Terrain:**

| Item ID                                                       | Color [R,G,B]   | Rationale                  |
| ------------------------------------------------------------- | --------------- | -------------------------- |
| t_thconc_floor_olight / t_floor_olight / t_metal_floor_olight | [240, 250, 255] | Fluorescent ceiling lights |
| t_utility_light                                               | [230, 245, 255] | Cool industrial lighting   |
| t_lava                                                        | [255, 80, 20]   | Magma glow                 |
| t_console                                                     | [120, 200, 255] | Monitor screen glow        |
| t_dimensional_portal                                          | [200, 100, 255] | Purple portal energy       |

### BN-Forked mods (data/mods/):

**Arcana_BN:**

| Item                         | Color [R,G,B]   | Rationale          |
| ---------------------------- | --------------- | ------------------ |
| Arcana field "strange light" | [180, 120, 255] | Purple arcane glow |
| Arcana furniture (various)   | [180, 120, 255] | Purple arcane      |
| Arcana terrain               | [180, 120, 255] | Purple arcane      |

**Jump Pad:**

| Item                | Color [R,G,B]   | Rationale             |
| ------------------- | --------------- | --------------------- |
| Jump pad item light | [100, 255, 100] | Green activation glow |

**MagicalNights:**

| Item                    | Color [R,G,B]   | Rationale         |
| ----------------------- | --------------- | ----------------- |
| MagicalNights furniture | [200, 150, 255] | Purple magic glow |

### CataMods repo items (only BN-compatible mods):

**cdda-arcana-mod:**

- Arcana fields → purple [180, 120, 255]
- Arcana furniture → purple [180, 120, 255]
- Arcana terrain → purple [180, 120, 255]

**Fallout-CDDA-Remastered:**

- Fallout-themed light sources → appropriate colors (yellow warning lights, blue terminal glow)

**BrightNights-Structured-Kenan-Modpack:**

- BL9 furniture/terrain → match DDA equivalents where applicable
- Hydroponics → green grow lights [100, 255, 100]
- Steampunk → warm brass/orange [255, 180, 80]

---

## Phase 7: Performance Optimization (~40 lines)

### 7.1 Distance LUT for castLight inner loop (optional, follow-up)

Precomputed 61x61 table replacing sqrt calls in the shadowcasting distance calculation. Eliminates sqrt from hottest code path. Can be done as a follow-up PR after core color system is stable.

### 7.2 Skip lightmap rebuild when nothing changed

BN-Forked already has `lightmap_dirty` flag — extend to skip color blur pass too (already conditional on `has_colored_lights`).

---

## Phase 8: Testing (Visual)

- Test colored light propagation through transparent/semi-transparent terrain
- Test multiple colored sources mixing on same tile
- Test dawn/dusk tint at various sun altitudes
- Test isometric mode (should skip tint overlay)
- Test performance with many colored lights (fire + electric fields)

---

## File Change Summary

| File                 | Changes                                                                                         | Lines               |
| -------------------- | ----------------------------------------------------------------------------------------------- | ------------------- |
| `src/lightmap.h`     | +light_color_rgb struct, dawn_dusk declaration                                                  | ~80 added           |
| `src/mapdata.h`      | +light_color on terrain and furniture structs                                                   | ~2 added            |
| `src/field_type.h`   | +light_color on intensity levels                                                                | ~1 added            |
| `src/veh_type.h`     | +light_color on vehicle parts                                                                   | ~1 added            |
| `src/map.h`          | +light_color_cache, has_colored_lights, buffered_light_source struct                            | ~15 added           |
| `src/lightmap.cpp`   | Major: add_light_source/apply_light_source/arc color support, dual-pass fusion, blur, dawn/dusk | ~400 modified/added |
| `src/cata_tiles.cpp` | Colored overlay rendering, dawn/dusk overlay, iso_mode skip                                     | ~150 added          |
| `src/mapdata.cpp`    | JSON parsing for light_color on terrain/furniture                                               | ~20 added           |
| `src/field_type.cpp` | JSON parsing for light_color on fields                                                          | ~15 added           |
| `src/map.cpp`        | level_cache constructor init for new vectors                                                    | ~3 added            |
| `data/json/*.json`   | ~30+ entries with light_color values                                                            | ~60 lines           |
| `data/mods/*/`       | Light color entries in BN mods + CataMods                                                       | ~20 entries         |

**Total: ~750-800 lines, 10-12 files modified/added**

---

## Implementation Order (Recommended)

1. **Phase 1**: Data structures (light_color_rgb struct, extend terrain/furniture/fields/vehicles)
2. **Phase 2**: JSON parsing (mapdata.cpp, field_type.cpp)
3. **Phase 3**: Lightmap engine (level_cache, add_light_source, apply_light_source, castLight integration) ← **hardest part**
4. **Phase 6a**: Core BN-Forked items (fire, electric fields, key appliances, Migo furniture) — ~15 entries
5. **Phase 4**: Dawn/dusk tint
6. **Phase 5**: Renderer overlay in cata_tiles.cpp
7. **Phase 6b**: Remaining mods (Arcana_BN, Jump Pad, MagicalNights, CataMods items)

This order lets you test the engine before adding visual polish. Phase 3 + 6a gives you colored fire and electric fields — immediate visual payoff to verify correctness.

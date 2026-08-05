# Cloud Shadow Shader

## Context

The game has no spatial cloud-cover effect: `weather_cloud_mult()` (`src/sdl_render_frame.cpp:354`) already dims `sun_intensity` as a **flat, whole-scene** scalar keyed to the current weather type (overcast vs. clear), but nothing produces the classic **moving, patchy shadow shapes** that drift across the ground as clouds pass overhead — the effect referenced (Stardew Valley, RimWorld, and similar top-down survival/colony games all animate this). The ask is to add that: a procedural, animated cloud-shadow overlay on terrain during the day.

End state: `sprite.frag.hlsl` computes a per-pixel, world-locked, time-animated noise field and multiplies it onto the **sun contribution only** (clouds block direct sunlight; ambient sky-light is untouched), so the effect is an exact no-op at night, composes correctly underneath `weather_cloud_mult()`, and is live-tunable from the F4 dev panel exactly like every other knob in this renderer (`ao_strength`, `nrm_radial_amount`, etc.). It ships **on** by default at a moderate strength, following the established precedent of `debug_params` fields defaulting to their shipping look (e.g. `ao_strength = 0.35f`, `g_rain_enable = true`).

## Approach

All steps touch the same wire-stable structure (`DebugParams`/`debug_params`) and must land together for the tree to build; there is no meaningful sub-ordering — do them in the order below because each later step depends on the field names introduced in step 1.

### 1. Add the 8 new fields to `debug_params` (C++ struct — source of truth for defaults)

Edit `src/lighting/sprite_batcher.h`. `debug_params` currently ends with `nrm_radial_amount` (last field, struct closes at line 284 in the read snapshot — re-read to confirm before editing since line numbers shift). Append, in this exact order and with these exact names/defaults/types (all `float`, matching every other field in the struct — no `uint32_t`):

```cpp
    // Passing cloud shadows (procedural, moving noise pattern of cloud coverage
    // drifting over the terrain). Multiplies the SUN contribution only — clouds
    // block direct sunlight, ambient sky-light is unaffected — so this is an
    // exact no-op at night (sun_intensity=0) and stacks correctly UNDER
    // weather_cloud_mult() (src/sdl_render_frame.cpp): that is a flat whole-sky
    // multiplier for the current weather TYPE; this is the moving SHAPE of
    // individual clouds passing overhead on top of it.
    float cloud_strength = 0.35f;  // 0=off .. 1=full darkening under a cloud (ships ON)
    float cloud_scale = 0.03f;     // noise frequency, tiles^-1 (~33-tile cloud period)
    float cloud_wind_x = 0.4f;     // cloud drift velocity, world tiles/second, x
    float cloud_wind_y = 0.15f;    // cloud drift velocity, world tiles/second, y
    float cloud_threshold = 0.55f; // noise cutoff where a cloud begins casting shadow
    float cloud_softness = 0.15f;  // feather width of the cloud edge around cloud_threshold
    float cloud_pad0 = 0.0f;       // reserved: keeps DebugParams a multiple of 16 bytes
    float cloud_pad1 = 0.0f;       // reserved: keeps DebugParams a multiple of 16 bytes
```

This grows `debug_params` from 224 to 256 bytes (14×16 → 16×16 — HLSL cbuffers pack plain scalars 4-per-16-byte-row with no gaps, so any multiple of 4 fields keeps both sides' offsets identical; 8 new fields keeps the total a clean multiple of 16, matching the project's existing padding convention seen in `light_params::lp_sun_pad`, `sun_params::sp_pad`, `frame_params::pad`). The two pad floats are required, not decorative — declare them exactly as shown, always zero, never read.

No other C++ plumbing changes: `sprite_batcher_impl::set_lighting_resources()` (`src/lighting/sprite_batcher.cpp:307-311`) already does a wholesale `lp_debug = *dbg;` struct copy, and `end_pass()` pushes `&lp_dbg_use, sizeof(lp_dbg_use)` (`src/lighting/sprite_batcher.cpp:770,783-784`) — both automatically carry the new fields once the struct grows. `sdl_render_frame.cpp:698` (`in.debug = g_dbg_params;`) likewise copies the whole struct, so the new fields' defaults reach the GPU with zero additional wiring.

### 2. Update the wire-stability assertion

Edit `src/lighting/sprite_batcher.cpp`. Change:
```cpp
static_assert(sizeof(debug_params) == 224, "debug_params wire-stable with DebugParams cbuffer");
```
to `256`. This is the only C++ file besides step 1 that needs a change for the struct growth itself.

### 3. Mirror the 8 fields into both HLSL `DebugParams` declarations, same order

Both `data/shaders/lighting/src/sprite.vert.hlsl` and `data/shaders/lighting/src/sprite.frag.hlsl` declare `cbuffer DebugParams` member-for-member with the C++ struct (see the extensive "declared here for cbuffer layout parity" comments already in both files for `spec_strength`, `light_eps`, `sky_dirs`, etc. — those are fields the vertex shader never reads but must still declare so later fields land at the right byte offset). There is no automated test enforcing `DebugParams` parity (unlike `sprite_instance`, which `tests/sprite_instance_wire_test.cpp` parses and checks byte-for-byte — that test is scoped to `SpriteInstance` only and is not touched by this change), so get this right by hand, in both files, in the same order as step 1:

```hlsl
    float cloud_strength;
    float cloud_scale;
    float cloud_wind_x;
    float cloud_wind_y;
    float cloud_threshold;
    float cloud_softness;
    float cloud_pad0;
    float cloud_pad1;
```

Append immediately after `nrm_radial_amount;` and before the cbuffer's closing `};` — in `sprite.vert.hlsl` that is line 110→111 in the read snapshot; in `sprite.frag.hlsl` it is line 172→173. Re-read both files immediately before editing to get current line numbers (this plan's earlier reads already confirm the field is last in both). In `sprite.vert.hlsl` these 8 fields are parity-only (unused by that shader, same treatment as the existing tail fields there) — add a one-line comment matching the file's established phrasing, e.g. `// Cloud shadow knobs are fragment-stage only; declared here for cbuffer layout parity.`

### 4. Implement the noise + cloud-shadow term in `sprite.frag.hlsl`

No GPU-side noise primitive exists anywhere in this shader or `attenuation.hlsl`/`shadow_trace.hlsl` (confirmed: the only procedural pattern in the fragment shader is the fixed 4×4 Bayer dither table `k_bayer4`, which is a quantization matrix, not a noise field) — this is new code with no existing equivalent to reuse.

**4a. Add three helper functions** immediately after `dither_threshold()` closes (currently ends at line 365, right before the `// --- Step 7 palette shade ramp helpers ---` comment). Insert:

```hlsl
// ---- Cloud shadow noise -----------------------------------------------
// Hash + bilinear value-noise + 3-octave fbm. No existing GPU noise primitive
// in this file (only the fixed Bayer dither matrix above); this is the shape
// generator for the passing-cloud-shadow term applied to sun_contrib in main().
float cloud_hash(float2 p) {
    return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453123);
}
float cloud_value_noise(float2 p) {
    const float2 i = floor(p);
    const float2 f = frac(p);
    const float  a = cloud_hash(i);
    const float  b = cloud_hash(i + float2(1.0, 0.0));
    const float  c = cloud_hash(i + float2(0.0, 1.0));
    const float  d = cloud_hash(i + float2(1.0, 1.0));
    const float2 u = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}
float cloud_fbm(float2 p) {
    const float2x2 rot = float2x2(0.8, 0.6, -0.6, 0.8);
    float sum = 0.0;
    float amp = 0.55;
    for(int o = 0; o < 3; ++o) {
        sum += amp * cloud_value_noise(p);
        p = mul(rot, p) * 2.02;
        amp *= 0.5;
    }
    return sum;
}
```

**4b. Compute the multiplicative factor once**, right after the ambient-occlusion block closes (currently `ao_strength` block ends at line 906, immediately before the `// Ambient floor.` comment). Insert:

```hlsl
    // ---- Passing cloud shadows (procedural, animated) -----------------------
    // Sampled at shade_pos — the same world-locked, art-texel-quantised position
    // sdf_bilinear/indirect_bilinear use — so the pattern sticks to the ground on
    // scroll and to the tile-base for tall sprites. Animated by anim_time (wrapped
    // render seconds, injected per-frame — see sprite_batcher.h) x cloud_wind, so
    // clouds visibly drift instead of shimmering on scroll or standing still.
    // cloud_strength=0 is an exact no-op: the branch is skipped entirely and
    // cloud_mul stays 1.0, matching every other knob in this shader (ao_strength,
    // gi_strength, etc.).
    float cloud_mul = 1.0;
    if(cloud_strength > 0.001) {
        const float2 cloud_cp = (shade_pos + float2(cloud_wind_x, cloud_wind_y) * anim_time) * cloud_scale;
        const float  cloud_n  = saturate(cloud_fbm(cloud_cp));
        const float  cloud_cover = smoothstep(cloud_threshold - cloud_softness,
                                              cloud_threshold + cloud_softness, cloud_n);
        cloud_mul = lerp(1.0, 1.0 - saturate(cloud_strength), cloud_cover);
    }
```

**4c. Apply it to the sun term only.** Inside the existing `if(sun_intensity > 0.001 && sun_sky_vis > 0.05 && sdf_map_w > 0u) { ... }` block, the last statement is currently:
```hlsl
        sun_contrib += float3(sun_r, sun_g, sun_b) * sun_intensity
                       * sun_shadow * sun_sky_vis * mask_term * sun_spec;
    }
```
Add one line right before that block's closing `}`:
```hlsl
        sun_contrib *= cloud_mul;
    }
```
Placing it inside this gate (rather than unconditionally on `sun_contrib` outside) means cloud shadows only ever attenuate light that would otherwise reach the fragment from the sun — never fabricate darkening on an already-shadowed or indoor fragment, and never touch `sky_contrib`, `emitter_light`, or `gpu_total`'s ambient floor. Do **not** apply `cloud_mul` to `sky_contrib` or the ambient floor — clouds occlude the sun's direct disk, not the whole hemisphere's scattered skylight; conflating the two would also double-darken already-overcast weather on top of `weather_cloud_mult()`.

**4d. Add a debug visualization mode** so the effect is inspectable in isolation, following the established one-mode-per-feature convention (modes 1–16 already cover every other lighting term). Insert a new `else if` branch immediately after the existing `debug_mode == 16u` branch closes (currently ends with `dbg_opaque = 1.0;` followed by the block's closing `}`, right before `if(replace) { final_rgb = vis; }`):
```hlsl
        } else if(debug_mode == 17u) {
            // Cloud-shadow view: grayscale of cloud_mul, the factor actually
            // applied to sun_contrib. White = no cloud overhead; darkens toward
            // (1 - cloud_strength) under a cloud. Independent of sun/sky colour,
            // ambient, GI, dither. Uniform white everywhere = cloud_strength is 0.
            vis = float3(cloud_mul, cloud_mul, cloud_mul);
            replace = true;
        }
```
This does not need to join `dbg_ungated` (`debug_mode == 8u || debug_mode == 16u`) — modes 8 and 16 bypass the tint gate for reasons specific to them (main-menu backdrop, categorical alpha); mode 17 should be gated like every other mid-range mode (1–7, 9–15), visible only on world-lit (`mode_gpu_lit`) fragments, which is already how `dbg_active` is computed with no changes needed.

### 5. Wire the new knobs into the F4 dev panel

**5a.** Edit `src/sdl_lighting_devui.cpp`. Add six `c.Bind(...)` calls (the two pad fields are never bound — they are not knobs) immediately after the existing `c.Bind( "nrm_radial_amount", &g_dbg_params.nrm_radial_amount );` (line 609):
```cpp
    c.Bind( "cloud_strength", &g_dbg_params.cloud_strength );
    c.Bind( "cloud_scale", &g_dbg_params.cloud_scale );
    c.Bind( "cloud_wind_x", &g_dbg_params.cloud_wind_x );
    c.Bind( "cloud_wind_y", &g_dbg_params.cloud_wind_y );
    c.Bind( "cloud_threshold", &g_dbg_params.cloud_threshold );
    c.Bind( "cloud_softness", &g_dbg_params.cloud_softness );
```

**5b.** In the same file, append `"cloud shadow"` to the `g_dbg_mode_names` initializer (currently ends `..., "vision frontier", "light mode"` around lines 913-916):
```cpp
    g_dbg_mode_names = {
        "off",     "ambient", "emitter", "sun",         "sky", "total",      "SDF",    "sky_vis",
        "emit_bw", "normal",  "AO",      "shadow mask", "GI",  "sky access", "sun occ",
        "vision frontier", "light mode", "cloud shadow"
    };
```
(Re-read the exact current literal before editing — this plan quotes it from an earlier read and the closing brace/formatting must match exactly.)

**5c.** Edit `src/sdl_input.cpp`. The F7 key handler cycles `debug_mode` with a hardcoded modulus (line 591): `g_current_dbg_mode = ( g_current_dbg_mode + 1 ) % 17u;`. Change `17u` to `18u` so F7 cycling reaches the new mode 17, and update the preceding comment (line 586, currently `// F7: cycle debug visualization mode (0-16). Modes include:`) to say `(0-17)` and add `17 = cloud shadow (cloud_mul, the sun-darkening factor)` to the enumerated list.

**5d.** Edit `data/gui/devui.rml`. Add six slider rows immediately after the existing `ambient occlusion` row (line 302), before the `night floor` row, following the exact markup pattern already used there (`<div class="devui-row">Label <input type="range" min="..." max="..." step="..." data-value="field_name"/> <span class="devui-val">{{field_name}}</span></div>`):
```html
                <div class="devui-row">cloud strength <input type="range" min="0" max="1" step="0.01" data-value="cloud_strength"/> <span class="devui-val">{{cloud_strength}}</span></div>
                <div class="devui-row">cloud scale <input type="range" min="0.005" max="0.2" step="0.005" data-value="cloud_scale"/> <span class="devui-val">{{cloud_scale}}</span></div>
                <div class="devui-row">cloud wind x <input type="range" min="-2" max="2" step="0.05" data-value="cloud_wind_x"/> <span class="devui-val">{{cloud_wind_x}}</span></div>
                <div class="devui-row">cloud wind y <input type="range" min="-2" max="2" step="0.05" data-value="cloud_wind_y"/> <span class="devui-val">{{cloud_wind_y}}</span></div>
                <div class="devui-row">cloud threshold <input type="range" min="0" max="1" step="0.01" data-value="cloud_threshold"/> <span class="devui-val">{{cloud_threshold}}</span></div>
                <div class="devui-row">cloud softness <input type="range" min="0.01" max="0.5" step="0.01" data-value="cloud_softness"/> <span class="devui-val">{{cloud_softness}}</span></div>
```
No further RML wiring is needed — `attach_slider_wheel()` (`src/sdl_lighting_devui.cpp:490`) discovers every `<input type="range">` in the document generically via `doc.GetElementsByTagName(inputs, "input")`.

## Critical files & anchors

- `src/lighting/sprite_batcher.h` — `struct debug_params` (defaults + field order = the single source of truth every other file mirrors).
- `data/shaders/lighting/src/sprite.frag.hlsl` — `cbuffer DebugParams` (must mirror step 1 exactly), `dither_threshold()` (insertion point for noise helpers), the `sun_intensity > 0.001` block (~line 841-872, insertion point for `cloud_mul` application), `debug_mode == 16u` branch (~line 1237-1252, insertion point for the new view).
- `data/shaders/lighting/src/sprite.vert.hlsl` — `cbuffer DebugParams` (parity-only mirror, unused by this shader).
- `src/sdl_lighting_devui.cpp` — `g_dbg_mode_names` initializer and the `c.Bind(...)` block inside `devui_rml_open()`.
- `src/sdl_input.cpp` — F7 handler's `% 17u` modulus, the one place a debug-mode count is hardcoded outside the name list.

## Verification

1. **Build.** `cmake --build --preset osx-arm-slim --target cataclysm-bn-tiles cata_test-tiles`, run as a background job with a 1200s+ timeout per AGENTS.md's hard build rules (never kill mid-run; a killed build corrupts `.ninja_deps`). A clean build proves the C++ struct/static_assert changes compile; HLSL is compiled at runtime by SDL_shadercross, not at C++ build time, so this step alone does not validate the shader edits.
2. **Wire-format regression (existing, must still pass unmodified).** `./out/build/osx-arm-slim/tests/cata_test-tiles "[lighting]"` — runs `sprite_instance_wire_test` (parses both HLSL `SpriteInstance` declarations against the canonical list). This change does not touch `sprite_instance`, so this must pass exactly as before; a failure here means an edit accidentally landed in the wrong struct/cbuffer.
3. **Shader-compile smoke test (exercises the NEW code directly).** `./out/build/osx-arm-slim/tests/cata_test-tiles "[.gpu][render]"` — runs `render_regression_test`, which builds a real `sprite_batcher` pipeline (compiling `sprite.vert.hlsl`/`sprite.frag.hlsl` via SDL_shadercross) and renders sprites through it. This is the first point where a cbuffer-size mismatch between the C++ struct and either HLSL declaration, or an HLSL syntax error in the new noise functions, would surface as a pipeline-creation or shader-compile failure rather than a silent visual bug. If `get_harness()` reports no GPU context available (`h.rs` null), the test skips rather than failing — in that case step 4 is the only proof and is mandatory, not optional.
4. **In-game visual proof (the actual new behavior).** Launch the installed build and reach outdoor gameplay during daytime (`sun_intensity > 0`, e.g. via the debug menu's "quit to test" time-set or `--world`/save-load into a save near noon). Open the F4 dev panel (F4 key):
   - Select debug mode **"cloud shadow"** (index 17, via F7 cycling or the panel dropdown). Take a screenshot, wait ~15 real seconds (`anim_time` default `cloud_wind_x=0.4` tiles/s drifts the pattern visibly at the shipped `cloud_scale=0.03`), take a second screenshot. The two must differ (soft grayscale blobs shifted position) — proves the noise animates and is not a static or all-white/all-black degenerate result. A uniform white frame means `cloud_strength` read as 0 (a wiring bug); a uniform mid-gray with no variation means the noise function itself is degenerate.
   - Switch back to debug mode "off" (index 0) with the shipped default `cloud_strength=0.35`. Take a screenshot, wait ~15 seconds, take a second screenshot, both outdoors in full daylight with no player/camera movement between them. Pixel-diff the pair (mirrors the existing `cbn-shader-ab-determinism-gate` / `cbn-ab-measurement-validity` project convention for proving a shader effect is real): expect a nonzero, spatially-patchy difference confined to sunlit ground/wall tiles, not a uniform whole-frame shift (which would indicate a bug elsewhere, e.g. camera drift) and not exactly zero (which would mean the effect never reached `sun_contrib`).
   - Confirm the null case: set the in-game clock to midnight (`sun_intensity = 0`) via the debug menu, confirm two screenshots ~15 seconds apart in mode "off" are pixel-identical modulo any unrelated night-time animation (foliage sway, emitters) — proves `cloud_mul` is correctly gated inside the `sun_intensity > 0.001` block and contributes nothing at night.

## Assumptions & contingencies

- **Default shipped strength (`cloud_strength = 0.35f`).** Chosen to match the project's precedent of shipping cosmetic GPU knobs on-by-default at a moderate value (`ao_strength = 0.35f` is the direct analog). If this reads as too strong or too subtle in the step-4 visual check, it is a single-line default change in `src/lighting/sprite_batcher.h` — no structural rework needed.
- **Cloud size/speed defaults (`cloud_scale = 0.03f`, `cloud_wind_x = 0.4f`, `cloud_wind_y = 0.15f`).** Picked for a ~33-tile cloud period drifting mostly horizontally, timed to cross a typical viewport over roughly a minute of real time. These are exactly the six values exposed on the F4 panel (step 5), so any retuning during the visual check in Verification step 4 is a live slider drag, not a code change.
- **No Options-menu / `ANIMATIONS` gating.** Checked the precedent directly: `g_rain_enable` (the closest analog — a GPU weather visual effect) is a pure F4-debug global defaulting `true`, never read from `get_option<bool>(...)`, and `ANIMATION_RAIN` in `options_registration.cpp` only gates a separate curses/ASCII rain animation path, not the GPU `rain_effect`. Cloud shadows follow the same pattern: ships on via the `debug_params` default, tunable only via F4, no new `OPTIONS.json` entry. If a player-facing on/off toggle is later wanted, that is new scope beyond this ask.
- **No compute pass / separate render pass.** The effect is folded directly into `sprite.frag.hlsl`'s existing per-fragment sun term rather than a new fullscreen post-process pass (like `bloom_pass`/`volumetric_pass`). This was chosen because the sun contribution is already computed per-fragment with exactly the world-position and day/night data the cloud term needs, avoiding a new pipeline, new render target, and new pass-ordering slot in `render_state`/`refresh_display` for what is a single multiplicative scalar. If a future requirement needs clouds to also tint `sky_contrib` or cast onto UI-independent layers, that would justify revisiting this as a separate pass — out of scope here since the ask is terrain-tile shadow, and the sun-only fragment approach already reaches every terrain tile the sprite pipeline draws (confirmed: terrain tiles are `sprite_instance` draws through the same `tile_batcher`/`sprite.frag` path as everything else — no separate terrain shader exists).

## Status

Implemented and verified in commit `a40f69675d` on branch `feature/improvements` (2026-08-05). All 5 approach steps applied. Build clean on `osx-arm-slim`; F4 panel and F7 cycling confirmed via debug.log; mode-17 isolated view confirmed animated drift outdoors at noon (183k/1.03M px changed over 16s). Post-review fix: mode-17 debug view gated to the same `sun_intensity`/`sun_sky_vis`/`sdf_map_w` condition as `sun_contrib` itself, so it reads flat white indoors/underground/at night instead of showing a drifting pattern with no real effect.

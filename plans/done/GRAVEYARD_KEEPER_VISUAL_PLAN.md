# GK visual-fidelity plan — IMPLEMENTATION BUILD SHEET

## STATUS (reviewed 2026-07-18)
§1 COMPLETE — all knobs (nrm_entity_amount, ripple_k, gust_amp, gust_freq, part_radius, part_strength, sway_amp, sway_freq) present in debug_params (176 bytes). §2 COMPLETE — alpha-shape bevel implemented in `surface_normal()` (sprite.frag.hlsl); tall-sprite entity blend uses `nrm_entity_amount` knob (default 0.3) instead of hardcoded value; F4 slider added. §3 DEFERRED — blocked on Metal second-sampler spike (shadercross mis-bind; see §3a). §4/§5 COMPLETE — foliage sway with ripple/gust/parting fully implemented in sprite.vert.hlsl. §6 tonemap: AgX shipped; grade knobs (grade_desat/cool/bright) live in debug_params and consumed in sprite.frag.hlsl tone-grade block.
STALE FACTS in §0: debug_params is now **176 B** (11 × 16), not the §1 projection. §0 register-space table lists fragment storage buffers as `space4`, but live code uses `space2`. Re-derive anchors before implementing §3.

> Optimized for a small-context implementer. **Read §0 first, then load ONE chunk (§1–§6) per work session.** Each chunk is self-contained. Anchors are **grep strings** (line numbers drift — confirm by grep before editing).

---

## §0 SHARED FACTS (always load)

**Build / run / log (Mac dev target):**
```
cmake --build out/build/osx-arm-slim --target cataclysm-bn-tiles   # add --clean-first if stale
# run the binary under out/build/osx-arm-slim/ — NOT the repo-root copy (stale-mtime trap)
# runtime log: ~/Library/Application Support/Cataclysm-BN/config/debug.log
```
Win11/MSVC/D3D12 is the primary RELEASE target; Mac/Metal is dev. Sources are GLOB_RECURSE+CONFIGURE_DEPENDS (new files auto-picked-up). HLSL frag/vert shaders are **runtime-loaded from `data/shaders/lighting/src/`** (no rebuild needed for shader-only edits) via `load_lighting_shader_source`.

**SDL_GPU → HLSL register-space map (critical):**
| SDL call | HLSL register |
|---|---|
| `BindGPUVertexStorageBuffers(rp,N,…)` | `register(tN, space0)` |
| `PushGPUVertexUniformData(cb,N,…)` | `register(bN, space1)` |
| `BindGPUFragmentSamplers(rp,N,…)` | `register(tN, space2)` + `register(sN, space2)` |
| `BindGPUFragmentStorageTextures(rp,N,…)` | storage tex, `space2` t-slots after samplers |
| `BindGPUFragmentStorageBuffers(rp,N,…)` | `register(tN, space4)` |
| `PushGPUFragmentUniformData(cb,N,…)` | `register(bN, space3)` |

**Key structs / cbuffers:**
- `sprite_instance` = **64 B**, `static_assert(sizeof==64)` in `sprite_batcher.h`. Fields: `dst_x,dst_y,dst_w,dst_h | src_u,src_v,src_uw,src_vh | tint_r,g,b,a | rotation, light_mul/pad0, pad1, pad2`. **`pad1` = foliage sway weight** (set in `enqueue_tile_sprite`, read in `sprite.vert.hlsl`). **`pad2` = ONLY free field.**
- `debug_params` = **128 B** (32×float), `static_assert` in `sprite_batcher.cpp`. Bound frag `b2,space3` AND pushed to **vertex `b2,space1`** (so vert sees all fields incl. `player_x,player_y,sway_amp,sway_freq,anim_time,nrm_amount,nrm_relief,nrm_elev,sdf_sharp,grade_desat,grade_cool,grade_bright`). Growing it = bump the static_assert + extend BOTH the C++ struct and the HLSL cbuffers (frag `sprite.frag.hlsl` + vert `sprite.vert.hlsl`) + keep 16-B alignment.
- `sun_params` 48 B (`b1,space3`), `light_params` 32 B, `TonemapParams` 16 B (`b0,space2`, `{exposure,min_ev,max_ev,pad}`).

**Fragment bindings today** (`sprite_batcher.cpp`, grep `bind_lighting_resources` + `BindGPUFragmentSamplers`): **Atlas is the ONLY sampler** (`t0/s0,space2`, rebound **per segment** in the flush loop). Storage textures (IndirectTex, ShadowMask) + 5 storage buffers (Emitters/Sdf/SkyVis/Vis…) bound **once per flush** via `bind_lighting_resources(rp)` BEFORE the segment loop. **Never bind null to a sampler → D3D12 command-list corruption/crash.**

**Metal gotcha (governs §3):** shadercross @ commit `6b06e55c` mis-binds a 2nd fragment **sampler** on Metal (`.Sample/.Load` returns 0). That's why Atlas is the only sampler and SDF/etc. were moved to storage buffers. **May be fixed now — §3 spikes it.**

**Foliage flags** (`cata_tiles.cpp`, grep `foliage_sway_weight`): `TFLAG_TREE→0.5`, `TFLAG_YOUNG→0.8`, `TFLAG_SHRUB→1.0`, `TFLAG_TALL_GRASS→0.6`, else 0. `_canopy` overlay split: grep `"_canopy"` in cata_tiles.cpp (static base draw at sway=0 + overlay draw at full sway).

**Debug mode / F4:** `debug_mode` in debug_params; mode 9 = raw normal RGB. F4 ImGui sliders live in `imgui_layer` dev-ui callback; pattern = slider writes a C++ global (`g_*`), global passed into the pass `record()` / pushed in a cbuffer.

**Do the `debug_params` growth ONCE (§1) before §2/§4/§5** — they all add knobs; batch into a single ABI change.

---

## §1 GROW debug_params (do first) — NOT started (none of the 6 knobs present; debug_params currently 152 B)

GOAL: one ABI change reserving every knob §2/§4/§5 need.
FILES: `src/lighting/sprite_batcher.h` (struct + static_assert), `src/lighting/sprite_batcher.cpp` (static_assert value, vertex+fragment push sites — grep `PushGPUVertexUniformData` & `PushGPUFragmentUniformData` for slot 2/`debug_params`), `data/shaders/lighting/src/sprite.frag.hlsl` + `sprite.vert.hlsl` (cbuffer mirrors).
ADD these floats (group at end, keep 16-B align; new size = 128 + N*4 rounded to 16):
- `nrm_entity_amount` (creature relief dial, §2)
- `ripple_k`, `gust_amp`, `gust_freq` (§4)
- `part_radius`, `part_strength` (§5)
EDIT: bump `static_assert(sizeof(debug_params)==NEW)`. Mirror the exact field order into BOTH HLSL cbuffers (frag `DebugParams register(b2,space3)`, vert `register(b2,space1)`).
VERIFY: build clean (no static_assert fail); game inits (no `E_INVALIDARG` pipeline crash on D3D12 = layout matches). Existing look unchanged (new knobs default 0 except `nrm_entity_amount`).

---

## §2 NORMALS B1 — inline alpha-shape + un-flatten — NOT started (surface_normal still albedo-luma Sobel @ sprite.frag.hlsl:355; flat override @ :404)

GOAL: replace albedo-luma Sobel with alpha-shape bevel; give ALL sprites (incl. creatures) relief.
FILE: `data/shaders/lighting/src/sprite.frag.hlsl` (shader-only, no rebuild).
CURRENT (grep `float3 surface_normal(`): 4-tap Sobel of **albedo luma** `dot(sR.rgb,luma)-dot(sL.rgb,luma)`; edge-fade via `min` of 4 alpha neighbours; returns `lerp((0,0,1), n, edge)`.
CHANGE:
1. Swap the gradient source from albedo luma → **alpha**, at a **widened tap radius** (~2–3 texels): `dx = aR - aL; dy = aD - aU` where `aX = Atlas.Sample(...).a` at ±radius. Keeps the edge-fade. This bevels the silhouette.
2. Find the **tall-sprite flat override** (grep nearby for the branch that forces `normal=(0,0,1)` / `lambert=1.0` for tall/entity sprites, ~line 379). REMOVE/relax it so trees/walls/furniture/creatures get the bevel.
3. Gate creatures with the new `nrm_entity_amount` knob (§1): multiply the relief contribution for entity sprites by it (need a per-sprite "is entity" signal — if none exists in-frag, reuse the tall-sprite flag you just found instead of deleting it: keep the flag, but replace `flat` with `lerp(flat, bevel, nrm_entity_amount)`).
Lambert sites unchanged (grep the two `nrm_amount` `lerp(...dot(normal,...))` blocks — emitter & sun).
KNOBS: existing `nrm_amount/relief/elev` + new `nrm_entity_amount`. `nrm_amount=0` must collapse to old flat (safety).
VERIFY: F4 debug mode 9 → rounded silhouettes (shape), NOT albedo hatch; trees/creatures show relief. Sweep F4 cursor light → relief catches beam. Creatures noisy → lower `nrm_entity_amount`.

---

## §3 NORMALS B2 — baked SpriteDLight atlas (spike binding FIRST) — NOT started (no normal page / 2nd-sampler spike in code)

GOAL: per-sprite shape-dome normals baked at load, sampled at runtime (true volume).

**§3a SPIKE (gate — do before any atlas work):** In `sprite.frag.hlsl` add a dummy 2nd sampler `Texture2D Probe:register(t1,space2); SamplerState ProbeSmp:register(s1,space2);`, sample it, output to mode-9. In `sprite_batcher.cpp` flush loop (grep `BindGPUFragmentSamplers( rp, /*first_slot=*/0`), bind a real texture at slot 1. Build+run **on Metal**. If Probe sample is **nonzero** → 2nd sampler works → use **PATH-S**. If **zero** (bug persists) → **PATH-T**. Remove the probe after deciding.

**§3b GEN (prebake at tileset-load):** source PNGs load at tileset-load — compute normals there. Algorithm: alpha silhouette → **interior distance transform** → smooth height dome → gradient → normal, packed **RG8** (z reconstructed `sqrt(1-x²-y²)` in shader). Cache by the existing **sprite hash** (grep `get_surface_hash` / dedup in `copy_surface_to_dynamic_atlas`). Hold RG8 surfaces in a hash-keyed map.

**§3c UPLOAD (rides existing streaming):** in `copy_surface_to_dynamic_atlas` (`cata_tiles.cpp`, grep) / `upload_surface_subregion_to_gpu_texture` (`render_state.cpp`, grep): when a sprite streams into the atlas, look up its precomputed normal and copy the sub-rect into a **parallel normal page** allocated in lockstep with each atlas page (grep `create_rgba_gpu_texture` — make a sibling RG8 page per page).

**§3d BIND + SAMPLE:**
- PATH-S (sampler works): bind normal page at **sampler slot 1**, rebound per-segment next to Atlas in the flush loop. Shader: `surface_normal` reads `NormalTex.Sample(NormalSmp, uv).rg`, reconstruct z.
- PATH-T (fallback): normal page = **storage texture**. Must rebind it **inside the segment loop** (storage tex currently bound once at `bind_lighting_resources`) AND renumber storage-buffer t-slots (t3→t4…) in shader+C++ (the IndirectBuf→IndirectTex pattern). Shader: `.Load(int3(px,py,0)).rg` + manual bilinear, reconstruct z.
Replace §2's inline Sobel with the texture read.
VERIFY: §3a probe nonzero (records PATH). Mode 9 → smooth interior dome, no atlas-neighbour rim bleed. Enter a new area → no hitch (gen was at load). A/B vs §2 inline → clearly stronger volume. D3D12 inits (no `E_INVALIDARG`).

---

## §4 VEGETATION — ripple + gust — NOT started (sprite.vert.hlsl still single `sin()` sway, no ripple_k/gust)

GOAL: kill the uniform-sine look. Shader-only.
FILE: `data/shaders/lighting/src/sprite.vert.hlsl` (grep the sway block: `swayw`, `BASE_PIN`, `sin( anim_time * sway_freq + ph )`).
CURRENT: `ph = base_tile.x*0.7 + base_tile.y*1.3; wind = sin(anim_time*sway_freq+ph); bend = 1-smoothstep(0,0.55,c.y); c_uv.x += bend*swayw*sway_amp*wind/dst_w`.
CHANGE:
1. **Intra-sprite ripple:** `ph += c.x * ripple_k` (per-column desync → shear, not rigid slide).
2. **Multi-octave gust:** `wind = sin(t*f+ph) + 0.5*sin(t*f*2.3+ph*1.7)`; multiply by a slow envelope `(0.6 + 0.4*sin(anim_time*gust_freq))` scaled by `gust_amp`.
3. Optional slight vertical: add a small `c_uv.y` term at the canopy (`bend * tiny`).
KNOBS: `ripple_k`, `gust_amp`, `gust_freq` (§1). `_canopy` split already exists — no change unless extending art coverage.
VERIFY: canopies ripple/desync (not rigid), gusts non-periodic, bases stay pinned (`BASE_PIN`).

---

## §5 INTERACTION — player foliage parting — NOT started (`player_x/y` in vert cbuffer but UNUSED by sway block)

GOAL: foliage bends away from player. Shader-only + verify the cbuffer reaches vert.
FILE: `data/shaders/lighting/src/sprite.vert.hlsl` (same sway block as §4).
PRECHECK: confirm `player_x,player_y` (in debug_params, vert `b2,space1`) are in **map-tile space** matching `base_tile` (grep how `base_tile` is derived: `base_px/tile_pixel_size - camera_off`). If player_x/y is a different space, convert.
CHANGE (inside `if(swayw>0)`):
```
float2 d = base_tile - float2(player_x, player_y);
float dist = length(d);
if(dist < part_radius){
  float k = (1.0 - dist/part_radius) * part_strength;
  c_uv.x += normalize(d).x * k * bend;   // push away, canopy-weighted
  c_uv.y += k * bend * 0.3;              // slight downward bend
}
```
KNOBS: `part_radius`, `part_strength` (§1).
RESERVE (no code now, note for later): multi-creature = `sprite_instance.pad2` per-instance disturbance scalar, fed by a creature-position storage buffer built via the emitter-collector pattern.
VERIFY: walk player through grass/crops → foliage parts AWAY, springs back; no parting on non-foliage (`swayw==0`); phase world-locked on scroll.

---

## §6 GRADING — knob suite + bake-to-LUT — NOT started (tonemap.frag = AgX only; no grade block, no LUT; `grade_desat/cool/bright` do NOT exist)

GOAL: replace weak `grade_desat/cool/bright` with a real grade; runtime = live grade; bake to LUT PNG for presets.
FILES: `data/shaders/lighting/src/tonemap.frag.hlsl` (grade math, after AgX), new grade cbuffer, `src/lighting/tonemap_pass.{h,cpp}` (params + optional LUT bind + bake), `src/lighting/render_state.*`, `src/sdl_render_frame.cpp` (grep `tonemap().record(`), `imgui_layer.cpp` (sliders + Bake button).
A1 GRADE MODEL: implement **ASC-CDL** `out = pow(saturate(in*slope+offset), power)` per-RGB, then **temperature/tint**, **saturation** (`lerp(luma,rgb,sat)`), **pivoted contrast** (`(c-0.5)*contrast+0.5`). New cbuffer (own slot, NOT debug_params). Seed slider **defaults** by eyeballing a GK LUT/graded-still mood (approximate). Replace the old wash block (grep `grade_desat`).
A2 RUNTIME: evaluate grade live in tonemap.frag (cheap). No LUT lookup needed for the global look.
A3 BAKE (single source of truth): "Bake LUT" F4 button → generate procedural identity LUT strip (32³ = 1024×32), render it **through the same grade shader** to an offscreen RGBA8 target → **GPU→CPU readback** (pattern: grep `save_screenshot` for the copy-pass/readback idiom) → write PNG to `data/raw/lighting/lut/`.
A4 LUT SAMPLE (optional preset mode): `Texture2D LutTex:register(t1,space2)` + strip-trilinear `sample_lut()`; `lerp(graded, sample_lut(graded), lut_strength)`. Bind at frag sampler slot 1 in tonemap record; **bind source into slot 1 if no LUT (never null)**. NOTE: this is the §3 sampler-gate question again but in the **tonemap** pass (which already binds 1 sampler) — if §3a said PATH-T, test separately here or keep A4 off.
KNOBS: slope/offset/power(×3), temp, tint, sat, contrast, lut_strength, lut_enable. Bake button.
VERIFY: neutral knobs ⇒ frame ≈ current (minus old wash). Grade live → Bake → load baked LUT in sample mode ⇒ **identical to live** (proves single-source). No banding.

---

## ORDER
§1 → §2+§3 (together) → §4 → §5 → §6. Commit each separately.

## OPTIONAL (skip unless asked)
R1 front/back light fade (`sprite.frag.hlsl` emitter loop, steep falloff behind tall occluder). R3 GK altitude-fog (fallback only if Step 6 sun-shaft volumetric fails its legibility gate).

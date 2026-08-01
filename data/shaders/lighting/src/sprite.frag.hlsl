// Phase 7/8: per-pixel Lambert + SDF soft shadows + sun/sky.
//
// SDL_CreateGPUShader binding convention for pixel shaders (DXBC/DXIL):
//   (t[n], space2) — sampled textures first, then storage textures, then
//                    storage buffers; share the t-register range.
//   (s[n], space2) — samplers, indices match their sampled textures.
//   (b[n], space3) — uniform buffers.
//
// Resources (all space2). t-register order is sampled → storage textures →
// storage buffers, so 1 sampled (t0) + 1 storage texture (t1) ⇒ the storage
// buffers start at t2.
//   t0 / s0  Atlas       — sampled, current atlas texture (per segment)
//   t1       IndirectTex — Texture2D<float4> GRAPHICS_STORAGE_READ, 1-bounce GI
//                          (storage-texture slot 0; Step-3 Phase 1b consumer
//                          path — RC will write this texture in Phase 2)
//   t2       Emitters    — StructuredBuffer<GpuEmitter>, 8192-entry  (storage buf slot 0)
//   t3       SdfBuf      — StructuredBuffer<float>, sdf_pass dims    (storage buf slot 1)
//   t4       SkyVisBuf   — StructuredBuffer<float>, sdf_pass dims    (storage buf slot 2)
//   t5       VisBuf      — StructuredBuffer<float>, per-tile vis     (storage buf slot 3)
//
// Emitter, SDF AND sky-vis data carriers ALL live in storage buffers, NOT
// sampler textures: SDL_shadercross @ 6b06e55c silently mis-binds sampler
// textures on Metal (readback proves the upload reaches the GPU, but the
// shader's Load/Sample returns all zeros for every fragment). Atlas (slot 0)
// is the only sampler texture that works. Emitters moved off Texture2D
// 2026-05-29; SDF + SkyVis followed 2026-05-30 (the SDF Load returned 0 →
// shadow=0 → emitters killed in-game; the SkyVis Load returned 0 → sky_vis=0
// → sun gated off entirely). SkyVisBuf holds per-tile open-sky (1.0) /
// roofed (0.0) from map outside_cache.
// 64-byte layout, must mirror struct gpu_emitter in gpu_emitter.h exactly.
// Packed as four float4s to guarantee 16-byte alignment under both DXC
// (cbuffer-style) and SPIRV-Cross MSL output — empirical: declaring this
// as float3/float interleaved made the shader read `radius` from
// `cone_half_angle` (~π → tiny blob instead of full-screen gradient).
//   slot0.xyz = pos       slot0.w = radius
//   slot1.xyz = color     slot1.w = falloff
//   slot2.xy  = cone_dir  slot2.z = cone_half_angle  slot2.w = asfloat(shape)
//   slot3.x   = asfloat(flicker_seed)   slot3.yzw = pad0/1/2
#include "attenuation.hlsl"

struct GpuEmitter {
    float4 pos_radius;
    float4 color_falloff;
    float4 cone_shape;
    float4 misc;
};
Texture2D<float4>            Atlas       : register(t0, space2);
SamplerState                 AtlasSmp    : register(s0, space2);
// Silhouette sun-shadow mask (Phase 2). Screen-space coverage, read-only storage
// texture (Texture2D, .Load only — that is what makes shadercross reflect it as a
// storage texture, not a 2nd sampled image). Now the SOLE storage texture ⇒
// storage-texture slot 0 ⇒ t1, ahead of the storage buffers (t2..t7). Read at the
// fragment's SCREEN pixel (SV_Position.xy) — the mask shares world_target's
// physical size + viewport+proj, so a direct screen-texel fetch aligns 1:1 (no
// camera/proj math). (GI moved off a storage texture to GiBuf below — Stage 1 of
// GI_COMPUTE_AND_PERF_PLAN; ShadowMask being the only storage texture removes the
// old all-or-none 2-slot bind hazard.)
Texture2D<float4>            ShadowMask  : register(t1, space2);
StructuredBuffer<GpuEmitter> Emitters    : register(t2, space2);
StructuredBuffer<float>      SdfBuf      : register(t3, space2);
StructuredBuffer<float>      SkyVisBuf   : register(t4, space2);
// Storage buffer slot 3 ⇒ t5.  (VisBuf, the live-visibility buffer, used to sit
// here; "remove vision overlay" (46261515ac) deleted its only reader and left the
// declaration orphaned.  DXC then stripped the unused buffer, punching a hole in
// the t2..t7 SRV range: shadercross reflected 5 storage buffers while the DXIL
// still referenced t7, so D3D12 rejected the root signature with E_INVALIDARG and
// the whole lighting pipeline failed to build.  Fragment storage buffers MUST stay
// contiguous and every one MUST be read.)
// 1-bounce indirect light (GI). GPU compute GI pass output (gi_bounce.comp) → gi_buf.
// Storage buffer slot 3 ⇒ t5 (Stage 2b removed SunSdfBuf, which was here — the sun
// shadow moved to the compute coverage march, see SkyBuf). Tile-res, 4 floats/tile
// (rgb + pad), x-major gi[(x*sdf_map_h+y)*4 + c], scalar StructuredBuffer<float>.
StructuredBuffer<float>      GiBuf       : register(t5, space2);
// Stage 2a/2b sky+sun/moon (sky_sun.comp output) — LAST fragment storage buffer,
// slot 4 ⇒ t6. Tile-res, 4 floats/tile [(x*sdf_map_h+y)*4 + c], x-major. rgb =
// directional sky-access (alcove/overhang self-shading + indoor daylight from
// window openings — REPLACES the flat sky_vis ambient; CPU bleed flood-fill gone).
// a = celestial (sun/moon) occlusion from the 3D coverage-occluder march
// (REPLACES the inline sun trace_shadow + SunSdfBuf — Stage 2b).
StructuredBuffer<float>      SkyBuf      : register(t6, space2);
cbuffer LightParams : register(b0, space3) {
    float tile_pixel_size; float current_z;
    uint  emitter_count;   float ambient;
    float camera_off_x;    float camera_off_y;
    uint  sdf_map_w;       uint  sdf_map_h;
};
// Phase 8: sun + skylight params.
cbuffer SunParams : register(b1, space3) {
    float sun_dir_x, sun_dir_y, sun_sin_elev, sun_intensity;
    float sun_r,     sun_g,     sun_b,        sky_r;
    float sky_g,     sky_b,     sky_intensity, sp_pad;
};
// Debug visualisation + runtime-tunable knobs. See lighting::debug_params
// in sprite_batcher.cpp for the C++-side wire-stable layout.
cbuffer DebugParams : register(b2, space3) {
    uint  debug_mode;          // 0=off, 1..7=visualisation modes (see C++ comment)
    float debug_opacity;       // blend fraction for modes 1-5
    float emitter_scale;       // per-channel multiplier applied to emitter_light
    float sun_scale;           // multiplier for sun_contrib
    float sky_scale;           // multiplier for sky_contrib
    float shadow_k;            // sphere-trace cone hardness (was hardcoded 8.0)
    uint  shadow_steps;        // per-emitter march iteration cap (was 16)
    float dither_amt;          // 0=smooth .. 1=full ordered dither
    float dither_bands;        // quantisation levels for the dither (e.g. 6)
    float gi_strength;         // 1-bounce indirect light multiplier (0=off)
    float vis_curve;           // vision-edge falloff exponent (0=off → no falloff)
    float mem_dim;             // memorized-tile brightness floor
    float mem_desat;           // memorized-tile desaturation 0..1
    float night_floor;         // ambient floor at night (sun_intensity=0)
    float day_floor;           // ambient floor at noon  (sun_intensity=1)
    float grade_desat;         // tone grade: 0=full colour, 1=greyscale
    float grade_cool;          // tone grade: blend toward cool teal tint
    float grade_bright;        // tone grade: brightness multiplier (lit world tiles)
    float vis_radius;          // radial player-distance falloff radius (tiles; 0=off)
    float player_x;            // player map-tile centre x (radial origin)
    float player_y;            // player map-tile centre y
    float mem_radius;          // memory distance-fade scale in tiles (effect 3)
    float nrm_amount;          // A1 normal Lambert blend: 0=flat(off) .. 1=full
    float nrm_relief;          // A1 normal tilt magnitude; SIGNED (negative flips global relief direction)
    float nrm_elev;            // A1 implied light height above plane; LOWER=more grazing=stronger relief
    float sdf_sharp;           // SDF sample sharpness: 0=bilinear(smooth) .. 1=nearest(tight, grid-snapped)
    float ao_strength;         // A4 ambient occlusion: 0=off .. 1=full SDF-cavity darkening of the ambient fills
    float shadow_mask_str;     // Phase 2: silhouette sun-shadow mask strength on ground (0=off/default .. 1=full)
    // Tail mirrors the C++ debug_params layout. sway_* / anim_time are vertex-stage
    // only — declared here purely so spec_strength lands at the right cbuffer offset.
    float sway_amp;            // (vertex-only padding here)
    float sway_freq;           // (vertex-only padding here)
    float anim_time;           // (vertex-only padding here)
    float spec_strength;       // wet specular glint strength (0=off; CPU-folded with rain intensity)
    float light_eps;           // P1: contribution epsilon for shadow march gating (default 0.004)
    float max_shadow_k;        // P2: max emitters per pixel that get full shadow trace (default 16)
    // P5b: sky/sun quality knobs (vertex+fragment padding — not used here; consumed by
    // sky_sun.comp via sky_sun_params cbuffer. Declared for cbuffer layout parity.)
    float sky_dirs;            // sky hemisphere directions (float → uint in compute push)
    float sky_reach;           // sky march max distance (tiles)
    float sun_steps;           // celestial march steps (float → uint in compute push)
    float sun_penumbra;        // penumbra angular samples (float → uint in compute push)
    // Wave 2: vegetation life knobs (vertex-stage only; declared for cbuffer layout parity).
    float ripple_k;
    float gust_amp;
    float gust_freq;
    float part_radius;
    float part_strength;
    float nrm_entity_amount;
    // Pixel-art quantisation / sub-tile occluders / palette ramps.
    float texels_per_tile;   // tileset native tile width in art texels (DATA)
    float light_quant;       // 1 = snap light sample to art texels, 0 = per-screen-pixel
    float occ_soft_gain;     // partial-occluder block gain (compute-stage; parity here)
    float self_eps_tall;     // trace_shadow self-shadow escape radius for TALL sprites
    float ramp_enable;       // 0 = plain multiply, 1 = full palette-ramp resolve
    float ramp_steps;        // shade steps per palette row
    float ramp_chroma;       // how much coloured light tints the ramped surface
    float dbg_pad2;          // alignment
};
struct VS_OUT {
    float4 pos      : SV_Position;
    float2 uv       : TEXCOORD0;
    float4 tint     : TEXCOORD1;
    float2 world_pos: TEXCOORD2;
    float  light_mul: TEXCOORD3; // memory-fade marker (<0 = -(dist); else no-op)
    float2 light_pos: TEXCOORD4; // base-tile centre for tall sprites, else world_pos
    float  outline  : TEXCOORD5; // >0.5 = silhouette mask mode (hover outline)
    float  dark_frac: TEXCOORD6; // 0 at sprite base → extrude_dark at canopy; applied in frag
};
// SDF supersample factor — MUST match lighting::SDF_SUPERSAMPLE (sdf_pass.h).
// SdfBuf is the SS-finer grid: dims (sdf_map_w*SDF_SS) x (sdf_map_h*SDF_SS),
// x-major (stride = sdf_map_h*SDF_SS). Stored distances are already in TILE
// units (the CPU ÷SS), so the cone trace below is unchanged.
static const int SDF_SS = 8;
// P1: contribution epsilon — skip shadow march when atten*lambert is negligible.
// Edge-of-radius overlapping emitters dominate count but add ~0 brightness.
static const float LIGHT_EPS_DEFAULT = 0.004;
// P2: maximum number of emitters that get full shadow tracing per pixel.
// Weaker in-range lights still contribute unshadowed. Default 16 covers most scenes.
static const int MAX_SHADOW_K_DEFAULT = 16;
// Clamped raw SDF fetch; (x,y) are SS-GRID coords.
float sdf_texel(int x, int y) {
    const int gw = (int)sdf_map_w * SDF_SS;
    const int gh = (int)sdf_map_h * SDF_SS;
    x = clamp(x, 0, gw - 1);
    y = clamp(y, 0, gh - 1);
    return SdfBuf[x * gh + y];
}
// Bilinear SDF sample over the SS-finer Euclidean grid → sub-tile-fine, tight
// AND smooth penumbra (no stair-step). p is in TILE units; map to subcell-grid
// coords g = p*SDF_SS - 0.5, where subcell c's CENTRE sits at tile (c+0.5)/SDF_SS
// (so a tile centre still lands on a subcell sample, preserving the old p-0.5
// alignment that keeps shadows glued to occluders).
float sdf_bilinear(float2 p) {
    const float2 g  = p * (float)SDF_SS - 0.5;
    const float2 fp = floor(g);
    const int   x0  = (int)fp.x;
    const int   y0  = (int)fp.y;
    const float2 w  = g - fp;         // frac
    const float a = sdf_texel(x0,     y0    );
    const float b = sdf_texel(x0 + 1, y0    );
    const float c = sdf_texel(x0,     y0 + 1);
    const float d = sdf_texel(x0 + 1, y0 + 1);
    const float bil = lerp(lerp(a, b, w.x), lerp(c, d, w.x), w.y);
    // sdf_sharp biases bilinear→nearest. With the SS grid this is rarely needed
    // (the field is already fine), but kept as a live tightness lever.
    if(sdf_sharp <= 0.001) { return bil; }
    const float nr = sdf_texel((int)floor(g.x + 0.5), (int)floor(g.y + 0.5));
    return lerp(bil, nr, saturate(sdf_sharp));
}
// Stage 2b: the wall-only sun SDF + its bilinear sampler are GONE. The sun (and
// moon) shadow is now the unified coverage occluder marched in 3D by
// sky_sun.comp → SkyBuf.a; the fragment no longer reads SunSdfBuf.
// Per-tile 1-bounce indirect light (RGB). Read from GiBuf, the GPU compute GI
// pass's output: tile-res, 4 floats/tile (rgb + pad), x-major
// gi[(x*sdf_map_h+y)*4 + c] — the same x-major layout as SdfBuf/SkyVisBuf, no
// transpose (the compute shader writes the CPU-style index directly, unlike the
// old IndirectTex storage texture which needed a row/col swap). Bilinear with the
// same p-0.5 centre convention as sdf_bilinear.
float3 indirect_texel(int x, int y) {
    x = clamp(x, 0, (int)sdf_map_w - 1);
    y = clamp(y, 0, (int)sdf_map_h - 1);
    const int o = (x * (int)sdf_map_h + y) * 4;
    return float3(GiBuf[o + 0], GiBuf[o + 1], GiBuf[o + 2]);
}
float3 indirect_bilinear(float2 p) {
    const float2 sp = p - 0.5;
    const float2 fp = floor(sp);
    const int   x0  = (int)fp.x;
    const int   y0  = (int)fp.y;
    const float2 w  = sp - fp;
    const float3 a = indirect_texel(x0,     y0    );
    const float3 b = indirect_texel(x0 + 1, y0    );
    const float3 c = indirect_texel(x0,     y0 + 1);
    const float3 d = indirect_texel(x0 + 1, y0 + 1);
    return lerp(lerp(a, b, w.x), lerp(c, d, w.x), w.y);
}
// Stage 2a directional skylight reader (SkyBuf). Same tile-res x-major layout +
// p-0.5 bilinear centre as the GI reader. rgb = sky-access, a = sun-occ.
float4 sky_texel(int x, int y) {
    x = clamp(x, 0, (int)sdf_map_w - 1);
    y = clamp(y, 0, (int)sdf_map_h - 1);
    const int o = (x * (int)sdf_map_h + y) * 4;
    return float4(SkyBuf[o + 0], SkyBuf[o + 1], SkyBuf[o + 2], SkyBuf[o + 3]);
}
float4 sky_bilinear(float2 p) {
    const float2 sp = p - 0.5;
    const float2 fp = floor(sp);
    const int   x0  = (int)fp.x;
    const int   y0  = (int)fp.y;
    const float2 w  = sp - fp;
    const float4 a = sky_texel(x0,     y0    );
    const float4 b = sky_texel(x0 + 1, y0    );
    const float4 c = sky_texel(x0,     y0 + 1);
    const float4 d = sky_texel(x0 + 1, y0 + 1);
    return lerp(lerp(a, b, w.x), lerp(c, d, w.x), w.y);
}
// Sky visibility, bilinear. UNLIKE sdf/vis, SkyVisBuf is TILE-res (one float per
// tile, stride sdf_map_h — NOT supersampled), so the centre map is g = p-0.5
// (no SDF_SS factor). Smooths the indoor daylight-bleed gradient over ~1 tile so
// the interior fill reads cleanly instead of stair-stepping per tile.
float skyvis_texel(int x, int y) {
    x = clamp(x, 0, (int)sdf_map_w - 1);
    y = clamp(y, 0, (int)sdf_map_h - 1);
    return SkyVisBuf[x * (int)sdf_map_h + y];
}
float skyvis_bilinear(float2 p) {
    const float2 g  = p - 0.5;
    const float2 fp = floor(g);
    const int   x0  = (int)fp.x;
    const int   y0  = (int)fp.y;
    const float2 w  = g - fp;
    const float a = skyvis_texel(x0,     y0    );
    const float b = skyvis_texel(x0 + 1, y0    );
    const float c = skyvis_texel(x0,     y0 + 1);
    const float d = skyvis_texel(x0 + 1, y0 + 1);
    return lerp(lerp(a, b, w.x), lerp(c, d, w.x), w.y);
}
// Shared soft-shadow sphere trace (Inigo Quilez cone ratio). Used by BOTH
// emitters and the sun so they share the shadow_k / shadow_steps knobs and
// look consistent. `dist_to_light` is the march length (real distance for
// point emitters; a fixed reach for the directional sun).
float trace_shadow(float2 origin, float2 dir, float dist_to_light,
                   float k, int steps, bool directional) {
    if(sdf_map_w == 0u || steps <= 0) {
        return 1.0;
    }
    float shadow = 1.0;
    float t = min(0.3, dist_to_light * 0.5);
    // Self-shadow guard (sun AND point lights). A tall sprite is lit by its BASE
    // tile (vertex light_pos), which is itself an occluder (the tree/wall the
    // shadow is cast from). A naive march would hit that occluder at t~0 and report
    // the lit TOP as fully shadowed. So when the RECEIVER ITSELF sits on an occluder
    // (sdf(origin) < 0.05 — true only for tall sprites anchored to their own tile,
    // NOT for open ground near a wall, which is ~0.3), step out of that occluder
    // body before shadowing. Exit the instant we re-enter open air (sd >= 0.05) so
    // the NEXT occluder still shadows — a tree inside a building marches out of the
    // tree, hits the building wall, stays dark (option A). No-op for ground (any
    // light): its SDF is well above the threshold so the guard never fires.
    if(sdf_bilinear(origin) < 0.05) {
        [loop] for(int ss = 0; ss < steps; ++ss) {
            if(t >= dist_to_light - 0.4) return 1.0;           // never left occluder → sunlit top
            const float sg = sdf_bilinear(origin + dir * t);
            if(sg >= 0.05) break;  // back in open air
            t += 0.15;
        }
    }
    [loop] for(int ss = 0; ss < steps; ++ss) {
        if(t >= dist_to_light - 0.4) break;
        const float sd = sdf_bilinear(origin + dir * t);
        if(sd < 0.05) { shadow = 0.0; break; }
        // Penumbra reference for the IQ cone ratio. A POINT light keys it to the
        // real remaining distance-to-light (dist_to_light - t), which is valid
        // because dist_to_light is a true distance. A DIRECTIONAL light (the sun)
        // has no finite distance — dist_to_light is only the march CAP — so
        // (cap - t) keys the penumbra to a meaningless constant and inverts the
        // soft/hard ends (the "vertically flipped" sun penumbra). Directional uses
        // the textbook IQ form k*sd/t (t = distance from the receiver).
        const float denom = directional ? max(t, 0.01) : max(dist_to_light - t, 0.01);
        shadow = min(shadow, k * sd / denom);
        t += max(sd, 0.15);
    }
    return saturate(shadow);
}
// 4x4 ordered (Bayer) dither matrix, values 0..15.
static const float k_bayer4[16] = {
     0.0,  8.0,  2.0, 10.0,
    12.0,  4.0, 14.0,  6.0,
     3.0, 11.0,  1.0,  9.0,
    15.0,  7.0, 13.0,  5.0
};
// World-locked Bayer threshold in (0,1). Keyed to world PIXEL coords so the
// pattern sticks to the terrain and does not shimmer when the camera pans.
float dither_threshold(float2 world_px) {
    const int bx = ((int)floor(world_px.x)) & 3;
    const int by = ((int)floor(world_px.y)) & 3;
    return (k_bayer4[by * 4 + bx] + 0.5) / 16.0;
}
// --- Bucket A / A1: inline alpha-shape bevel surface normal ----------------
// Per-pixel relief derived from the sprite's alpha silhouette (decision #15,
// upgraded from albedo-luma Sobel to alpha-shape bevel for cleaner results).
// Alpha defines the sprite's actual shape, so the bevel follows rounded armor,
// angular weapons, organic creature silhouettes — not internal artwork
// hatching. Widened 2-texel tap radius for a smoother gradient that reads as
// a soft height dome rather than a sharp edge outline. Edge-fade-to-flat at
// silhouettes prevents packed-atlas neighbours from bleeding fake edges.
// Tuning is LIVE via DebugParams (F4 panel): nrm_amount / nrm_relief / nrm_elev.
// nrm_amount=0 reduces both Lambert sites to flat-normal behaviour exactly.
float3 surface_normal(float2 uv) {
    float aw, ah;
    Atlas.GetDimensions(aw, ah);
    const float2 ts = 1.0 / float2(max(aw, 1.0), max(ah, 1.0));
    // Widened 2-texel tap radius — smoother bevel on the alpha silhouette.
    const float tap_r = 2.0;
    const float4 sL = Atlas.Sample(AtlasSmp, uv - float2(ts.x * tap_r, 0.0));
    const float4 sR = Atlas.Sample(AtlasSmp, uv + float2(ts.x * tap_r, 0.0));
    const float4 sU = Atlas.Sample(AtlasSmp, uv - float2(0.0, ts.y * tap_r));
    const float4 sD = Atlas.Sample(AtlasSmp, uv + float2(0.0, ts.y * tap_r));
    // Alpha-shape gradient: the silhouette alpha IS the height field.
    // Interior pixels (a≈1) flatten; edge pixels (a<1) produce the bevel.
    const float dx = sR.a - sL.a;
    const float dy = sD.a - sU.a;
    // Symmetric height-field tilt; nrm_relief is SIGNED so the F4 slider flips the
    // global relief direction (negative = full negation of both axes) to resolve
    // the y-down/atlas-V sign without a rebuild. Magnitude = relief strength.
    const float3 n = normalize(float3(-dx * nrm_relief, -dy * nrm_relief, 1.0));
    // Edge-fade-to-flat: a (near-)transparent neighbour means we're at the
    // sprite silhouette — collapse back toward flat so a packed-atlas
    // neighbour can't bleed a hard fake edge across the gap.
    const float edge = min(min(sL.a, sR.a), min(sU.a, sD.a));
    return normalize(lerp(float3(0.0, 0.0, 1.0), n, saturate(edge)));
}
// Wet specular glint (Blinn-Phong). View dir is +Z (top-down ortho). The albedo-
// Sobel normal is too high-frequency for a tight specular lobe (it fireflies), so
// flatten it toward up by SPEC_FLATTEN for the spec term ONLY. SPEC_SHININESS sets
// the lobe tightness. Caller scales the result by spec_strength (× sky_vis).
static const float SPEC_SHININESS = 24.0;
static const float SPEC_FLATTEN   = 0.5;
float wet_spec(float3 n, float3 L) {
    const float3 V  = float3(0.0, 0.0, 1.0);
    const float3 ns = normalize(lerp(n, float3(0.0, 0.0, 1.0), SPEC_FLATTEN));
    const float3 H  = normalize(L + V);
    return pow(saturate(dot(ns, H)), SPEC_SHININESS);
}
float4 main(VS_OUT i) : SV_Target0 {
    const float4 texel = Atlas.Sample(AtlasSmp, i.uv);
    if(texel.a < 0.01) discard;
    // Hover-outline silhouette mask (set per-instance via sprite_instance.pad2).
    // Skip ALL lighting: emit the flat outline colour wherever the sprite is
    // opaque. Offset copies of the sprite are drawn behind the real one, so the
    // union forms a dilated ring around it (see HOVER_OUTLINE_PLAN.md).
    if(i.outline > 0.5) {
        // light_mul carries the alpha cutoff for outline instances (reused field;
        // normal lighting is skipped here). Higher = drops translucent baked
        // drop-shadows in the sprite art so the ring follows the body only.
        const float cut = max(i.light_mul, 0.35);
        if(texel.a < cut) discard;
        return float4(i.tint.rgb, texel.a * i.tint.a);
    }
    // Per-pixel surface normal from inline alpha-shape bevel (Bucket A / A1),
    // driving the emitter + sun Lambert. TALL sprites (creatures, trees, walls,
    // tall furniture — light_pos != world_pos) get a GENTLE bevel (30% of full
    // nrm_amount). The alpha-shape gradient no longer catches interior artwork
    // hatching (the old albedo-Sobel "weird toning" cause), so tall sprites can
    // safely receive subtle silhouette relief without darkening. Ground terrain
    // (light_pos == world_pos) keeps the full tuned relief.
    const bool   frag_is_tall_n = ( i.light_pos.x != i.world_pos.x )
                                  || ( i.light_pos.y != i.world_pos.y );
    const float3 normal = frag_is_tall_n
                          ? lerp( float3( 0.0, 0.0, 1.0 ), surface_normal( i.uv ), nrm_entity_amount )
                          : surface_normal( i.uv );
    // Sky exposure (0 roofed .. 1 open), hoisted above the emitter loop so the
    // wet-specular term can gate on it (no indoor glint). SkyVisBuf is x-major
    // (skyvis[x*H+y]); bilinear so the indoor daylight-bleed gradient reads smooth.
    const float sky_vis = (sdf_map_w > 0u) ? saturate(skyvis_bilinear(i.light_pos)) : 0.0;
    // emitter_light accumulates GPU point-light contributions (starts at zero).
    // Combined with CPU tint ADDITIVELY so colored emitter glow is visible on
    // top of the CPU-shadowcasting result, not suppressed by max().
    float3 emitter_light = float3(0.0, 0.0, 0.0);
    const uint me = min(emitter_count, 8192u);
    // P1: use runtime light_eps from cbuffer (falls back to default when 0).
    const float eps = max(light_eps, LIGHT_EPS_DEFAULT);
    // P2: K-max shadow budget — track strongest emitters for full shadow trace.
    const int   k_max = (int)max(max_shadow_k, MAX_SHADOW_K_DEFAULT);
    // Tracking arrays for K-strongest by atten*lambert. 64 covers extreme horde density.
    uint   top_idx[64];
    float  top_val[64];
    int    top_n = 0;

    // --- PASS 1: accumulate unshadowed light + track K-max candidates ---
    for(uint ei = 0u; ei < me; ++ei) {
        const GpuEmitter e = Emitters[ei];
        const float3 e_pos    = e.pos_radius.xyz;
        const float  e_radius = e.pos_radius.w;
        const float3 e_color  = e.color_falloff.xyz;
        const float  e_falloff= e.color_falloff.w;
        if(abs(e_pos.z - current_z) > 0.5) continue;
        const float2 dv   = e_pos.xy - i.light_pos;
        const float  dist = length(dv);
        if(dist < 0.01) continue;
        const float  atten = point_light_atten(dist, e_radius, e_falloff);
        if(atten <= 0.0) continue;

        // Per-pixel Lambert against this emitter (Bucket A / A1).
        const float2 sh_dir = dv / max(dist, 0.001);
        const float  lambert = saturate(lerp(1.0,
                                    saturate(dot(normal, normalize(float3(sh_dir, nrm_elev)))),
                                    nrm_amount));

        // P1: skip negligible contributions entirely — no shadow march needed.
        if(atten * lambert <= eps) continue;

        const float3 rgb = (e_color.x < 0.01 && e_color.y < 0.01 && e_color.z < 0.01)
                           ? float3(1, 1, 1) : e_color;
        // Unshadowed contribution (always added — weaker lights still illuminate).
        emitter_light += rgb * atten * lambert;

        // P2: track K-strongest for full shadow trace (inline selection, no sort).
        if(top_n < k_max) {
            top_idx[top_n] = ei;
            top_val[top_n] = atten * lambert;
            ++top_n;
        } else if(atten * lambert > top_val[0]) {
            // Replace weakest tracked entry.
            int min_i = 0;
            for(int mi = 1; mi < top_n; ++mi) {
                if(top_val[mi] < top_val[min_i]) min_i = mi;
            }
            top_idx[min_i] = ei;
            top_val[min_i] = atten * lambert;
        }
    }

    // --- PASS 2: full shadow trace for K strongest candidates ---
    for(int ti = 0; ti < top_n; ++ti) {
        const uint ei = top_idx[ti];
        const GpuEmitter e = Emitters[ei];
        const float3 e_pos    = e.pos_radius.xyz;
        const float  e_radius = e.pos_radius.w;
        const float3 e_color  = e.color_falloff.xyz;
        const float  e_falloff= e.color_falloff.w;
        const float2 e_cone_dir = e.cone_shape.xy;
        const float  e_cone_ha  = e.cone_shape.z;
        const uint   e_shape    = asuint(e.cone_shape.w);

        const float2 dv   = e_pos.xy - i.light_pos;
        const float  dist = length(dv);
        const float  atten = point_light_atten(dist, e_radius, e_falloff);
        const float2 sh_dir = dv / max(dist, 0.001);
        const float  lambert = saturate(lerp(1.0,
                                    saturate(dot(normal, normalize(float3(sh_dir, nrm_elev)))),
                                    nrm_amount));

        // Subtract unshadowed contribution (added in pass 1) and re-add with shadow.
        const float3 rgb = (e_color.x < 0.01 && e_color.y < 0.01 && e_color.z < 0.01)
                           ? float3(1, 1, 1) : e_color;
        emitter_light -= rgb * atten * lambert;

        const float  shadow = trace_shadow(i.light_pos, sh_dir, dist,
                                            shadow_k, (int)shadow_steps,
                                            /*directional=*/false);

        // Cone / spotlight angular falloff.
        float cone = 1.0;
        if(e_shape == 1u) {            // emitter_shape::CONE
            const float cosang = dot(normalize(e_cone_dir), -sh_dir);
            cone = smoothstep(cos(e_cone_ha), cos(e_cone_ha * 0.6), cosang);
        }

        // Wet specular glint on top of diffuse.
        const float e_spec = (spec_strength > 0.001)
                             ? spec_strength * sky_vis *
                               wet_spec(normal, normalize(float3(sh_dir, nrm_elev)))
                             : 0.0;
        emitter_light += rgb * atten * shadow * cone * (lambert + e_spec);
    }
    // Phase 8 + Stage 2a: directional skylight + sun. sky_sun.comp provides
    // per-tile directional sky-access (rgb) + sun occlusion (a); sample it once
    // at the tile (light_pos, matching the GI read). The hoisted `sky_vis` is now
    // the RAW open/roofed field — only the sun's overhead gate still uses it.
    const float4 sky_dir = sky_bilinear(i.light_pos);
    // Sky ambient: directional sky-access REPLACES the old flat `* sky_vis`. An
    // open tile sees most of the hemisphere (~1); an alcove/overhang self-shades;
    // a roofed tile lit only through window directions gets partial sky FROM the
    // opening — the unified indoor/outdoor merge, no CPU bleed flood-fill needed.
    float3 sky_contrib = float3(sky_r, sky_g, sky_b) * sky_intensity * sky_dir.rgb;
    // Sun direct: directional soft shadow via the SAME shared trace as
    // emitters, so it honours shadow_k / shadow_steps and matches their
    // softness (was a hardcoded copy: k=4, 16 steps, reach 8.0). 8.0 = the
    // directional march reach (sun has no finite distance). Gated to open-sky
    // tiles (sky_vis) — the sun cannot reach roofed interiors.
    // Gate at a LOW epsilon (just skip the march where there's no sky at all);
    // the smooth `* sky_vis` multiply below feathers the shaft edge. A high gate
    // (>0.99) hard-thresholds the bilinear sky_vis back into a binary per-tile
    // mask → tile-square "blocky sun". With bilinear sky_vis the open↔roofed edge
    // ramps over ~1 tile, so the sun shaft through an opening fades smoothly.
    // (With indoor bleed>0 the interior gradient also passes here — that is the
    // sunbeam-through-window behaviour; trace_shadow keeps it to tiles with a
    // clear path to the sun.)
    // Phase 2 silhouette sun-shadow mask: coverage at this fragment's SCREEN
    // pixel (the mask shares world_target's physical size + viewport+proj, so a
    // direct SV_Position fetch aligns 1:1). Read UNCONDITIONALLY so the storage
    // texture stays live in reflection (a debug-only read gets DCE-stripped).
    // Applied only to GROUND fragments' sun term below; tall sprites (trees/
    // walls — light_pos is their base-tile centre, ≠ world_pos) keep the SDF
    // self-shadow so they stay lit on top. shadow_mask_str=0 → exact no-op.
    const float sun_mask_cov  = ShadowMask.Load(int3((int)i.pos.x, (int)i.pos.y, 0)).r;
    const bool  frag_is_tall  = (i.light_pos.x != i.world_pos.x)
                                || (i.light_pos.y != i.world_pos.y);

    float3 sun_contrib = float3(0.0, 0.0, 0.0);
    if(sun_intensity > 0.001 && sky_vis > 0.05 && sdf_map_w > 0u) {
        const float2 toward_sun = -float2(sun_dir_x, sun_dir_y);
        // Sun shadow (Stage 2b): the unified coverage occluder marched in 3D toward
        // the sun by sky_sun.comp → SkyBuf.a (0 shadowed .. 1 lit). Replaces the
        // inline SDF trace + SunSdfBuf — half-walls/furniture register by coverage,
        // roofs/overhangs by the roof bit, and a HIGH sun clears what a LOW sun
        // shadows (real elevation). Debug mode 14 shows this term in isolation.
        const float sun_shadow = sky_dir.a;
        // Per-pixel Lambert against the sun (Bucket A / A1). The 3D sun ray is
        // (toward_sun.xy, sin_elev). At NRM_AMOUNT=0 this collapses to the old
        // flat-normal value: dot((0,0,1), normalize(toward_sun, sin_elev)) =
        // sin_elev / sqrt(1 + sin_elev^2).
        const float flat_sun = sun_sin_elev / sqrt(1.0 + sun_sin_elev * sun_sin_elev);
        const float3 sun_L   = normalize(float3(toward_sun, sun_sin_elev));
        const float sun_lambert = saturate(lerp(flat_sun, saturate(dot(normal, sun_L)), nrm_amount));
        // Silhouette mask darkens the GROUND sun term (knob-gated; 0=identity).
        // Tall sprites skip it (mask_term=1) so trees/walls stay lit on top.
        // NOTE (Phase 2.2): trees still ALSO cast via the SDF (sun_shadow) here →
        // a temporary double shadow until 2.3 drops trees from the sun SDF.
        const float mask_term = frag_is_tall
                                ? 1.0
                                : saturate(1.0 - sun_mask_cov * shadow_mask_str);
        sun_contrib = float3(sun_r, sun_g, sun_b) * sun_intensity * sun_lambert
                      * sun_shadow * sky_vis * mask_term;
        // Wet specular glint from the sun (same gating: sky_vis + shadow + mask).
        const float sun_spec = (spec_strength > 0.001) ? spec_strength * wet_spec(normal, sun_L) : 0.0;
        sun_contrib += float3(sun_r, sun_g, sun_b) * sun_intensity
                       * sun_shadow * sky_vis * mask_term * sun_spec;
    }

    // Apply runtime tuning scales BEFORE compositing. emitter_scale tunes
    // global brightness of all emitter contributions; sun_scale / sky_scale
    // tune the sun + sky streams. Defaults are 1.0 (no-op).
    emitter_light *= emitter_scale;
    sun_contrib   *= sun_scale;
    sky_contrib   *= sky_scale;

    // Ambient occlusion (Bucket A / A4). SDF-proximity cavity term: 8 short taps
    // of the (4x-supersampled, transposed) SDF around the fragment via the shared
    // sdf_bilinear, averaged to a mean openness. Near walls the taps read small →
    // darker; in the open they saturate to 1 → no-op. Deliberately the SDF cavity
    // term, NOT a normal-hemisphere trace: in top-down the relief normal is near
    // z-up so normal-weighting adds noise, not shape; wall proximity is the term
    // that reads as 3D here. AO modulates ONLY the directionless fills (ambient
    // floor + sky + GI) below — emitter/sun already self-shadow via trace_shadow,
    // so AO must not touch them or crevices double-darken. ao_strength=0 → exact
    // no-op (off, the committed default).
    float ao = 1.0;
    if(ao_strength > 0.001 && sdf_map_w > 0u) {
        const float aor = 1.5;              // tap radius in tiles
        const float ad  = aor * 0.70711;    // diagonal taps at the same radius
        float open = 0.0;
        open += saturate(sdf_bilinear(i.light_pos + float2( aor, 0.0)) / aor);
        open += saturate(sdf_bilinear(i.light_pos + float2(-aor, 0.0)) / aor);
        open += saturate(sdf_bilinear(i.light_pos + float2( 0.0, aor)) / aor);
        open += saturate(sdf_bilinear(i.light_pos + float2( 0.0,-aor)) / aor);
        open += saturate(sdf_bilinear(i.light_pos + float2( ad,  ad)) / aor);
        open += saturate(sdf_bilinear(i.light_pos + float2(-ad,  ad)) / aor);
        open += saturate(sdf_bilinear(i.light_pos + float2( ad, -ad)) / aor);
        open += saturate(sdf_bilinear(i.light_pos + float2(-ad, -ad)) / aor);
        open *= 0.125;                      // mean of the 8 taps
        ao = lerp(1.0, open, saturate(ao_strength));
    }

    // Ambient floor. In-game (sdf_map_w>0) it auto-tracks time-of-day:
    // lerp(night_floor, day_floor, sun_intensity) → darker, more immersive
    // nights (effect 4). UI / main menu (no world) keep the CPU `ambient`
    // uniform so backdrops/text are unaffected. night_floor==day_floor
    // disables the day/night swing (bisect).
    const float amb_floor = (sdf_map_w > 0u)
                            ? lerp(night_floor, day_floor, saturate(sun_intensity))
                            : ambient;
    const float3 ambient_v = float3(amb_floor, amb_floor, amb_floor) * ao;

    // Multi-band ordered (Bayer) dither, world-locked. Quantise ONLY the
    // dynamic light (emitter + sky + sun) into `dither_bands` levels and
    // stipple the band transitions. The ambient floor is
    // added AFTER so flat near-black areas stay clean (dithering the floor
    // makes dark areas sparkle). Ordered dither is mean-preserving, so no
    // global brightness shift. Anchored to world PIXELS (world_pos *
    // tile_pixel_size) → pattern sticks to terrain, no shimmer on scroll.
    float3 dyn = emitter_light + sky_contrib * ao + sun_contrib;
    // 1-bounce indirect fill (fake GI): colored light diffused off surfaces into
    // open neighbours on the CPU, added here before dither so it bands with the
    // rest of the dynamic light.
    if(gi_strength > 0.001 && sdf_map_w > 0u) {
        dyn += gi_strength * indirect_bilinear(i.light_pos) * ao;
    }
    if(dither_amt > 0.001) {
        const float  bands = max(dither_bands, 1.0);
        const float  bthr  = dither_threshold(i.world_pos * tile_pixel_size);
        const float3 dithered = floor(dyn * bands + bthr) / bands;
        dyn = lerp(dyn, dithered, saturate(dither_amt));
    }

    // GPU total light (dithered dynamic light + un-dithered ambient floor).
    const float3 gpu_total = min(ambient_v + dyn, float3(2.0, 2.0, 2.0));
    // max(tint, gpu_total):
    //   Game tiles:  tint = 0 (set by CPU side) → gpu_total drives brightness
    //   UI / fonts:  tint = element color → stays fully visible (unlit segment also zeroes emitter_count)
    //   Main menu:   tint = 1.0 (no game state) → bright; emitters add glow
    const float3 combined = max(i.tint.rgb, gpu_total);
    float3 final_rgb = texel.rgb * combined;

    // Debug visualisation. Modes 1-5 BLEND a per-component visualisation
    // over the lit scene at debug_opacity; modes 6-7 REPLACE the scene with
    // raw SDF / sky_vis colormaps. Modes 1-7 are gated to game tiles
    // (tint near zero) so HUD glyphs / UI rects always render normally.
    // Mode 8 (emit_bw diagnostic) bypasses the tint gate so it works on the
    // tinted main-menu blue backdrop — emitter_count==0 segments still
    // short-circuit, so HUD/font segments stay untouched.
    const float dbg_tint_sum = i.tint.r + i.tint.g + i.tint.b;

    // (effect 3) Memory distance-fade. Memorized tiles carry light_mul =
    // -(distance from player in tiles). Dim by distance, floored at mem_dim so
    // remembered terrain persists (never black). Applied post-combine (after
    // max(tint, gpu_total)) so current lighting can't leak into unseen memory.
    // Desaturation already comes from the tileset memory FX. Not tint-gated:
    // memory tiles have tint=1.0, the negative marker is the sole trigger.
    if(i.light_mul < -0.0001) {
        const float d = -i.light_mul;
        const float t = saturate(1.0 - d / max(mem_radius, 1.0));
        final_rgb *= mem_dim + (1.0 - mem_dim) * t;
    }

    const bool  dbg_active   = (debug_mode == 8u)
                               || (debug_mode > 0u && debug_mode != 8u
                                   && dbg_tint_sum < 0.01);
    // Height depth: dims canopy to enhance depth perception. dark_frac=0 at base → no-op.
    // Applied after memory-fade, before debug modes — so debug replace=true cleanly overrides.
    final_rgb *= ( 1.0 - i.dark_frac );

    if(dbg_active) {
        float3 vis = float3(0, 0, 0);
        bool   replace = false;
        if(debug_mode == 1u) {
            vis = ambient_v;
        } else if(debug_mode == 2u) {
            vis = emitter_light;
        } else if(debug_mode == 3u) {
            vis = sun_contrib;
        } else if(debug_mode == 4u) {
            vis = sky_contrib;
        } else if(debug_mode == 5u) {
            vis = gpu_total;
        } else if(debug_mode == 6u) {
            // SDF view: red (wall, s≈0) → yellow (s≈4) → green (open, s≥8).
            // Bilinear at light_pos — the SAME sample the shadow march uses, so
            // tall sprites (walls/trees) show their base-tile distance, not the
            // sprite's own s≈0 tile (frag_is_tall: light_pos != world_pos).
            const float s = (sdf_map_w > 0u) ? sdf_bilinear(i.light_pos) : 0.0;
            const float t = saturate(s / 8.0);
            vis = float3(1.0 - t, t, 0.0);
            replace = true;
        } else if(debug_mode == 7u) {
            // SkyVis view: grayscale 0..1. Sample at light_pos (base-tile centre
            // for tall sprites) to match the live sky-vis read, not world_pos.
            const int sx = clamp((int)i.light_pos.x, 0, (int)sdf_map_w - 1);
            const int sy = clamp((int)i.light_pos.y, 0, (int)sdf_map_h - 1);
            const float v = (sdf_map_w > 0u) ? SkyVisBuf[sx * (int)sdf_map_h + sy] : 0.0;
            vis = float3(v, v, v);
            replace = true;
        } else if(debug_mode == 8u) {
            // emit_bw — grayscale luminance of accumulated emitter
            // contribution. Bypasses tint gate so the main-menu blue
            // backdrop sprite reveals the gradient. Bright top-left
            // fading toward dim = working emitter pipeline.
            const float L = max(emitter_light.r, max(emitter_light.g, emitter_light.b));
            vis = float3(L, L, L);
            replace = true;
        } else if(debug_mode == 9u) {
            // Normal view (Bucket A / A1): encode the inline-Sobel surface
            // normal as RGB via n*0.5+0.5. Flat tiles read ~(0.5,0.5,1.0)
            // lavender-blue; relief tilts push R (x-slope) / G (y-slope).
            // Replace so it shows raw; game tiles only (tint-gated below).
            vis = normal * 0.5 + 0.5;
            replace = true;
        } else if(debug_mode == 10u) {
            // AO view (Bucket A / A4): grayscale mean SDF-openness. White =
            // fully open (ao≈1, no darkening), darkening toward walls/crevices.
            // Uniform white = ao_strength is 0 or the SDF is empty.
            vis = float3(ao, ao, ao);
            replace = true;
        } else if(debug_mode == 11u) {
            // Shadow-mask view (Phase 2.1 alignment gate): the silhouette mask
            // .Load()ed at this fragment's SCREEN pixel. Should match the
            // Phase-1 sampler debug blit (g_shadow_debug) — same silhouettes in
            // the same screen positions = the storage-read fetch is aligned.
            const float m = ShadowMask.Load(int3((int)i.pos.x, (int)i.pos.y, 0)).r;
            vis = float3(m, m, m);
            replace = true;
        } else if(debug_mode == 12u) {
            // GI view: the raw 1-bounce indirect field (GiBuf) from the compute
            // GI pass, shown directly as colour — independent of gi_strength,
            // ambient, and dither. Lit emitters appear as coloured blobs that
            // bleed into shadow / around corners (the bounce). Uniform black =
            // GI not computing (no emitters in range, SDF not ready, or the
            // compute pass failed to create). The definitive GI confirmation.
            vis = (sdf_map_w > 0u) ? indirect_bilinear(i.light_pos) : float3(0.0, 0.0, 0.0);
            replace = true;
        } else if(debug_mode == 13u) {
            // Sky-access view (Stage 2a): GREY = per-tile directional sky-access
            // (hemisphere fraction reaching open sky) straight from sky_sun.comp,
            // independent of sky colour/intensity, ambient, GI, dither. Grade:
            // open ground ≈ white; alcove/overhang/against-wall = mid-grey;
            // indoor near a window = partial (FROM the opening); deep interior =
            // black. Uniform black everywhere = pass not computing (no SDF / pipe
            // failed). 2D limit: an open tile boxed in by walls reads dark.
            const float a = (sdf_map_w > 0u) ? sky_bilinear(i.light_pos).r : 0.0;
            vis = float3(a, a, a);
            replace = true;
        } else if(debug_mode == 14u) {
            // Sun-occlusion view (Stage 2b): GREY = the per-tile celestial shadow
            // sky_sun.comp marches from the unified coverage occluder into SkyBuf.a
            // (1 = lit toward the sun, 0 = shadowed). This IS the live sun shadow now
            // (sun_shadow = SkyBuf.a; the inline fragment trace + SunSdfBuf are gone).
            // Independent of sun colour/intensity. Uniform white = no occluders / off.
            const float s = (sdf_map_w > 0u) ? sky_bilinear(i.light_pos).a : 0.0;
            vis = float3(s, s, s);
            replace = true;
        }
        if(replace) {
            final_rgb = vis;
        } else {
            final_rgb = lerp(final_rgb, vis, saturate(debug_opacity));
        }
    }
    return float4(final_rgb, texel.a * i.tint.a);
}

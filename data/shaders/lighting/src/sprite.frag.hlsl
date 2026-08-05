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
//   t5       GiBuf       — StructuredBuffer<float>, 4 floats/tile    (storage buf slot 3)
//   t6       SkyBuf      — StructuredBuffer<float>, 4 floats/tile    (storage buf slot 4)
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
// NOTE: fragment storage buffers MUST stay contiguous and every one MUST be read.
// VisBuf, the live-visibility buffer, used to sit at t5; "remove vision overlay"
// (46261515ac) deleted its only reader and left the declaration orphaned. DXC then
// stripped the unused buffer, punching a hole in the t2..t7 SRV range: shadercross
// reflected 5 storage buffers while the DXIL still referenced t7, so D3D12 rejected
// the root signature with E_INVALIDARG and the whole lighting pipeline failed to
// build. The buffer and its CPU-side field are now fully deleted; the sub-tile
// vision carve marches SdfBuf instead.
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
// (REPLACES the inline sun shadow march + SunSdfBuf — Stage 2b).
StructuredBuffer<float>      SkyBuf      : register(t6, space2);
// Step 7 palette shade ramps. Appended at t7/t8 so no existing slot renumbers.
// Storage BUFFERS, not sampled textures, for three reasons all recorded in this
// repo: shadercross mis-binds sampler textures on Metal (silent zeros); adding
// samplers would shift the storage-texture and every storage-buffer register in
// lockstep; and the lookup wants nearest/integer indexing anyway. BOTH are read
// UNCONDITIONALLY below (gated with lerp, never an `if`) — D3D12 strips an unread
// fragment storage buffer and the hole breaks the root signature.
StructuredBuffer<uint>       RampBuf     : register(t7, space2); // palette_size*steps RGBA8
StructuredBuffer<uint>       PalIdxBuf   : register(t8, space2); // 32^3 palette rows
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
    float self_eps_tall;     // soft_shadow_march self-shadow escape radius, TALL sprites
    float ramp_enable;       // 0 = plain multiply, 1 = full palette-ramp resolve
    float ramp_steps;        // shade steps per palette row
    float ramp_chroma;       // how much coloured light tints the ramped surface
    float gi_bilat;          // Step 6: 1 = SDF-bilateral GI upsample, 0 = plain bilinear
    float vis_edge;          // Step 8: 1 = feathered vision frontier, 0 = hard tile edge
    // Normalised V offset from a colour texel to its NORMAL texel in the same atlas
    // page (0.5 for a double-height page). 0.0 = no page carries normals, and
    // atlas_normal() below is then an exact identity. Mirrors debug_params::nrm_atlas_v.
    float nrm_atlas_v;
    // Signed strength of the per-sprite vertical-face arc. Mirrors debug_params::face_arc.
    float face_arc;
    float nrm_radial_amount;
    float cloud_strength;
    float cloud_scale;
    float cloud_wind_x;
    float cloud_wind_y;
    float cloud_threshold;
    float cloud_softness;
    float cloud_pad0;
    float cloud_pad1;
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
    // Mirrors sprite.vert.hlsl's VS_OUT member-for-member, INCLUDING the absence of
    // `nointerpolation` — see the long note there. Briefly: that qualifier made D3D12
    // pipeline creation fail (0x80070057) whenever this shader did not consume the
    // value, which blacked out the entire world, and it bought nothing because the
    // value is a per-instance constant that interpolates exactly. Consumers still
    // compare BY BAND, never ==.
    float light_mode : TEXCOORD7; // sprite_light_mode: 0 unlit, 1 gpu_lit, 2 memory
    float3 flash : TEXCOORD8; // coloured light override: colour * strength, max(colour) == 1
    // Per-sprite "this is a vertical surface" amount (sprite_instance::face_amt), 0..1.
    // Per-instance constant, so interpolation across the quad is exact; and like flash it
    // is a QUANTITY, not a categorical selector, so drift could only nudge a shade.
    float face_amt : TEXCOORD10;
    // Quad-local vertical fraction: 0 at the sprite's TOP edge, 1 at its BOTTOM edge.
    // Taken from quad_uv[vid].y BEFORE any UV flip, because it describes physical height
    // on screen, not texture addressing. This is the ONLY vertical position information
    // available for a 1-tile sprite: light_pos == world_pos for anything not `is_tall`,
    // so the old light_pos.y - world_pos.y trick is identically zero for every wall.
    // MUST NOT be `nointerpolation` -- see the long note on light_mode above; that
    // qualifier broke D3D12 pipeline creation outright (0x80070057).
    // Sprite centre in UV space and half-extents, for radial macro-normal in fragment.
    // Per-instance constant, so interpolation is exact.
    float2 center_uv : TEXCOORD11;
    float2 uv_half   : TEXCOORD12;
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
// Bilateral GI upsample: reject taps whose SDF differs sharply from the sample
// point's, so bounce light does not cross a wall. GI is one probe per TILE, so a
// plain bilinear blends a lit interior probe with the dark exterior probe on the
// other side of the wall — indoor bounce bleeds outdoors and the wall edge reads
// as a blocky tile-scale blob. Weighting each tap by SDF similarity fixes the
// dominant GI artefact for four extra SDF taps, where raising probe density to
// 2x2/tile would cost 4x the compute.
//
// This only bites once the SDF carries sub-tile detail (Step 3): with tile-square
// occluders there is no sub-tile contrast to key on.
static const float GI_BILAT_SIGMA = 0.35; // tiles
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
    const float3 plain = lerp(lerp(a, b, w.x), lerp(c, d, w.x), w.y);

    // Bilinear weights, then an SDF-similarity factor per tap. Tap centres are the
    // probe tile centres (integer tile + 0.5), which is what sdf_bilinear expects.
    const float sd_c = sdf_bilinear(p);
    const float wa = (1.0 - w.x) * (1.0 - w.y);
    const float wb =        w.x  * (1.0 - w.y);
    const float wc = (1.0 - w.x) *        w.y;
    const float wd =        w.x  *        w.y;
    const float inv_sigma = 1.0 / GI_BILAT_SIGMA;
    const float ba = wa * exp(-abs(sdf_bilinear(float2(x0,     y0    ) + 0.5) - sd_c) * inv_sigma);
    const float bb = wb * exp(-abs(sdf_bilinear(float2(x0 + 1, y0    ) + 0.5) - sd_c) * inv_sigma);
    const float bc = wc * exp(-abs(sdf_bilinear(float2(x0,     y0 + 1) + 0.5) - sd_c) * inv_sigma);
    const float bd = wd * exp(-abs(sdf_bilinear(float2(x0 + 1, y0 + 1) + 0.5) - sd_c) * inv_sigma);
    const float wsum = ba + bb + bc + bd;
    // A fully-rejected neighbourhood must not produce black — fall back to the
    // unweighted result rather than dividing by ~0. gi_bilat lerps the whole term
    // back to plain bilinear so the upsample is A/B-able at runtime like every
    // other step in this plan.
    const float3 bilat = (wsum < 1e-4) ? plain : ((a * ba + b * bb + c * bc + d * bd) / wsum);
    return lerp(plain, bilat, saturate(gi_bilat));
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
// Soft-shadow sphere trace, shared with gi_field.comp and vol.frag. Included
// here (not at the top) because it calls sdf_bilinear, defined above.
#include "shadow_trace.hlsl"
// 4x4 ordered (Bayer) dither matrix, values 0..15.
static const float k_bayer4[16] = {
     0.0,  8.0,  2.0, 10.0,
    12.0,  4.0, 14.0,  6.0,
     3.0, 11.0,  1.0,  9.0,
    15.0,  7.0, 13.0,  5.0
};
// Ordered Bayer threshold in (0,1). Keyed to world ART-TEXEL coords (shade_pos *
// texels_per_tile) so the 4x4 cell is exactly 4 tileset texels wide at EVERY zoom
// level and sticks to the terrain — it neither shimmers when the camera pans nor
// changes size relative to the artwork when the view zooms.
float dither_threshold(float2 world_px) {
    const int bx = ((int)floor(world_px.x)) & 3;
    const int by = ((int)floor(world_px.y)) & 3;
    return (k_bayer4[by * 4 + bx] + 0.5) / 16.0;
}
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
// --- Step 7 palette shade ramp helpers -------------------------------------
float luma(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }
float3 unpack_rgba8(uint p) {
    return float3((p & 0xFFu), ((p >> 8) & 0xFFu), ((p >> 16) & 0xFFu)) * (1.0 / 255.0);
}
uint pal_index_of(float3 rgb) {   // 5 bits/channel → 32^3
    const uint3 q = (uint3)clamp(rgb * 31.0 + 0.5, 0.0, 31.0);
    return (q.r * 32u + q.g) * 32u + q.b;
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
// --- Procedural normal atlas (double-height page) --------------------------
// Each atlas page carries colour in its TOP half and, at the SAME rect in its
// bottom half, a normal map generated from the sprite art at tileset load
// (src/lighting/normal_gen.*). It is a V OFFSET rather than a second texture
// because shadercross silently mis-binds every sampler texture past slot 0 on
// Metal — see the note at the top of this file; Atlas is the one sampler
// binding known to work, so nothing new is bound here.
//
// Texel encoding (RGBA8): rg = nx/ny biased to 0..1, b = the coherence-gated
// per-pixel amplitude used as the blend weight against the alpha bevel, a = 255.
// The generator deliberately emits b = 0 for dither-noise art rather than
// fabricating relief, so an unstructured sprite falls back to `bevel_n` on its
// own without needing a per-sprite opt-in.
//
// nrm_atlas_v is the normalised V distance from a colour texel to its normal
// texel (0.5 for a double-height page). 0.0 means no page carries normals and
// this is an exact identity on the alpha-bevel normal.
float3 atlas_normal(float2 uv, float3 bevel_n) {
    if(nrm_atlas_v <= 0.0) { return bevel_n; }
    const float4 t   = Atlas.Sample(AtlasSmp, uv + float2(0.0, nrm_atlas_v));
    const float2 nxy = t.rg * 2.0 - 1.0;
    const float  nz  = sqrt(saturate(1.0 - dot(nxy, nxy)));
    return normalize(lerp(bevel_n, float3(nxy, nz), saturate(t.b)));
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
    float4 texel = Atlas.Sample(AtlasSmp, i.uv);
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
    // --- Lighting composite mode (src/tile_light_mode.h) ---------------------
    // Compared BY BAND, never ==: the varying is nointerpolation, but a band test
    // costs nothing and makes any drift fail toward the nearest class instead of
    // silently to `unlit`, which renders full unlit albedo — the exact defect this
    // composite replaces.
    const bool mode_memory  = i.light_mode > 1.5;
    const bool mode_gpu_lit = !mode_memory && i.light_mode > 0.5;
    // --- Step 8: sub-tile vision frontier -----------------------------------
    // The seen/remembered boundary is decided ONCE PER TILE: remembered terrain is
    // dimmed uniformly to `mem_dim`, and never-seen tiles get a full-tile `lighting_*`
    // overlay. Either way the frontier can only be a tile staircase — the last grid
    // artefact, and a jarring one beside the now-continuous lighting.
    //
    // cata_tiles encodes the 8 neighbours' "also not clearly visible" bits NEGATIVELY
    // into the outline lane (only ever tested > 0.5 above, for the hover silhouette),
    // so this costs no new vertex attribute, cbuffer field or GPU buffer.
    //
    // Corner coverage = mean of the 4 cells meeting at that corner (this tile is
    // always on the hidden side, so it contributes 1), then bilinear across the tile.
    // That is the standard marching-squares smoothing: a diagonal step becomes a
    // diagonal ramp instead of a right angle, and a convex corner rounds off.
    //
    // The remap MUST be zero at cov = 0.5 and one at cov = 1.0. Only the hidden side
    // feathers — the visible neighbour is untouched — and on a straight frontier the
    // two corners on the shared edge both average to exactly 0.5. Any band centred on
    // 0.5 (e.g. smoothstep(0.3,0.7)) would leave that edge at half dim against a
    // neighbour at zero: a softer staircase, but a staircase. Anchoring the ramp at
    // 0.5 puts the whole feather INSIDE the hidden tile, so it meets the visible
    // neighbour continuously and the seam disappears.
    //
    // The feather can only ever WEAKEN this tile's own hiding, and only toward
    // neighbours the CPU already draws as visible — it cannot reveal anything the
    // game considers unseen.
    float frontier_cov = 1.0;
    bool  frontier_desat = false;
    if(i.outline < -0.5) {
        const uint enc = (uint)(-i.outline - 1.0 + 0.5);
        // Bit 8 selects the treatment: set = desaturate (the dim edge of sight),
        // clear = hide/dim (remembered tile, or a never-seen tile's overlay).
        frontier_desat = (enc & 256u) != 0u;
        const uint m = enc & 255u;
        // Bit order matches cata_tiles: W, E, N, S, NW, NE, SW, SE.
        const float w  = (m & 1u)   != 0u ? 1.0 : 0.0;
        const float e  = (m & 2u)   != 0u ? 1.0 : 0.0;
        const float n  = (m & 4u)   != 0u ? 1.0 : 0.0;
        const float s  = (m & 8u)   != 0u ? 1.0 : 0.0;
        const float nw = (m & 16u)  != 0u ? 1.0 : 0.0;
        const float ne = (m & 32u)  != 0u ? 1.0 : 0.0;
        const float sw = (m & 64u)  != 0u ? 1.0 : 0.0;
        const float se = (m & 128u) != 0u ? 1.0 : 0.0;
        const float c_nw = (1.0 + n + w + nw) * 0.25;
        const float c_ne = (1.0 + n + e + ne) * 0.25;
        const float c_sw = (1.0 + s + w + sw) * 0.25;
        const float c_se = (1.0 + s + e + se) * 0.25;
        // Position within this tile. camera_off carries a +0.5, so a tile CENTRE sits
        // at an integer world_pos and its edges at +/-0.5 (the same p-0.5 convention
        // sdf_bilinear uses). Shift by half a tile so f = 0 is the north/west edge,
        // matching the corner naming above. y is south-down.
        float2 f = frac(i.world_pos + 0.5);
        // Quantise to the art-texel lattice so the feather stays pixel-art crisp
        // instead of becoming a smooth photographic ramp (same lattice as shade_pos).
        if(light_quant > 0.5 && texels_per_tile > 0.5) {
            f = (floor(f * texels_per_tile) + 0.5) / texels_per_tile;
        }
        const float cov = lerp(lerp(c_nw, c_ne, f.x), lerp(c_sw, c_se, f.x), f.y);
        frontier_cov = lerp(1.0, smoothstep(0.5, 1.0, cov), saturate(vis_edge));
    }
    // Dim edge of sight. cata_tiles used to swap in a whole GREYSCALE atlas variant
    // for a lit_level::LOW tile, which made the edge of vision a hard per-tile colour
    // change. It now keeps the normal sprite and sends us the frontier shape instead,
    // so reproduce color_pixel_grayscale here — feathered, so the desaturation ramps
    // in across the boundary tile rather than snapping on at its edge.
    //
    // Mirrors sdl_utils.cpp exactly at frontier_cov = 1, so a tile deep inside a LOW
    // region renders as it always did:
    //     average_pixel_color: av = 85 * (r + g + b) >> 8   (85/256, NOT 1/3)
    //     color_pixel_grayscale: max(av * 5 >> 3, 1), pure black passed through
    // The atlas is R8G8B8A8_UNORM, so the shader samples the very same encoded values
    // the CPU filter operated on — no sRGB/linear conversion sits between them. Only
    // the integer truncation at each step is dropped, worth at most 1/255.
    if(frontier_desat) {
        const float av = (texel.r + texel.g + texel.b) * (85.0 / 256.0);
        const float gv = max(av * 0.625, 1.0 / 255.0);
        // is_black() passes pure black through untouched; without this the 1/255 floor
        // would lift every transparent-black texel to a visible dark grey.
        const bool  blk = (texel.r + texel.g + texel.b) <= 0.0;
        const float3 grey = blk ? texel.rgb : float3(gv, gv, gv);
        texel.rgb = lerp(texel.rgb, grey, frontier_cov);
    }
    // A never-seen tile's overlay sprite (drawn at lit_level::LIT, so it classifies as
    // gpu_lit) fades out at the frontier. Remembered terrain instead arrives in the
    // memory MODE and is handled by the rad_mem cross-fade below, where the SAME
    // coverage lerps the dim — so its own alpha must stay solid. The distance value
    // still comes from light_mul; only the CLASS test moved to light_mode.
    // A desaturate-kind marker has already been consumed above and must not also
    // punch a hole in the sprite's alpha.
    const float vis_overlay_a = (frontier_desat || mode_memory) ? 1.0 : frontier_cov;
    // Per-pixel surface normal from inline alpha-shape bevel (Bucket A / A1),
    // driving the emitter + sun Lambert. TALL sprites (creatures, trees, walls,
    // tall furniture — light_pos != world_pos) get a GENTLE bevel (30% of full
    // nrm_amount). The alpha-shape gradient no longer catches interior artwork
    // hatching (the old albedo-Sobel "weird toning" cause), so tall sprites can
    // safely receive subtle silhouette relief without darkening. Ground terrain
    // (light_pos == world_pos) keeps the full tuned relief.
    const bool   frag_is_tall_n = ( i.light_pos.x != i.world_pos.x )
                                  || ( i.light_pos.y != i.world_pos.y );
    // Art-texel light quantisation. Light is resolved once per TILESET texel rather
    // than per screen pixel, so a zoomed sprite shades in its own pixel blocks
    // instead of receiving a continuous gradient across them. 1/32 tile is 4x finer
    // than the SDF subcell grid, so sub-tile shadow curvature is preserved.
    // MUST be computed AFTER frag_is_tall_n, which compares the UNSNAPPED values.
    float2 shade_pos = i.light_pos;
    if(light_quant > 0.5 && texels_per_tile > 0.5) {
        shade_pos = (floor(i.light_pos * texels_per_tile) + 0.5) / texels_per_tile;
    }
    const float3 bevel_n = frag_is_tall_n
                           ? lerp( float3( 0.0, 0.0, 1.0 ), surface_normal( i.uv ), nrm_entity_amount )
                           : surface_normal( i.uv );
    // Procedural normal atlas, when a page carries one; exact identity otherwise.
    const float3 base_n = atlas_normal( i.uv, bevel_n );
    // Radial macro-normal: treat each sprite as a gently rounded cylinder.
    // Offset from centre, normalised by half-extents (aspect-correct, [-0.5, 0.5]).
    // Centre faces viewer (+Z), edges face sideways. Blends with atlas micro-relief
    // so edge detail survives. nrm_radial_amount=0 is exact no-op.
    // GATED: only applies to sprites WITHOUT face_arc (face_amt == 0). Walls, windows,
    // and tall furniture use their per-edge cardinal normals instead.
    const float2 r_offset = (i.uv - i.center_uv) / i.uv_half;
    const float3 radial_n = normalize(float3(r_offset, 1.0));
    const float3 normal_macro = (i.face_amt > 0.001f)
                                ? base_n
                                : normalize(lerp(base_n, radial_n, saturate(nrm_radial_amount)));
    // Vertical-face arc. The alpha-shape bevel in surface_normal() CANNOT shade a sprite
    // body: alpha IS its height field, so a fully-opaque wall face has zero gradient and
    // comes out exactly (0,0,1). Measured in debug view 9: grass, asphalt, wall and
    // building interior all read 121.6/121.6/243.5 -- identical. This arc supplies the
    // MACRO orientation the bevel structurally cannot.
    //
    // -1 at the top .. +1 at the base. Screen/map +Y is DOWN (south), so pushing the
    // normal's y toward +1 at the base makes the base face SOUTH -- lit when the light
    // is south of the tile -- while the top swings north and falls into shade. That
    // gradient is what reads as a wall FACE instead of a uniformly brighter square.
    //
    // face_arc is the F4-tunable strength of the rotation toward the outward normal.
    // Exact no-op when face_amt == 0: the tilt is identically zero and `base_n` is
    // returned untouched, with no renormalisation to perturb it. Every sprite that is
    // not a wall, window or tall furniture -- including every zero-initialised UI, font
    // and overlay instance -- ships face_amt == 0.
    // DECODE face_amt ONCE. Packed as `edge_mask + amount` (see sprite_instance::face_amt):
    // floor() = 4-bit mask of which tile EDGES are exposed to the outside (1=N, 2=E, 4=S,
    // 8=W), frac() = amount, and exactly 0 means "not a vertical face". Every consumer
    // below uses the decoded values, never the raw varying, or it reads a mask as a
    // magnitude.
    // Encoded as mask + 0.25 + 0.5 * amount, so the fraction sits in [0.25, 0.75] and
    // interpolation drift can never carry floor() across an integer boundary (which
    // previously zeroed the mask and the amount at once -- measured as a byte-identical
    // frame). Round rather than floor for the same reason.
    const float face_mask_f = floor( i.face_amt );
    const float face_amt    = ( i.face_amt - face_mask_f - 0.25 ) * 2.0;
    // Outward direction of the face this fragment belongs to; also steps the sun's
    // sky-visibility sample below, so it must be the direction the face LOOKS, not the
    // direction of the sun.
    float2 face_out = float2( 0.0, 0.0 );
    float3 normal   = normal_macro;
    if( face_amt > 0.001 ) {
        const uint mask = (uint)( face_mask_f + 0.5 );
        // In-tile position, 0..1, y south-down — same convention as the corner feather.
        const float2 ft = frac( i.world_pos + 0.5 );
        // PER-EDGE normals, like the chamfer of a bevelled box: each exposed side owns a
        // strip whose normal points out of the building, so the lit and shaded faces of
        // one wall run differ. A corner tile has TWO exposed edges and gets both, blended.
        //
        // Per-edge rather than one direction per sprite because a single averaged
        // direction cancels on a corner and cannot describe two faces at once. And unlike
        // Graveyard Keeper -- which solves this by having an artist paint light from four
        // sides per sprite and merging that into a normal map -- a tile grid hands us the
        // topology for free, so walls need no authored art at all.
        const float bev = 0.42;                     // strip width, tile units
        float2 acc = float2( 0.0, 0.0 );
        if( mask & 1u ) { acc += float2(  0.0, -1.0 ) * saturate( 1.0 - ft.y / bev ); }
        if( mask & 2u ) { acc += float2(  1.0,  0.0 ) * saturate( 1.0 - ( 1.0 - ft.x ) / bev ); }
        if( mask & 4u ) { acc += float2(  0.0,  1.0 ) * saturate( 1.0 - ( 1.0 - ft.y ) / bev ); }
        if( mask & 8u ) { acc += float2( -1.0,  0.0 ) * saturate( 1.0 - ft.x / bev ); }
        const float amag = length( acc );
        if( amag > 0.001 ) {
            face_out = acc / amag;
            // nz small but non-zero: with the sun near zenith (measured sin_elev 0.87) a
            // purely horizontal normal would drop the wall to black.
            const float3 face_n = normalize( float3( face_out, 0.30 ) );
            normal = normalize( lerp( base_n, face_n,
                                      saturate( face_amt * face_arc ) * saturate( amag ) ) );
        }
    }
    // Sky exposure (0 roofed .. 1 open), hoisted above the emitter loop so the
    // wet-specular term can gate on it (no indoor glint). SkyVisBuf is x-major
    // (skyvis[x*H+y]); bilinear so the indoor daylight-bleed gradient reads smooth.
    const float sky_vis = (sdf_map_w > 0u) ? saturate(skyvis_bilinear(shade_pos)) : 0.0;
    // emitter_light accumulates GPU point-light contributions (starts at zero).
    // It is one additive term of the radiance sum (emitters + sky + sun + GI); that
    // sum is folded with the ambient floor into `gpu_total` and MULTIPLIED onto
    // albedo in the composite below. Nothing maxes it against the tint. The comment
    // that used to sit here claimed the tint was combined "ADDITIVELY ... not
    // suppressed by max()", which contradicted the code even before the composite
    // was rewritten — the composite WAS a max().
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
        const float2 dv   = e_pos.xy - shade_pos;
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

        const float2 dv   = e_pos.xy - shade_pos;
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

        // Direct emitter shadows key the penumbra to the RECEIVER distance (the
        // textbook IQ form). Switching reference also changes what shadow_k
        // means: the penumbra half-angle is 1/k about the ray, so the legacy
        // k=8 that read correctly against the remaining-distance reference is
        // far too soft here - it makes the cone half-width t/8, which at 30
        // tiles dims anything passing within ~3.7 tiles of a wall. POINT_K_GAIN
        // restores the intended sharpness; raising it both tightens the
        // penumbra and removes that range dimming, since both scale as 1/k.
        const float POINT_K_GAIN = 4.0;
        const float  shadow = soft_shadow_march(shade_pos, sh_dir, dist,
                                            shadow_k * POINT_K_GAIN, (int)shadow_steps,
                                            frag_is_tall_n ? self_eps_tall : 0.05,
                                            /*ref_receiver=*/true);

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
    const float4 sky_dir = sky_bilinear(shade_pos);
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
    // sunbeam-through-window behaviour; the shadow march keeps it to tiles with a
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

    // ---- Sun exposure for a VERTICAL FACE -----------------------------------------
    // A building's roof covers its own wall footprint, so a wall tile is flagged
    // "not outside" and every per-tile sun gate below reads it as interior:
    //   * sky_vis     (own tile's TOP) -> 0, which fails the block's entry test
    //   * sky_dir.a   (sun_shadow, marched with the roof bit) -> shadowed
    //   * mask_term   exempts only `frag_is_tall`, and is_tall comes from sprite ART
    //                 height, which is false for EVERY wall (t_wall is 32x32 in
    //                 normal_terrain.png with no size override)
    // Measured: outside_true was 30586/32400 with 2066 roofed tiles at z=1 — the
    // building footprints. So walls rendered permanently veiled, never sunlit.
    //
    // A wall's FACE is exposed to the sky even though its top is covered, and per-tile
    // fields simply cannot express that. So for a vertical face take the sun's
    // exposure from the tile the face LOOKS TOWARD: a south wall is sunlit when the
    // tile south of it is open sky. `face_amt` is exactly the "this is a vertical
    // surface" signal, so this costs nothing on ground sprites (face_amt == 0 keeps
    // every term bit-identical).
    // Threshold, not `> 0`: cata_tiles assigns walls 1.0, transparent walls and
    // windows 0.6, and furniture only 0.5 * a coverage ramp. Relaxing the sun gates
    // for furniture too made scattered objects across the map pop as bright speckles,
    // because a chair is not a vertical face whose neighbour's sky belongs to it.
    // 0.55 admits walls and windows and excludes every furniture value.
    const bool  is_face   = face_amt > 0.55; // DECODED amount, not the packed varying
    // Step along the FACE's outward direction, not toward the sun: for a north-facing
    // wall with the sun to the south, stepping sunward samples the building INTERIOR,
    // where sky_vis is 0 — so the relaxation would still fail on half of every building.
    const float2 sun_step = face_out * 1.25;
    const float sun_sky_vis = is_face
                              ? max(sky_vis, (sdf_map_w > 0u)
                                    ? saturate(skyvis_bilinear(shade_pos + sun_step)) : 0.0)
                              : sky_vis;
    const float sun_occl = is_face
                           ? max(sky_dir.a, sky_bilinear(shade_pos + sun_step).a)
                           : sky_dir.a;

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
    // Clouds attenuate both direct sun AND diffuse sky light reaching the ground
    // (a passing cloud dims the whole sky-dome overhead, not just the sun disc) —
    // gated to sun_intensity>0.001 so this is an EXACT no-op at night/dusk/dawn,
    // matching the sun_contrib gate below and keeping the night look untouched.
    //
    // Sun-only darkening measured as only ~2% visible in the composited image even
    // at cloud_strength=1.0 (verified via debug modes 3/4 + AB pixel diff). Root
    // cause: open daylight tiles commonly push luma(rad_lit) ABOVE 1.0 (confirmed
    // via debug mode 5 — bright ground reads flat white, i.e. saturated), and both
    // the palette-ramp shade selector (`saturate(luma(rad_lit))`) and the plain
    // tonemap fallback (`saturate(hdr)`, used whenever ramp_enable=1) hard-clip at
    // exactly that point. A moderate cut to ONE term (sun) rarely drives the pixel
    // back under 1.0, so it stays clipped and invisible — only a cut deep enough to
    // cross that ceiling reads at all. Darkening sky too roughly doubles the
    // achievable cut per pixel; cloud_threshold/cloud_softness (below) are tuned
    // tight so more of the noise field actually reaches FULL coverage (cloud_mul at
    // its floor) rather than sitting in the soft partial-coverage gradient, where
    // the same clip renders it a no-op. This is a property of the existing
    // ramp/tonemap pipeline, not something this feature can route around locally.
    if(sun_intensity > 0.001) {
        sky_contrib *= cloud_mul;
    }

    float3 sun_contrib = float3(0.0, 0.0, 0.0);
    if(sun_intensity > 0.001 && sun_sky_vis > 0.05 && sdf_map_w > 0u) {
        const float2 toward_sun = -float2(sun_dir_x, sun_dir_y);
        // Sun shadow (Stage 2b): the unified coverage occluder marched in 3D toward
        // the sun by sky_sun.comp → SkyBuf.a (0 shadowed .. 1 lit). Replaces the
        // inline SDF trace + SunSdfBuf — half-walls/furniture register by coverage,
        // roofs/overhangs by the roof bit, and a HIGH sun clears what a LOW sun
        // shadows (real elevation). Debug mode 14 shows this term in isolation.
        const float sun_shadow = sun_occl;
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
        // `frag_is_tall` is sprite ART height, so it is false for every wall; a
        // vertical face must be exempted explicitly or the ground silhouette mask
        // darkens the very surface this block exists to light.
        const float mask_term = (frag_is_tall || is_face)
                                ? 1.0
                                : saturate(1.0 - sun_mask_cov * shadow_mask_str);
        sun_contrib = float3(sun_r, sun_g, sun_b) * sun_intensity * sun_lambert
                      * sun_shadow * sun_sky_vis * mask_term;
        // Wet specular glint from the sun (same gating: sky_vis + shadow + mask).
        const float sun_spec = (spec_strength > 0.001) ? spec_strength * wet_spec(normal, sun_L) : 0.0;
        sun_contrib += float3(sun_r, sun_g, sun_b) * sun_intensity
                       * sun_shadow * sun_sky_vis * mask_term * sun_spec;
        sun_contrib *= cloud_mul;
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
    // floor + sky + GI) below — emitter/sun already self-shadow in the march,
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
        dyn += gi_strength * indirect_bilinear(shade_pos) * ao;
    }
    if(dither_amt > 0.001) {
        const float  bands = max(dither_bands, 1.0);
        const float  bthr  = dither_threshold(shade_pos * texels_per_tile);
        const float3 dithered = floor(dyn * bands + bthr) / bands;
        dyn = lerp(dyn, dithered, saturate(dither_amt));
    }

    // GPU total light (dithered dynamic light + un-dithered ambient floor).
    const float3 gpu_total = min(ambient_v + dyn, float3(2.0, 2.0, 2.0));
    // What was here: a `combined` term that took the per-channel MAXIMUM of the memory
    // tint and `gpu_total`, then multiplied it onto the raw texel. It read as a blend
    // but was a SELECTOR. The CPU only ever emitted two tint values (cata_tiles.cpp
    // ~1932-1944): 0 on a lit world tile, so gpu_total won; and 1.0 on everything else
    // — UI, fonts, overmap, main menu, remembered terrain, and any tile whose CPU
    // lightmap read <= 0.001 — so 1.0 won. On a lit tile the CPU lightmap therefore
    // contributed no brightness whatsoever; it was a one-bit "is this tile lit at all"
    // gate.
    //
    // Its fallback failed BRIGHT. A genuinely pitch-dark but VISIBLE tile took
    // max(1.0, gpu_total) = 1.0, rendered at full unlit albedo and threw its own
    // shadows away, so shadows were structurally unrepresentable exactly where they
    // should be deepest. And because the gate was a hard binary at a 0.001 lightmap
    // threshold, a tile crossing it POPPED rather than ramped.
    //
    // The standard pipeline computes radiance ONCE and MULTIPLIES it onto albedo,
    // keeping visibility as a separate multiplicative mask. That is what follows:
    // one albedo, three radiances, and a categorical select on light_mode.
    const float3 albedo = texel.rgb * i.tint.rgb;

    // Coloured light override (melee hit-flash; main-menu backdrop blue base).
    // Encoded as `flash = colour * strength` with max(colour) == 1, so
    // strength == max3(flash) and `radiance * (1 - strength) + flash` is exactly
    // lerp(radiance, colour, strength) — hue-preserving at BOTH brightness extremes
    // out of a single lane, with no second strength channel to keep in sync.
    //
    // Deliberately NOT additive emissive: in daylight gpu_total ~ 1, so adding a red
    // (0.6, 0, 0) clips to white and destroys the hue that says WHAT was hit.
    // Deliberately NOT folded into `tint` (which is what the old code effectively
    // did): a tint multiply is multiplicative, so it vanishes at night — the only
    // place the old max()-based flash was ever visible in the first place.
    //
    // flash == 0 is an EXACT no-op: flash_s = 0 ⇒ flash_k = 1, so each radiance below
    // reduces to its unflashed value identically, with no epsilon and no branch.
    const float flash_s = saturate(max(i.flash.r, max(i.flash.g, i.flash.b)));
    const float flash_k = 1.0 - flash_s;

    // (effect 3) Memory distance-fade, hoisted up from the old post-composite block.
    // Remembered tiles carry light_mul = -(distance from player in tiles). Dim by
    // distance, floored at mem_dim so remembered terrain persists and never goes
    // black. Desaturation already comes from the tileset memory FX.
    //
    // -min(light_mul, 0.0) is deliberate: the lane is 0 or positive on every ordinary
    // sprite, so this yields distance 0 there and mem_mul collapses to 1.0. mem_mul is
    // only ever CONSUMED via rad_mem under mode_memory, so computing it branchlessly
    // costs one min and cannot reach any other mode.
    const float mem_d   = -min(i.light_mul, 0.0);
    const float mem_t   = saturate(1.0 - mem_d / max(mem_radius, 1.0));
    const float mem_mul = mem_dim + (1.0 - mem_dim) * mem_t;

    // The three radiances. Each is `base * flash_k + i.flash`, i.e.
    // lerp(base, flash_colour, flash_s), with base = gpu_total / 1.0 / mem_mul.
    const float3 rad_lit   = gpu_total * flash_k + i.flash;
    const float3 rad_unlit = flash_k + i.flash;
    const float3 rad_mem   = mem_mul * flash_k + i.flash;

    float3 lit_rgb = albedo * rad_lit;

    // Palette shade ramp. The lighting result selects a SHADE STEP within the base
    // texel's OWN ramp, so a red surface darkens toward its palette's dark red rather
    // than toward grey — which is how pixel art shades, and the single biggest reason
    // a straight light multiply reads as "HD lighting bolted onto pixel art".
    // Coloured light still reads: ramp_chroma re-tints the ramped colour by the
    // light's normalised chroma. The ordered-dither term on shade_f is what stops 8
    // steps from banding (error diffusion is not available in a fragment shader, and
    // the world-locked Bayer helper already exists).
    //
    // Applied to lit_rgb ONLY, and keyed off `rad_lit`. The old code keyed off
    // `combined` and justified that by claiming gpu_total "stays low in daylight
    // because the engine lights tiles through the CPU lightmap TINT". That
    // justification was already FALSE: the CPU ZEROED the tint on exactly the tiles
    // the ramp applied to, so `combined` WAS gpu_total right here. The numbers do not
    // move; only the reasoning is now honest.
    //
    // The `step(tint sum, 0.01)` factor is GONE, along with its local tint sum. It was
    // a proxy for "this is a lit world tile", and it would have flipped to ALWAYS TRUE
    // the moment tint stopped being 1.0 on UI and memory sprites — at which point the
    // ramp would have started eating HUD glyphs. The ramp now lives STRUCTURALLY
    // inside the gpu_lit branch: it only ever touches lit_rgb, which only mode_gpu_lit
    // and the memory cross-fade read, so UI rects, font glyphs and remembered tiles
    // cannot reach it by construction rather than by a tint coincidence.
    //
    // RampBuf and PalIdxBuf are still read UNCONDITIONALLY and the effect applied with
    // a lerp, never an `if`; this stays a bare top-level scope and must NOT be moved
    // inside an `if` — D3D12 strips an unread fragment storage buffer and the hole
    // breaks the root signature.
    {
        const uint   pal_row = PalIdxBuf[pal_index_of(texel.rgb)];
        const float  steps_n = max(ramp_steps, 2.0);
        const float  shade_f = saturate(luma(rad_lit)) * (steps_n - 1.0)
                               + (dither_threshold(shade_pos * texels_per_tile) - 0.5);
        const uint   shade_i = (uint)clamp(shade_f + 0.5, 0.0, steps_n - 1.0);
        const float3 ramped  = unpack_rgba8(RampBuf[pal_row * (uint)steps_n + shade_i]);
        // Normalised light chroma, scaled so neutral white light is a no-op (x1).
        const float3 lit_chroma = normalize(max(rad_lit, 1e-4)) * 1.7320508;
        const float3 ramp_rgb = lerp(ramped, ramped * lit_chroma, ramp_chroma);
        const float  ramp_mask = saturate(ramp_enable)
                                 * ((sdf_map_w > 0u) ? 1.0 : 0.0);
        lit_rgb = lerp(lit_rgb, ramp_rgb, ramp_mask);
    }

    const float3 mem_rgb   = albedo * rad_mem;
    const float3 unlit_rgb = albedo * rad_unlit;
    // Mode select. `memory` is a LERP TOWARD THE LIT RESULT, never an independent
    // branch. frontier_cov is 1.0 deep inside a remembered region and falls to 0 at
    // the edge shared with a currently-visible tile, so a branch that never read
    // lit_rgb would have nothing to cross-fade toward: the feather would ramp to the
    // memory look against a fully-lit neighbour and the seen/remembered boundary would
    // return as a DARK RIM — the exact artefact Step 8 exists to remove. At
    // frontier_cov == 0 this is bit-identical to the visible neighbour; at 1 it is the
    // pure remembered look.
    //
    // Between those two endpoints the frontier band is an INTENTIONAL change from the
    // old max(), which agreed only AT the endpoints and in between was a
    // content-dependent "brighter wins" rather than a cross-fade. So "no visual
    // diff" is NOT the acceptance criterion for this band; tools/frontier_profile.py
    // gates it on MONOTONICITY of the luma ramp between the two plateaus, sampled via
    // debug view 15.
    float3 final_rgb = mode_memory ? lerp(lit_rgb, mem_rgb, frontier_cov)
                       : (mode_gpu_lit ? lit_rgb : unlit_rgb);

    // Debug visualisation. Modes 1-5 BLEND a per-component visualisation
    // over the lit scene at debug_opacity; modes 6-7 REPLACE the scene with
    // raw SDF / sky_vis colormaps. Modes 1-7 are gated to world sprites that carry
    // light (light_mode != unlit) so HUD glyphs / UI rects always render normally.
    // Modes 8 and 16 bypass that gate: mode 8 (emit_bw diagnostic) has to work on the
    // tinted main-menu blue backdrop, and mode 16 reports the classification itself so
    // it must be able to display `unlit`. emitter_count==0 segments still
    // short-circuit mode 8, so HUD/font segments stay untouched.

    // Sub-tile vision carve. The CPU already decided, per tile, whether this tile is
    // drawn at all (lit_level -> apply_vision_effects skips non-CLEAR tiles), so this
    // term can only ever SUBTRACT within an already-granted tile — it is structurally
    // incapable of revealing anything gameplay says is unseen. What it buys is the
    // SHAPE: a wall corner cutting your sight now produces a smooth sub-tile curve
    // instead of a tile staircase, because it marches the same sub-tile SDF the
    // shadows use. vis_curve = 0 restores the previous look exactly.
    //
    // vis_curve / vis_radius are the lanes the deleted VisBuf path left orphaned;
    // reusing them is semantically exact and costs no cbuffer churn.
    if(vis_curve > 0.001 && sdf_map_w > 0u) {
        const float2 eye = float2(player_x, player_y);
        const float2 ev  = eye - shade_pos;
        const float  ed  = length(ev);
        const float  los = (ed < 0.5) ? 1.0
                           : soft_shadow_march(shade_pos, ev / ed, ed, shadow_k,
                                          (int)shadow_steps,
                                          frag_is_tall_n ? self_eps_tall : 0.05,
                                          /*ref_receiver=*/false);
        float v = pow(saturate(los), vis_curve);
        if(vis_radius > 0.001) {
            v *= saturate(1.0 - smoothstep(vis_radius * 0.6, vis_radius, ed));
        }
        // Fade toward the memory look rather than to black, so the carve reads as the
        // same material the unseen region already uses.
        final_rgb *= mem_dim + (1.0 - mem_dim) * v;
    }

    // The gate used to be `tint sum < 0.01`, i.e. "the CPU zeroed the tint here".
    // That ALSO excluded memory tiles (tint 1.0), which made debug_mode 15's
    // remembered-tile branch UNREACHABLE. `mode != unlit` is what it always meant.
    // Modes 8 and 16 bypass it: 8 needs the tinted main-menu backdrop, and 16 reports
    // the classification itself so it must be able to show `unlit`.
    const bool dbg_ungated = (debug_mode == 8u) || (debug_mode == 16u);
    const bool dbg_active  = debug_mode > 0u && (dbg_ungated || mode_memory || mode_gpu_lit);
    // Height depth: dims canopy to enhance depth perception. dark_frac=0 at base → no-op.
    // Applied after memory-fade, before debug modes — so debug replace=true cleanly overrides.
    final_rgb *= ( 1.0 - i.dark_frac );

    // dbg_opaque is hoisted out of the block so the return can force full alpha for
    // debug view 16 ONLY. A categorical view that gets alpha-blended is not
    // categorical: sprite quads draw with SRC_ALPHA, several stack per tile
    // (background, foreground, z-overlays at alpha 24..192/255), so a flat (0,1,0)
    // reaches the screen as e.g. rgb(8, 172, 9) and two classes can mix at a seam.
    // Measured: 28% of a mode-16 capture failed an exact-colour test while every one
    // of those pixels was plainly green.
    //
    // Scoped to 16 deliberately. The other replace-mode views NEED their alpha:
    // view 15 encodes the frontier feather in vis_overlay_a — that IS the thing it
    // exists to show — and views 9-14 rely on texel.a for sprite silhouettes, which
    // would become solid quads.
    float dbg_opaque = 0.0;
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
            const float s = (sdf_map_w > 0u) ? sdf_bilinear(shade_pos) : 0.0;
            const float t = saturate(s / 8.0);
            vis = float3(1.0 - t, t, 0.0);
            replace = true;
        } else if(debug_mode == 7u) {
            // SkyVis view: grayscale 0..1. Sample at light_pos (base-tile centre
            // for tall sprites) to match the live sky-vis read, not world_pos.
            const int sx = clamp((int)shade_pos.x, 0, (int)sdf_map_w - 1);
            const int sy = clamp((int)shade_pos.y, 0, (int)sdf_map_h - 1);
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
        } else if(debug_mode == 15u) {
            // Vision-frontier view (Step 8): which fragments carry a sub-tile frontier
            // marker, which treatment it selects, and how the feather ramps inside it.
            // In every case the GREEN channel is frontier_cov, so the colour is the
            // "edge" hue where the tile meets the bright/visible side and shifts toward
            // yellow/white/cyan deep inside its own region.
            //   GREEN          = drawn, ordinary sprite (no frontier here)
            //   BLACK          = nothing drawn at all — the true unseen region
            //   MAGENTA -> WHITE = lit_level::LOW, the dim edge of sight (desaturate)
            //   RED     -> YELLOW = remembered tile (mem_dim)
            //   BLUE    -> CYAN   = never-seen tile's `lighting_*` overlay
            if(i.outline < -0.5) {
                vis = frontier_desat
                      ? float3(1.0, frontier_cov, 1.0)
                      : mode_memory
                      ? float3(1.0, frontier_cov, 0.0)
                      : float3(0.0, frontier_cov, 1.0);
            } else {
                // Drawn, but not on a frontier. Deliberately a loud GREEN rather than
                // black or the tint: an undrawn tile leaves the target untouched, so
                // "black" would be indistinguishable from "no marker" and the view
                // could not tell a visible tile from an unseen one. With this,
                // BLACK = nothing rendered here at all = the true unseen region.
                vis = float3(0.0, 0.55, 0.0);
            }
            replace = true;
        } else if(debug_mode == 16u) {
            // light_mode view: RED = unlit, GREEN = gpu_lit, BLUE = memory. A flat
            // CATEGORICAL hue, deliberately carrying no shading whatsoever, because
            // categorical membership is immune to the zoom and resolution bistability
            // that makes every brightness comparison in this project untrustworthy
            // (measured: 8.02 px vs 42.86 px tile pitch across two runs of the SAME
            // scenario moved whole-frame mean luma by 19.7). One capture answers "is
            // every sprite classified as intended". Consumed by
            // tools/light_mode_check.py.
            vis = mode_memory ? float3(0.0, 0.0, 1.0)
                  : (mode_gpu_lit ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0));
            replace = true;
            // Full alpha: see dbg_opaque above. Without it the flat hue is blended by
            // every stacked quad and the class is no longer exactly recoverable.
            dbg_opaque = 1.0;
        } else if(debug_mode == 17u) {
            // Cloud-shadow view: grayscale of cloud_mul, the factor actually
            // applied to sun_contrib. White = no cloud overhead; darkens toward
            // (1 - cloud_strength) under a cloud. Independent of sun/sky colour,
            // ambient, GI, dither. Uniform white everywhere = cloud_strength is 0.
            // Gated by the SAME condition sun_contrib itself uses (sun_intensity,
            // sun_sky_vis, sdf_map_w) — indoors/underground/at night cloud_mul has
            // zero real effect, so the view must show flat white there too, not a
            // drifting pattern that implies an effect the render doesn't apply.
            const bool sun_applies = sun_intensity > 0.001 && sun_sky_vis > 0.05 && sdf_map_w > 0u;
            const float cloud_vis = sun_applies ? cloud_mul : 1.0;
            vis = float3(cloud_vis, cloud_vis, cloud_vis);
            replace = true;
        }
        if(replace) {
            final_rgb = vis;
        } else {
            final_rgb = lerp(final_rgb, vis, saturate(debug_opacity));
        }
    }
    // vis_overlay_a is 1.0 for every sprite except a vision-frontier overlay, whose
    // coverage is feathered across the tile (Step 8). Debug view 16 overrides alpha
    // entirely so its categorical colour survives to the screenshot.
    const float out_a = lerp(texel.a * i.tint.a * vis_overlay_a, 1.0, dbg_opaque);
    return float4(final_rgb, out_a);
}

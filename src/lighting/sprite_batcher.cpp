#include "sprite_batcher.h"

#include "shader_compiler.h"
#include "debug.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>
#include <vector>

#define dbg( x ) DebugLogFL( ( x ), DC::SDL )

namespace lighting
{

// --- HLSL sources embedded as string literals so phase 2d has no runtime
// data-file dependency. When we add disk-loaded shaders later (phases 5+),
// this is the template for how the load path looks. -----------------------

static const char *const SPRITE_VERT_HLSL = R"HLSL(
// ---- Sprite instance (64 bytes, wire-stable) ----
struct SpriteInstance {
    float dst_x, dst_y, dst_w, dst_h;
    float src_u, src_v, src_uw, src_vh;
    float tint_r, tint_g, tint_b, tint_a;
    float rotation, pad0, pad1, pad2;
};

// Vertex storage slot 0: sprite instances
StructuredBuffer<SpriteInstance> Instances : register(t0, space0);

// Cbuffer slot 0: per-segment viewport + instance base (wire-stable)
cbuffer FrameParams : register(b0, space1) {
    float2 target_size;
    uint   instance_base;
    uint   fp_pad;
};

// Cbuffer slot 1: per-frame lighting params (world_pos computation)
cbuffer LightParams : register(b1, space1) {
    float tile_pixel_size;
    float current_z;
    uint  emitter_count;
    float ambient;
    float camera_off_x;
    float camera_off_y;
    uint  sdf_map_w;
    uint  sdf_map_h;
};

struct VS_OUT {
    float4 pos      : SV_Position;
    float2 uv       : TEXCOORD0;
    float4 tint     : TEXCOORD1; // Phase 5 CPU lightmap tint (ambient floor)
    float2 world_pos: TEXCOORD2; // map tile coords for fragment per-pixel lighting
};
static const float2 quad_uv[6] = {
    float2(0.0,0.0), float2(1.0,0.0), float2(0.0,1.0),
    float2(1.0,0.0), float2(1.0,1.0), float2(0.0,1.0)
};

VS_OUT main(uint vid : SV_VertexID, uint iid : SV_InstanceID) {
    const SpriteInstance s = Instances[iid + instance_base];
    const float2 c = quad_uv[vid];

    const float2 centre = float2(s.dst_x + 0.5 * s.dst_w,
                                 s.dst_y + 0.5 * s.dst_h);
    const float2 off    = float2((c.x - 0.5) * s.dst_w,
                                 (c.y - 0.5) * s.dst_h);
    const float  cs     = cos(s.rotation);
    const float  sn     = sin(s.rotation);
    const float2 pixel  = centre + float2(off.x * cs - off.y * sn,
                                          off.x * sn + off.y * cs);
    const float2 ndc = float2(
        pixel.x / target_size.x *  2.0 - 1.0,
        pixel.y / target_size.y * -2.0 + 1.0);

    // Use per-VERTEX pixel (not sprite centre) so world_pos interpolates across
    // the quad.  For small tiles (32px) the difference is < 0.5 tile — negligible.
    // For a fullscreen background quad this gives the lighting gradient we want.
    const float2 tile_tu = pixel / max(tile_pixel_size, 1.0);
    const float2 map_pos = tile_tu - float2(camera_off_x, camera_off_y);

    VS_OUT o;
    o.pos       = float4(ndc, 0.0, 1.0);
    o.uv        = float2(s.src_u + c.x * s.src_uw, s.src_v + c.y * s.src_vh);
    o.tint      = float4(s.tint_r, s.tint_g, s.tint_b, s.tint_a);
    o.world_pos = map_pos;
    return o;
}
)HLSL";

static const char *const SPRITE_FRAG_HLSL = R"HLSL(
// Phase 7/8: per-pixel Lambert + SDF soft shadows + sun/sky.
//
// SDL_CreateGPUShader binding convention for pixel shaders (DXBC/DXIL):
//   (t[n], space2) — sampled textures first, then storage textures, then
//                    storage buffers; share the t-register range.
//   (s[n], space2) — samplers, indices match their sampled textures.
//   (b[n], space3) — uniform buffers.
//
// Resources (all space2). K=1 sampled texture ⇒ storage buffers start at t1.
//   t0 / s0  Atlas       — sampled, current atlas texture (per segment)
//   t1       Emitters    — StructuredBuffer<GpuEmitter>, 8192-entry  (storage slot 0)
//   t2       SdfBuf      — StructuredBuffer<float>, sdf_pass dims    (storage slot 1)
//   t3       SkyVisBuf   — StructuredBuffer<float>, sdf_pass dims    (storage slot 2)
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
struct GpuEmitter {
    float4 pos_radius;
    float4 color_falloff;
    float4 cone_shape;
    float4 misc;
};
Texture2D<float4>            Atlas     : register(t0, space2);
SamplerState                 AtlasSmp  : register(s0, space2);
StructuredBuffer<GpuEmitter> Emitters  : register(t1, space2);
StructuredBuffer<float>      SdfBuf    : register(t2, space2);
StructuredBuffer<float>      SkyVisBuf : register(t3, space2);
StructuredBuffer<float>      IndirectBuf : register(t4, space2); // 1-bounce GI, 3 floats/tile RGB
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
    float dp_pad1;
    float dp_pad2;
};
struct VS_OUT {
    float4 pos      : SV_Position;
    float2 uv       : TEXCOORD0;
    float4 tint     : TEXCOORD1;
    float2 world_pos: TEXCOORD2;
};
// Clamped raw SDF fetch. SdfBuf is x-major (sdf[x*H+y]).
float sdf_texel(int x, int y) {
    x = clamp(x, 0, (int)sdf_map_w - 1);
    y = clamp(y, 0, (int)sdf_map_h - 1);
    return SdfBuf[x * (int)sdf_map_h + y];
}
// Bilinear SDF sample. The CPU SDF is a Chebyshev BFS at tile resolution, so
// raw nearest reads ((int)p.x) produced diamond-faceted (sawtooth) penumbrae.
// Interpolating the four surrounding tile samples smooths those steps into a
// clean gradient.
//
// Sample alignment: SdfBuf[x] stores the distance FOR tile x, whose CENTRE is
// at world_pos x+0.5 (world_pos = mx+0.5 convention). So subtract 0.5 before
// floor/frac — at a tile centre (world_pos = int+0.5) this returns that tile's
// stored value with weight 1 (matching the old nearest read and the HUD's
// sdf[player] readout). Using bare floor(p) would shift the whole field half a
// tile and detach shadows ~16px from their occluders.
float sdf_bilinear(float2 p) {
    const float2 sp = p - 0.5;        // tile-centre alignment
    const float2 fp = floor(sp);
    const int   x0  = (int)fp.x;
    const int   y0  = (int)fp.y;
    const float2 w  = sp - fp;        // frac
    const float a = sdf_texel(x0,     y0    );
    const float b = sdf_texel(x0 + 1, y0    );
    const float c = sdf_texel(x0,     y0 + 1);
    const float d = sdf_texel(x0 + 1, y0 + 1);
    return lerp(lerp(a, b, w.x), lerp(c, d, w.x), w.y);
}
// Per-tile 1-bounce indirect light (RGB, 3 floats/tile, x-major). Bilinear with
// the same p-0.5 centre convention as sdf_bilinear (per-tile nearest would show
// hard tile squares of fill).
float3 indirect_texel(int x, int y) {
    x = clamp(x, 0, (int)sdf_map_w - 1);
    y = clamp(y, 0, (int)sdf_map_h - 1);
    const int b = (x * (int)sdf_map_h + y) * 3;
    return float3(IndirectBuf[b], IndirectBuf[b + 1], IndirectBuf[b + 2]);
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
// Shared soft-shadow sphere trace (Inigo Quilez cone ratio). Used by BOTH
// emitters and the sun so they share the shadow_k / shadow_steps knobs and
// look consistent. `dist_to_light` is the march length (real distance for
// point emitters; a fixed reach for the directional sun).
float trace_shadow(float2 origin, float2 dir, float dist_to_light,
                   float k, int steps) {
    if(sdf_map_w == 0u || steps <= 0) {
        return 1.0;
    }
    float shadow = 1.0;
    float t = min(0.3, dist_to_light * 0.5);
    [loop] for(int ss = 0; ss < steps; ++ss) {
        if(t >= dist_to_light - 0.4) break;
        const float sd = sdf_bilinear(origin + dir * t);
        if(sd < 0.05) { shadow = 0.0; break; }
        shadow = min(shadow, k * sd / max(dist_to_light - t, 0.01));
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
float4 main(VS_OUT i) : SV_Target0 {
    const float4 texel = Atlas.Sample(AtlasSmp, i.uv);
    if(texel.a < 0.01) discard;
    // Flat surface normal — Phase 7b will sample a normal atlas texture.
    const float3 normal = float3(0.0, 0.0, 1.0);
    // emitter_light accumulates GPU point-light contributions (starts at zero).
    // Combined with CPU tint ADDITIVELY so colored emitter glow is visible on
    // top of the CPU-shadowcasting result, not suppressed by max().
    float3 emitter_light = float3(0.0, 0.0, 0.0);
    const uint me = min(emitter_count, 8192u);
    for(uint ei = 0u; ei < me; ++ei) {
        const GpuEmitter e = Emitters[ei];
        const float3 e_pos    = e.pos_radius.xyz;
        const float  e_radius = e.pos_radius.w;
        const float3 e_color  = e.color_falloff.xyz;
        const float  e_falloff= e.color_falloff.w;
        if(abs(e_pos.z - current_z) > 0.5) continue;
        const float2 dv   = e_pos.xy - i.world_pos;
        const float  dist = length(dv);
        if(dist >= e_radius || dist < 0.01) continue;
        const float  atten   = 1.0 - pow(saturate(dist / e_radius), e_falloff);
        // Lambert = 1.0 for omnidirectional point lights with flat normal.
        // Directional shading from real surface normals comes in Phase 7b.
        const float  lambert = 1.0;
        // Per-emitter soft shadow via the shared SDF sphere trace (bilinear,
        // shadow_k / shadow_steps tunable). See trace_shadow above.
        const float2 sh_dir = dv / max(dist, 0.001);
        const float  shadow = trace_shadow(i.world_pos, sh_dir, dist,
                                            shadow_k, (int)shadow_steps);
        const float3 rgb = (e_color.x < 0.01 && e_color.y < 0.01 && e_color.z < 0.01)
                           ? float3(1, 1, 1) : e_color;
        emitter_light += rgb * atten * lambert * shadow;
    }
    // Phase 8: sky ambient + directional sun contribution.
    // SkyVisBuf is x-major (skyvis[x*H+y]) — index directly, no transpose.
    const int sky_ix = clamp((int)i.world_pos.x, 0, (int)sdf_map_w - 1);
    const int sky_iy = clamp((int)i.world_pos.y, 0, (int)sdf_map_h - 1);
    float sky_vis = (sdf_map_w > 0u) ? SkyVisBuf[sky_ix * (int)sdf_map_h + sky_iy] : 0.0;
    // Sky ambient: soft, no shadowing needed.
    float3 sky_contrib = float3(sky_r, sky_g, sky_b) * sky_intensity * sky_vis;
    // Sun direct: directional soft shadow via the SAME shared trace as
    // emitters, so it honours shadow_k / shadow_steps and matches their
    // softness (was a hardcoded copy: k=4, 16 steps, reach 8.0). 8.0 = the
    // directional march reach (sun has no finite distance). Gated to open-sky
    // tiles (sky_vis) — the sun cannot reach roofed interiors.
    float3 sun_contrib = float3(0.0, 0.0, 0.0);
    if(sun_intensity > 0.001 && sky_vis > 0.01 && sdf_map_w > 0u) {
        const float2 toward_sun = -float2(sun_dir_x, sun_dir_y);
        const float sun_shadow = trace_shadow(i.world_pos, toward_sun, 8.0,
                                               shadow_k, (int)shadow_steps);
        // Lambert with flat normal: dot((0,0,1), normalize(sun_dir_xy, sin_elev))
        const float sun_lambert = sun_sin_elev / sqrt(1.0 + sun_sin_elev * sun_sin_elev);
        sun_contrib = float3(sun_r, sun_g, sun_b) * sun_intensity * sun_lambert
                      * sun_shadow * sky_vis;
    }

    // Apply runtime tuning scales BEFORE compositing. emitter_scale tunes
    // global brightness of all emitter contributions; sun_scale / sky_scale
    // tune the sun + sky streams. Defaults are 1.0 (no-op).
    emitter_light *= emitter_scale;
    sun_contrib   *= sun_scale;
    sky_contrib   *= sky_scale;

    const float3 ambient_v = float3(ambient, ambient, ambient);

    // Multi-band ordered (Bayer) dither, world-locked. Quantise ONLY the
    // dynamic light (emitter + sky + sun) into `dither_bands` levels and
    // stipple the band transitions — Stoneshard-style. The ambient floor is
    // added AFTER so flat near-black areas stay clean (dithering the floor
    // makes dark areas sparkle). Ordered dither is mean-preserving, so no
    // global brightness shift. Anchored to world PIXELS (world_pos *
    // tile_pixel_size) → pattern sticks to terrain, no shimmer on scroll.
    float3 dyn = emitter_light + sky_contrib + sun_contrib;
    // 1-bounce indirect fill (fake GI): colored light diffused off surfaces into
    // open neighbours on the CPU, added here before dither so it bands with the
    // rest of the dynamic light.
    if(gi_strength > 0.001 && sdf_map_w > 0u) {
        dyn += gi_strength * indirect_bilinear(i.world_pos);
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
    const bool  dbg_active   = (debug_mode == 8u)
                               || (debug_mode > 0u && debug_mode < 8u
                                   && dbg_tint_sum < 0.01);
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
            // Bilinear so the view matches what the shadow march now samples.
            const float s = (sdf_map_w > 0u) ? sdf_bilinear(i.world_pos) : 0.0;
            const float t = saturate(s / 8.0);
            vis = float3(1.0 - t, t, 0.0);
            replace = true;
        } else if(debug_mode == 7u) {
            // SkyVis view: grayscale 0..1.
            const int sx = clamp((int)i.world_pos.x, 0, (int)sdf_map_w - 1);
            const int sy = clamp((int)i.world_pos.y, 0, (int)sdf_map_h - 1);
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
        }
        if(replace) {
            final_rgb = vis;
        } else {
            final_rgb = lerp(final_rgb, vis, saturate(debug_opacity));
        }
    }
    return float4(final_rgb, texel.a * i.tint.a);
}
)HLSL";

// FrameParams cbuffer payload pushed as a vertex uniform per draw segment.
// Layout must match the cbuffer in sprite.vert.hlsl exactly.
struct frame_params {
    float    target_w;
    float    target_h;
    Uint32   instance_base;
    Uint32   pad;
};
static_assert( sizeof( frame_params ) == 16, "frame_params is wire-stable with vert shader" );

// Phase 6/6b: per-frame lighting params (cbuffer slot 1, vertex shader).
// 32 bytes, wire-stable with the LightParams cbuffer in SPRITE_VERT_HLSL.
struct light_params {
    float  tile_pixel_size; // screen pixels per tile (e.g. 32.0)
    float  current_z;       // player z-level
    Uint32 emitter_count;   // live entries in the emitter SSBO
    float  ambient;         // base ambient (0=dark dungeon, 0.05-0.3=outdoors)
    // Phase 6b: camera offset (screen tile → map tile: map_pos = tile_tu - offset)
    float  camera_off_x;    // = tile_map_origin_px.x / tile_px + 0.5
    float  camera_off_y;
    Uint32 sdf_map_w;       // SDF/map width  in tiles
    Uint32 sdf_map_h;       // SDF/map height in tiles (was lp_pad; same type, sizeof unchanged)
};
static_assert( sizeof( light_params ) == 32, "light_params wire-stable with LightParams cbuffer" );

static_assert( sizeof( sun_params ) == 48, "sun_params wire-stable with SunParams cbuffer" );

// debug_params struct now lives in sprite_batcher.h so render_state.h can
// embed it by value in frame_light_inputs. Wire-stable layout enforced here.
static_assert( sizeof( debug_params ) == 48, "debug_params wire-stable with DebugParams cbuffer" );

// ---- 24h sun LUT -------------------------------------------------------
// Defined at file scope so MSVC won't complain about static-local in nested block.
namespace {
struct sun_lut_key { float hr, si, sr, sg, sb, sky_i, sky_r, sky_g, sky_b, elev; };
static const sun_lut_key k_sun[] = {
  //  hr    si     sr     sg     sb    sky_i  sky_r  sky_g  sky_b  elev
    {  0, 0.00f, 0.f,  0.f,  0.f,  0.03f, 0.05f, 0.05f, 0.15f, 0.f   },
    {  5, 0.00f, 0.f,  0.f,  0.f,  0.05f, 0.05f, 0.10f, 0.25f, 0.f   },
    {  6, 0.10f, 0.90f,0.50f,0.20f,0.15f, 0.60f, 0.40f, 0.30f, 0.15f },
    {  8, 0.60f, 1.00f,0.80f,0.50f,0.50f, 0.55f, 0.65f, 0.85f, 0.50f },
    { 12, 1.00f, 1.00f,0.95f,0.80f,0.80f, 0.50f, 0.60f, 0.90f, 0.87f },
    { 16, 0.80f, 1.00f,0.90f,0.60f,0.60f, 0.50f, 0.60f, 0.85f, 0.70f },
    { 19, 0.20f, 1.00f,0.40f,0.10f,0.20f, 0.70f, 0.40f, 0.30f, 0.20f },
    { 21, 0.00f, 0.f,  0.f,  0.f,  0.05f, 0.10f, 0.08f, 0.15f, 0.f   },
    { 24, 0.00f, 0.f,  0.f,  0.f,  0.03f, 0.05f, 0.05f, 0.15f, 0.f   },
};
static constexpr int k_sun_n = static_cast<int>( sizeof( k_sun ) / sizeof( k_sun[0] ) );
} // anonymous namespace

sun_params make_sun_params( float sun_hour ) noexcept
{
    int ki = 0;
    while( ki < k_sun_n - 2 && k_sun[ki + 1].hr <= sun_hour ) {
        ++ki;
    }
    const auto &a = k_sun[ki], &b = k_sun[ki + 1];
    const float dt = ( b.hr > a.hr ) ? ( sun_hour - a.hr ) / ( b.hr - a.hr ) : 0.f;
    auto lp = [dt]( float x, float y ) { return x + dt * ( y - x ); };
    sun_params sp{};
    sp.sun_intensity = lp( a.si,    b.si );
    sp.sun_r         = lp( a.sr,    b.sr );
    sp.sun_g         = lp( a.sg,    b.sg );
    sp.sun_b         = lp( a.sb,    b.sb );
    sp.sky_intensity = lp( a.sky_i, b.sky_i );
    sp.sky_r         = lp( a.sky_r, b.sky_r );
    sp.sky_g         = lp( a.sky_g, b.sky_g );
    sp.sky_b         = lp( a.sky_b, b.sky_b );
    sp.sun_sin_elev  = lp( a.elev,  b.elev );
    // Sun direction rotates E→W (noon = overhead, dawn from east, dusk from west).
    const float angle = ( sun_hour - 12.f ) * 3.14159f / 12.f;
    sp.sun_dir_x = static_cast<float>( cos( static_cast<double>( angle ) ) );
    sp.sun_dir_y = 0.f;
    sp.sp_pad = 0.f;
    return sp;
}

// ---- PIMPL body --------------------------------------------------------

class sprite_batcher_impl
{
    public:
        // Sized so a 1080p screen worth of tiles + UI elements has comfortable
        // headroom (a tile screen at the densest zoom is ~3-4 k instances).
        // 262144 × 64 bytes = 16 MB per ring slot (×3 = 48 MB).
// Needed for 4K displays: minimap (~17K) + large terminal sidebar (~27K) +
// tile sprites (~10K) easily exceed 65536 on high-res setups.
static constexpr Uint32 MAX_INSTANCES = 262144;
        // SDL_GPU defaults to 2-3 frames in flight; 3 ring slots is enough to
        // avoid waiting for the GPU to finish reading the previous frame's
        // storage buffer before we overwrite it.
        static constexpr Uint32 RING_SLOTS   = 3;

        gpu_device *dev = nullptr;
        pipeline_desc desc{};

        SDL_GPUGraphicsPipeline *pipeline = nullptr;
        SDL_GPUShader *vert_shader = nullptr;
        SDL_GPUShader *frag_shader = nullptr;
        SDL_GPUSampler *default_sampler = nullptr;

        SDL_GPUBuffer         *storage_bufs[RING_SLOTS] = {};
        SDL_GPUTransferBuffer *xfer_bufs[RING_SLOTS]    = {};
        Uint32 cur_slot = 0;

        struct segment {
            SDL_GPUTexture *tex;
            SDL_GPUSampler *sampler;
            Uint32 start;
            Uint32 count;
            SDL_Rect scissor     = {};
            bool     has_scissor = false;
            // When false, end_pass pushes a zeroed light_params / sun_params
            // for this segment so the fragment shader skips the per-emitter
            // loop and the sun march (saves wasted GPU on HUD/UI fragments
            // whose lighting result is discarded by max(tint, gpu_total)).
            // Default true preserves tile-sprite behaviour.
            bool     is_lit      = true;
        };
        std::vector<sprite_instance> pending;
        std::vector<segment>          segments;

        // Pending segment in progress (not yet pushed to `segments`).
        SDL_GPUTexture *bound_tex      = nullptr;
        SDL_GPUSampler *bound_sampler  = nullptr;
        Uint32          seg_start      = 0;
        SDL_Rect        bound_scissor  = {};
        bool            bound_has_scissor = false;
        // Lighting state of the segment currently being accumulated.
        // True = run full fragment-shader lighting; false = push zeroed
        // light_params + sun_params so the loop + sun march short-circuit.
        bool            bound_is_lit   = true;

        // Pass-scope state captured by begin_pass().
        SDL_GPUCommandBuffer *cur_cb = nullptr;
        SDL_GPUTexture       *cur_target = nullptr;
        Uint32 cur_target_w = 0;
        Uint32 cur_target_h = 0;
        // Shader pixel→NDC projection extents. Equal to cur_target_w/h
        // when caller doesn't override (legacy); decoupled for HiDPI so
        // logical-coord UI draws fill the physical swapchain via the
        // larger viewport.
        Uint32 cur_proj_w   = 0;
        Uint32 cur_proj_h   = 0;
        bool   cur_clear    = false;
        float  cur_clear_color[4] = {};
        bool   pass_open = false;

        // Phase 7/8: per-frame lighting resources. Emitter, SDF AND sky-vis
        // data all live in fragment storage buffers (slots 0/1/2), not
        // sampler textures — Metal mis-binds sampler-texture Load (see
        // SPRITE_FRAG_HLSL comment). Atlas is the only sampler texture.
        SDL_GPUBuffer  *lp_emitter_buf  = nullptr;  // fragment storage slot 0
        SDL_GPUBuffer  *lp_sdf_buf      = nullptr;  // fragment storage slot 1
        SDL_GPUBuffer  *lp_sky_vis_buf  = nullptr;  // fragment storage slot 2
        SDL_GPUBuffer  *lp_indirect_buf = nullptr;  // fragment storage slot 3 (IndirectBuf)
        SDL_GPUSampler *lp_data_sampler = nullptr;
        light_params    lp              = {};  // defaults: all zero
        sun_params      lp_sun          = {};  // Phase 8: sun/sky params
        debug_params    lp_debug        = {};  // Debug viz + tuning knobs (DebugParams cbuffer)

        void set_lighting_resources( float           tile_pixel_size,
                                     float           z_level,
                                     Uint32          count,
                                     float           ambient,
                                     float           cam_off_x    = 0.0f,
                                     float           cam_off_y    = 0.0f,
                                     Uint32          sdf_map_w    = 0u,
                                     Uint32          sdf_map_h    = 0u,
                                     SDL_GPUBuffer  *emitter_buf  = nullptr,
                                     SDL_GPUBuffer  *sdf_buf       = nullptr,
                                     SDL_GPUSampler *data_sampler = nullptr,
                                     SDL_GPUBuffer  *sky_vis_buf  = nullptr,
                                     SDL_GPUBuffer  *indirect_buf = nullptr,
                                     const sun_params *sp         = nullptr,
                                     const debug_params *dbg      = nullptr ) noexcept {
            // data_sampler is vestigial now that all lighting data (emitters,
            // SDF, sky-vis) lives in storage buffers — Atlas is the only
            // sampler texture and carries its own sampler from set_texture().
            // Kept for signature stability; fall back to default if null.
            if( !data_sampler ) {
                data_sampler = default_sampler;
            }
            lp_emitter_buf  = emitter_buf;
            lp_sdf_buf      = sdf_buf;
            lp_sky_vis_buf  = sky_vis_buf;
            lp_indirect_buf = indirect_buf;
            lp_data_sampler = data_sampler;
            lp.tile_pixel_size = tile_pixel_size;
            lp.current_z       = z_level;
            lp.emitter_count   = emitter_buf ? count : 0u;
            lp.ambient         = ambient;
            lp.camera_off_x    = cam_off_x;
            lp.camera_off_y    = cam_off_y;
            lp.sdf_map_w       = sdf_buf ? sdf_map_w : 0u;
            lp.sdf_map_h       = sdf_buf ? sdf_map_h : 0u;
            if( sp ) { lp_sun = *sp; } else { lp_sun = {}; }
            // Default-construct debug_params when none passed — the member
            // defaults provide sensible runtime values (emitter_scale=1,
            // sun_scale=1, sky_scale=1, shadow_k=8, shadow_steps=16,
            // debug_mode=0) so the shader behaves identically to the pre-
            // debug-widget code path.
            if( dbg ) { lp_debug = *dbg; } else { lp_debug = {}; }
        }

        // ---- lifecycle -------------------------------------------------

        void init( gpu_device &d, const pipeline_desc &pd, const char *label ) {
            if( !d.ready() ) {
                throw std::runtime_error( "sprite_batcher::init: gpu_device not ready" );
            }
            dev = &d;
            desc = pd;

            init_shader_compiler();

            // Compile shaders.
            auto v = compile_graphics_shader( d, SPRITE_VERT_HLSL, "main",
                                              SDL_SHADERCROSS_SHADERSTAGE_VERTEX,
                                              "sprite_batcher.vert" );
            if( !v ) {
                throw std::runtime_error( "sprite_batcher vert shader compile failed" );
            }
            auto f = compile_graphics_shader( d, SPRITE_FRAG_HLSL, "main",
                                              SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT,
                                              "sprite_batcher.frag" );
            if( !f ) {
                SDL_ReleaseGPUShader( d.raw(), v.shader );
                throw std::runtime_error( "sprite_batcher frag shader compile failed" );
            }
            vert_shader = v.shader;
            frag_shader = f.shader;

            // Pipeline.
            SDL_GPUColorTargetBlendState blend{};
            blend.enable_blend = desc.enable_blend;
            blend.src_color_blendfactor = desc.src_color_blend;
            blend.dst_color_blendfactor = desc.dst_color_blend;
            blend.color_blend_op        = desc.color_blend_op;
            blend.src_alpha_blendfactor = desc.src_alpha_blend;
            blend.dst_alpha_blendfactor = desc.dst_alpha_blend;
            blend.alpha_blend_op        = desc.alpha_blend_op;
            blend.color_write_mask = SDL_GPU_COLORCOMPONENT_R |
                                     SDL_GPU_COLORCOMPONENT_G |
                                     SDL_GPU_COLORCOMPONENT_B |
                                     SDL_GPU_COLORCOMPONENT_A;
            blend.enable_color_write_mask = false;

            SDL_GPUColorTargetDescription color_target{};
            color_target.format = desc.color_target_format;
            color_target.blend_state = blend;

            SDL_GPUGraphicsPipelineCreateInfo pci{};
            pci.vertex_shader   = vert_shader;
            pci.fragment_shader = frag_shader;
            pci.primitive_type  = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
            pci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
            pci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
            pci.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
            pci.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
            pci.target_info.num_color_targets = 1;
            pci.target_info.color_target_descriptions = &color_target;
            pci.target_info.has_depth_stencil_target  = false;
            pci.props = 0;

            pipeline = SDL_CreateGPUGraphicsPipeline( d.raw(), &pci );
            if( !pipeline ) {
                throw std::runtime_error( std::string( "sprite_batcher pipeline: " ) +
                                          SDL_GetError() );
            }

            // Default sampler: NEAREST to keep pixel-art parity with the
            // legacy SDL_Renderer SCALEMODE_NEAREST atlas.
            SDL_GPUSamplerCreateInfo si{};
            si.min_filter     = SDL_GPU_FILTER_NEAREST;
            si.mag_filter     = SDL_GPU_FILTER_NEAREST;
            si.mipmap_mode    = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
            si.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
            si.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
            si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
            default_sampler = SDL_CreateGPUSampler( d.raw(), &si );
            if( !default_sampler ) {
                throw std::runtime_error( std::string( "sprite_batcher sampler: " ) +
                                          SDL_GetError() );
            }

            // Ring of (transfer, storage) buffer pairs.
            const Uint32 byte_size = MAX_INSTANCES * sizeof( sprite_instance );
            for( Uint32 i = 0; i < RING_SLOTS; ++i ) {
                SDL_GPUBufferCreateInfo bci{};
                bci.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
                bci.size  = byte_size;
                bci.props = 0;
                storage_bufs[i] = SDL_CreateGPUBuffer( d.raw(), &bci );

                SDL_GPUTransferBufferCreateInfo tci{};
                tci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
                tci.size  = byte_size;
                tci.props = 0;
                xfer_bufs[i] = SDL_CreateGPUTransferBuffer( d.raw(), &tci );

                if( !storage_bufs[i] || !xfer_bufs[i] ) {
                    throw std::runtime_error( std::string( "sprite_batcher buffers: " ) +
                                              SDL_GetError() );
                }
            }

            pending.reserve( MAX_INSTANCES );
            segments.reserve( 64 );

            dbg( DL::Info ) << "sprite_batcher initialised (" << ( label ? label : "?" )
                            << ", max_instances=" << MAX_INSTANCES << ")";
        }

        void shutdown() noexcept {
            if( !dev ) {
                return;
            }
            SDL_GPUDevice *r = dev->raw();
            for( Uint32 i = 0; i < RING_SLOTS; ++i ) {
                if( storage_bufs[i] ) {
                    SDL_ReleaseGPUBuffer( r, storage_bufs[i] );
                    storage_bufs[i] = nullptr;
                }
                if( xfer_bufs[i] ) {
                    SDL_ReleaseGPUTransferBuffer( r, xfer_bufs[i] );
                    xfer_bufs[i] = nullptr;
                }
            }
            if( default_sampler ) {
                SDL_ReleaseGPUSampler( r, default_sampler );
                default_sampler = nullptr;
            }
            if( pipeline ) {
                SDL_ReleaseGPUGraphicsPipeline( r, pipeline );
                pipeline = nullptr;
            }
            if( vert_shader ) {
                SDL_ReleaseGPUShader( r, vert_shader );
                vert_shader = nullptr;
            }
            if( frag_shader ) {
                SDL_ReleaseGPUShader( r, frag_shader );
                frag_shader = nullptr;
            }
            pending.clear();
            segments.clear();
            dev = nullptr;
        }

        // ---- per-frame ------------------------------------------------

        void begin_frame() noexcept {
            cur_slot = ( cur_slot + 1 ) % RING_SLOTS;
        }

        // ---- pass scope -----------------------------------------------

        void begin_pass( SDL_GPUCommandBuffer *cb, SDL_GPUTexture *target,
                         Uint32 w, Uint32 h, const float *clear,
                         Uint32 proj_w, Uint32 proj_h ) {
            if( pass_open ) {
                throw std::runtime_error( "sprite_batcher: begin_pass without end_pass" );
            }
            cur_cb = cb;
            cur_target = target;
            cur_target_w = w;
            cur_target_h = h;
            cur_proj_w   = proj_w ? proj_w : w;
            cur_proj_h   = proj_h ? proj_h : h;
            cur_clear = clear != nullptr;
            if( clear ) {
                std::memcpy( cur_clear_color, clear, sizeof( cur_clear_color ) );
            }
            pending.clear();
            segments.clear();
            bound_tex         = nullptr;
            bound_sampler     = nullptr;
            seg_start         = 0;
            bound_has_scissor = false;
            bound_scissor     = {};
            bound_is_lit      = true;
            pass_open         = true;
        }

        void set_texture( SDL_GPUTexture *atlas, SDL_GPUSampler *sampler,
                          bool is_lit ) {
            if( !sampler ) {
                sampler = default_sampler;
            }
            if( atlas == bound_tex && sampler == bound_sampler
                && is_lit == bound_is_lit ) {
                return;
            }
            close_segment();
            bound_tex     = atlas;
            bound_sampler = sampler;
            bound_is_lit  = is_lit;
            seg_start     = static_cast<Uint32>( pending.size() );
        }

        void set_scissor( const SDL_Rect *rect ) {
            const bool want = ( rect != nullptr );
            const SDL_Rect r = want ? *rect : SDL_Rect{};
            if( want == bound_has_scissor
                && ( !want || ( r.x == bound_scissor.x && r.y == bound_scissor.y
                                && r.w == bound_scissor.w && r.h == bound_scissor.h ) ) ) {
                return;
            }
            close_segment();
            bound_has_scissor = want;
            bound_scissor     = r;
        }

        void draw( const sprite_instance &inst ) {
            if( pending.size() >= MAX_INSTANCES ) {
                dbg( DL::Warn ) << "sprite_batcher: per-pass instance cap reached, dropping";
                return;
            }
            pending.push_back( inst );
        }

        void draw_many( const sprite_instance *insts, std::size_t count ) {
            const std::size_t headroom = MAX_INSTANCES - pending.size();
            const std::size_t to_add = std::min( headroom, count );
            if( to_add < count ) {
                dbg( DL::Warn ) << "sprite_batcher: dropped " << ( count - to_add )
                                << " instances over per-pass cap";
            }
            pending.insert( pending.end(), insts, insts + to_add );
        }

        void flush() {
            close_segment();
        }

        void end_pass() {
            if( !pass_open ) {
                return;
            }
            close_segment();

            // No work and no clear → trivially nothing to do.
            if( segments.empty() && !cur_clear ) {
                pass_open = false;
                return;
            }

            // Upload pending instances if any.
            if( !pending.empty() ) {
                const Uint32 slot = cur_slot;
                const Uint32 byte_size = static_cast<Uint32>(
                                             pending.size() * sizeof( sprite_instance ) );

                void *mapped = SDL_MapGPUTransferBuffer( dev->raw(), xfer_bufs[slot],
                                                        /*cycle=*/true );
                if( !mapped ) {
                    dbg( DL::Error ) << "MapGPUTransferBuffer failed: " << SDL_GetError();
                    pass_open = false;
                    return;
                }
                std::memcpy( mapped, pending.data(), byte_size );
                SDL_UnmapGPUTransferBuffer( dev->raw(), xfer_bufs[slot] );

                SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass( cur_cb );
                if( !cp ) {
                    dbg( DL::Error ) << "BeginGPUCopyPass failed: " << SDL_GetError();
                    pass_open = false;
                    return;
                }
                SDL_GPUTransferBufferLocation src{};
                src.transfer_buffer = xfer_bufs[slot];
                src.offset = 0;
                SDL_GPUBufferRegion dst{};
                dst.buffer = storage_bufs[slot];
                dst.offset = 0;
                dst.size = byte_size;
                // 2026-05-22: cycle=true on the storage buffer upload.
                // RING_SLOTS=3 lets the application rotate buffers across
                // frames, but on Win11 D3D12 with heavy frames (5000+
                // instances) the GPU still reads the previous frame's
                // contents of this slot when the CPU starts overwriting,
                // because the SDL_GPU device queues more than RING_SLOTS
                // frames in flight under load. Forcing cycle here makes
                // SDL discard the in-use allocation and provide a fresh
                // one, removing the race. Bridge (1 inst) and per-glyph
                // segments (1 inst each) never showed the symptom; the
                // tile-sprite white-tex segment with thousands of
                // instances did.
                SDL_UploadToGPUBuffer( cp, &src, &dst, /*cycle=*/true );
                SDL_EndGPUCopyPass( cp );
            }

            // Render pass.
            SDL_GPUColorTargetInfo ct{};
            ct.texture = cur_target;
            ct.load_op = cur_clear ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
            ct.store_op = SDL_GPU_STOREOP_STORE;
            if( cur_clear ) {
                ct.clear_color.r = cur_clear_color[0];
                ct.clear_color.g = cur_clear_color[1];
                ct.clear_color.b = cur_clear_color[2];
                ct.clear_color.a = cur_clear_color[3];
            }
            ct.cycle = false;

            SDL_GPURenderPass *rp = SDL_BeginGPURenderPass( cur_cb, &ct, 1, nullptr );
            if( !rp ) {
                dbg( DL::Error ) << "BeginGPURenderPass failed: " << SDL_GetError();
                pass_open = false;
                return;
            }

            if( !segments.empty() ) {
                SDL_BindGPUGraphicsPipeline( rp, pipeline );

                SDL_GPUBufferBinding storage_binding{};
                storage_binding.buffer = storage_bufs[cur_slot];
                storage_binding.offset = 0;
                SDL_BindGPUVertexStorageBuffers( rp, /*first_slot=*/0,
                                                 &storage_binding.buffer, 1 );

                // After 2026-05-29 refactor: fragment resource layout is
                //   sampler slot 0 → Atlas      (per-segment)
                //   storage  slot 0 → Emitters  (StructuredBuffer<GpuEmitter>)
                //   storage  slot 1 → SdfBuf      (StructuredBuffer<float>)
                //   storage  slot 2 → SkyVisBuf   (StructuredBuffer<float>)
                //   storage  slot 3 → IndirectBuf (StructuredBuffer<float>)
                // matching HLSL t0 sampled + t1/t2/t3/t4 storage (space2).
                // Bind all storage buffers in one call so a later bind can't
                // zero an earlier slot. SDF/sky/indirect reads are gated by
                // sdf_map_w>0 in the shader, so unbound slots (no SDF yet) are safe.
                if( lp_emitter_buf && lp_sdf_buf && lp_sky_vis_buf && lp_indirect_buf ) {
                    SDL_GPUBuffer *sbufs[4] = { lp_emitter_buf, lp_sdf_buf, lp_sky_vis_buf, lp_indirect_buf };
                    SDL_BindGPUFragmentStorageBuffers( rp, /*first_slot=*/0, sbufs, 4 );
                } else if( lp_emitter_buf && lp_sdf_buf && lp_sky_vis_buf ) {
                    SDL_GPUBuffer *sbufs[3] = { lp_emitter_buf, lp_sdf_buf, lp_sky_vis_buf };
                    SDL_BindGPUFragmentStorageBuffers( rp, /*first_slot=*/0, sbufs, 3 );
                } else if( lp_emitter_buf && lp_sdf_buf ) {
                    SDL_GPUBuffer *sbufs[2] = { lp_emitter_buf, lp_sdf_buf };
                    SDL_BindGPUFragmentStorageBuffers( rp, /*first_slot=*/0, sbufs, 2 );
                } else if( lp_emitter_buf ) {
                    SDL_BindGPUFragmentStorageBuffers( rp, /*first_slot=*/0,
                                                       &lp_emitter_buf, 1 );
                }

                const SDL_GPUViewport vp{
                    0.0f, 0.0f,
                    static_cast<float>( cur_target_w ),
                    static_cast<float>( cur_target_h ),
                    0.0f, 1.0f
                };
                SDL_SetGPUViewport( rp, &vp );

                bool     last_has_scissor = false;
                SDL_Rect last_scissor     = {};
                for( const segment &s : segments ) {
                    const bool changed = ( s.has_scissor != last_has_scissor )
                                         || ( s.has_scissor
                                              && ( s.scissor.x != last_scissor.x
                                                   || s.scissor.y != last_scissor.y
                                                   || s.scissor.w != last_scissor.w
                                                   || s.scissor.h != last_scissor.h ) );
                    if( changed ) {
                        if( s.has_scissor ) {
                            SDL_SetGPUScissor( rp, &s.scissor );
                        } else {
                            const SDL_Rect full{ 0, 0,
                                                 static_cast<int>( cur_target_w ),
                                                 static_cast<int>( cur_target_h ) };
                            SDL_SetGPUScissor( rp, &full );
                        }
                        last_has_scissor = s.has_scissor;
                        last_scissor     = s.scissor;
                    }

                    SDL_GPUTextureSamplerBinding tsb{};
                    tsb.texture = s.tex;
                    tsb.sampler = s.sampler;
                    SDL_BindGPUFragmentSamplers( rp, /*first_slot=*/0, &tsb, 1 );

                    // Diagnostic: log the first LIT segment per frame so we
                    // can see, on the CPU, what state actually reaches the
                    // GPU draw. Confirms is_lit, emitter texture binding,
                    // emitter_count uniform, and debug_mode without
                    // depending on screen-colour interpretation.
                    {
                        static int sb_dbg_frame = 0;
                        static bool sb_dbg_logged_this_frame = false;
                        // Reset the per-frame guard once per 60 frames.
                        if( ++sb_dbg_frame % 60 == 0 ) {
                            sb_dbg_logged_this_frame = false;
                        }
                        if( s.is_lit && !sb_dbg_logged_this_frame ) {
                            sb_dbg_logged_this_frame = true;
                            DebugLogFL( DL::Info, DC::Main )
                                    << "sprite_batcher lit_seg: tex=" << static_cast<const void *>( s.tex )
                                    << " emitter_buf=" << static_cast<const void *>( lp_emitter_buf )
                                    << " ec=" << lp.emitter_count
                                    << " tile_px=" << lp.tile_pixel_size
                                    << " cam_off=(" << lp.camera_off_x << ","
                                    << lp.camera_off_y << ")"
                                    << " sdf_map_w=" << lp.sdf_map_w
                                    << " sdf_buf=" << ( lp_sdf_buf ? "ok" : "NULL" )
                                    << " skyvis_buf=" << ( lp_sky_vis_buf ? "ok" : "NULL" )
                                    << " dbg_mode=" << lp_debug.debug_mode;
                        }
                    }

                    // Re-bind emitter+SDF+sky-vis storage each segment.
                    // SDL_BindGPUFragmentStorageBuffers can rebuild the
                    // descriptor table and zero any slot not included, so we
                    // re-issue for lit segments. Unlit (HUD/UI) segments skip —
                    // emitter_count==0 / sdf_map_w==0 guards in the shader
                    // short-circuit any reads there.
                    if( s.is_lit && lp_emitter_buf && lp_sdf_buf && lp_sky_vis_buf && lp_indirect_buf ) {
                        SDL_GPUBuffer *sbufs[4] = { lp_emitter_buf, lp_sdf_buf, lp_sky_vis_buf, lp_indirect_buf };
                        SDL_BindGPUFragmentStorageBuffers( rp, /*first_slot=*/0, sbufs, 4 );
                    } else if( s.is_lit && lp_emitter_buf && lp_sdf_buf && lp_sky_vis_buf ) {
                        SDL_GPUBuffer *sbufs[3] = { lp_emitter_buf, lp_sdf_buf, lp_sky_vis_buf };
                        SDL_BindGPUFragmentStorageBuffers( rp, /*first_slot=*/0, sbufs, 3 );
                    } else if( s.is_lit && lp_emitter_buf && lp_sdf_buf ) {
                        SDL_GPUBuffer *sbufs[2] = { lp_emitter_buf, lp_sdf_buf };
                        SDL_BindGPUFragmentStorageBuffers( rp, /*first_slot=*/0, sbufs, 2 );
                    } else if( s.is_lit && lp_emitter_buf ) {
                        SDL_BindGPUFragmentStorageBuffers( rp, /*first_slot=*/0,
                                                           &lp_emitter_buf, 1 );
                    }

                    // Shader pixel→NDC math uses the projection extent
                    // (logical UI space), NOT the viewport extent. The
                    // SDL_GPUViewport above is set to physical pixels so
                    // logical-coord draws stretch to fill the full
                    // physical framebuffer on HiDPI.
                    frame_params fp{
                        static_cast<float>( cur_proj_w ),
                        static_cast<float>( cur_proj_h ),
                        s.start,
                        0u
                    };
                    SDL_PushGPUVertexUniformData( cur_cb, /*slot=*/0, &fp, sizeof( fp ) );

                    // For unlit segments push a copy of lp with all
                    // light-driving counts zeroed; the vertex shader still
                    // needs camera_off + tile_pixel_size for world_pos
                    // (consistent geometry), and the fragment shader's
                    // existing guards then skip the loop and march.
                    // Similarly, lp_sun gets sun_intensity zeroed, and
                    // lp_debug gets debug_mode=0 so the visualisation
                    // dispatch never fires on HUD fragments.
                    light_params  lp_use     = lp;
                    sun_params    lp_sun_use = lp_sun;
                    debug_params  lp_dbg_use = lp_debug;
                    if( !s.is_lit ) {
                        lp_use.emitter_count   = 0u;
                        lp_use.sdf_map_w       = 0u;
                        lp_use.sdf_map_h       = 0u;
                        lp_sun_use.sun_intensity = 0.0f;
                        lp_sun_use.sky_intensity = 0.0f;
                        lp_dbg_use.debug_mode  = 0u;
                    }
                    // Vertex slot 1: LightParams (world_pos computation)
                    SDL_PushGPUVertexUniformData( cur_cb, /*slot=*/1, &lp_use, sizeof( lp_use ) );
                    // Fragment slot 0: LightParams (ambient, emitter_count, sdf_map_w)
                    SDL_PushGPUFragmentUniformData( cur_cb, /*slot=*/0, &lp_use, sizeof( lp_use ) );
                    // Fragment slot 1: SunParams (sun/sky direction + color)
                    SDL_PushGPUFragmentUniformData( cur_cb, /*slot=*/1, &lp_sun_use, sizeof( lp_sun_use ) );
                    // Fragment slot 2: DebugParams (visualisation + tunable scales)
                    SDL_PushGPUFragmentUniformData( cur_cb, /*slot=*/2, &lp_dbg_use, sizeof( lp_dbg_use ) );

                    SDL_DrawGPUPrimitives( rp, /*num_vertices=*/6, /*num_instances=*/s.count,
                                           /*first_vertex=*/0, /*first_instance=*/0 );
                }
            }

            SDL_EndGPURenderPass( rp );
            pass_open = false;
        }

    private:
        // Move the currently bound (tex, sampler) + accumulated count into
        // `segments`. Safe to call when no instances are pending — no-ops.
        void close_segment() {
            const Uint32 end = static_cast<Uint32>( pending.size() );
            if( !bound_tex || end <= seg_start ) {
                return;
            }
            segment s{ bound_tex, bound_sampler, seg_start, end - seg_start };
            s.scissor     = bound_scissor;
            s.has_scissor = bound_has_scissor;
            s.is_lit      = bound_is_lit;
            segments.push_back( s );
            seg_start = end;
        }
};

// ---- sprite_batcher trampolines ---------------------------------------

sprite_batcher::sprite_batcher() : p( std::make_unique<sprite_batcher_impl>() ) {}
sprite_batcher::sprite_batcher( sprite_batcher && ) noexcept = default;
sprite_batcher &sprite_batcher::operator=( sprite_batcher && ) noexcept = default;
sprite_batcher::~sprite_batcher()
{
    if( p ) {
        p->shutdown();
    }
}

void sprite_batcher::init( gpu_device &dev, const pipeline_desc &desc,
                           const char *debug_label )
{
    p->init( dev, desc, debug_label );
}

void sprite_batcher::shutdown() noexcept
{
    if( p ) {
        p->shutdown();
    }
}

void sprite_batcher::begin_pass( SDL_GPUCommandBuffer *cb,
                                 SDL_GPUTexture *target,
                                 std::uint32_t target_w,
                                 std::uint32_t target_h,
                                 const float *clear_color_rgba,
                                 std::uint32_t proj_w,
                                 std::uint32_t proj_h )
{
    p->begin_pass( cb, target, target_w, target_h, clear_color_rgba,
                   proj_w, proj_h );
}

void sprite_batcher::set_texture( SDL_GPUTexture *atlas, SDL_GPUSampler *sampler,
                                  bool is_lit )
{
    p->set_texture( atlas, sampler, is_lit );
}

void sprite_batcher::set_scissor( const SDL_Rect *rect )
{
    p->set_scissor( rect );
}

void sprite_batcher::set_lighting_resources( float            tile_pixel_size,
                                              float            z_level,
                                              Uint32           emitter_count,
                                              float            ambient,
                                              float            cam_off_x,
                                              float            cam_off_y,
                                              Uint32           sdf_map_w,
                                              Uint32           sdf_map_h,
                                              SDL_GPUBuffer   *emitter_buf,
                                              SDL_GPUBuffer   *sdf_buf,
                                              SDL_GPUSampler  *data_sampler,
                                              SDL_GPUBuffer   *sky_vis_buf,
                                              SDL_GPUBuffer   *indirect_buf,
                                              const sun_params   *sp,
                                              const debug_params *dbg )
{
    p->set_lighting_resources( tile_pixel_size, z_level,
                                emitter_count, ambient,
                                cam_off_x, cam_off_y, sdf_map_w, sdf_map_h,
                                emitter_buf, sdf_buf, data_sampler,
                                sky_vis_buf, indirect_buf, sp, dbg );
}

void sprite_batcher::draw( const sprite_instance &inst )
{
    p->draw( inst );
}

void sprite_batcher::draw( const sprite_instance *insts, std::size_t count )
{
    p->draw_many( insts, count );
}

void sprite_batcher::flush()
{
    p->flush();
}

void sprite_batcher::end_pass()
{
    p->end_pass();
}

void sprite_batcher::begin_frame()
{
    p->begin_frame();
}

} // namespace lighting

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
struct GpuEmitter {
    float4 pos_radius;
    float4 color_falloff;
    float4 cone_shape;
    float4 misc;
};
Texture2D<float4>            Atlas       : register(t0, space2);
SamplerState                 AtlasSmp    : register(s0, space2);
// 1-bounce GI as a read-only storage texture (Texture2D, NO sampler, read via
// .Load only — that is what makes shadercross reflect it as a storage texture,
// not a 2nd sampled image). Storage-texture slot 0 ⇒ t1, ahead of the storage
// buffers. RGBA32F, width=sdf_map_h height=sdf_map_w (see indirect_texel).
Texture2D<float4>            IndirectTex : register(t1, space2);
StructuredBuffer<GpuEmitter> Emitters    : register(t2, space2);
StructuredBuffer<float>      SdfBuf      : register(t3, space2);
StructuredBuffer<float>      SkyVisBuf   : register(t4, space2);
StructuredBuffer<float>      VisBuf      : register(t5, space2); // per-tile visibility (>=0 live, <0 memory)
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
    float dp_pad1;
    float dp_pad2;
};
struct VS_OUT {
    float4 pos      : SV_Position;
    float2 uv       : TEXCOORD0;
    float4 tint     : TEXCOORD1;
    float2 world_pos: TEXCOORD2;
    float  light_mul: TEXCOORD3; // memory-fade marker (<0 = -(dist); else no-op)
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
// Per-tile 1-bounce indirect light (RGB). Now a read-only storage texture
// (Phase 1b). The CPU source is x-major arr[x*map_h+y]; the texture is uploaded
// in that same linear order into dims width=map_h height=map_w, so tile (x,y)
// lives at texel (col=y, row=x) → Load(int3(y, x, 0)). This row/col swap is the
// row-major texture convention (cf. the sampler-texture note in CLAUDE.md);
// it makes the texture read pixel-identical to the old x-major buffer index.
// Bilinear with the same p-0.5 centre convention as sdf_bilinear.
float3 indirect_texel(int x, int y) {
    x = clamp(x, 0, (int)sdf_map_w - 1);
    y = clamp(y, 0, (int)sdf_map_h - 1);
    return IndirectTex.Load(int3(y, x, 0)).rgb;
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
// Per-tile LIVE visibility (1 float/tile, x-major). VisBuf[i] is raw
// max(seen_cache, camera_cache) in [0,1]; 0 = not currently visible.
float vis_texel(int x, int y) {
    x = clamp(x, 0, (int)sdf_map_w - 1);
    y = clamp(y, 0, (int)sdf_map_h - 1);
    return VisBuf[x * (int)sdf_map_h + y];
}
// Bilinear live visibility. Taps clamped to max(0, .) defensively. Same
// p-0.5 tile-centre convention as sdf_bilinear (keeps the falloff glued to
// tile centres; no half-tile shift).
float vis_bilinear(float2 p) {
    const float2 sp = p - 0.5;
    const float2 fp = floor(sp);
    const int   x0  = (int)fp.x;
    const int   y0  = (int)fp.y;
    const float2 w  = sp - fp;
    const float a = max(0.0, vis_texel(x0,     y0    ));
    const float b = max(0.0, vis_texel(x0 + 1, y0    ));
    const float c = max(0.0, vis_texel(x0,     y0 + 1));
    const float d = max(0.0, vis_texel(x0 + 1, y0 + 1));
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
// --- Bucket A / A1: inline alpha-aware Sobel surface normal ----------------
// Per-pixel relief derived from the albedo atlas itself (decision #15's
// "alpha-aware Sobel + edge-fade-to-flat at silhouettes"), computed in the
// fragment instead of baked to a normal atlas at tileset load. This needs NO
// second texture, so it sidesteps the Metal/shadercross sampler-vs-storage
// gate entirely — the question of whether per-pixel normals add visible value
// in a 2D tile game is answered before any normal-atlas pipeline is built.
// GetDimensions queries the *currently bound* atlas page, so per-page texel
// size is correct without a uniform. Tuning is shader constants for now
// (promote to F4 sliders only if the effect earns its keep). NRM_AMOUNT=0
// reduces both Lambert sites to the previous flat-normal behaviour exactly.
static const float NRM_RELIEF = 2.0;   // luminance-gradient → surface tilt
static const float NRM_ELEV   = 0.7;   // implied emitter height above the tile plane
static const float NRM_AMOUNT = 0.5;   // 0 = flat (off) .. 1 = full Lambert
float3 surface_normal(float2 uv) {
    float aw, ah;
    Atlas.GetDimensions(aw, ah);
    const float2 ts = 1.0 / float2(max(aw, 1.0), max(ah, 1.0));
    const float4 sL = Atlas.Sample(AtlasSmp, uv - float2(ts.x, 0.0));
    const float4 sR = Atlas.Sample(AtlasSmp, uv + float2(ts.x, 0.0));
    const float4 sU = Atlas.Sample(AtlasSmp, uv - float2(0.0, ts.y));
    const float4 sD = Atlas.Sample(AtlasSmp, uv + float2(0.0, ts.y));
    const float3 luma = float3(0.299, 0.587, 0.114);
    const float dx = dot(sR.rgb, luma) - dot(sL.rgb, luma);
    const float dy = dot(sD.rgb, luma) - dot(sU.rgb, luma);
    const float3 n = normalize(float3(-dx * NRM_RELIEF, -dy * NRM_RELIEF, 1.0));
    // Edge-fade-to-flat: a (near-)transparent neighbour means we're at the
    // sprite silhouette — collapse back toward flat so a packed-atlas
    // neighbour can't bleed a hard fake edge across the gap.
    const float edge = min(min(sL.a, sR.a), min(sU.a, sD.a));
    return normalize(lerp(float3(0.0, 0.0, 1.0), n, saturate(edge)));
}
float4 main(VS_OUT i) : SV_Target0 {
    const float4 texel = Atlas.Sample(AtlasSmp, i.uv);
    if(texel.a < 0.01) discard;
    // Per-pixel surface normal from inline alpha-aware Sobel (Bucket A / A1).
    // Replaces the old hardcoded flat normal; drives the emitter + sun Lambert.
    const float3 normal = surface_normal(i.uv);
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
        const float2 e_cone_dir = e.cone_shape.xy;          // normalized beam axis
        const float  e_cone_ha  = e.cone_shape.z;           // half-angle (rad; π = omni)
        const uint   e_shape    = asuint(e.cone_shape.w);   // emitter_shape enum
        if(abs(e_pos.z - current_z) > 0.5) continue;
        const float2 dv   = e_pos.xy - i.world_pos;
        const float  dist = length(dv);
        if(dist >= e_radius || dist < 0.01) continue;
        const float  atten   = 1.0 - pow(saturate(dist / e_radius), e_falloff);
        // Per-pixel Lambert against this emitter (Bucket A / A1). The light's
        // in-plane direction is sh_dir (fragment→light); NRM_ELEV lifts it off
        // the tile plane so relief tilts catch it. Blended with NRM_AMOUNT so
        // flat surfaces stay near full brightness (0 = old omni lambert=1).
        const float2 sh_dir = dv / max(dist, 0.001);
        const float  lambert = lerp(1.0,
                                    saturate(dot(normal, normalize(float3(sh_dir, NRM_ELEV)))),
                                    NRM_AMOUNT);
        // Per-emitter soft shadow via the shared SDF sphere trace (bilinear,
        // shadow_k / shadow_steps tunable). See trace_shadow above. sh_dir is
        // declared above (the A1 Lambert reuses it).
        const float  shadow = trace_shadow(i.world_pos, sh_dir, dist,
                                            shadow_k, (int)shadow_steps);
        // Cone / spotlight angular falloff (Bucket A / A2). Held flashlights and
        // vehicle headlights are tagged CONE by build_emitter_snapshot with a
        // normalized beam direction (cone_dir) and half-angle; OMNI emitters keep
        // cone=1. sh_dir points fragment→light, so the light→fragment ray is
        // -sh_dir; both the beam axis and that ray live in world tile space
        // (x, y-down) matching the CPU cos/sin(idir) / v->face.dir() convention.
        // Soft edge: full intensity within ~0.6 of the half-angle, smoothstep to
        // zero at the rim (cos is monotone-decreasing in angle, so the inner
        // threshold cos(0.6·ha) > the rim threshold cos(ha)).
        float cone = 1.0;
        if(e_shape == 1u) {            // emitter_shape::CONE
            const float cosang = dot(normalize(e_cone_dir), -sh_dir);
            cone = smoothstep(cos(e_cone_ha), cos(e_cone_ha * 0.6), cosang);
        }
        const float3 rgb = (e_color.x < 0.01 && e_color.y < 0.01 && e_color.z < 0.01)
                           ? float3(1, 1, 1) : e_color;
        emitter_light += rgb * atten * lambert * shadow * cone;
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
        // Per-pixel Lambert against the sun (Bucket A / A1). The 3D sun ray is
        // (toward_sun.xy, sin_elev). At NRM_AMOUNT=0 this collapses to the old
        // flat-normal value: dot((0,0,1), normalize(toward_sun, sin_elev)) =
        // sin_elev / sqrt(1 + sin_elev^2).
        const float flat_sun = sun_sin_elev / sqrt(1.0 + sun_sin_elev * sun_sin_elev);
        const float3 sun_L   = normalize(float3(toward_sun, sun_sin_elev));
        const float sun_lambert = lerp(flat_sun, saturate(dot(normal, sun_L)), NRM_AMOUNT);
        sun_contrib = float3(sun_r, sun_g, sun_b) * sun_intensity * sun_lambert
                      * sun_shadow * sky_vis;
    }

    // Apply runtime tuning scales BEFORE compositing. emitter_scale tunes
    // global brightness of all emitter contributions; sun_scale / sky_scale
    // tune the sun + sky streams. Defaults are 1.0 (no-op).
    emitter_light *= emitter_scale;
    sun_contrib   *= sun_scale;
    sky_contrib   *= sky_scale;

    // Ambient floor. In-game (sdf_map_w>0) it auto-tracks time-of-day:
    // lerp(night_floor, day_floor, sun_intensity) → darker, more immersive
    // nights (effect 4). UI / main menu (no world) keep the CPU `ambient`
    // uniform so backdrops/text are unaffected. night_floor==day_floor
    // disables the day/night swing (bisect).
    const float amb_floor = (sdf_map_w > 0u)
                            ? lerp(night_floor, day_floor, saturate(sun_intensity))
                            : ambient;
    const float3 ambient_v = float3(amb_floor, amb_floor, amb_floor);

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

    // Vision falloff (Stoneshard-style soft edge + radial darkening). Gated to
    // game tiles (tint≈0) so UI/HUD glyphs are never darkened, and to
    // sdf_map_w>0 (no world → main menu skips). vis_curve>1 steepens the edge;
    // Vision + Stoneshard tone grade — LIT world tiles only (gpu_light==0 →
    // tint≈0). UI/HUD (tint>0) and memory/dark tiles (tint=1.0) are untouched.
    // Each sub-effect disables at its off-value for live bisect.
    if(sdf_map_w > 0u && dbg_tint_sum < 0.01) {
        // (a) Vision-edge falloff from seen_cache (softens the rim).
        if(vis_curve > 0.0001) {
            final_rgb *= pow(saturate(vis_bilinear(i.world_pos)), vis_curve);
        }
        // (b) Radial player-distance falloff — the torch-bubble gradient that
        // seen_cache can't give (it's saturated ≈1 across the open interior).
        // Darkens continuously with distance from the player. vis_radius=0=off.
        if(vis_radius > 0.01) {
            const float d = length(i.world_pos - float2(player_x, player_y));
            const float r = saturate(1.0 - d / vis_radius);
            final_rgb *= r * r * (3.0 - 2.0 * r); // smoothstep: bright centre, eased edge
        }
        // (c) Tone grade (Stoneshard wash): desaturate → cool tint → dim.
        const float luma = dot(final_rgb, float3(0.299, 0.587, 0.114));
        final_rgb = lerp(final_rgb, float3(luma, luma, luma), saturate(grade_desat));
        final_rgb *= lerp(float3(1.0, 1.0, 1.0), float3(0.60, 0.85, 1.0), saturate(grade_cool));
        final_rgb *= grade_bright;
    }

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
        } else if(debug_mode == 9u) {
            // Normal view (Bucket A / A1): encode the inline-Sobel surface
            // normal as RGB via n*0.5+0.5. Flat tiles read ~(0.5,0.5,1.0)
            // lavender-blue; relief tilts push R (x-slope) / G (y-slope).
            // Replace so it shows raw; game tiles only (tint-gated below).
            vis = normal * 0.5 + 0.5;
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

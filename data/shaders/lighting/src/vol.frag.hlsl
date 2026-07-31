// Volumetric "lit fog" — Step-6 / C2, first slice (full-res sun shafts).
//
// Top-down orthographic has no view ray, so the classic depth-march in-scatter
// integral is meaningless here: it collapses to "how much sunlight reaches the
// air over this pixel" × local fog density. So
//   lit_fog(P) = density · sun_colour · sun_intensity · skyvis(P) · sun_shadow(P)
// evaluated ONCE per screen pixel. It is a SEPARATE fullscreen pass (not inline
// in sprite.frag) because the sprite shader runs once per stacked sprite layer
// and an additive emissive term would multi-count where sprites overlap. The
// result is composited additively into the HDR world_target BEFORE bloom +
// tonemap (shafts bloom; AgX rolls off the highlights).
//
// skyvis gates to open-sky (outdoor) air. The engine sky-gates the sun, so the
// shafts here are the lit haze between wall/tree shadow LANES outdoors, not
// indoor window beams (those would need decoupling the sun from skyvis — a
// later, larger change). Strict light-shaft = shadow air contributes zero, so
// shadows stay black and the additive haze does not wash the AgX black floor.
//
// v1 density is a single uniform (weather-scaled CPU-side); per-tile field
// density (fd_smoke / fd_*gas / fd_fog) is the follow-on. The in-plane "march"
// is the shared trace_shadow (smooth SDF cone penumbra), so no blue-noise
// jitter is needed at v1.
//
// Resources (space2), sampler-less ⇒ storage buffers start at t0:
//   t0  SdfBuf    — StructuredBuffer<float>, SS-finer grid (stride sdf_map_h*SDF_SS)
//   t1  SkyVisBuf — StructuredBuffer<float>, tile-res, x-major skyvis[x*map_h+y]
// (shadercross reflection must show samplers=0 storage_buffers=2.)

StructuredBuffer<float> SdfBuf    : register(t0, space2);
StructuredBuffer<float> SkyVisBuf : register(t1, space2);

cbuffer VolParams : register(b0, space3) {
    float tile_pixel_size;  // screen px per tile (mirror sprite.vert)
    float camera_off_x;     // = light_params.camera_off_x (single source of truth)
    float camera_off_y;
    float current_z;        // unused at v1 (single-z march); kept for parity
    float sun_dir_x;        // sun in-plane direction (light→ground), as in sprite.frag
    float sun_dir_y;
    float sun_intensity;    // weather-scaled (A3), 0 = no sun → no shaft
    float sun_r;
    float sun_g;
    float sun_b;
    float vol_density;      // uniform haze amount (F4; weather-scalable later)
    float vol_intensity;    // overall multiplier (F4)
    float vol_reach;        // sun-shadow march reach in tiles (shaft length)
    float shadow_k;         // reuse sprite soft-shadow hardness
    uint  shadow_steps;     // reuse sprite march cap
    uint  sdf_map_w;        // runtime SDF tile dims
    uint  sdf_map_h;
    float proj_w;           // projection (game-view) size in px — NOT the target
    float proj_h;           // texture size; proj-space stretches to fill the target
    float vol_shadow;       // 0 = uniform sky-gated haze (no cast shadow); >0 lerps
                            // the directional wall/tree shadow lanes back in
};

// MUST match lighting::SDF_SUPERSAMPLE (sdf_pass.h) and sprite.frag's SDF_SS.
static const int SDF_SS = 8;

// SDF helpers — VERBATIM from the current (supersampled) sprite.frag so the
// shaft occlusion march samples the exact same field the surface shadows do.
float sdf_texel(int x, int y) {
    const int gw = (int)sdf_map_w * SDF_SS;
    const int gh = (int)sdf_map_h * SDF_SS;
    x = clamp(x, 0, gw - 1);
    y = clamp(y, 0, gh - 1);
    return SdfBuf[x * gh + y];
}
float sdf_bilinear(float2 p) {
    const float2 g  = p * (float)SDF_SS - 0.5;
    const float2 fp = floor(g);
    const int   x0  = (int)fp.x;
    const int   y0  = (int)fp.y;
    const float2 w  = g - fp;
    const float a = sdf_texel(x0,     y0    );
    const float b = sdf_texel(x0 + 1, y0    );
    const float c = sdf_texel(x0,     y0 + 1);
    const float d = sdf_texel(x0 + 1, y0 + 1);
    return lerp(lerp(a, b, w.x), lerp(c, d, w.x), w.y);
}
// Sky visibility, bilinear. SkyVisBuf is TILE-res (one float/tile, stride
// sdf_map_h — NOT supersampled), so centre map is g = p-0.5 (no SDF_SS). Mirrors
// sprite.frag's skyvis_bilinear so the fog's open-sky gate softens over ~1 tile
// instead of reading as hard tile squares (the dawn "blocky sun" the uniform fog
// otherwise shows, since at low sun the shaft march is near-degenerate).
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
float trace_shadow(float2 origin, float2 dir, float dist_to_light, float k, int steps) {
    if(sdf_map_w == 0u || steps <= 0) { return 1.0; }
    float shadow = 1.0;
    float t = min(0.3, dist_to_light * 0.5);
    [loop] for(int ss = 0; ss < steps; ++ss) {
        if(t >= dist_to_light - 0.4) break;
        const float sd = sdf_bilinear(origin + dir * t);
        if(sd < 0.05) { shadow = 0.0; break; }
        // Directional (sun): penumbra keys to t (distance from receiver), the
        // textbook IQ form — NOT (dist_to_light - t), which is for point lights
        // with a real distance. dist_to_light here is just the march cap, so
        // (cap - t) inverts the soft/hard ends. Mirrors sprite.frag directional.
        shadow = min(shadow, k * sd / max(t, 0.01));
        t += max(sd, 0.15);
    }
    return saturate(shadow);
}

struct VS_OUT {
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

float4 main(VS_OUT i) : SV_Target0 {
    if(sdf_map_w == 0u || sun_intensity <= 0.001 || vol_density <= 0.0001) {
        return float4(0.0, 0.0, 0.0, 0.0);
    }
    // Reconstruct the world TILE position. The sprite pass renders proj-space
    // pixels [0,proj] STRETCHED to fill the world_target texture [0,target]
    // (its ndc divides by proj_size but its viewport is the full target). So the
    // proj-space pixel at this fragment is uv*proj_size (uv is 0..1 across the
    // target, top-left origin), and then world_pos = pixel/tile_px - camera_off
    // exactly mirrors sprite.vert. (Using SV_Position — target pixels — would be
    // wrong by the proj/target scale, anchored at the origin.)
    const float2 pixel = i.uv * float2(proj_w, proj_h);
    const float2 world_pos = pixel / max(tile_pixel_size, 1.0)
                             - float2(camera_off_x, camera_off_y);
    // Outdoor gate: the sun only reaches open-sky air. Bilinear (matches
    // sprite.frag) so the fog's open↔roofed edge ramps over ~1 tile instead of
    // hard tile squares. Roofed → no sun fog (and shadows stay black).
    const float skyvis = saturate(skyvis_bilinear(world_pos));
    if(skyvis <= 0.01) {
        return float4(0.0, 0.0, 0.0, 0.0);
    }
    // Directional shaft = the wall/tree shadow LANES. Vol's trace_shadow is now
    // byte-identical to sprite.frag's, and both read the SAME sun_dir
    // (make_sun_params) over the SAME world_pos frame — so the march MUST use the
    // same direction as the sprite surface shadow, toward_sun = -sun_dir. Marching
    // the opposite way puts the dark lane on the sun-FACING (lit) side of every
    // occluder, which reads as the shadow being OFFSET from the lit-scene shadow.
    // (A 2026-06-04 "inversion fix" flipped this to +sun_dir while the penumbra
    // denominator was still being tuned; with the denominator now == sprite's the
    // flip is pure error — vol is just a longer-reach copy of the sprite sun
    // shadow, so by construction the lanes co-register with the lit scene.)
    // vol_shadow=0 (default) skips the march → uniform sky-gated haze, blacks black.
    float shadow_term = 1.0;
    if(vol_shadow > 0.001) {
        const float2 toward_sun = -float2(sun_dir_x, sun_dir_y);
        const float  sun_shadow = trace_shadow(world_pos, toward_sun, vol_reach,
                                               shadow_k, (int)shadow_steps);
        shadow_term = lerp(1.0, sun_shadow, saturate(vol_shadow));
    }
    const float3 fog = float3(sun_r, sun_g, sun_b)
                       * (sun_intensity * vol_density * vol_intensity * skyvis * shadow_term);
    return float4(fog, 0.0); // additive (ONE/ONE); alpha unused
}

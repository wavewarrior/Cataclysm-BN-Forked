// Sound wave visualization fragment shader — expanding ring with colored wake.
//
// Renders a single disc per sound pulse. Each fragment computes its distance
// from the pulse source in screen pixels:
//   - A bright ring marks the wavefront at the outer edge.
//   - A colored wake fills the interior, fading toward the source.
//   - A ray-marched SDF visibility test occludes the disc behind walls, so
//     the wavefront wraps around corners and stops at geometry instead of
//     drawing straight through it like a plain overlay.
//   - SDF diffraction adds a subtle glow near walls (additive RGB, alpha
//     unchanged).
//
// Output is premultiplied alpha (pipeline: SRC=ONE, DST=ONE_MINUS_SRC_ALPHA).
//
// Binding convention:
//   t0/space2 — SdfBuf (StructuredBuffer<float>) — optional
//   b0/space3 — SndFragParams (camera + SDF transform)

#include "jfa_shared.hlsl" // SDF_SS

cbuffer SndFragParams : register(b0, space3) {
    float camera_off_x;
    float camera_off_y;
    float op_x;
    float op_y;
    float tile_px_inv;
    float pixel_ratio; // physical / logical pixel ratio (e.g. 2.0 on Retina)
    uint  sdf_map_w;
    uint  sdf_map_h;
}

StructuredBuffer<float> SdfBuf : register(t0, space2);

struct VS_OUT {
    float4 pos    : SV_POSITION;
    float2 source : TEXCOORD0; // source center in screen pixels
    float2 params : TEXCOORD1; // x = radius_px, y = life
};

// ---- SDF helpers (no static on functions — matches sprite.frag.hlsl convention) ----

float sdf_texel(int x, int y)
{
    const int gw = (int)(sdf_map_w * SDF_SS);
    const int gh = (int)(sdf_map_h * SDF_SS);
    x = clamp(x, 0, gw - 1);
    y = clamp(y, 0, gh - 1);
    return SdfBuf[x * gh + y];
}

float sdf_sample(float2 p)
{
    const float2 g = p * (float)SDF_SS - 0.5;
    const int x0 = (int)g.x;
    const int y0 = (int)g.y;
    const float2 w = g - float2(x0, y0);
    const float a = sdf_texel(x0,     y0    );
    const float b = sdf_texel(x0 + 1, y0    );
    const float c = sdf_texel(x0,     y0 + 1);
    const float d = sdf_texel(x0 + 1, y0 + 1);
    return lerp(lerp(a, b, w.x), lerp(c, d, w.x), w.y);
}

float2 screen_to_tile(float2 screen_px)
{
    return (screen_px - float2(op_x, op_y)) * tile_px_inv - float2(camera_off_x, camera_off_y);
}

// Ray-marched line-of-sight test from the pulse source to a target tile,
// respecting wall geometry via the SDF. True sphere tracing — each step
// advances by the SDF's own safe distance, so it converges in a handful of
// steps through open rooms and only slows down (and stays precise) right
// next to walls. A Quilez-style soft penumbra keeps the shadow boundary
// from being a hard binary cutoff — this is what makes the wavefront feel
// bound by room geometry (wraps around corners, stops at walls) instead of
// an overlay drawn on top of it.
static const int   SND_VIS_STEPS    = 32;
static const float SND_VIS_WALL     = 0.08; // SDF distance treated as "blocked"
static const float SND_VIS_K        = 6.0;  // penumbra softness (higher = sharper)
static const float SND_VIS_MIN_STEP = 0.05; // floor so marching always progresses

float sdf_visibility(float2 from_tile, float2 to_tile)
{
    const float2 delta = to_tile - from_tile;
    const float  total  = length(delta);
    if (total < 0.05) return 1.0;
    const float2 dir = delta / total;

    float t   = 0.1; // skip the source's own cell
    float vis = 1.0;
    for (int s = 0; s < SND_VIS_STEPS && t < total; ++s) {
        const float wd = sdf_sample(from_tile + dir * t);
        if (wd < SND_VIS_WALL) {
            return 0.0;
        }
        vis = min(vis, saturate(SND_VIS_K * wd / t));
        t  += max(wd, SND_VIS_MIN_STEP);
    }
    return vis;
}

// ---- Main ----

float4 main(VS_OUT inp) : SV_Target
{
    const float r    = inp.params.x;
    const float life = inp.params.y;

    // SV_POSITION.xy is in physical framebuffer pixels, but source/radius are
    // in logical pixels. Convert to logical before any distance calculation.
    const float pr          = pixel_ratio > 0.0 ? pixel_ratio : 1.0;
    const float2 pos_logical = inp.pos.xy / pr;

    const float d = distance(inp.source, pos_logical);

    // Outside disc (with small AA fringe): discard.
    if (r < 1.0 || d > r * 1.03) discard;

    // Ring at wavefront: narrow bright band at the outer edge.
    const float ring_width = max(r * 0.04, 3.0);
    const float to_edge    = r - d;
    const float ring_t     = 1.0 - smoothstep(0.0, ring_width, to_edge);

    // Wake fill: gradient behind the ring, dims toward the source.
    const float norm   = d / r;
    const float behind = smoothstep(0.0, ring_width, to_edge);
    const float wake_t = norm * behind * 0.4;

    // Ray-marched occlusion: is this fragment visible from the pulse source?
    // Blocks the disc at walls and softens the shadow edge so the wavefront
    // wraps around corners instead of drawing straight through geometry.
    float diffraction_glow = 0.0;
    float sdf_occ          = 1.0;
    float vis              = 1.0;
    if (sdf_map_w > 0 && sdf_map_h > 0 && tile_px_inv > 0.0) {
        const float2 tile_pos  = screen_to_tile(pos_logical);
        const float  wall_dist = sdf_sample(tile_pos);
        const float  near_wall = 0.55;
        if (wall_dist < near_wall) {
            const float wf   = 1.0 - smoothstep(0.0, near_wall, wall_dist);
            diffraction_glow = wf * ring_t * 0.5;
            sdf_occ          = smoothstep(-0.25, 0.25, wall_dist);
        }
        vis = sdf_visibility(screen_to_tile(inp.source), tile_pos);
    }

    // Combine. `vis` gates everything on line-of-sight from the source;
    // `sdf_occ` additionally darkens fragments that are themselves inside
    // or touching wall geometry.
    const float base      = max(ring_t * 0.9, wake_t);
    const float occlusion = sdf_occ * vis;
    const float intensity = base * occlusion;
    const float alpha     = intensity * life;

    if (alpha < 0.004) discard;

    // Color: bright cyan ring → teal/blue wake.
    const float3 ring_col = float3(0.30, 0.90, 1.00);
    const float3 wake_col = float3(0.04, 0.22, 0.55);
    const float3 col      = lerp(wake_col, ring_col, ring_t);

    // Premultiplied alpha: bake alpha into RGB.
    // Diffraction glow is additive in RGB only — never inflate alpha
    // (alpha > 1.0 would reverse premultiplied compositing).
    const float3 diff_col  = float3(0.5, 1.0, 1.0);
    const float3 final_rgb = col * alpha + diff_col * (diffraction_glow * life * vis);

    return float4(final_rgb, alpha);
}

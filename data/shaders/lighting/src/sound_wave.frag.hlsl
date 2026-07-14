// Sound wave visualization fragment shader — expanding ring with colored wake.
//
// Renders a single disc per sound pulse. Each fragment computes its distance
// from the pulse source in screen pixels:
//   - A bright ring marks the wavefront at the outer edge.
//   - A colored wake fills the interior, fading toward the source.
//   - SDF diffraction modulates intensity near walls (bright rim + soft edge).
//
// Output is premultiplied alpha (pipeline: SRC=ONE, DST=ONE_MINUS_SRC_ALPHA).
// Diffraction glow is purely additive in RGB (does not inflate alpha).
//
// Binding convention:
//   t0/space2 — SdfBuf (StructuredBuffer<float>) — optional
//   b0/space3 — SndFragParams (camera + SDF transform)

#include "jfa_shared.hlsl" /// SDF_SS, SDF_FLOOD

/// Fragment cbuffer — mirrors snd_frag_params exactly.
cbuffer SndFragParams : register(b0, space3) {
    float camera_off_x;
    float camera_off_y;
    float op_x;
    float op_y;
    float tile_px_inv;
    float pad0;
    uint  sdf_map_w;
    uint  sdf_map_h;
}

/// SDF storage buffer — optional wall-distance field.
StructuredBuffer<float> SdfBuf : register(t0, space2);

struct VS_OUT {
    float4 pos    : SV_POSITION;
    float2 source : TEXCOORD0; // source center in screen pixels
    float2 params : TEXCOORD1; // x = radius_px, y = life
};

// ---- SDF helpers ----

static float sdf_texel(int x, int y)
{
    const int gw = (int)(sdf_map_w * SDF_SS);
    const int gh = (int)(sdf_map_h * SDF_SS);
    x = clamp(x, 0, gw - 1);
    y = clamp(y, 0, gh - 1);
    return SdfBuf[x * gh + y];
}

static float sdf_sample(float2 p)
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

static float2 screen_to_tile(float2 screen_px)
{
    return (screen_px - float2(op_x, op_y)) * tile_px_inv - float2(camera_off_x, camera_off_y);
}

// ---- Main ----

float4 main(VS_OUT inp) : SV_Target
{
    const float d    = distance(inp.source, inp.pos.xy);
    const float r    = inp.params.x;
    const float life = inp.params.y;

    // Outside disc (with AA fringe): discard.
    if (r < 1.0 || d > r * 1.03) discard;

    // ── Ring at wavefront ──
    // Narrow bright band at the outer edge of the disc.
    // ring_width scales with radius (~4% of radius, minimum 3px for small pulses).
    const float ring_width = max(r * 0.04, 3.0);
    const float to_edge    = r - d;                        // positive inside disc
    const float ring_t     = 1.0 - smoothstep(0.0, ring_width, to_edge);

    // ── Wake fill (interior) ──
    // Gradient: dim near source, brightening toward wavefront, zero inside ring zone.
    const float norm   = d / r;                            // 0 = source, 1 = edge
    const float behind = smoothstep(0.0, ring_width, to_edge); // 0 in ring, 1 deeper in
    const float wake_t = norm * behind * 0.4;              // max 40% intensity at far wake

    // ── SDF diffraction + wall softening ──
    float diffraction_glow = 0.0;
    float sdf_occ          = 1.0;
    if (sdf_map_w > 0 && sdf_map_h > 0 && tile_px_inv > 0) {
        const float2 tile_pos  = screen_to_tile(inp.pos.xy);
        const float  wall_dist = sdf_sample(tile_pos);

        const float near_wall = 0.55;
        if (wall_dist < near_wall) {
            // Bright cyan rim where the ring meets walls.
            const float wf   = 1.0 - smoothstep(0.0, near_wall, wall_dist);
            diffraction_glow = wf * ring_t * 0.5;
            // Soft fade as the disc penetrates wall interiors.
            sdf_occ          = smoothstep(-0.25, 0.25, wall_dist);
        }
    }

    // ── Combine ──
    const float base      = max(ring_t * 0.9, wake_t);
    const float intensity = base * sdf_occ;
    const float alpha     = intensity * life;

    if (alpha < 0.004) discard;

    // Color: bright cyan at ring → teal/blue wake.
    const float3 ring_col = float3(0.30, 0.90, 1.00);
    const float3 wake_col = float3(0.04, 0.22, 0.55);
    const float3 col      = lerp(wake_col, ring_col, ring_t);

    // Premultiplied alpha: bake alpha into RGB.
    const float3 premul_rgb = col * alpha;

    // Diffraction glow is purely additive in RGB; does NOT inflate alpha to avoid
    // negative compositing artifacts with premultiplied-alpha blending.
    const float3 diff_col   = float3(0.5, 1.0, 1.0);
    const float3 final_rgb  = premul_rgb + diff_col * (diffraction_glow * life);

    return float4(final_rgb, alpha);
}

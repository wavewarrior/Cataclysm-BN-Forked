// Radiance bounce gather — Step-3 Phase 3 (single-bounce GI).
//
// Pass 2 of the radiance pass. Each fragment is a probe/tile. It marches N rays
// out from the probe through the per-tile radiance FIELD (pass 1's direct-light
// gather), accumulating lit-surface radiance reachable before a wall. A probe
// sitting in shadow next to a lit wall has rays that reach that wall and gather
// its colour → real coloured bounce into shadow / around corners, which the
// direct gather (Phase 2) cannot produce.
//
// Resources (space2). 1 storage texture + 1 storage buffer, no sampler ⇒ the
// storage texture is t0 and the storage buffer follows at t1:
//   t0  RadianceField — Texture2D<float4> GRAPHICS_STORAGE_READ (pass-1 output)
//   t1  SdfBuf        — StructuredBuffer<float>, x-major sdf[x*map_h+y]
// (reflection must show storage_textures=1 storage_buffers=1.)

Texture2D<float4>       RadianceField : register(t0, space2);
StructuredBuffer<float> SdfBuf        : register(t1, space2);

cbuffer RcParams : register(b0, space3) {
    uint  emitter_count; // unused here (kept: shared rc_params push)
    uint  sdf_map_w;
    uint  sdf_map_h;
    float current_z;
    float shadow_k;
    uint  shadow_steps;
    float rc_pad0;
    float rc_pad1;
};

struct VS_OUT {
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

// Gather tuning (constants first; promote to rc_params knobs later if useful).
static const int   RC_DIRS      = 16;   // rays per probe
static const int   RC_STEPS     = 20;   // max march steps per ray
static const float RC_STEP      = 0.75; // tile units per step
static const float RC_START     = 0.70; // skip the probe's own tile
static const float RC_WALL      = 0.10; // SDF distance treated as "blocked"

// The field is stored transposed (texel(col=y,row=x)=tile(x,y)) — same as the
// cascade — so tile (x,y) is Load(int3(y, x, 0)).
float3 field_at(int x, int y) {
    x = clamp(x, 0, (int)sdf_map_w - 1);
    y = clamp(y, 0, (int)sdf_map_h - 1);
    return RadianceField.Load(int3(y, x, 0)).rgb;
}

float sdf_texel(int x, int y) {
    x = clamp(x, 0, (int)sdf_map_w - 1);
    y = clamp(y, 0, (int)sdf_map_h - 1);
    return SdfBuf[x * (int)sdf_map_h + y];
}
float sdf_bilinear(float2 p) {
    const float2 sp = p - 0.5;
    const float2 fp = floor(sp);
    const int   x0  = (int)fp.x;
    const int   y0  = (int)fp.y;
    const float2 w  = sp - fp;
    const float a = sdf_texel(x0,     y0    );
    const float b = sdf_texel(x0 + 1, y0    );
    const float c = sdf_texel(x0,     y0 + 1);
    const float d = sdf_texel(x0 + 1, y0 + 1);
    return lerp(lerp(a, b, w.x), lerp(c, d, w.x), w.y);
}

float4 main(VS_OUT i) : SV_Target0 {
    // Transposed: fragment (px,py) is the probe for tile (x=py, y=px).
    const int tileX = (int)floor(i.pos.y);
    const int tileY = (int)floor(i.pos.x);
    if(tileX >= (int)sdf_map_w || tileY >= (int)sdf_map_h) {
        return float4(0.0, 0.0, 0.0, 1.0);
    }
    const float2 probe = float2((float)tileX + 0.5, (float)tileY + 0.5);

    float3 acc = float3(0.0, 0.0, 0.0);
    [loop] for(int d = 0; d < RC_DIRS; ++d) {
        const float  ang = 6.2831853 * ((float)d + 0.5) / (float)RC_DIRS;
        const float2 dir = float2(cos(ang), sin(ang));
        float t = RC_START;
        [loop] for(int s = 0; s < RC_STEPS; ++s) {
            const float2 pos = probe + dir * t;
            // Distance-weighted radiance sampled from the field along the ray.
            acc += field_at((int)floor(pos.x), (int)floor(pos.y)) / (1.0 + t);
            if(sdf_bilinear(pos) < RC_WALL) {
                break; // wall: sample it (the lit surface) then stop the ray
            }
            t += RC_STEP;
        }
    }
    // Average over directions. Magnitude is scaled by gi_strength in the sprite.
    return float4(acc / (float)RC_DIRS, 1.0);
}

// Splatmap stamp vertex shader — procedural quad renderer for decal stamps.
//
// Draws ONE rotated/scaled quad per decal stamp into a per-submap splatmap
// texture (SPLAT_TEX_DIM²), in SUBMAP-LOCAL splatmap pixels. The instance
// struct is byte-identical to rain_droplet.vert.hlsl's SpriteInstance so the
// C++ side reuses the same 64-byte quad_instance layout:
//   dst_*     — quad rect in splatmap px
//   src_*     — atlas cell UV rect (unused by the rain shaders, live here)
//   tint_rgb  — target channel mask (1,0,0 = blood, 0,1,0 = wet, 0,0,1 = snow)
//   tint_a    — coverage alpha multiplier
//   rotation  — radians, about the quad centre
//
// target_size is (SPLAT_TEX_DIM, SPLAT_TEX_DIM) — the splatmap, NOT the screen.

struct SpriteInstance {
    float dst_x, dst_y, dst_w, dst_h;
    float src_u, src_v, src_uw, src_vh; // atlas cell UV rect
    float tint_r, tint_g, tint_b, tint_a;
    float rotation, light_mul, pad1, pad2;
};

StructuredBuffer<SpriteInstance> Instances : register(t0, space0);

cbuffer FrameParams : register(b0, space1) {
    float2 target_size;
    uint   instance_base;
    uint   fp_pad;
};

struct VS_OUT {
    float4 pos  : SV_Position;
    float2 uv   : TEXCOORD0; // atlas UV (into the stamp atlas)
    float4 tint : TEXCOORD1; // rgb = channel mask, a = coverage
};

static const float2 quad_uv[6] = {
    float2(0.0,0.0), float2(1.0,0.0), float2(0.0,1.0),
    float2(1.0,0.0), float2(1.0,1.0), float2(0.0,1.0)
};

VS_OUT main(uint vid : SV_VertexID, uint iid : SV_InstanceID) {
    const SpriteInstance s = Instances[iid + instance_base];
    const float2 c = quad_uv[vid];

    // Rotate the quad around its centre (same math as sprite.vert).
    const float2 centre = float2(s.dst_x + 0.5 * s.dst_w,
                                 s.dst_y + 0.5 * s.dst_h);
    const float2 off    = float2((c.x - 0.5) * s.dst_w,
                                 (c.y - 0.5) * s.dst_h);
    const float  cs     = cos(s.rotation);
    const float  sn     = sin(s.rotation);
    const float2 pixel  = centre + float2(off.x * cs - off.y * sn,
                                          off.x * sn + off.y * cs);

    // NDC conversion (Y-flipped for texture coords) — copied from sprite.vert.
    const float2 ndc = float2(
        pixel.x / target_size.x *  2.0 - 1.0,
        pixel.y / target_size.y * -2.0 + 1.0);

    VS_OUT o;
    o.pos  = float4(ndc, 0.0, 1.0);
    o.uv   = float2(s.src_u + c.x * s.src_uw,
                    s.src_v + c.y * s.src_vh);
    o.tint = float4(s.tint_r, s.tint_g, s.tint_b, s.tint_a);
    return o;
}

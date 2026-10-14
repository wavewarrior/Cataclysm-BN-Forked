// Decorative emitter glow vertex shader — one expanding quad per light source.
//
// "Smoke and mirrors": this does NOT read any lighting buffer. Each instance
// is one visible emitter (fire, torch, lamp, ...) already resolved to screen
// pixels + a tint colour on the CPU (sdl_render_frame.cpp). The vertex shader
// just expands a unit quad to cover the glow radius; the fragment shader
// paints a soft radial falloff tinted to the instance colour, additively.
//
// Instance struct uses PLAIN SCALAR floats, not float2/float3 — HLSL pads a
// vector to the next 16-byte boundary if it would otherwise straddle one
// (same rule as cbuffers), silently shifting every field after it relative
// to the tightly-packed C++ struct (emitter_glow_pass.h). Scalars have no
// such rule, so this is the only layout that is guaranteed to match.
//
// Binding convention:
//   t0/space0 — vertex storage buffer (instances)
//   b0/space1 — vertex uniform (projection dims)

struct EmitterGlowInstance {
    float x;         // screen-space centre X, pixels
    float y;         // screen-space centre Y, pixels
    float radius_px; // glow radius, pixels
    float r;         // tint (0..1 linear)
    float g;
    float b;
    float strength;  // opacity/intensity multiplier
    float pad0;
};

StructuredBuffer<EmitterGlowInstance> Instances : register(t0, space0);

cbuffer EmitterGlowParams : register(b0, space1) {
    float proj_w;
    float proj_h;
    float pad0;
    float pad1;
};

struct VS_OUT {
    float4 pos      : SV_POSITION;
    float2 uv       : TEXCOORD0; // -1..1 local quad coords (radial distance space)
    float3 color    : TEXCOORD1;
    float  strength : TEXCOORD2;
};

static const float2 quad_verts[6] = {
    float2(-1.0, -1.0), float2( 1.0, -1.0), float2( 1.0,  1.0),
    float2(-1.0, -1.0), float2( 1.0,  1.0), float2(-1.0,  1.0)
};

VS_OUT main(uint vid : SV_VertexID, uint iid : SV_InstanceID) {
    const EmitterGlowInstance inst = Instances[iid];
    const float2 center = float2(inst.x, inst.y);
    const float2 local = quad_verts[vid];

    const float2 screen_pos = center + local * inst.radius_px;
    // Pixel -> NDC (flip Y: screen +Y down -> NDC +Y up).
    const float2 ndc = float2(
        screen_pos.x / proj_w * 2.0 - 1.0,
        screen_pos.y / proj_h * -2.0 + 1.0);

    VS_OUT o;
    o.pos      = float4(ndc, 0.0, 1.0);
    o.uv       = local;
    o.color    = float3(inst.r, inst.g, inst.b);
    o.strength = inst.strength;
    return o;
}

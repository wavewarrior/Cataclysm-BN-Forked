// Dust mote vertex shader — one small soft-dot quad per particle (Step 6c,
// atmospheric-lighting-coherence plan). Mirrors emitter_glow.vert.hlsl /
// godray_shaft.vert.hlsl's pattern: no lighting buffer reads, just expands a
// unit quad around a screen-space centre already resolved on the CPU
// (dust_mote_effect.cpp).
//
// Instance struct uses PLAIN SCALAR floats — see emitter_glow_instance's
// comment for why (HLSL silently pads a vector to the next 16-byte boundary).
//
// Binding convention:
//   t0/space0 — vertex storage buffer (instances)
//   b0/space1 — vertex uniform (projection dims)

struct DustMoteInstance {
    float cx;        // centre X, screen pixels
    float cy;        // centre Y, screen pixels
    float radius_px; // dot radius, screen pixels
    float r;         // premultiplied colour (already * alpha on the CPU)
    float g;
    float b;
    float alpha;
    float pad0;
};

StructuredBuffer<DustMoteInstance> Instances : register(t0, space0);

cbuffer DustMoteParams : register(b0, space1) {
    float proj_w;
    float proj_h;
    float pad0;
    float pad1;
};

struct VS_OUT {
    float4 pos   : SV_POSITION;
    float2 uv    : TEXCOORD0; // -1..1 local quad coords (radial distance space)
    float3 color : TEXCOORD1;
    float  alpha : TEXCOORD2;
};

static const float2 quad_verts[6] = {
    float2(-1.0, -1.0), float2( 1.0, -1.0), float2( 1.0,  1.0),
    float2(-1.0, -1.0), float2( 1.0,  1.0), float2(-1.0,  1.0)
};

VS_OUT main(uint vid : SV_VertexID, uint iid : SV_InstanceID) {
    const DustMoteInstance inst = Instances[iid];
    const float2 center = float2(inst.cx, inst.cy);
    const float2 local = quad_verts[vid];

    const float2 screen_pos = center + local * inst.radius_px;
    // Pixel -> NDC (flip Y: screen +Y down -> NDC +Y up).
    const float2 ndc = float2(
        screen_pos.x / proj_w * 2.0 - 1.0,
        screen_pos.y / proj_h * -2.0 + 1.0);

    VS_OUT o;
    o.pos   = float4(ndc, 0.0, 1.0);
    o.uv    = local;
    o.color = float3(inst.r, inst.g, inst.b);
    o.alpha = inst.alpha;
    return o;
}

// Sound wave visualization vertex shader — per-pulse single expanding disc.
//
// Each instance is one sound pulse. The vertex shader expands a unit quad to
// cover the full wavefront disc (centered at the source, sized to radius_px).
// The fragment shader computes per-pixel distance from source and renders a
// bright ring at the wavefront edge with a colored wake filling the interior.
//
// Binding convention:
//   t0/space0 — vertex storage buffer (instances)
//   b0/space1 — vertex uniform (projection dims)

struct SoundWavePulseInstance {
    float2 source_screen; // source center in screen pixels
    float  radius_px;     // current wavefront radius in pixels
    float  life;          // 0..1 fade
};

StructuredBuffer<SoundWavePulseInstance> Instances : register(t0, space0);

cbuffer SoundWaveParams : register(b0, space1) {
    float proj_w;
    float proj_h;
    float pad0;
    float pad1;
};

struct VS_OUT {
    float4 pos       : SV_POSITION;
    float2 source    : TEXCOORD0; // source center in screen pixels
    float  radius_px : TEXCOORD1; // wavefront radius in pixels
    float  life      : TEXCOORD2; // fade
};

// Unit quad (6 verts = 2 triangles), centered at origin, ±1.
static const float2 quad_verts[6] = {
    float2(-1.0, -1.0), float2( 1.0, -1.0), float2( 1.0,  1.0),
    float2(-1.0, -1.0), float2( 1.0,  1.0), float2(-1.0,  1.0)
};

VS_OUT main(uint vid : SV_VertexID, uint iid : SV_InstanceID) {
    const SoundWavePulseInstance inst = Instances[iid];

    // Expand quad to cover the full disc + small AA margin.
    const float2 screen_pos = inst.source_screen + quad_verts[vid] * (inst.radius_px * 1.05);

    // Pixel → NDC (flip Y: screen +Y down → NDC +Y up).
    const float2 ndc = float2(
        screen_pos.x / proj_w * 2.0 - 1.0,
        screen_pos.y / proj_h * -2.0 + 1.0);

    VS_OUT o;
    o.pos       = float4(ndc, 0.0, 1.0);
    o.source    = inst.source_screen;
    o.radius_px = inst.radius_px;
    o.life      = inst.life;
    return o;
}

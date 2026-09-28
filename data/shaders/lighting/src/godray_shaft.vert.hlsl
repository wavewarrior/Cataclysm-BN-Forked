// God-ray window light-shaft vertex shader — one oriented beam quad per
// visible window (Step 6b, atmospheric-lighting-coherence plan).
//
// "Smoke and mirrors", same trick as emitter_glow.vert.hlsl: this does NOT
// read any lighting buffer. Each instance is one visible window, already
// resolved to a screen-space origin + beam direction + length/width on the
// CPU (sdl_render_frame.cpp, filtered from the per-window CONE emitters
// snapshot.cpp already builds). The vertex shader stretches a unit quad along
// the beam axis; the fragment shader paints a soft gradient (bright at the
// window, fading toward the far end), additively.
//
// Instance struct uses PLAIN SCALAR floats, not float2/float3 — HLSL pads a
// vector to the next 16-byte boundary if it would otherwise straddle one
// (same rule as cbuffers), silently shifting every field after it relative to
// the tightly-packed C++ struct (godray_shaft_pass.h). Scalars have no such
// rule, so this is the only layout guaranteed to match.
//
// Binding convention (mirrors emitter_glow.vert.hlsl):
//   t0/space0 — vertex storage buffer (instances)
//   b0/space1 — vertex uniform (projection dims + anim_time)

struct GodrayShaftInstance {
    float cx;            // beam origin (window centre) X, screen pixels
    float cy;            // beam origin (window centre) Y, screen pixels
    float dir_x;          // unit beam axis X (light travel direction, into the room)
    float dir_y;          // unit beam axis Y
    float length_px;      // beam length, screen pixels
    float half_width_px;  // beam half-width at the base, screen pixels
    float r;              // tint (0..1 linear)
    float g;
    float b;
    float strength;       // opacity/intensity multiplier
};

StructuredBuffer<GodrayShaftInstance> Instances : register(t0, space0);

cbuffer GodrayShaftParams : register(b0, space1) {
    float proj_w;
    float proj_h;
    float anim_time; // wrapped render seconds; faint beam shimmer (frag-side)
    float pad1;
};

struct VS_OUT {
    float4 pos      : SV_POSITION;
    // Beam-space uv: x = across-width (-1..1), y = along-length (0=window, 1=far end).
    float2 uv       : TEXCOORD0;
    float3 color    : TEXCOORD1;
    float  strength : TEXCOORD2;
    float  anim     : TEXCOORD3;
};

static const float2 quad_verts[6] = {
    float2(-1.0, 0.0), float2( 1.0, 0.0), float2( 1.0, 1.0),
    float2(-1.0, 0.0), float2( 1.0, 1.0), float2(-1.0, 1.0)
};

VS_OUT main(uint vid : SV_VertexID, uint iid : SV_InstanceID) {
    const GodrayShaftInstance inst = Instances[iid];
    const float2 origin = float2(inst.cx, inst.cy);
    const float2 dir = float2(inst.dir_x, inst.dir_y);
    const float2 perp = float2(-dir.y, dir.x); // rotate 90 degrees
    const float2 uv = quad_verts[vid];

    const float2 screen_pos =
        origin + dir * (uv.y * inst.length_px) + perp * (uv.x * inst.half_width_px);
    // Pixel -> NDC (flip Y: screen +Y down -> NDC +Y up).
    const float2 ndc = float2(
        screen_pos.x / proj_w * 2.0 - 1.0,
        screen_pos.y / proj_h * -2.0 + 1.0);

    VS_OUT o;
    o.pos      = float4(ndc, 0.0, 1.0);
    o.uv       = uv;
    o.color    = float3(inst.r, inst.g, inst.b);
    o.strength = inst.strength;
    o.anim     = anim_time;
    return o;
}

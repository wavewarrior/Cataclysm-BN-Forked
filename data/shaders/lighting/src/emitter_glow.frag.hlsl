// Decorative emitter glow fragment shader — a soft radial mark whose visible
// gradient spans the emitter's own light radius (passed in as the instance
// radius_px by the CPU builder in sdl_render_frame.cpp).
//
// The core sits just above bloom's extract threshold, so the EXISTING bloom
// pass (bloom_extract.frag.hlsl's above-threshold extraction -> Kawase blur ->
// bloom_composite.frag.hlsl's additive add-back) adds a gentle halo on top.
// Deliberately restrained: an earlier version drew a tiny, massively
// overdriven core decoupled from the emitter radius, which read as a blown-out
// white dot detached from the light it was supposed to mark.
//
// Output alpha is always 0 so the ONE/ONE additive blend leaves the target's
// own alpha untouched (same convention as bloom_composite.frag.hlsl).

struct VS_OUT {
    float4 pos      : SV_POSITION;
    float2 uv       : TEXCOORD0;
    float3 color    : TEXCOORD1;
    float  strength : TEXCOORD2;
};

float4 main(VS_OUT i) : SV_Target0 {
    const float d = length(i.uv); // 0 at centre, 1 at the emitter's light radius
    // Quadratic falloff with a smoothed rim: the visible gradient extends to
    // (almost) the full quad, so the glow's apparent size tracks the emitter's
    // real light radius. The old cubic curve concentrated nearly all energy in
    // the inner half, which read as a small dot floating inside a much bigger
    // light — the disconnect users saw between blob size and light size.
    const float falloff = saturate(1.0 - d);
    const float rim = smoothstep(0.0, 0.15, falloff);
    const float glow = falloff * falloff * rim * i.strength;
    return float4(i.color * glow, 0.0);
}

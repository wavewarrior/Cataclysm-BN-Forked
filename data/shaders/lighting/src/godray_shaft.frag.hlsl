// God-ray window light-shaft fragment shader — procedural gradient beam
// (Step 6b, atmospheric-lighting-coherence plan).
//
// No texture asset exists for this (and none is needed): a beam is exactly a
// far-end fade (bright at the window, fading to nothing at the beam's reach)
// crossed with a soft falloff across its width, plus a faint animated ripple
// so it doesn't read as a flat static wedge. Bloom (recorded AFTER this pass,
// see sdl_render_frame.cpp) supplies the soft outer halo — same "small hot
// core, let bloom do the spreading" approach as emitter_glow.frag.hlsl.
//
// Output alpha is always 0 so the ONE/ONE additive blend leaves the target's
// own alpha untouched (same convention as bloom_composite.frag.hlsl /
// emitter_glow.frag.hlsl).

struct VS_OUT {
    float4 pos      : SV_POSITION;
    float2 uv       : TEXCOORD0; // x: across-width (-1..1), y: along-length (0..1)
    float3 color    : TEXCOORD1;
    float  strength : TEXCOORD2;
    float  anim     : TEXCOORD3;
};

float4 main(VS_OUT i) : SV_Target0 {
    // Far-end fade: bright at the window (uv.y=0), gone by the beam's reach.
    const float len_fade = 1.0 - saturate(i.uv.y);
    // Soft falloff across the width, squared for a tighter, more beam-like core.
    const float width_fade = saturate(1.0 - abs(i.uv.x));
    const float width_fade2 = width_fade * width_fade;
    // Faint shimmer: a slow sine keyed on distance-along-beam + time, so the
    // beam reads as dusty/alive rather than a flat static wedge. Small
    // amplitude (+-6%) — this is flavour, not the primary falloff.
    const float ripple = 1.0 + 0.06 * sin(i.uv.y * 9.0 - i.anim * 1.3);
    const float falloff = len_fade * width_fade2 * ripple;
    return float4(i.color * falloff * i.strength, 0.0);
}

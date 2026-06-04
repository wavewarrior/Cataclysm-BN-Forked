// ---- Silhouette sun-shadow fragment shader (Phase 1) ----
// Samples the caster's atlas alpha and writes it as coverage into the
// screen-space shadow mask. The sheared quad (shadow.vert) places the
// silhouette where the shadow falls; this just turns sprite alpha into a dark
// coverage value.
//
// Resource interface is DELIBERATELY minimal: one sampler texture (the atlas),
// ZERO cbuffers, ZERO storage buffers/textures. The shared sprite_batcher
// end_pass would normally push 3 fragment uniform slots (LightParams/SunParams/
// DebugParams) and bind the lighting storage buffers — but the shadow batcher
// sets pipeline_desc.push_frag_lighting_uniforms=false (skips the 3 frag
// pushes) and is stamped with null lighting buffers (bind_lighting_resources
// no-ops). So this shader's reflection (samplers=1, storage=0, uniforms=0)
// must match what reaches the GPU. Confirm via the frag reflection log at init.
//
// Output: float4(cov, cov, cov, 1.0). Alpha is forced to 1 so the mask blits
// OPAQUE for the Phase-1 debug kill-gate (clean grey silhouettes on black) and
// MAX-blend on the alpha channel keeps it 1. Phase 2 reads .r as coverage.

Texture2D<float4> Atlas    : register(t0, space2);
SamplerState      AtlasSmp : register(s0, space2);

struct VS_OUT {
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

float4 main(VS_OUT i) : SV_Target0 {
    // Nearest sampler (set in render_state::init) → no atlas-neighbour bleed
    // even though uv sits inside the packed page's src rect.
    const float cov = Atlas.Sample(AtlasSmp, i.uv).a;
    return float4(cov, cov, cov, 1.0);
}

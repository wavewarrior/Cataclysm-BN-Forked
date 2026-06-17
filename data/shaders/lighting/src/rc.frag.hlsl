// SHADER_BUILD_TAG: rc_field_tag_i
// DIAGNOSTIC: real field gather but with the SHADOW MARCH removed entirely — no
// sdf helpers, no trace_shadow, so main's [loop] no longer contains a nested
// [loop]. Keeps pow() + the dynamic emitter loop. Isolates nested-dynamic-loops
// (trace_shadow's [loop] inside main's [loop]) as the D3D12 pipeline trigger:
//   builds → nested loops are the trigger (and this is a usable unshadowed-GI
//            fallback); pow + main loop are fine.
//   fails  → pow() or the bare dynamic loop is the trigger.
// Real shader saved at rc.frag.hlsl.real.bak.

StructuredBuffer<float4> Emitters : register(t0, space2);
StructuredBuffer<float>  SdfBuf   : register(t1, space2);

static const float LIGHT_EPS = 0.004;

cbuffer RcParams : register(b0, space3) {
    uint  emitter_count;
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

float4 main(VS_OUT i) : SV_Target0 {
    const int tileX = (int)floor(i.pos.y);
    const int tileY = (int)floor(i.pos.x);
    if(tileX >= (int)sdf_map_w || tileY >= (int)sdf_map_h) {
        return float4(0.0, 0.0, 0.0, 1.0);
    }
    const float2 probe = float2((float)tileX + 0.5, (float)tileY + 0.5);

    float3 gi = float3(0.0, 0.0, 0.0);
    const uint me = min(emitter_count, 8192u);

    // SdfBuf kept bound (touch it once so reflection still shows sb=2) but NOT
    // marched — no nested loop.
    gi += SdfBuf[0] * 0.0;

    [loop] for(uint ei = 0u; ei < me; ++ei) {
        const float4 e_pr      = Emitters[ei * 4u + 0u];
        const float4 e_cf      = Emitters[ei * 4u + 1u];
        const float3 e_pos     = e_pr.xyz;
        const float  e_radius  = e_pr.w;
        const float3 e_color   = e_cf.xyz;
        const float  e_falloff = e_cf.w;
        if(abs(e_pos.z - current_z) > 0.5) continue;
        const float2 dv   = e_pos.xy - probe;
        const float  dist = length(dv);
        if(dist >= e_radius || dist < 0.01) continue;
        const float  atten = 1.0 - pow(saturate(dist / e_radius), e_falloff);
        if(atten <= LIGHT_EPS) continue;
        const float3 rgb = (e_color.x < 0.01 && e_color.y < 0.01 && e_color.z < 0.01)
                           ? float3(1.0, 1.0, 1.0) : e_color;
        gi += rgb * atten; // unshadowed only (no trace_shadow)
    }
    return float4(gi, 1.0);
}

// SHADER_BUILD_TAG: rc_field_tag_g
// DIAGNOSTIC: tag_f (which BUILT: float4 BufA + scalar BufB + all-float cbuffer,
// minimal file) with ONLY the cbuffer swapped to the real RcParams uint layout.
// Isolates whether the uint-bearing cbuffer is FIELD's D3D12 trigger:
//   fails  → the uint cbuffer (leading uints) is the trigger.
//   builds → cbuffer is fine; tag_d failed because its file still held the dead
//            main_full body — i.e. the real BODY's DXIL is the trigger.
// Real shader saved at rc.frag.hlsl.real.bak.

StructuredBuffer<float4> BufA : register(t0, space2);
StructuredBuffer<float>  BufB : register(t1, space2);

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
    const float a = BufA[0].x;
    const float b = BufB[0];
    const float c = (float)emitter_count + (float)shadow_steps;
    return float4(saturate(a + b + c) * 0.0001, 0.0, 0.0, 1.0);
}

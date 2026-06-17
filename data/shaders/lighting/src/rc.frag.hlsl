// SHADER_BUILD_TAG: rc_field_tag_e
// DIAGNOSTIC: exact vol.frag resource SHAPE (2× StructuredBuffer<float> at
// t0/t1 space2, a float-only 32B cbuffer at b0/space3, sampler-less, trivial
// body). vol.frag with this shape builds on D3D12; rc.frag does not. Running
// THIS through radiance_cascade_pass's pipeline path isolates shader-decls vs
// the C++ make_pipeline call:
//   builds  → rc's real decls (float4 SB / uint cbuffer) are the D3D12 trigger.
//   fails   → the C++ pipeline path (CASCADE_FORMAT / texture / blend) is, and
//             the shader is fully exonerated.
// Real shader saved at rc.frag.hlsl.real.bak — restore after this round.

StructuredBuffer<float> BufA : register(t0, space2);
StructuredBuffer<float> BufB : register(t1, space2);

cbuffer P : register(b0, space3) {
    float p0; float p1; float p2; float p3;
    float p4; float p5; float p6; float p7;
};

struct VS_OUT {
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

float4 main(VS_OUT i) : SV_Target0 {
    const float a = BufA[0];
    const float b = BufB[0];
    return float4(saturate(a + b + p0) * 0.0001, 0.0, 0.0, 1.0);
}

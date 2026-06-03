// Bloom composite — Step-4. Sample the blurred half-res bloom (bilinear upscale
// to full res) and emit it scaled by `intensity`. The pipeline blends ADDITIVELY
// onto the HDR world_target, so this only outputs the glow to add. Alpha 0 keeps
// the target's alpha unchanged under ONE/ONE blending. One sampler.

Texture2D    SrcTex : register( t0, space2 );
SamplerState SrcSmp : register( s0, space2 );

cbuffer BloomComposite : register( b0, space3 ) {
    float bc_intensity;
    float bc_pad0, bc_pad1, bc_pad2;
};

struct VS_OUT {
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

float4 main( VS_OUT i ) : SV_Target0 {
    const float3 b = SrcTex.Sample( SrcSmp, i.uv ).rgb;
    return float4( b * bc_intensity, 0.0 );
}

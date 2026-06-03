// Bloom separable Gaussian blur — Step-4. One axis per pass (dir = texel step
// in UV: (1/w,0) horizontal, then (0,1/h) vertical). 9-tap. One sampler.

Texture2D    SrcTex : register( t0, space2 );
SamplerState SrcSmp : register( s0, space2 );

cbuffer BloomBlur : register( b0, space3 ) {
    float2 bb_dir;   // UV texel step along the blur axis
    float2 bb_pad;
};

struct VS_OUT {
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

float4 main( VS_OUT i ) : SV_Target0 {
    // Normalised 9-tap Gaussian (sigma ~2).
    const float w0 = 0.227027;
    const float w1 = 0.1945946;
    const float w2 = 0.1216216;
    const float w3 = 0.054054;
    const float w4 = 0.016216;
    float3 sum = SrcTex.Sample( SrcSmp, i.uv ).rgb * w0;
    sum += SrcTex.Sample( SrcSmp, i.uv + bb_dir * 1.0 ).rgb * w1;
    sum += SrcTex.Sample( SrcSmp, i.uv - bb_dir * 1.0 ).rgb * w1;
    sum += SrcTex.Sample( SrcSmp, i.uv + bb_dir * 2.0 ).rgb * w2;
    sum += SrcTex.Sample( SrcSmp, i.uv - bb_dir * 2.0 ).rgb * w2;
    sum += SrcTex.Sample( SrcSmp, i.uv + bb_dir * 3.0 ).rgb * w3;
    sum += SrcTex.Sample( SrcSmp, i.uv - bb_dir * 3.0 ).rgb * w3;
    sum += SrcTex.Sample( SrcSmp, i.uv + bb_dir * 4.0 ).rgb * w4;
    sum += SrcTex.Sample( SrcSmp, i.uv - bb_dir * 4.0 ).rgb * w4;
    return float4( sum, 1.0 );
}

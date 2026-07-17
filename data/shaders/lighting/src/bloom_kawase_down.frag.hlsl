// Kawase downfilter — dual-filter bloom downsample. 5-tap box that naturally
// halves resolution: centre weighted 4×, four diagonal half-texel taps 1× each.
// Register layout matches bloom_extract / bloom_composite convention.

Texture2D    SrcTex : register( t0, space2 );
SamplerState SrcSmp : register( s0, space2 );

cbuffer KawaseParams : register( b0, space3 ) {
    float2 texel_size;
    float2 kp_pad;
};

struct VS_OUT {
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

float4 main( VS_OUT i ) : SV_Target0 {
    const float2 hs = texel_size * 0.5;
    float3 s  = SrcTex.Sample( SrcSmp, i.uv ).rgb * 4.0;
    s += SrcTex.Sample( SrcSmp, i.uv + float2( -hs.x, -hs.y ) ).rgb;
    s += SrcTex.Sample( SrcSmp, i.uv + float2(  hs.x, -hs.y ) ).rgb;
    s += SrcTex.Sample( SrcSmp, i.uv + float2( -hs.x,  hs.y ) ).rgb;
    s += SrcTex.Sample( SrcSmp, i.uv + float2(  hs.x,  hs.y ) ).rgb;
    return float4( s / 8.0, 1.0 );
}

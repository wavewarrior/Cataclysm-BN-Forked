// Kawase upfilter — dual-filter bloom upsample. 8-tap tent (no centre): four
// axis-aligned taps at 1× weight, four corner taps at 0.5× weight. Composited
// additively with the previous mip via pipeline blend state.

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
    const float2 ts = texel_size;
    float3 s = float3( 0, 0, 0 );
    // Axis-aligned taps (weight 1)
    s += SrcTex.Sample( SrcSmp, i.uv + float2( -ts.x,  0.0  ) ).rgb;
    s += SrcTex.Sample( SrcSmp, i.uv + float2(  ts.x,  0.0  ) ).rgb;
    s += SrcTex.Sample( SrcSmp, i.uv + float2(  0.0,  -ts.y ) ).rgb;
    s += SrcTex.Sample( SrcSmp, i.uv + float2(  0.0,   ts.y ) ).rgb;
    // Corner taps (weight 0.5 — softer falloff)
    s += SrcTex.Sample( SrcSmp, i.uv + float2( -ts.x, -ts.y ) ).rgb * 0.5;
    s += SrcTex.Sample( SrcSmp, i.uv + float2(  ts.x, -ts.y ) ).rgb * 0.5;
    s += SrcTex.Sample( SrcSmp, i.uv + float2( -ts.x,  ts.y ) ).rgb * 0.5;
    s += SrcTex.Sample( SrcSmp, i.uv + float2(  ts.x,  ts.y ) ).rgb * 0.5;
    return float4( s / 6.0, 1.0 );
}

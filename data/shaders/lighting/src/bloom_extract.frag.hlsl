// Bloom bright-pass — Step-4. Sample the HDR world_target and keep only the
// energy above `threshold` (soft: scaled by how far over), so blurring it gives
// a glow around bright sources. One sampler (SrcTex) — no Metal 2-sampler path.

Texture2D    SrcTex : register( t0, space2 );
SamplerState SrcSmp : register( s0, space2 );

cbuffer BloomExtract : register( b0, space3 ) {
    float bx_threshold;
    float bx_pad0, bx_pad1, bx_pad2;
};

struct VS_OUT {
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

float4 main( VS_OUT i ) : SV_Target0 {
    const float3 c   = SrcTex.Sample( SrcSmp, i.uv ).rgb;
    const float  lum = dot( c, float3( 0.2126, 0.7152, 0.0722 ) );
    const float  over = max( lum - bx_threshold, 0.0 );
    // Preserve hue: scale the colour by the over-threshold fraction of its luma.
    const float3 bright = c * ( over / max( lum, 1e-4 ) );
    return float4( bright, 1.0 );
}

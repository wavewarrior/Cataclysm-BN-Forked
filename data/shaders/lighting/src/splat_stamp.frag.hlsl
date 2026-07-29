// Splatmap stamp fragment shader — writes decal coverage into one channel.
//
// The stamp atlas carries pixel-art decals whose RGB is irrelevant: only the
// ALPHA is read, as a coverage mask. Coverage is multiplied by the per-stamp
// alpha and written into the channels selected by tint.rgb (the channel mask),
// leaving the others at 0.
//
// The stamp pipeline blends with BLENDOP_MAX (src=ONE, dst=ONE), so the zeros
// written to non-target channels leave those channels untouched — one pipeline
// serves all three channels with no colour write masks — and overlapping
// stamps saturate toward 1 (organic buildup) instead of washing out.

struct PS_IN {
    float4 pos  : SV_Position;
    float2 uv   : TEXCOORD0;
    float4 tint : TEXCOORD1;
};

Texture2D<float4> Atlas    : register(t0, space2);
SamplerState      AtlasSmp : register(s0, space2);

float4 main( PS_IN i ) : SV_Target
{
    const float cov = Atlas.Sample( AtlasSmp, i.uv ).a * i.tint.a;
    return float4( i.tint.rgb * cov, cov );
}

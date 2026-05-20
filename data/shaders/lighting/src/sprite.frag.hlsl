// Sprite batcher fragment shader — phase 2c placeholder.
//
// Single textured-quad path. Matches the per-instance tint that emulates the
// legacy SDL_SetTextureColorMod/SetTextureAlphaMod call so the golden-image
// regression in sub-phase 2h passes.

Texture2D<float4> Atlas    : register( t0, space2 );
SamplerState      AtlasSmp : register( s0, space2 );

struct VS_OUT {
    float4 pos  : SV_Position;
    float2 uv   : TEXCOORD0;
    float4 tint : TEXCOORD1;
};

float4 main( VS_OUT i ) : SV_Target0
{
    const float4 texel = Atlas.Sample( AtlasSmp, i.uv );
    return texel * i.tint;
}

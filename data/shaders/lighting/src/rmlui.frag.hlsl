// RmlUi fragment shader (spike). Canonical RmlUi backend: sampled texel times
// the (premultiplied) vertex colour. The render interface binds a 1x1 opaque
// white texture for untextured geometry (TextureHandle 0), so this degenerates
// to the vertex colour for solid fills. Premultiplied-alpha blend state lives in
// the pipeline (ONE, ONE_MINUS_SRC_ALPHA).
//
// Fragment sampler slot 0 -> register(t0, space2) + register(s0, space2)
// (SDL_BindGPUFragmentSamplers). No storage buffers -> sidesteps the shadercross
// storage-buffer reflection bug entirely.
Texture2D    Tex  : register( t0, space2 );
SamplerState Samp : register( s0, space2 );

struct PS_IN {
    float4 pos    : SV_Position;
    float4 colour : TEXCOORD0;
    float2 uv      : TEXCOORD1;
};

float4 main( PS_IN i ) : SV_Target
{
    return Tex.Sample( Samp, i.uv ) * i.colour;
}

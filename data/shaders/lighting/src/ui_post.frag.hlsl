// UI post-processing fragment shader — subtle bloom + chromatic aberration.
//
// Reads the UI composite texture, applies:
//   1. Chromatic aberration: offset R/B channel UVs radially from center.
//   2. Subtle bloom: 5-tap horizontal + 5-tap vertical box blur on bright
//      pixels, composited additively.
//
// Driven by ui_post_pass::record(). Uniforms pushed as fragment cbuffer:
//   UiPostParams { float ca_intensity, float bloom_strength, float pad0, float pad1 }

Texture2D<float4> ui_tex : register( t0, space2 );
SamplerState lin_sampler : register( s0, space2 );

cbuffer UiPostParams : register( b0, space3 ) {
    float ca_intensity;
    float bloom_strength;
    float pad0;
    float pad1;
};

struct VS_OUT {
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

float4 main( VS_OUT i ) : SV_Target
{
    const float2 center = float2( 0.5, 0.5 );
    const float2 dir = i.uv - center;
    const float dist = length( dir );

    // Chromatic aberration: sample R and B at offset UVs, G at center.
    // Offset scales with distance from center (edge distortion) and intensity.
    const float ca_offset = ca_intensity * dist * 0.004; // max ~2px at corners
    const float2 ndir = ( dist > 0.001 ) ? dir / dist : float2( 0.0, 0.0 );
    const float2 uv_r = i.uv + ndir * ca_offset;
    const float2 uv_b = i.uv - ndir * ca_offset;

    const float r = ui_tex.SampleLevel( lin_sampler, uv_r, 0.0 ).r;
    const float g = ui_tex.SampleLevel( lin_sampler, i.uv, 0.0 ).g;
    const float b = ui_tex.SampleLevel( lin_sampler, uv_b, 0.0 ).b;
    const float a = ui_tex.SampleLevel( lin_sampler, i.uv, 0.0 ).a;

    float4 base = float4( r, g, b, a );

    // Subtle bloom: 5-tap box blur on bright pixels only.
    // Threshold: pixels brighter than 0.7 contribute to bloom.
    const float threshold = 0.7;
    const float texel_size = 0.001; // approximate; will be tuned per resolution

    float bloom_r = 0.0, bloom_g = 0.0, bloom_b = 0.0;
    const int taps = 5;
    const float spread = 2.0 * texel_size;

    [unroll]
    for( int dx = -2; dx <= 2; ++dx ) {
        [unroll]
        for( int dy = -2; dy <= 2; ++dy ) {
            const float2 offset_uv = i.uv + float2( dx, dy ) * spread;
            const float4 s = ui_tex.SampleLevel( lin_sampler, offset_uv, 0.0 );
            const float lum = dot( s.rgb, float3( 0.299, 0.587, 0.114 ) );
            if( lum > threshold ) {
                const float excess = lum - threshold;
                bloom_r += s.r * excess;
                bloom_g += s.g * excess;
                bloom_b += s.b * excess;
            }
        }
    }

    // Normalize and scale bloom contribution.
    const float bloom_scale = bloom_strength * 0.15;
    base.rgb += float3( bloom_r, bloom_g, bloom_b ) * bloom_scale;

    return base;
}
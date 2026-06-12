// Rain splat fade + splash accumulation — fullscreen pass.
//
// Reads the current splat texture, applies per-frame fade, accumulates new
// splash contributions from uniform data, then writes to the ping texture.
//
// Resources:
//   t0  SplatIn    — Texture2D (current frame's splat)
//   s0  SampLinear — SamplerState (linear filtering for soft edges)
//   b0  SplatParams — cbuffer with fade rate + splash count
//   b1  SplashData  — cbuffer with per-splash x, y, intensity data

Texture2D    SplatIn : register( t0, space2 );
SamplerState SampLinear : register( s0, space2 );

// Fade multiplier per frame. <1 = wetness decays; >1 = rapid accumulation.
cbuffer SplatParams : register( b0, space3 ) {
    float  fade_rate;       // 0.98 = moderate decay (~50% in ~35 frames)
    uint   splash_count;    // number of active splash particles
    float  pad0, pad1;      // alignment padding (cbuffer must be multiple of 16)
};

// Per-splash data: x, y position + intensity. 512 splashes × 3 floats = 6KB.
// Split into a separate cbuffer to stay within the 4KB HLSL limit per cbuffer.
cbuffer SplashData : register( b1, space3 ) {
    float splash_x[512];
    float splash_y[512];
    float splash_intensity[512];
};

struct PS_IN {
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

float4 main( PS_IN i ) : SV_Target
{
    // Sample current wetness at this screen pixel.
    float4 wet = SplatIn.Sample( SampLinear, i.uv );

    // Apply per-frame fade to all channels uniformly.
    wet.rgb *= fade_rate;

    // Accumulate splash contributions from uniform data.
    for( uint s = 0u; s < splash_count && s < 512u; ++s ) {
        const float dist = length( i.xy - float2( splash_x[ s ], splash_y[ s ] ) );
        const float radius = 8.0f; // splash spread in pixels

        if( dist < radius ) {
            // Radial falloff: bright at centre, fading outward (quadratic).
            const float t = 1.0f - ( dist / radius );
            const float contribution = splash_intensity[ s ] * t * t * 0.3f;
            wet.rgb += contribution;
        }
    }

    // Clamp to prevent overflow from accumulation.
    wet.rgb = min( wet.rgb, 1.5f );

    return wet;
}

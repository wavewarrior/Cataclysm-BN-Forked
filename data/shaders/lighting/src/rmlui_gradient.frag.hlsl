// RmlUi gradient decorator fragment shader (linear / radial / conic, + repeating
// variants). Ported from RmlUi's GL3 backend gradient shader so the hand-written
// SDL_GPU RenderInterface can support gradient decorators (CompileShader /
// RenderShader). Reuses rmlui.vert.hlsl: i.uv carries the element-local position
// RmlUi bakes into the gradient quad, and i.colour the per-vertex modulation.
//
// Fragment uniform cbuffer at register(b0, space3) — SDL_PushGPUFragmentUniformData
// slot 0. Layout mirrors the C++ grad_params struct exactly (HLSL 16-byte cbuffer
// packing): stop positions are packed as 4 float4 (not float[16], which HLSL would
// pad to one-per-register).

cbuffer GradientParams : register( b0, space3 ) {
    int    g_func;             // 0 lin, 1 rad, 2 conic, 3 rep-lin, 4 rep-rad, 5 rep-conic
    int    g_num_stops;
    float2 g_p;                // linear: p0 ; radial/conic: center
    float2 g_v;                // linear: p1-p0 ; radial: 1/radius ; conic: unit angle vec
    float2 g_pad;
    float4 g_stop_colors[16];
    float4 g_stop_pos[4];      // 16 normalized stop positions, packed 4-per-register
};

struct PS_IN {
    float4 pos    : SV_Position;
    float4 colour : TEXCOORD0;
    float2 uv     : TEXCOORD1;
};

static const float PI = 3.14159265358979323846;

float stop_pos( int i )
{
    return g_stop_pos[i >> 2][i & 3];
}

float4 mix_stop_colors( float t )
{
    float4 color = g_stop_colors[0];
    for( int i = 1; i < g_num_stops; i++ ) {
        color = lerp( color, g_stop_colors[i], smoothstep( stop_pos( i - 1 ), stop_pos( i ), t ) );
    }
    return color;
}

float4 main( PS_IN i ) : SV_Target
{
    float t = 0.0;
    const float2 V = i.uv - g_p;

    if( g_func == 0 || g_func == 3 ) {          // linear
        const float d2 = dot( g_v, g_v );
        t = dot( g_v, V ) / d2;
    } else if( g_func == 1 || g_func == 4 ) {   // radial
        t = length( g_v * V );
    } else {                                    // conic
        const float2x2 R = float2x2( g_v.x, -g_v.y, g_v.y, g_v.x );
        const float2 VR = mul( R, V );
        t = 0.5 + atan2( -VR.x, VR.y ) / ( 2.0 * PI );
    }

    if( g_func >= 3 ) {                          // repeating: wrap t into [t0, t1]
        const float t0 = stop_pos( 0 );
        const float t1 = stop_pos( g_num_stops - 1 );
        t = t0 + fmod( t - t0, t1 - t0 );
    }

    float4 color = mix_stop_colors( t );

    // De-band: dither by ±0.5 LSB of 8-bit before the LDR swapchain write. A smooth
    // radial vignette otherwise shows visible alpha steps. Hash on pixel position.
    const float d = frac( sin( dot( i.pos.xy, float2( 12.9898, 78.233 ) ) ) * 43758.5453 );
    color += ( d - 0.5 ) / 255.0;

    // Black-gradient use cases (scanlines/vignette) have RGB 0, so premultiplied vs
    // straight is identical; mirror GL3's straight multiply for the general case.
    return i.colour * color;
}

// Tonemap fragment shader — samples the HDR scene target and maps it to the
// displayable 8-bit output.
//
// STEP 1a/1b: IDENTITY passthrough (returns the sampled texel unchanged) so the
// RT-backbone plumbing and the RGBA16F format flip can each be verified
// pixel-identical before the curve is introduced. STEP 1c replaces the body
// with the AgX tonemap. Fragment sampler lives at register(t0/s0, space2) per
// the SDL_shadercross D3D12 register-space mapping (see src/lighting/CLAUDE.md).

Texture2D    SrcTex : register( t0, space2 );
SamplerState SrcSmp : register( s0, space2 );

// Phase 1a HARD-GATE spike (LIGHTING_REWORK_PLAN.md step 3): a read-only
// storage texture. It is a Texture2D with NO paired SamplerState and is read
// with .Load() only — that is precisely what makes SDL_shadercross reflect it
// as a storage texture (num_separate_images - num_separate_samplers) rather
// than a 2nd sampled image. As fragment storage slot 0 it lands at t1/space2
// (after the t0 sampled SrcTex). Sentinel is 1.0: if storage-read textures
// bind correctly the output is identity; if they read 0 (the known Metal
// sampler-zero failure) the whole frame goes black. Removed after the gate.
Texture2D<float> ProbeTex : register( t1, space2 );

// Live tuning from the F4 dev panel (SDL_PushGPUFragmentUniformData slot 0 →
// b0/space3). Pre-AgX exposure scale; see agx_tonemap().
cbuffer TonemapParams : register( b0, space3 )
{
    float tm_exposure;  // pre-AgX scale
    float tm_min_ev;    // AgX log2 floor (default -12.47393)
    float tm_max_ev;    // AgX log2 ceil  (default   4.026069)
    float tm_pad;
};

struct VS_OUT {
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

// --- Minimal AgX tonemap (Troy Sobotka curve, Benjamin Wrensch polynomial fit).
// Maps scene light -> display, rolling bright values off filmically instead of
// hard-clipping. Drop-in "outputs sRGB display values" variant: no extra OETF,
// correct for the SDR UNORM swapchain this pass writes into.
//
// The HLSL float3x3 rows below are the standard AgX matrices used directly with
// mul(M, v) (row-major) — same numbers as the canonical GLSL column-major
// constructors, which makes mul(M,v) == M*v there. Verified equivalent.
//
// TUNE (step 1c eyeball): if the image reads too bright/washed or too dark,
// adjust exposure before AgX (col *= exposure) or revisit the min/max EV range
// — this is the intended visual-iteration knob, not a correctness fault.

float3 agx_contrast_approx( float3 x )
{
    float3 x2 = x * x;
    float3 x4 = x2 * x2;
    return  15.5   * x4 * x2
            - 40.14  * x4 * x
            + 31.96  * x4
            - 6.868  * x2 * x
            + 0.4298 * x2
            + 0.1191 * x
            - 0.00232;
}

float3 agx_tonemap( float3 col, float exposure, float min_ev, float max_ev )
{
    const float3x3 AGX_MAT = float3x3(
                                 0.842479062253094,  0.0423282422610123, 0.0423756549057051,
                                 0.0784335999999992, 0.878468636469772,  0.0784336,
                                 0.0792237451477643, 0.0791661274605434, 0.879142973793104 );
    const float3x3 AGX_MAT_INV = float3x3(
                                     1.19687900512017,   -0.0528968517574562, -0.0529716355144438,
                                     -0.0980208811401368,  1.15190312990417,   -0.0980434501171241,
                                     -0.0990297440797205, -0.0989611768448433,  1.15107367264116 );
    // The lit world is LINEAR light (additive emitter/sun/ambient, clamped ~2.0),
    // sitting well above AgX's 0.18 mid-gray anchor — fed raw, the whole scene
    // lands on the bright end of the curve and washes out. Expose down so a
    // normally-lit value (~1.0) sits near mid-gray. Driven live by the F4
    // "exposure" slider (lower = darker/more contrast, higher = brighter).
    col = max( col, 0.0 ) * exposure;
    col = mul( AGX_MAT, col );
    col = clamp( log2( col ), min_ev, max_ev );
    col = ( col - min_ev ) / ( max_ev - min_ev );
    col = agx_contrast_approx( col );
    col = mul( AGX_MAT_INV, col );
    return saturate( col );
}

float4 main( VS_OUT i ) : SV_Target0
{
    float4 c = SrcTex.Sample( SrcSmp, i.uv );
    c.rgb = agx_tonemap( c.rgb, tm_exposure, tm_min_ev, tm_max_ev );
    // Phase 1a gate: sentinel 1.0 → identity; 0 → black (storage-read broken).
    c.rgb *= ProbeTex.Load( int3( 0, 0, 0 ) );
    return c;
}

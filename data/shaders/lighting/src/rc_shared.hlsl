// Shared SS-finer JFA SDF sampling — sphere-trace helpers for Radiance
// Cascades (Stage 7, gpu-daylight-black-scene-bisect-plan).
//
// NOT the same file as jfa_shared.hlsl (that one holds the JFA seed/flood
// constants SDF_SS/SDF_FLOOD/k_jfa_bayer4 shared by jfa_seed/jfa_flood/
// jfa_resolve/occ_base/occ_raster/sound_wave.frag — six existing includers.
// Colliding with that file's name or symbols breaks the entire JFA SDF
// pipeline; this file is intentionally separate).
//
// The including file MUST already declare, with these EXACT names, before
// this #include:
//     StructuredBuffer<float> SdfBuf;   // SS-finer grid, x-major sdf[x*gh+y]
//     uint sdf_map_w;                   // tile-res grid width
//     uint sdf_map_h;                   // tile-res grid height
//     uint sdf_ss;                      // SDF supersample factor (matches
//                                       // sdf_pass.h::SDF_SUPERSAMPLE, 8)
//
// This is the same "pre-declare then #include" convention shadow_trace.hlsl
// already uses in this codebase (see its header comment) — not a new
// pattern. Existing passes (sprite.frag, gi_field.comp, sky_sun.comp) each
// still carry their OWN copy of this exact function; this file is for
// rc_build.comp.hlsl only. Do not widen this include to them without
// re-verifying each one separately.

float sdf_texel( int x, int y )
{
    const int gw = (int)sdf_map_w * (int)sdf_ss;
    const int gh = (int)sdf_map_h * (int)sdf_ss;
    x = clamp( x, 0, gw - 1 );
    y = clamp( y, 0, gh - 1 );
    return SdfBuf[x * gh + y];
}

float sdf_bilinear( float2 p )
{
    const float2 g  = p * (float)sdf_ss - 0.5;
    const float2 fp = floor( g );
    const int   x0  = (int)fp.x;
    const int   y0  = (int)fp.y;
    const float2 w  = g - fp;
    const float a = sdf_texel( x0,     y0     );
    const float b = sdf_texel( x0 + 1, y0     );
    const float c = sdf_texel( x0,     y0 + 1 );
    const float d = sdf_texel( x0 + 1, y0 + 1 );
    return lerp( lerp( a, b, w.x ), lerp( c, d, w.x ), w.y );
}

// Nearest sub-cell sample (one load) — shadow_trace.hlsl's far-field step.
float sdf_nearest( float2 p )
{
    return sdf_texel( (int)floor( p.x * (float)sdf_ss ), (int)floor( p.y * (float)sdf_ss ) );
}

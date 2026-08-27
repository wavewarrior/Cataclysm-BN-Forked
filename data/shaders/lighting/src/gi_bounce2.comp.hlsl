// GI compute — pass 3 of 3: SECOND-bounce ray march + temporal EMA.
//
// One compute thread = one map tile. Identical ray march to pass 2, but the
// field it marches through is pass 2's OUTPUT (Gi1st — the 1-bounce radiance),
// so light that bounced once is bounced a second time: corridors/rooms past a
// doorway get real two-hop light (single-bounce dies at the first wall).
//
// The 2nd-bounce TERM is temporally filtered: gi2 = α·new + (1−α)·prev, where
// prev is this pass's previous output (GiPrev). The world is static between
// structure rebuilds, so accumulating across rebuilds kills the pop/flicker
// when a tile's ray set changes and lets the pass afford more rays.
// α = gi_temporal (0 = no filtering; 1 = full replace).
//
// Output: the COMBINED field (1st + k·2nd) → GiOut, the sprite's GI input
// (moves from pass 2's buffer to this one). The ping-pong buffer (GiPrev ↔
// GiTerm) stores the 2nd-bounce TERM only, so the EMA converges on the term,
// not the combined field.
//
//   t0 space0  Gi1st    — StructuredBuffer<float>, 4 floats/tile (rgb+pad),
//              tile-res, x-major (pass-2 output = the 1-bounce field marched
//              here for the second hop).
//   t1 space0  SdfBuf   — StructuredBuffer<float>, SS-finer grid (wall test).
//   t2 space0  GiPrev   — StructuredBuffer<float>, 4 floats/tile, x-major
//              (this pass's PREVIOUS 2nd-bounce term — the EMA source).
//   u0 space1  GiTerm   — RWStructuredBuffer<float>, 4 floats/tile, x-major
//              (this pass's 2nd-bounce term; becomes GiPrev next frame).
//   u1 space1  GiOut    — RWStructuredBuffer<float>, 4 floats/tile, x-major
//              (combined 1st+k·2nd; sprite.frag GI input).
//   b0 space2  GiParams (shared push).

StructuredBuffer<float>   Gi1st    : register(t0, space0);
StructuredBuffer<float>   SdfBuf   : register(t1, space0);
StructuredBuffer<float>   GiPrev   : register(t2, space0);
RWStructuredBuffer<float> GiTerm   : register(u0, space1);
RWStructuredBuffer<float> GiOut    : register(u1, space1);

cbuffer GiParams : register(b0, space2) {
    uint  emitter_count; // unused here (kept: shared gi_params push)
    uint  sdf_map_w;
    uint  sdf_map_h;
    float current_z;
    float shadow_k;
    uint  shadow_steps;
    float gi_temporal;  // EMA blend: out = α·new + (1−α)·prev (0=off, 1=replace)
    float gi_bounce2;   // second-bounce mix: out = 1st + k·2nd (0=off)
    // P2 sun/sky surface-radiance injection (gi_field.comp reads SkyBuf).
    float sun_r, sun_g, sun_b, sun_intensity;
    float sky_r, sky_g, sky_b, sky_intensity;
    float gi_albedo;    // albedo-bleed mix (0=off) — read by gi_field.comp
};

// Gather tuning (constants; matches rc_bounce.frag / gi_bounce.comp).
static const int   RC_DIRS  = 16;   // rays per tile
static const int   RC_STEPS = 20;   // max march steps per ray
static const float RC_STEP  = 0.75; // tile units per step
static const float RC_START = 0.70; // skip the tile's own cell
static const float RC_WALL  = 0.10; // SDF distance treated as "blocked"
static const int   SDF_SS   = 8;    // MUST match sdf_pass.h / sprite.frag
// Gi1st is tile-res, x-major gi1[(x*map_h+y)*4 + c] — the 1-bounce field this
// pass marches through (the second hop).
float3 field_at( int x, int y )
{
    x = clamp( x, 0, (int)sdf_map_w - 1 );
    y = clamp( y, 0, (int)sdf_map_h - 1 );
    const uint o = ( (uint)x * sdf_map_h + (uint)y ) * 4u;
    return float3( Gi1st[o + 0u], Gi1st[o + 1u], Gi1st[o + 2u] );
}

// SS-finer SDF (wall test) — identical math to sprite.frag / gi_field.
float sdf_texel( int x, int y )
{
    const int gw = (int)sdf_map_w * SDF_SS;
    const int gh = (int)sdf_map_h * SDF_SS;
    x = clamp( x, 0, gw - 1 );
    y = clamp( y, 0, gh - 1 );
    return SdfBuf[x * gh + y];
}
float sdf_bilinear( float2 p )
{
    const float2 g  = p * (float)SDF_SS - 0.5;
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

[numthreads(8, 8, 1)]
void main( uint3 tid : SV_DispatchThreadID )
{
    const int tileX = (int)tid.x;
    const int tileY = (int)tid.y;
    if( tileX >= (int)sdf_map_w || tileY >= (int)sdf_map_h ) {
        return;
    }
    const float2 probe = float2( (float)tileX + 0.5, (float)tileY + 0.5 );

    float3 acc = float3( 0.0, 0.0, 0.0 );
    [loop] for( int d = 0; d < RC_DIRS; ++d ) {
        const float  ang = 6.2831853 * ( (float)d + 0.5 ) / (float)RC_DIRS;
        const float2 dir = float2( cos( ang ), sin( ang ) );
        float t = RC_START;
        [loop] for( int s = 0; s < RC_STEPS; ++s ) {
            const float2 pos = probe + dir * t;
            // Distance-weighted radiance sampled from the field along the ray.
            acc += field_at( (int)floor( pos.x ), (int)floor( pos.y ) ) / ( 1.0 + t );
            if( sdf_bilinear( pos ) < RC_WALL ) {
                break; // wall: sample it (the lit surface) then stop the ray
            }
            t += RC_STEP;
        }
    }
    // Average over directions. This is the SECOND-bounce term: light that
    // already bounced once (Gi1st) gathered a second time.
    const float3 gi2_new = acc / (float)RC_DIRS;

    const uint o = ( (uint)tileX * sdf_map_h + (uint)tileY ) * 4u;
    // Temporal EMA on the 2nd-bounce TERM (converges on the term, not the
    // combined field).
    const float3 prev_term = float3( GiPrev[o + 0u], GiPrev[o + 1u], GiPrev[o + 2u] );
    const float  a         = clamp( gi_temporal, 0.0, 1.0 );
    const float3 gi2       = a * gi2_new + ( 1.0 - a ) * prev_term;
    // Store the term for next frame's EMA.
    GiTerm[o + 0u] = gi2.x;
    GiTerm[o + 1u] = gi2.y;
    GiTerm[o + 2u] = gi2.z;
    GiTerm[o + 3u] = 0.0;
    // Combined field: 1st bounce (Gi1st) + k · 2nd bounce → sprite GI input.
    // NOTE: `out` is a reserved HLSL keyword — name the local `comb`.
    const float3 first = float3( Gi1st[o + 0u], Gi1st[o + 1u], Gi1st[o + 2u] );
    const float3 comb  = first + gi_bounce2 * gi2;
    GiOut[o + 0u] = comb.x;
    GiOut[o + 1u] = comb.y;
    GiOut[o + 2u] = comb.z;
    GiOut[o + 3u] = 0.0;
}

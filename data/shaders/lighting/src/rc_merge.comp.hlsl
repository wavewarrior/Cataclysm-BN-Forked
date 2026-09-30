// Radiance Cascades — pass 2 of 3: MERGE. One dispatch per cascade `i`, run
// DESCENDING (i = RC_CASCADES-2 .. 0) from the caller. One thread = one
// cascade-`i` probe; loops over that cascade's directions, merging each
// against the cascade ABOVE it (i+1) and writing the merged result BACK into
// cascade i's own atlas slot (overwriting its raw BUILD output). Because
// dispatches run in descending order on one command buffer, by the time
// cascade i is merged, cascade i+1 already holds ITS merged result (or, for
// the top cascade, its raw BUILD output — there is nothing above it to merge
// with) — so merging always reads the fully-resolved cascade above.
//
// THE LOAD-BEARING DETAIL: the probe-position mapping between cascades.
// Cascade i probe (px,py) sits at world position (px+0.5)*spacing_i tiles
// (probe CENTRES, spacing 2^i); cascade i+1's probes sit at spacing 2^(i+1),
// so a cascade-i probe never coincides with a cascade-(i+1) probe. It is
// found by BILINEAR weight over the 4 surrounding i+1 probes at
// g = world_i/spacing_{i+1} - 0.5 (the same `p - 0.5` probe-centre convention
// rc_shared.hlsl's sdf_bilinear already uses). Getting this offset wrong is
// the single most common RC bug and leaks light through wall corners.
//
// THE BILINEAR FIX. Each cascade-i direction wedge spans RC_BRANCH
// consecutive cascade-(i+1) direction indices. For each of the 4 spatial
// neighbours, the RC_BRANCH angular sub-directions are averaged FIRST, THEN
// the 4 (now angularly-averaged) neighbour values are combined by bilinear
// SPATIAL weight. Averaging angle before space is the fix; doing it in the
// other order is the classic mistake that reintroduces the corner leak.
//
// Merge rule (Sannikov, "Radiance Cascades", "Calculating indirect
// lighting"): L = L_i + beta_i * L_upper ; beta = beta_i * beta_upper.
//
//   u0 space1  RcAtlas — RWStructuredBuffer<float>, the flat multi-cascade
//              atlas (same layout as rc_build.comp.hlsl). Read AND written
//              in place; no separate readonly binding needed (ro_sb=0).
//   b0 space2  RcParams (shared push; see rc_params.h for the C++ mirror).

RWStructuredBuffer<float> RcAtlas : register(u0, space1);

static const uint RC_CASCADES = 5u;
static const uint RC_BRANCH   = 4u;

cbuffer RcParams : register(b0, space2) {
    uint  map_w;
    uint  map_h;
    uint  sdf_map_w;
    uint  sdf_map_h;
    uint  cascade;    // which cascade THIS dispatch merges (i, not i+1)
    uint  sdf_ss;
    float c0_interval;
    float rc_pad0;
    uint4 geom[RC_CASCADES];
};

float4 read_texel( uint offset, uint probes_x, uint dirs, uint px, uint py, uint d )
{
    const uint o = offset + ( ( py * probes_x + px ) * dirs + d ) * 4u;
    return float4( RcAtlas[o + 0u], RcAtlas[o + 1u], RcAtlas[o + 2u], RcAtlas[o + 3u] );
}

[numthreads(8, 8, 1)]
void main( uint3 tid : SV_DispatchThreadID )
{
    const uint4 g_own = geom[cascade];
    const uint probes_x = g_own.x;
    const uint probes_y = g_own.y;
    const uint dirs      = g_own.z;
    const uint offset    = g_own.w;
    if( tid.x >= probes_x || tid.y >= probes_y ) {
        return;
    }
    // Nothing above the top cascade — BUILD's raw output already IS the
    // final value. (The caller never dispatches this for the top cascade,
    // but guard it anyway: writing here would read cascade+1 == RC_CASCADES,
    // out of the fixed 5-entry table.)
    if( cascade + 1u >= RC_CASCADES ) {
        return;
    }
    const uint4 g_up = geom[cascade + 1u];
    const uint up_probes_x = g_up.x;
    const uint up_probes_y = g_up.y;
    const uint up_dirs      = g_up.z;
    const uint up_offset    = g_up.w;

    const float spacing_own = (float)( 1u << cascade );
    const float spacing_up  = (float)( 1u << ( cascade + 1u ) );
    const float2 world_own  = ( float2( tid.xy ) + 0.5 ) * spacing_own;
    const float2 g          = world_own / spacing_up - 0.5;
    const float2 base       = floor( g );
    const float2 w          = g - base;
    const int2 b0 = int2( base );
    const int2 b1 = b0 + int2( 1, 1 );
    const int2 c00 = clamp( int2( b0.x, b0.y ), int2( 0, 0 ), int2( up_probes_x - 1, up_probes_y - 1 ) );
    const int2 c10 = clamp( int2( b1.x, b0.y ), int2( 0, 0 ), int2( up_probes_x - 1, up_probes_y - 1 ) );
    const int2 c01 = clamp( int2( b0.x, b1.y ), int2( 0, 0 ), int2( up_probes_x - 1, up_probes_y - 1 ) );
    const int2 c11 = clamp( int2( b1.x, b1.y ), int2( 0, 0 ), int2( up_probes_x - 1, up_probes_y - 1 ) );

    [loop] for( uint d = 0; d < dirs; ++d ) {
        // Own (cascade i) interval sample at this probe/direction.
        const float4 own = read_texel( offset, probes_x, dirs, tid.x, tid.y, d );

        // Angular sub-directions of cascade i+1 this direction wedge covers.
        const uint sub_base = d * RC_BRANCH;

        // Per spatial neighbour: average the RC_BRANCH angular sub-directions
        // FIRST (the bilinear fix), producing one (L, beta) per neighbour.
        float4 n00 = float4( 0, 0, 0, 0 );
        float4 n10 = float4( 0, 0, 0, 0 );
        float4 n01 = float4( 0, 0, 0, 0 );
        float4 n11 = float4( 0, 0, 0, 0 );
        [loop] for( uint sb = 0; sb < RC_BRANCH; ++sb ) {
            const uint sd = ( sub_base + sb ) % up_dirs;
            n00 += read_texel( up_offset, up_probes_x, up_dirs, (uint)c00.x, (uint)c00.y, sd );
            n10 += read_texel( up_offset, up_probes_x, up_dirs, (uint)c10.x, (uint)c10.y, sd );
            n01 += read_texel( up_offset, up_probes_x, up_dirs, (uint)c01.x, (uint)c01.y, sd );
            n11 += read_texel( up_offset, up_probes_x, up_dirs, (uint)c11.x, (uint)c11.y, sd );
        }
        const float inv_branch = 1.0 / (float)RC_BRANCH;
        n00 *= inv_branch;
        n10 *= inv_branch;
        n01 *= inv_branch;
        n11 *= inv_branch;

        // THEN bilinear spatial weight across the 4 (now angularly-averaged)
        // neighbours.
        const float4 upper = lerp( lerp( n00, n10, w.x ), lerp( n01, n11, w.x ), w.y );

        // Merge rule: L = L_own + beta_own * L_upper ; beta = beta_own * beta_upper.
        const float3 merged_rgb  = own.rgb + own.a * upper.rgb;
        const float  merged_beta = own.a * upper.a;

        const uint texel = offset + ( ( tid.y * probes_x + tid.x ) * dirs + d ) * 4u;
        RcAtlas[texel + 0u] = merged_rgb.x;
        RcAtlas[texel + 1u] = merged_rgb.y;
        RcAtlas[texel + 2u] = merged_rgb.z;
        RcAtlas[texel + 3u] = merged_beta;
    }
}

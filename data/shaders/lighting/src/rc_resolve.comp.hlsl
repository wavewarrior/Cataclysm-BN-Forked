// Radiance Cascades — pass 3 of 3: RESOLVE. One thread = one tile. Averages
// cascade 0's RC_C0_DIRS directions (already MERGED with every cascade above
// it by rc_merge.comp) into one irradiance value and writes it to GiOut in
// the SAME layout the old gi_bounce2.comp wrote — the sprite shader's GI
// input is unchanged; this is the only Radiance Cascades pass it observes.
//
//   t0 space0  RcAtlas — StructuredBuffer<float>, the flat multi-cascade
//              atlas (readonly here — MERGE already finished writing it).
//   t1 space0  SdfBuf  — StructuredBuffer<float>, SS-finer JFA SDF (rc_shared.hlsl).
//   u0 space1  GiOut   — RWStructuredBuffer<float>, 4 floats/tile, x-major
//              gi[(x*map_h+y)*4 + c] (sprite.frag GI input). .rgb = irradiance,
//              .a = SDF at the tile centre (sprite.frag bilateral GI weight).
//   b0 space2  RcParams (shared push; see rc_params.h for the C++ mirror).

StructuredBuffer<float>   RcAtlas : register(t0, space0);
StructuredBuffer<float>   SdfBuf  : register(t1, space0);
RWStructuredBuffer<float> GiOut   : register(u0, space1);

static const uint RC_CASCADES = 5u;

cbuffer RcParams : register(b0, space2) {
    uint  map_w;
    uint  map_h;
    uint  sdf_map_w;
    uint  sdf_map_h;
    uint  cascade; // unused here — resolve always reads cascade 0
    uint  sdf_ss;
    float c0_interval;
    float rc_pad0;
    uint4 geom[RC_CASCADES];
};

#include "rc_shared.hlsl"

[numthreads(8, 8, 1)]
void main( uint3 tid : SV_DispatchThreadID )
{
    if( tid.x >= map_w || tid.y >= map_h ) {
        return;
    }
    // Cascade 0: one probe per tile (RC_C0_PROBE_SPACING == 1.0), so the
    // probe grid IS the tile grid — no spatial resampling needed here.
    const uint4 g0 = geom[0];
    const uint probes_x = g0.x;
    const uint dirs      = g0.z;
    const uint offset    = g0.w;

    float3 sum = float3( 0.0, 0.0, 0.0 );
    [loop] for( uint d = 0; d < dirs; ++d ) {
        const uint o = offset + ( ( tid.y * probes_x + tid.x ) * dirs + d ) * 4u;
        sum += float3( RcAtlas[o + 0u], RcAtlas[o + 1u], RcAtlas[o + 2u] );
    }
    const float3 irradiance = sum / (float)max( dirs, 1u );

    const uint go = ( tid.x * map_h + tid.y ) * 4u;
    GiOut[go + 0u] = irradiance.x;
    GiOut[go + 1u] = irradiance.y;
    GiOut[go + 2u] = irradiance.z;
    // Tile-centre SDF, so sprite.frag's bilateral GI upsample reads its four tap
    // weights from here instead of four sdf_bilinear calls (16 loads) per pixel.
    GiOut[go + 3u] = sdf_bilinear( float2( tid.xy ) + 0.5 );
}

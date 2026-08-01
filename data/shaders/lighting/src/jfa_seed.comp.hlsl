// JFA SEED — one thread per SS-subcell. Reads the sub-tile COVERAGE field that
// occ_base + occ_raster built from the tileset artwork and converts it to per-subcell
// occupancy; writes to seed_a_.
//
// This used to read one binary opaque flag per TILE and replicate it into all
// SDF_SS^2 subcells, which meant the SDF paid 64x the memory and flood cost to carry
// tile-square, axis-aligned geometry: every tree, barrel, car and fence cast a square
// shadow. The coverage field is derived from the sprites' own alpha, so the silhouette
// the player sees is the silhouette that casts.
//
// Solid coverage (1.0) seeds every subcell, identical to the old binary path. Partial
// coverage — a hedge, a chainlink lattice, a smoke field — seeds a stable FRACTION of
// subcells via a world-locked 4x4 Bayer threshold at subcell resolution, so the SDF
// develops real holes and the existing sphere-march produces dappled light with no new
// field and no consumer change.
//
// Inputs:  t0 space0  OccSS   — StructuredBuffer<uint>, SS-grid, x-major,
//                               coverage * 65535 (occ_base then occ_raster).
// Output:  u0 space1  SeedBuf — RWStructuredBuffer<float2>, SS-grid, x-major.
// Uniform: b0 space2  JfaParams (map_w, map_h)

#include "jfa_shared.hlsl"

StructuredBuffer<uint>     OccSS   : register(t0, space0);
RWStructuredBuffer<float2> SeedBuf : register(u0, space1);

cbuffer JfaParams : register(b0, space2) {
    uint  map_w;        // runtime tile dims
    uint  map_h;
    float step;         // unused by seed
    float pad;
};

[numthreads(8, 8, 1)]
void main( uint3 tid : SV_DispatchThreadID )
{
    const int sx = (int)tid.x;  // SS subcell x
    const int sy = (int)tid.y;  // SS subcell y

    if( sx >= (int)(map_w * SDF_SS) || sy >= (int)(map_h * SDF_SS) ) {
        return;
    }

    const uint  idx = (uint)( sx * (int)( map_h * SDF_SS ) + sy );
    const float cov = (float)OccSS[idx] * ( 1.0 / 65535.0 );
    const float thr = ( k_jfa_bayer4[ ( sy & 3 ) * 4 + ( sx & 3 ) ] + 0.5 ) / 16.0;

    // Occupied subcells seed with their own coord; the rest get the (-1,-1) sentinel,
    // which the flood pass treats as +INF distance so a real seed always wins.
    SeedBuf[idx] = ( cov > thr ) ? float2( (float)sx, (float)sy ) : float2( -1.0, -1.0 );
}

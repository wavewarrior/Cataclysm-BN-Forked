// JFA RESOLVE — one thread per SS-subcell. Computes distance from this subcell to
// its nearest seed (stored in SeedBuf), divides by SDF_SS → tile units. Writes
// the final SDF buffer.
//
// Inputs:  t0 space0  SeedBuf  — StructuredBuffer<float2>, SS-grid, x-major
//          (nearest-seed subcell coord per subcell)
// Output:  u0 space1  SdfBuf   — RWStructuredBuffer<float>, SS-grid, x-major
// Uniform: b0 space2  JfaParams (map_w, map_h)

#include "jfa_shared.hlsl"

StructuredBuffer<float2> SeedBuf : register(t0, space0);
RWStructuredBuffer<float> SdfBuf : register(u0, space1);

cbuffer JfaParams : register(b0, space2) {
    uint  map_w;        // runtime tile dims
    uint  map_h;
    float step;         // unused by resolve
    float pad;
};

[numthreads(8, 8, 1)]
void main( uint3 tid : SV_DispatchThreadID )
{
    const int sx = (int)tid.x;
    const int sy = (int)tid.y;

    if( sx >= (int)(map_w * SDF_SS) || sy >= (int)(map_h * SDF_SS) ) {
        return;
    }

    float2 seed = SeedBuf[sx * (int)(map_h * SDF_SS) + sy];

    if( seed.x < 0.0 ) {
        // No seed reached this subcell — clamp to max distance (~4 tiles).
        SdfBuf[sx * (int)(map_h * SDF_SS) + sy] = SDF_FLOOD / (float)SDF_SS;
    } else {
        // Distance from this subcell to its nearest seed, in tile units.
        float dx = (float)sx - seed.x;
        float dy = (float)sy - seed.y;
        float dist_ss = sqrt( dx * dx + dy * dy );
        SdfBuf[sx * (int)(map_h * SDF_SS) + sy] = dist_ss / (float)SDF_SS;
    }
}

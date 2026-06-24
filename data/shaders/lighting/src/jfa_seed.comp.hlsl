// JFA SEED — one thread per SS-subcell. Reads tile-res transparency buffer; if
// the parent tile is opaque (trans < 0.5), seed = this subcell's coord, else
// sentinel (-1,-1). Writes to seed_a_.
//
// Inputs:  t0 space0  TransBuf — StructuredBuffer<float>, tile-res, x-major
//          trans[(x*map_h+y)] where 0.0=opaque .. 1.0=open
// Output:  u0 space1  SeedBuf  — RWStructuredBuffer<float2>, SS-grid, x-major
// Uniform: b0 space2  JfaParams (map_w, map_h)

#include "jfa_shared.hlsl"

StructuredBuffer<float>   TransBuf : register(t0, space0);
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

    // Subcell → parent tile.
    const int tx = sx / SDF_SS;
    const int ty = sy / SDF_SS;

    const float trans = TransBuf[tx * map_h + ty];

    if( trans < 0.5 ) {
        // Opaque tile — seed with this subcell's own coord.
        SeedBuf[sx * (int)(map_h * SDF_SS) + sy] = float2( (float)sx, (float)sy );
    } else {
        // Open tile — sentinel (no seed yet). The flood pass treats (-1,-1) as
        // +∞ distance so a real seed always wins.
        SeedBuf[sx * (int)(map_h * SDF_SS) + sy] = float2( -1.0, -1.0 );
    }
}

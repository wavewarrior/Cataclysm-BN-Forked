// JFA FLOOD — ping-pong propagation. Each pass halves the step size (powers of two).
// Reads from seed_in, writes to seed_out. For each subcell, checks ±step offsets
// in x and y; if a neighbor has a real seed (not sentinel), propagate it.
//
// Inputs:  t0 space0  SeedIn   — StructuredBuffer<float2>, SS-grid, x-major
// Output:  u0 space1  SeedOut  — RWStructuredBuffer<float2>, SS-grid, x-major
// Uniform: b0 space2  JfaParams (map_w, map_h, step)

#include "jfa_shared.hlsl"

StructuredBuffer<float2>   SeedIn : register(t0, space0);
RWStructuredBuffer<float2> SeedOut : register(u0, space1);

cbuffer JfaParams : register(b0, space2) {
    uint  map_w;        // runtime tile dims
    uint  map_h;
    float step;         // current flood step size (SS units)
    float pad;
};

static const int SDF_SS_DIM = 4;      // subcells per tile side

[numthreads(8, 8, 1)]
void main( uint3 tid : SV_DispatchThreadID )
{
    const int sx = (int)tid.x;
    const int sy = (int)tid.y;

    const int ss_w = (int)(map_w * SDF_SS_DIM);
    const int ss_h = (int)(map_h * SDF_SS_DIM);

    if( sx >= ss_w || sy >= ss_h ) {
        return;
    }

    const uint idx = sx * ss_h + sy;
    float2 best = SeedIn[idx];  // start with own value (sentinel or real seed)

    const int s = (int)step;

    // Check ±step in x and y. A neighbor with a real seed (x >= 0) wins over sentinel (-1,-1).
    auto check = [&]( int nx, int ny ) {
        if( nx < 0 || nx >= ss_w || ny < 0 || ny >= ss_h ) return;
        float2 nb = SeedIn[nx * ss_h + ny];
        if( nb.x >= 0.0 && best.x < 0.0 ) {
            best = nb;
        }
    };

    check( sx + s, sy );
    check( sx - s, sy );
    check( sx, sy + s );
    check( sx, sy - s );

    SeedOut[idx] = best;
}

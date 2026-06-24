// JFA FLOOD — ping-pong propagation. Each pass halves the step size (powers of two).
// Reads from seed_in, writes to seed_out. For each subcell, samples its 8 neighbours
// at ±step (axes + diagonals) and keeps whichever carried seed coord is NEAREST to
// this subcell (true Jump-Flood nearest-seed selection, not first-seed-wins).
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

// Consider neighbour (nx,ny): if it carries a real seed that is strictly CLOSER
// to (sx,sy) than the current best, adopt it. This nearest-seed selection is what
// makes JFA converge to the nearest occluder across passes instead of latching
// the first seed that arrives. (Plain function, not a lambda — HLSL/DXC has no
// lambda support; `auto`/`[&]` are rejected.)
void consider( int nx, int ny, int sx, int sy, int ss_w, int ss_h,
               inout float2 best, inout float best_d2 )
{
    if( nx < 0 || nx >= ss_w || ny < 0 || ny >= ss_h ) {
        return;
    }
    const float2 nb = SeedIn[nx * ss_h + ny];
    if( nb.x < 0.0 ) {
        return;                             // neighbour carries no seed
    }
    const float dx = (float)sx - nb.x;
    const float dy = (float)sy - nb.y;
    const float d2 = dx * dx + dy * dy;
    if( d2 < best_d2 ) {
        best    = nb;
        best_d2 = d2;
    }
}

[numthreads(8, 8, 1)]
void main( uint3 tid : SV_DispatchThreadID )
{
    const int sx = (int)tid.x;
    const int sy = (int)tid.y;

    // SDF_SS (subcells per tile side) comes from jfa_shared.hlsl — single source.
    const int ss_w = (int)(map_w * SDF_SS);
    const int ss_h = (int)(map_h * SDF_SS);

    if( sx >= ss_w || sy >= ss_h ) {
        return;
    }

    const uint idx = sx * ss_h + sy;
    float2 best = SeedIn[idx];  // current nearest-seed coord (or sentinel -1,-1)

    // Squared distance from THIS subcell to its current best seed. Sentinel
    // (no seed) → +inf so any real neighbour seed wins.
    float best_d2 = ( best.x < 0.0 )
        ? 1e30
        : ( (float)sx - best.x ) * ( (float)sx - best.x )
          + ( (float)sy - best.y ) * ( (float)sy - best.y );

    const int s = (int)step;

    // 8-neighbourhood: 4 axes + 4 diagonals (standard JFA stencil).
    consider( sx + s, sy,     sx, sy, ss_w, ss_h, best, best_d2 );
    consider( sx - s, sy,     sx, sy, ss_w, ss_h, best, best_d2 );
    consider( sx,     sy + s, sx, sy, ss_w, ss_h, best, best_d2 );
    consider( sx,     sy - s, sx, sy, ss_w, ss_h, best, best_d2 );
    consider( sx + s, sy + s, sx, sy, ss_w, ss_h, best, best_d2 );
    consider( sx + s, sy - s, sx, sy, ss_w, ss_h, best, best_d2 );
    consider( sx - s, sy + s, sx, sy, ss_w, ss_h, best, best_d2 );
    consider( sx - s, sy - s, sx, sy, ss_w, ss_h, best, best_d2 );

    SeedOut[idx] = best;
}

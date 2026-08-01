// OCCLUDER BASE — one thread per SS-subcell. Seeds the coarse tile-square coverage
// fallback ONLY for tiles that no sprite footprint was captured for (off-camera
// terrain, or tiles whose foreground sprite list is empty). Tiles that WERE captured
// are zeroed here and filled by occ_raster, so the alpha silhouette REPLACES the
// square rather than unioning with it — unioning would leave every occluder wrapped
// in its old tile-square hull and the whole exercise would be invisible.
//
// The fallback is load-bearing, not optional: the capture only covers tiles that were
// actually drawn, so without it a wall just outside the viewport would stop casting
// its shadow into view.
//
// Coverage is stored as uint fixed-point (cov * 65535) so occ_raster's overlapping
// writes can resolve with InterlockedMax — order-independent and deterministic.
//
// Compute HLSL register spaces (SDL_GPU): readonly storage = (tN, space0),
// read-write storage = (uN, space1), uniforms = (bN, space2).
//
//   t0 space0  TransBuf    — StructuredBuffer<float>, tile-res, x-major
//                            trans[x*map_h+y], 0.0=opaque .. 1.0=open.
//   t1 space0  CapturedBuf — StructuredBuffer<uint>, tile-res, x-major, 1 where at
//                            least one sprite footprint was captured this frame.
//   u0 space1  OccSS       — RWStructuredBuffer<uint>, SS-grid, x-major,
//                            coverage * 65535.
//   b0 space2  OccParams.

#include "jfa_shared.hlsl"

StructuredBuffer<float> TransBuf    : register(t0, space0);
StructuredBuffer<uint>  CapturedBuf : register(t1, space0);
RWStructuredBuffer<uint> OccSS      : register(u0, space1);

cbuffer OccParams : register(b0, space2) {
    uint  map_w;
    uint  map_h;
    uint  quad_base;   // unused by this pass
    uint  atlas_w;     // unused by this pass
    uint  atlas_h;     // unused by this pass
    float occ_soft_gain; // unused by this pass
    float op_pad0;
    float op_pad1;
};

[numthreads(8, 8, 1)]
void main( uint3 tid : SV_DispatchThreadID )
{
    const int sx = (int)tid.x;
    const int sy = (int)tid.y;
    if( sx >= (int)( map_w * SDF_SS ) || sy >= (int)( map_h * SDF_SS ) ) {
        return;
    }

    const int tx = sx / SDF_SS;
    const int ty = sy / SDF_SS;
    const uint tile_idx = (uint)( tx * (int)map_h + ty );

    const float base = ( CapturedBuf[tile_idx] != 0u )
                       ? 0.0
                       : ( ( TransBuf[tile_idx] < 0.5 ) ? 1.0 : 0.0 );

    OccSS[sx * (int)( map_h * SDF_SS ) + sy] = (uint)( saturate( base ) * 65535.0 );
}

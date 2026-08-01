// OCCLUDER RASTER — rasterise captured sprite alpha into the SDF coverage field.
//
// ONE THREAD GROUP PER QUAD, ONE THREAD PER SUBCELL of that quad's tile
// (numthreads(8,8,1) == SDF_SS x SDF_SS). Dispatched (quad_count, 1, 1) once per
// atlas page: a compute dispatch reads one texture, so gpu_sdf_pass sorts the quad
// list by page and issues one dispatch per contiguous run, offsetting into the
// buffer with quad_base.
//
// The quad is described exactly the way sprite.vert draws it — centre, size and
// rotation about that centre, all in TILE units relative to the target tile's own
// screen square. This pass INVERTS that transform per subcell instead of consuming a
// precomputed axis-aligned UV band, because single-sprite terrain that `rotates` is
// enqueued with a real -90/180/90 degree rotation; an axis-aligned band would seed
// the unrotated silhouette and a rotated fence, wall or vehicle hull would come out
// the wrong shape. Inverting also handles tall sprites, tile_type::offset and depth
// extrusion with no special cases: a subcell that maps outside the sprite quad
// simply contributes nothing.
//
// The atlas is read with Texture2D.Load (integer texel fetch, no sampler). That is
// what pixel art wants — a filtered sample would smear a hard alpha edge across
// neighbouring texels — and it also avoids binding a sampler in a compute pass,
// which shadercross has historically mis-bound (see src/lighting/CLAUDE.md).
//
// SDL_GPU compute t-register order: readonly storage TEXTURES occupy the first t
// slots and readonly storage BUFFERS follow, so Atlas MUST be t0 and Quads t1 even
// though the C++ bind-slot indices are per-kind and both start at 0. Getting this
// backwards is the exact mis-binding class src/lighting/CLAUDE.md warns about.
//
//   t0 space0  Atlas — Texture2D<float4>, the atlas page for this dispatch.
//   t1 space0  Quads — StructuredBuffer<OccQuad>, page-sorted.
//   u0 space1  OccSS — RWStructuredBuffer<uint>, SS-grid, coverage * 65535.
//   b0 space2  OccParams.

#include "jfa_shared.hlsl"

// Alpha taps per subcell axis. Each subcell covers texels_per_tile / SDF_SS art
// texels per axis (4 at 32/8), so 4x4 taps average that block exactly. Averaging
// rather than point-sampling is what turns a chainlink fence's real holes into
// fractional coverage instead of aliasing them to all-or-nothing.
static const uint OCC_TAPS = 4u;

struct OccQuad {
    float u0, v0, uw, vh;   // FULL sprite atlas rect, normalised, flip folded in
    float tile_x, tile_y;   // bubble-local tile this footprint seeds
    float cx, cy;           // quad centre minus tile screen origin, in TILE units
    float sw, sh;           // quad size in TILE units
    float rot;              // quad rotation, radians, screen-clockwise
    float block;            // 0..1 opacity multiplier from the transparency cache
};

Texture2D<float4>         Atlas : register(t0, space0);
StructuredBuffer<OccQuad> Quads : register(t1, space0);
RWStructuredBuffer<uint>  OccSS : register(u0, space1);

cbuffer OccParams : register(b0, space2) {
    uint  map_w;
    uint  map_h;
    uint  quad_base;     // index of this dispatch's first quad in Quads
    uint  atlas_w;       // atlas page dimensions, for the UV -> texel conversion
    uint  atlas_h;
    float occ_soft_gain; // 0 = hard occluders only (block==1), 1 = full partial coverage
    float op_pad0;
    float op_pad1;
};

[numthreads(8, 8, 1)]
void main( uint3 gid : SV_GroupID, uint3 tid : SV_GroupThreadID )
{
    const OccQuad q = Quads[quad_base + gid.x];

    // occ_soft_gain scales how much a PARTIAL occluder blocks. At 0 only exact hard
    // occluders (block == 1) survive, reproducing the pre-Step-3 hard/soft split.
    const float blk = ( q.block >= 0.999 ) ? 1.0 : ( q.block * occ_soft_gain );
    if( blk <= 0.0 ) {
        return;
    }

    const int tx = (int)q.tile_x;
    const int ty = (int)q.tile_y;
    if( tx < 0 || ty < 0 || tx >= (int)map_w || ty >= (int)map_h ) {
        return;
    }

    const float cs = cos( -q.rot );
    const float sn = sin( -q.rot );
    const float inv_sw = 1.0 / max( q.sw, 1e-6 );
    const float inv_sh = 1.0 / max( q.sh, 1e-6 );
    const float2 aw = float2( (float)max( atlas_w, 1u ), (float)max( atlas_h, 1u ) );

    // This subcell's extent inside the tile, in tile units (0..1 across the tile).
    const float2 f0 = float2( tid.x, tid.y ) / (float)SDF_SS;
    const float2 f1 = float2( tid.x + 1u, tid.y + 1u ) / (float)SDF_SS;

    float acc = 0.0;
    float n   = 0.0;
    [loop] for( uint sy = 0u; sy < OCC_TAPS; ++sy ) {
        [loop] for( uint sx = 0u; sx < OCC_TAPS; ++sx ) {
            const float2 fr = lerp( f0, f1, ( float2( sx, sy ) + 0.5 ) / (float)OCC_TAPS );
            // Tile-space -> quad-local: translate to the quad centre, undo the
            // rotation, then normalise by the quad size.
            const float2 rel = fr - float2( q.cx, q.cy );
            const float2 loc = float2( rel.x * cs - rel.y * sn,
                                       rel.x * sn + rel.y * cs );
            const float2 g   = float2( loc.x * inv_sw, loc.y * inv_sh ) + 0.5;
            n += 1.0;
            if( g.x < 0.0 || g.x > 1.0 || g.y < 0.0 || g.y > 1.0 ) {
                continue;   // this tap falls outside the sprite quad
            }
            const float2 uv = float2( q.u0 + q.uw * g.x, q.v0 + q.vh * g.y );
            const int2   px = (int2)clamp( uv * aw, float2( 0.0, 0.0 ), aw - 1.0 );
            acc += Atlas.Load( int3( px, 0 ) ).a;
        }
    }

    const float cov = ( acc / max( n, 1.0 ) ) * blk;
    const int gx = tx * SDF_SS + (int)tid.x;
    const int gy = ty * SDF_SS + (int)tid.y;
    InterlockedMax( OccSS[gx * (int)( map_h * SDF_SS ) + gy], (uint)( saturate( cov ) * 65535.0 ) );
}

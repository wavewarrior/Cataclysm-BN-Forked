// Radiance Cascades — pass 1 of 3: BUILD. One thread = one cascade-`cascade`
// probe; loops over that cascade's directions. Sphere-traces the probe's own
// world-tile interval [t_near, t_far) through the JFA SDF from its probe
// position along each direction, sampling FieldBuf (gi_field.comp's per-tile
// DIRECT radiance — occluded emitter gather + sun/sky injection, tinted by
// albedo) at the hit tile.
//
// A ray that escapes the interval without hitting an occluder writes ZERO
// radiance, not sky radiance: the sky term is already applied independently
// in sprite.frag via SkyBuf, and returning it here would double-count it.
//
// One dispatch per cascade (cascades are mutually independent at BUILD time —
// order does not matter here, only at MERGE time). See rc_params.h for the
// geometry this pass reads from RcParams.geom[cascade].
//
//   t0 space0  FieldBuf — StructuredBuffer<float>, 4 floats/tile (rgb+pad),
//              tile-res, x-major field[(x*map_h+y)*4 + c] (gi_field.comp
//              output — the direct-radiance emitter texture).
//   t1 space0  SdfBuf   — StructuredBuffer<float>, SS-finer grid (wall test).
//   u0 space1  RcAtlas  — RWStructuredBuffer<float>, flat multi-cascade
//              atlas. This cascade's texels start at geom[cascade].offset_floats;
//              texel (px,py,d) is at that offset + ((py*probes_x+px)*dirs+d)*4,
//              4 floats: rgb + beta (beta unused by BUILD's own write here —
//              always 0 on hit / 1 on escape, per the header above).
//   b0 space2  RcParams (shared push; see rc_params.h for the C++ mirror).

StructuredBuffer<float>   FieldBuf : register(t0, space0);
StructuredBuffer<float>   SdfBuf   : register(t1, space0);
RWStructuredBuffer<float> RcAtlas  : register(u0, space1);

// RC_CASCADES mirrors src/lighting/rc_params.h — a shader cannot include a
// C++ header; keep the two in lockstep by hand.
static const uint RC_CASCADES = 4u;

cbuffer RcParams : register(b0, space2) {
    uint  map_w;      // cascade-0 (tile) grid width
    uint  map_h;      // cascade-0 (tile) grid height
    uint  sdf_map_w;  // SDF tile-grid width (== map_w)
    uint  sdf_map_h;  // SDF tile-grid height (== map_h)
    uint  cascade;    // which cascade THIS dispatch builds
    uint  sdf_ss;     // SDF supersample factor (matches SDF_SUPERSAMPLE, 8)
    float c0_interval;
    float rc_pad0;
    uint4 geom[RC_CASCADES]; // .x probes_x .y probes_y .z dirs .w offset_floats
};

#include "rc_shared.hlsl"

// FieldBuf is tile-res, x-major field[(x*map_h+y)*4 + c].
float3 field_at( int x, int y )
{
    x = clamp( x, 0, (int)map_w - 1 );
    y = clamp( y, 0, (int)map_h - 1 );
    const uint o = ( (uint)x * map_h + (uint)y ) * 4u;
    return float3( FieldBuf[o + 0u], FieldBuf[o + 1u], FieldBuf[o + 2u] );
}

// Cascade i's march interval [d0*(4^i-1)/3, d0*(4^(i+1)-1)/3) — mirrors
// rc_params.h::rc_cascade_interval exactly.
void cascade_interval( uint i, out float t_near, out float t_far )
{
    float p_i = 1.0, p_i1 = 4.0;
    [loop] for( uint k = 0; k < i; ++k ) {
        p_i *= 4.0;
        p_i1 *= 4.0;
    }
    t_near = c0_interval * ( p_i - 1.0 ) / 3.0;
    t_far  = c0_interval * ( p_i1 - 1.0 ) / 3.0;
}

static const int   RC_MAX_STEPS = 48;
static const float RC_HIT_EPS   = 0.06;
static const float RC_MIN_STEP  = 0.10;

[numthreads(8, 8, 1)]
void main( uint3 tid : SV_DispatchThreadID )
{
    const uint4 g = geom[cascade];
    const uint probes_x = g.x;
    const uint probes_y = g.y;
    const uint dirs      = g.z;
    const uint offset    = g.w;
    if( tid.x >= probes_x || tid.y >= probes_y ) {
        return;
    }
    // RC_C0_PROBE_SPACING mirrors rc_params.h (1.0 tile); spacing at cascade
    // i is RC_C0_PROBE_SPACING * 2^i tiles.
    const float RC_C0_PROBE_SPACING = 1.0;
    const float spacing = RC_C0_PROBE_SPACING * (float)( 1u << cascade );
    const float2 probe = ( float2( tid.xy ) + 0.5 ) * spacing;

    float t_near, t_far;
    cascade_interval( cascade, t_near, t_far );

    [loop] for( uint d = 0; d < dirs; ++d ) {
        const float  ang = 6.2831853 * ( (float)d + 0.5 ) / (float)dirs;
        const float2 dir = float2( cos( ang ), sin( ang ) );

        float3 rgb  = float3( 0.0, 0.0, 0.0 );
        float  beta = 1.0; // escapes to t_far unless a hit breaks the loop
        float  t    = t_near;
        [loop] for( int s = 0; s < RC_MAX_STEPS; ++s ) {
            if( t >= t_far ) {
                break;
            }
            const float2 pos = probe + dir * t;
            const float  sd  = sdf_bilinear( pos );
            if( sd < RC_HIT_EPS ) {
                rgb  = field_at( (int)floor( pos.x ), (int)floor( pos.y ) );
                beta = 0.0;
                break;
            }
            t += max( sd, RC_MIN_STEP );
        }

        const uint texel = offset + ( ( tid.y * probes_x + tid.x ) * dirs + d ) * 4u;
        RcAtlas[texel + 0u] = rgb.x;
        RcAtlas[texel + 1u] = rgb.y;
        RcAtlas[texel + 2u] = rgb.z;
        RcAtlas[texel + 3u] = beta;
    }
}

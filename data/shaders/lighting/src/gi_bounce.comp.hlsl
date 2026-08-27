// GI compute — pass 2 of 2: single-bounce ray march (was rc_bounce.frag).
//
// One compute thread = one map tile. It marches N rays out from the tile
// through the per-tile radiance FIELD (pass 1's direct-light gather),
// accumulating lit-surface radiance reachable before a wall. A tile sitting in
// shadow next to a lit wall has rays that reach that wall and gather its colour
// → real coloured bounce into shadow / around corners, which the direct gather
// cannot produce. Result → GiBuf, read by sprite.frag as its GI term.
//
// FieldBuf is read-only HERE (RW in pass 1). SDL_GPU inserts the compute→compute
// barrier on it between the two passes. Compute HLSL register spaces: readonly
// storage = (tN, space0), read-write = (uN, space1), uniforms = (bN, space2).
//
//   t0 space0  FieldBuf — StructuredBuffer<float>, 4 floats/tile (rgb+pad),
//              tile-res, x-major field[(x*map_h+y)*4 + c]  (pass-1 output).
//   t1 space0  SdfBuf   — StructuredBuffer<float>, SS-finer grid (wall test).
//   u0 space1  GiBuf    — RWStructuredBuffer<float>, 4 floats/tile, x-major
//              gi[(x*map_h+y)*4 + c]  (sprite.frag GI input).
//   b0 space2  GiParams (shared push; only sdf_map_w/h used here).

StructuredBuffer<float>   FieldBuf : register(t0, space0);
StructuredBuffer<float>   SdfBuf   : register(t1, space0);
RWStructuredBuffer<float> GiBuf    : register(u0, space1);

cbuffer GiParams : register(b0, space2) {
    uint  emitter_count; // unused here (kept: shared gi_params push)
    uint  sdf_map_w;
    uint  sdf_map_h;
    float current_z;
    float shadow_k;
    uint  shadow_steps;
    float gi_temporal;  // (unused here — shared gi_params push)
    float gi_bounce2;   // (unused here — shared gi_params push)
    float sun_r, sun_g, sun_b, sun_intensity; // (unused here)
    float sky_r, sky_g, sky_b, sky_intensity; // (unused here)
    float gi_albedo;    // (unused here)
};

// Gather tuning (constants; matches rc_bounce.frag).
static const int   RC_DIRS  = 16;   // rays per tile
static const int   RC_STEPS = 20;   // max march steps per ray
static const float RC_STEP  = 0.75; // tile units per step
static const float RC_START = 0.70; // skip the tile's own cell
static const float RC_WALL  = 0.10; // SDF distance treated as "blocked"
static const int   SDF_SS   = 8;    // MUST match sdf_pass.h / sprite.frag

// FieldBuf is tile-res, x-major field[(x*map_h+y)*4 + c].
float3 field_at( int x, int y )
{
    x = clamp( x, 0, (int)sdf_map_w - 1 );
    y = clamp( y, 0, (int)sdf_map_h - 1 );
    const uint o = ( (uint)x * sdf_map_h + (uint)y ) * 4u;
    return float3( FieldBuf[o + 0u], FieldBuf[o + 1u], FieldBuf[o + 2u] );
}

// SS-finer SDF (wall test) — identical math to sprite.frag / gi_field.
float sdf_texel( int x, int y )
{
    const int gw = (int)sdf_map_w * SDF_SS;
    const int gh = (int)sdf_map_h * SDF_SS;
    x = clamp( x, 0, gw - 1 );
    y = clamp( y, 0, gh - 1 );
    return SdfBuf[x * gh + y];
}
float sdf_bilinear( float2 p )
{
    const float2 g  = p * (float)SDF_SS - 0.5;
    const float2 fp = floor( g );
    const int   x0  = (int)fp.x;
    const int   y0  = (int)fp.y;
    const float2 w  = g - fp;
    const float a = sdf_texel( x0,     y0     );
    const float b = sdf_texel( x0 + 1, y0     );
    const float c = sdf_texel( x0,     y0 + 1 );
    const float d = sdf_texel( x0 + 1, y0 + 1 );
    return lerp( lerp( a, b, w.x ), lerp( c, d, w.x ), w.y );
}

[numthreads(8, 8, 1)]
void main( uint3 tid : SV_DispatchThreadID )
{
    const int tileX = (int)tid.x;
    const int tileY = (int)tid.y;
    if( tileX >= (int)sdf_map_w || tileY >= (int)sdf_map_h ) {
        return;
    }
    const float2 probe = float2( (float)tileX + 0.5, (float)tileY + 0.5 );

    float3 acc = float3( 0.0, 0.0, 0.0 );
    [loop] for( int d = 0; d < RC_DIRS; ++d ) {
        const float  ang = 6.2831853 * ( (float)d + 0.5 ) / (float)RC_DIRS;
        const float2 dir = float2( cos( ang ), sin( ang ) );
        float t = RC_START;
        [loop] for( int s = 0; s < RC_STEPS; ++s ) {
            const float2 pos = probe + dir * t;
            // Distance-weighted radiance sampled from the field along the ray.
            acc += field_at( (int)floor( pos.x ), (int)floor( pos.y ) ) / ( 1.0 + t );
            if( sdf_bilinear( pos ) < RC_WALL ) {
                break; // wall: sample it (the lit surface) then stop the ray
            }
            t += RC_STEP;
        }
    }
    // Average over directions. Magnitude is scaled by gi_strength in the sprite.
    const float3 gi = acc / (float)RC_DIRS;

    const uint o = ( (uint)tileX * sdf_map_h + (uint)tileY ) * 4u;
    GiBuf[o + 0u] = gi.x;
    GiBuf[o + 1u] = gi.y;
    GiBuf[o + 2u] = gi.z;
    GiBuf[o + 3u] = 0.0;
}

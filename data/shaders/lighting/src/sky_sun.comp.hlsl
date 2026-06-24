// Sky/sun(/moon) directional skylight — Stage 2b of GI_COMPUTE_AND_PERF_PLAN.md.
//
// One compute thread = one map tile. Computes, from ONE unified occluder field:
//   rgb = directional sky-access (the sky as an occluded dome)
//   a   = celestial (sun OR moon) occlusion via a 3D (x,y,elevation) march
// written to SkyBuf, read by sprite.frag.
//
// THE UNIFIED SOURCE (2b): OccBuf, built from map::coverage(p) — the game's single
// 0..1 per-tile obstruction scalar (full wall ~1, half-wall ~0.5, fence/furniture
// low) used as an occluder HEIGHT in tile units — plus a roof bit from
// floor_cache(z+1). Walls, half-walls, furniture AND roofs all come from this one
// field, so there is one place to look when occlusion is wrong. The march is
// parameterized by the light direction + elevation, so the SAME code serves the
// sun (day) and the moon (night; "full moon = sun with different params").
//
// 3D elevation: marching toward the light, the ray climbs ray_h = t·tan(elev). An
// occluder of height h at horizontal distance t blocks iff h >= ray_h — so a HIGH
// sun clears a half-wall a LOW sun shadows, and dawn/dusk shadows lengthen. A roof
// blocks while the ray is still below roof height (the probe's own roof → no direct
// celestial light; an overhang clears once the ray climbs above it).
//
// Compute HLSL register spaces (SDL_GPU): readonly storage = (tN, space0),
// read-write = (uN, space1), uniforms = (bN, space2). Readonly declared
// StructuredBuffer (not RW) so D3D12 does not default it read-write.
//
//   t0 space0  OccBuf — StructuredBuffer<float>, 2 floats/tile, tile-res, x-major
//              occ[(x*map_h+y)*2 + c]: c0 = occluder height (tiles), c1 = roof bit.
//   u0 space1  SkyBuf — RWStructuredBuffer<float>, 4 floats/tile (rgb sky-access +
//              a celestial-occ), tile-res, x-major sky[(x*map_h+y)*4 + c].
//   b0 space2  SkySunParams (light dir + sin_elev shared by sun/moon).

StructuredBuffer<float>   OccBuf : register(t0, space0);
RWStructuredBuffer<float> SkyBuf : register(u0, space1);

cbuffer SkySunParams : register(b0, space2) {
    uint  map_w;        // runtime tile dims (thread/tile grid extent)
    uint  map_h;
    float sun_dir_x;    // celestial travel direction (toward = -dir)
    float sun_dir_y;
    float sun_sin_elev; // celestial elevation sine (drives the 3D climb)
    float shadow_k;     // (reserved; penumbra softness lever)
    uint  shadow_steps; // (reserved)
    float ss_pad;
};

// Sky-dome sampling.
static const int   SKY_DIRS    = 8;     // hemisphere directions per tile
static const int   SKY_STEPS   = 16;    // max march steps per direction
static const float SKY_STEP    = 0.70;  // tile units per step
static const float SKY_START   = 0.60;  // skip the tile's own cell
static const float SKY_REACH   = 10.0;  // give up looking for open sky past this
static const float SKY_WALL_H  = 0.60;  // occluder height that blocks a sky direction
// Celestial (sun/moon) 3D-elevation march.
static const int   SUN_STEPS   = 24;    // horizontal march steps toward the light
static const float SUN_STEP    = 0.50;  // tile units per step
static const float SUN_START   = 0.30;  // skip the probe cell
static const float ROOF_H      = 1.00;  // roof height (tiles) — ray clears above this
static const float MAX_OCC_H    = 1.20;  // ray above this has cleared all occluders
static const int   SUN_PENUMBRA = 4;     // angular samples for a soft sun edge (1 = today's hard edge)

// OccBuf is tile-res, x-major occ[(x*map_h+y)*2 + c]. c0 = height, c1 = roof.
float occ_height_at( int x, int y )
{
    x = clamp( x, 0, (int)map_w - 1 );
    y = clamp( y, 0, (int)map_h - 1 );
    return OccBuf[( (uint)x * map_h + (uint)y ) * 2u + 0u];
}
float roof_at( int x, int y )
{
    x = clamp( x, 0, (int)map_w - 1 );
    y = clamp( y, 0, (int)map_h - 1 );
    return OccBuf[( (uint)x * map_h + (uint)y ) * 2u + 1u];
}

// Does direction `dir` from `origin` reach open sky (a non-roofed tile) before a
// tall occluder blocks it? Returns 1 (admits) or 0. Half-walls (< SKY_WALL_H)
// don't block the sky (you see over them); full walls do.
float sky_admit( float2 origin, float2 dir )
{
    float t = SKY_START;
    [loop] for( int s = 0; s < SKY_STEPS; ++s ) {
        const float2 pos = origin + dir * t;
        const int px = (int)floor( pos.x );
        const int py = (int)floor( pos.y );
        if( occ_height_at( px, py ) >= SKY_WALL_H ) {
            return 0.0;                       // tall wall blocks this direction
        }
        if( roof_at( px, py ) < 0.5 ) {
            return 1.0;                       // reached open (non-roofed) sky
        }
        t += SKY_STEP;
        if( t > SKY_REACH ) {
            break;
        }
    }
    return 0.0;                                // no open sky reachable
}

// Celestial occlusion via the 3D elevation march. 1 = lit toward the light,
// 0 = shadowed (wall/half-wall of sufficient height for the sun's elevation, or
// a roof/overhang the ray hasn't climbed above).
float celestial_occ_dir( float2 probe, float2 toward )
{
    if( map_w == 0u ) {
        return 1.0;
    }
    // Probe under its own roof → no direct celestial light.
    if( roof_at( (int)probe.x, (int)probe.y ) > 0.5 ) {
        return 0.0;
    }
    const float cos_e    = sqrt( max( 1.0 - sun_sin_elev * sun_sin_elev, 0.02 ) );
    const float elev_tan = sun_sin_elev / cos_e;     // ray climb per horizontal tile
    float t = SUN_START;
    [loop] for( int s = 0; s < SUN_STEPS; ++s ) {
        const float2 pos   = probe + toward * t;
        const float  ray_h = t * elev_tan;
        if( ray_h > MAX_OCC_H ) {
            break;                            // ray has cleared every occluder
        }
        const int px = (int)floor( pos.x );
        const int py = (int)floor( pos.y );
        if( occ_height_at( px, py ) >= ray_h ) {
            return 0.0;                       // occluder taller than the ray here
        }
        if( roof_at( px, py ) > 0.5 && ray_h < ROOF_H ) {
            return 0.0;                       // overhang/roof still above the ray
        }
        t += SUN_STEP;
    }
    return 1.0;
}

[numthreads(8, 8, 1)]
void main( uint3 tid : SV_DispatchThreadID )
{
    const int tileX = (int)tid.x;
    const int tileY = (int)tid.y;
    if( tileX >= (int)map_w || tileY >= (int)map_h ) {
        return;
    }
    const float2 probe = float2( (float)tileX + 0.5, (float)tileY + 0.5 );

    float sky = 0.0;
    [loop] for( int d = 0; d < SKY_DIRS; ++d ) {
        const float  ang = 6.2831853 * ( (float)d + 0.5 ) / (float)SKY_DIRS;
        const float2 dir = float2( cos( ang ), sin( ang ) );
        sky += sky_admit( probe, dir );
    }
    sky /= (float)SKY_DIRS;

    const float2 sun_toward = -float2( sun_dir_x, sun_dir_y );
    const float  sun_ang0   = atan2( sun_toward.y, sun_toward.x );
    float occ = 0.0;
    [loop] for( int pi = 0; pi < SUN_PENUMBRA; ++pi ) {
        const float da = ( SUN_PENUMBRA > 1 )
            ? ( (float)pi / (float)( SUN_PENUMBRA - 1 ) - 0.5 ) * 0.18 : 0.0; // ~±5°
        const float a  = sun_ang0 + da;
        occ += celestial_occ_dir( probe, float2( cos( a ), sin( a ) ) );
    }
    occ /= (float)SUN_PENUMBRA;

    const uint o = ( (uint)tileX * map_h + (uint)tileY ) * 4u;
    SkyBuf[o + 0u] = sky;
    SkyBuf[o + 1u] = sky;
    SkyBuf[o + 2u] = sky;
    SkyBuf[o + 3u] = occ;
}

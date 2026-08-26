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
//   t1 space0  SdfBuf — StructuredBuffer<float>, the 8x-supersampled JFA SDF
//              (SDF_SS^2 subcells/tile, distances in TILE units), x-major
//              sdf[(x*sdf_map_h+y)*SDF_SS^2 + c]. Encodes terrain + furniture +
//              trees + vehicles + roofs — the sun now sphere-traces THIS for
//              sub-tile shadow edges instead of the blocky tile-res march.
//   u0 space1  SkyBuf — RWStructuredBuffer<float>, 4 floats/tile (rgb sky-access +
//              a celestial-occ), tile-res, x-major sky[(x*map_h+y)*4 + c].
//   b0 space2  SkySunParams (light dir + sin_elev shared by sun/moon).

StructuredBuffer<float>   OccBuf : register(t0, space0);
StructuredBuffer<float>   SdfBuf : register(t1, space0);
RWStructuredBuffer<float> SkyBuf : register(u0, space1);

cbuffer SkySunParams : register(b0, space2) {
    uint  map_w;        // runtime tile dims (thread/tile grid extent)
    uint  map_h;
    float sun_dir_x;    // celestial travel direction (toward = -dir)
    float sun_dir_y;
    float sun_sin_elev; // celestial elevation sine (drives the 3D climb)
    float shadow_k;     // (reserved; penumbra softness lever)
    uint  shadow_steps; // (reserved)
    uint  sky_dirs;     // hemisphere directions per tile (P5b — F4 knob)
    float sky_reach;    // sky march max distance in tiles (P5b)
    uint  sun_steps;    // celestial march steps (P5b)
    uint  sun_penumbra; // penumbra angular samples, 1=hard edge (P5b)
    uint  sdf_ss;       // SDF supersample factor (grid = map * sdf_ss)
    float sun_soft;     // SDF penumbra softness (tiles of feather; 0 = hard)
};

// Sky-dome sampling. SKY_DIRS/SKY_REACH are cbuffer-driven (P5b — F4 knobs).
// The march-micro constants keep static const (geometric, not quality/perf tradeoffs).
static const int   SKY_STEPS   = 16;    // max march steps per direction
static const float SKY_STEP    = 0.70;  // tile units per step
static const float SKY_START   = 0.60;  // skip the tile's own cell
static const float SKY_WALL_H  = 0.60;  // occluder height that blocks a sky direction
// Celestial (sun/moon) SDF sphere-trace. SUN_STEPS is cbuffer-driven.
static const float SUN_START   = 0.30;  // skip the probe cell
static const float ROOF_H      = 1.00;  // roof height (tiles) — ray clears above this
static const float MAX_OCC_H   = 1.20;  // ray above this has cleared all occluders
static const float SUN_FAR     = 8.00;  // trace distance where the step is fully coarse

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
        if( t > sky_reach ) {
            break;
        }
    }
    return 0.0;                                // no open sky reachable
}

// SDF bilinear sampler — same p-0.5 centre convention as sprite.frag's
// sdf_bilinear: (x,y) are TILE coords, the grid is map*sdf_ss, distances are
// already in tile units.
float sdf_bilinear( float2 p )
{
    const float2 g = p * (float)sdf_ss - 0.5;
    const float2 fp = floor( g );
    const int gw = (int)map_w * (int)sdf_ss;
    const int gh = (int)map_h * (int)sdf_ss;
    const int x0 = clamp( (int)fp.x, 0, gw - 2 );
    const int y0 = clamp( (int)fp.y, 0, gh - 2 );
    const float2 w = g - fp;
    const uint gx = (uint)gh; // x-stride in subcells
    const uint row = (uint)( x0 * gh + y0 );
    const float a = SdfBuf[row + 0u];          // (x0,   y0)
    const float b = SdfBuf[row + gx];          // (x0+1, y0)
    const float c = SdfBuf[row + 1u];          // (x0,   y0+1)
    const float d = SdfBuf[row + gx + 1u];     // (x0+1, y0+1)
    return lerp( lerp( a, b, w.x ), lerp( c, d, w.x ), w.y );
}

// Celestial occlusion via a sphere-trace of the 8x SDF. 1 = lit toward the
// light, 0 = shadowed. The SDF is a 2D horizontal distance field, so the 3D
// elevation test is recovered analytically: an occluder of height h at
// horizontal distance d blocks iff h >= d*tan(elev), i.e. the ray is still
// below h at that distance. The trace therefore only needs to run while the
// ray is below MAX_OCC_H — beyond that no occluder can block, and the
// distance-adaptive step (fine near the receiver, coarse far out) is the
// cascade: sharp soft-edged shadows nearby, cheap soft ones at range.
float celestial_occ_dir( float2 probe, float2 toward )
{
    if( map_w == 0u || sdf_ss == 0u ) {
        return 1.0;
    }
    // Probe under its own roof → no direct celestial light.
    if( roof_at( (int)probe.x, (int)probe.y ) > 0.5 ) {
        return 0.0;
    }
    const float cos_e    = sqrt( max( 1.0 - sun_sin_elev * sun_sin_elev, 0.02 ) );
    const float elev_tan = sun_sin_elev / cos_e;     // ray climb per horizontal tile
    const float t_end    = MAX_OCC_H / max( elev_tan, 0.001 );
    // Escape the probe's own occluder body (a tree trunk / wall tile) so the
    // lit TOP is not self-shadowed; stop at open air so the NEXT occluder
    // still shadows normally.
    float t = SUN_START;
    [loop] for( int ss = 0; ss < 8; ++ss ) {
        if( sdf_bilinear( probe + toward * t ) >= 0.05 ) {
            break;
        }
        t += 0.15;
    }
    float shadow = 1.0;
    [loop] for( int s = 0; s < (int)max( sun_steps, 1u ); ++s ) {
        if( t >= t_end ) {
            break;                            // ray has cleared every occluder
        }
        const float2 pos = probe + toward * t;
        const float  sd  = sdf_bilinear( pos );
        if( sd < 0.05 ) {
            shadow = 0.0;
            break;                            // inside an occluder
        }
        // Penumbra: the SDF distance IS the miss distance from the ray, so
        // feather the shadow over sun_soft tiles of clearance.
        if( sun_soft > 0.001 ) {
            shadow = min( shadow, saturate( sd / sun_soft ) );
        }
        // Distance-adaptive step: 0.25 tile near the receiver growing to 1.0
        // tile at SUN_FAR — the cascade. Never overshoot the trace end.
        const float step = min( 0.25 + 0.75 * saturate( t / SUN_FAR ),
                                t_end - t );
        t += max( step, 0.05 );
    }
    return shadow;
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
    const uint sd = max( sky_dirs, 1u );
    [loop] for( int d = 0; d < (int)sd; ++d ) {
        const float  ang = 6.2831853 * ( (float)d + 0.5 ) / (float)sd;
        const float2 dir = float2( cos( ang ), sin( ang ) );
        sky += sky_admit( probe, dir );
    }
    sky /= (float)sd;

    const float2 sun_toward = -float2( sun_dir_x, sun_dir_y );
    const float  sun_ang0   = atan2( sun_toward.y, sun_toward.x );
    float occ = 0.0;
    const uint sp = max( sun_penumbra, 1u );
    [loop] for( int pi = 0; pi < (int)sp; ++pi ) {
        const float da = ( sp > 1u )
            ? ( (float)pi / (float)( sp - 1u ) - 0.5 ) * 0.18 : 0.0; // ~±5°
        const float a  = sun_ang0 + da;
        occ += celestial_occ_dir( probe, float2( cos( a ), sin( a ) ) );
    }
    occ /= (float)sp;

    const uint o = ( (uint)tileX * map_h + (uint)tileY ) * 4u;
    SkyBuf[o + 0u] = sky;
    SkyBuf[o + 1u] = sky;
    SkyBuf[o + 2u] = sky;
    SkyBuf[o + 3u] = occ;
}
